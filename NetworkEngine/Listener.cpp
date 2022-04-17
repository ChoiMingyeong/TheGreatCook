#include "pch.h"
#include "Listener.h"

Listener::Listener(unsigned short port, unsigned int headCount)
	: m_ListenSocket(INVALID_SOCKET), m_hIOCP(nullptr), m_HeadCount(headCount),
	m_sSocketPool(headCount + headCount / 2), m_sOverlappedPool(headCount + 10, true),
	m_IsRun(false), m_WorkThreadArray(nullptr)/*,
	m_IsTimeWait(false)*/
{
	// IOCP �ڵ� ����
	m_hIOCP = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
	if (m_hIOCP == nullptr)
	{
		// ���� �߻� : IOCP �ڵ� ���� ����
		//throw ErrorCode::Listener_hIOCPCreate;
		return;
	}

	// ���� ���� ����
	m_ListenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (m_ListenSocket == INVALID_SOCKET)
	{
		// ���� �߻� : ListenSocket ���� ����
		//throw ErrorCode::Listener_CreateListenSocket;
		return;
	}

	// ���� ���� ���ε�
	if (BindListenSocket(port) == FALSE)
	{
		closesocket(m_ListenSocket);
		m_ListenSocket = INVALID_SOCKET;
		//throw ErrorCode::Listener_BindListenSocket;
		return;
	}

	// ���� ������ IOCP�� ����
	CreateIoCompletionPort(reinterpret_cast<HANDLE>(m_ListenSocket), m_hIOCP, 0, 0);

	// ���� ����
	if (listen(m_ListenSocket, SOMAXCONN) == SOCKET_ERROR)
	{
		closesocket(m_ListenSocket);
		m_ListenSocket = INVALID_SOCKET;
		//throw ErrorCode::Listener_Listen;
		return;
	}

	// Ȯ�� �Լ� ������ �޸𸮿� ���
	DWORD _dwByte;
	GUID _guid;

	// AcceptEx �Լ� ������ ȹ��
	_guid = WSAID_ACCEPTEX;
	if (SOCKET_ERROR == WSAIoctl(
		m_ListenSocket, SIO_GET_EXTENSION_FUNCTION_POINTER,
		&_guid, sizeof(_guid),
		&m_lpfnAcceptEx, sizeof(m_lpfnAcceptEx),
		&_dwByte, NULL, NULL))
	{
		//throw ErrorCode::Listener_GetExFunction;
		closesocket(m_ListenSocket);
		m_ListenSocket = INVALID_SOCKET;
		return;
	}

	// DisconnectEx �Լ� ������ ȹ��
	_guid = WSAID_DISCONNECTEX;
	if (SOCKET_ERROR == WSAIoctl(
		m_ListenSocket, SIO_GET_EXTENSION_FUNCTION_POINTER,
		&_guid, sizeof(_guid),
		&m_lpfnDisconnectEx, sizeof(m_lpfnDisconnectEx),
		&_dwByte, NULL, NULL))
	{
		//throw ErrorCode::Listener_GetExFunction;
		closesocket(m_ListenSocket);
		m_ListenSocket = INVALID_SOCKET;
		return;
	}

	// GetAcceptExSockAddrs �Լ� ������ ȹ��
	_guid = WSAID_GETACCEPTEXSOCKADDRS;
	if (SOCKET_ERROR == WSAIoctl(
		m_ListenSocket, SIO_GET_EXTENSION_FUNCTION_POINTER,
		&_guid, sizeof(_guid),
		&m_lpfnGetAcceptExSockAddrs, sizeof(m_lpfnGetAcceptExSockAddrs),
		&_dwByte, NULL, NULL))
	{
		//throw ErrorCode::Listener_GetExFunction;
		closesocket(m_ListenSocket);
		m_ListenSocket = INVALID_SOCKET;
		return;
	}

	// Accept �ɾ����
	for (int i = 0; i < m_HeadCount; i++)
	{
		// Overlapped ���� �� AcceptEx ����
		SOverlapped* _psOverlapped = m_sOverlappedPool.GetUsableObject();
		_psOverlapped->eType = SOverlapped::eIOType::eAccept;

		// ���� ����
		_psOverlapped->socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

		if (_psOverlapped->socket == INVALID_SOCKET)
		{
			//throw ErrorCode::Listener_SocketCreate;
			return;
		}

		// �Ｎ ���� �ɼ�
		LINGER _linger;
		_linger.l_onoff = 1;
		_linger.l_linger = 0;
		setsockopt(_psOverlapped->socket, SOL_SOCKET, SO_LINGER, (char*)&_linger, sizeof(_linger));

		// Accept ����
		DoAccept(_psOverlapped);
	}

	// ������ ����
	Run();
}

Listener::~Listener()
{
	// ���� ���� ����
	if (INVALID_SOCKET != m_ListenSocket)
	{
		closesocket(m_ListenSocket);
		m_ListenSocket = INVALID_SOCKET;
	}

	// �������� ������ ����
	//Stop();

	// IOCP �ڵ� ����
	if (nullptr != m_hIOCP)
	{
		CloseHandle(m_hIOCP);
	}
}

BOOL Listener::Send(SSocket* psSocket, PacketHeader* packet)
{
	// ��Ŷ ����
	if (packet->packetSize + 2 >= BUFSIZ)
	{
		return FALSE;
	}
	// �ɾ���� SOverlapped ����
	SOverlapped* _psOverlapped = m_sOverlappedPool.GetUsableObject();
	_psOverlapped->eType = SOverlapped::eIOType::eSend;
	_psOverlapped->socket = psSocket->socket;

	_psOverlapped->iDataSize = 2 + packet->packetSize;
	if (sizeof(_psOverlapped->buffer) < _psOverlapped->iDataSize)
	{
		m_sOverlappedPool.ReturnObject(_psOverlapped);
		return FALSE;
	}
	memcpy_s(_psOverlapped->buffer, sizeof(_psOverlapped->buffer), packet, _psOverlapped->iDataSize);

	// WSABUF ����
	WSABUF _wsaBuffer;
	_wsaBuffer.buf = _psOverlapped->buffer;
	_wsaBuffer.len = _psOverlapped->iDataSize;

	// WSASend() �������� �ɱ�
	DWORD _dwNumberOfBytesSent = 0;
	if (WSASend(
		_psOverlapped->socket,
		&_wsaBuffer,
		1,
		&_dwNumberOfBytesSent,
		0,
		&_psOverlapped->wsaOverlapped,
		nullptr) == SOCKET_ERROR)
	{
		if (WSAGetLastError() != WSA_IO_PENDING)
		{
			// WSASend() ����
			//throw ErrorCode::Listener_WSASend;
			m_sOverlappedPool.ReturnObject(_psOverlapped);
			return FALSE;
		}
	}

	return TRUE;
}

BOOL Listener::Receive(std::pair<SSocket*, PacketHeader*>*& networkMessageArray, int& arraySize)
{
	std::vector<std::pair<SSocket*, PacketHeader*>> _networkMessageVec;

	while (m_NetworkMessageQueue.empty() == false)
	{
		_networkMessageVec.push_back(m_NetworkMessageQueue.front());
		m_NetworkMessageQueue.pop();
	}

	arraySize = _networkMessageVec.size();
	if (arraySize <= 0)
	{
		return TRUE;
	}
	networkMessageArray = new std::pair<SSocket*, PacketHeader*>[arraySize];
	if (networkMessageArray == nullptr)
	{
		//throw ErrorCode::Listener_ArrayCreate;
		return FALSE;
	}

	for (int i = 0; i < arraySize; i++)
	{
		networkMessageArray[i] = _networkMessageVec[i];
	}

	return TRUE;
}

BOOL Listener::RegisterExFunctionPointer(GUID guid, LPVOID lpfnEx)
{
	DWORD _dwByte;

	// GetAcceptExSockAddrs �Լ� ������ ȹ��
	if (SOCKET_ERROR == WSAIoctl(
		m_ListenSocket, SIO_GET_EXTENSION_FUNCTION_POINTER,
		&guid, sizeof(guid),
		&lpfnEx, sizeof(lpfnEx),
		&_dwByte, NULL, NULL))
	{
		// GetAcceptExSockAddrs ���� ����
		//throw ErrorCode::Listener_GetExFunction;
		return FALSE;
	}

	return TRUE;
}

BOOL Listener::BindListenSocket(unsigned short port)
{
	// ���� ���� ����
	SOCKADDR_IN _serverAddr;
	ZeroMemory(&_serverAddr, sizeof(_serverAddr));
	_serverAddr.sin_family = AF_INET;
	_serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);
	_serverAddr.sin_port = htons(port);

	// ���ε�
	if (SOCKET_ERROR == ::bind(m_ListenSocket, reinterpret_cast<SOCKADDR*>(&_serverAddr), sizeof(_serverAddr)))
	{
		return FALSE;
	}

	return TRUE;
}

BOOL Listener::CreateThread()
{
	// ������ ���� ����
	SYSTEM_INFO systemInfo;
	GetSystemInfo(&systemInfo);
	m_ThreadCount = systemInfo.dwNumberOfProcessors * 2;

	// ������ ����
	m_WorkThreadArray = new std::thread * [m_ThreadCount];
	if (m_WorkThreadArray == nullptr)
	{
		return FALSE;
	}

	// ������ ���� �÷���
	m_IsRun = true;

	for (int i = 0; i < m_ThreadCount; i++)
	{
		m_WorkThreadArray[i] = new std::thread(&Listener::Update, this);
	}

	return TRUE;
}

BOOL Listener::Run()
{
	// �̹� �������� �����尡 �����Ǿ��ִ� ����
	if (m_WorkThreadArray != nullptr)
	{
		return TRUE;
	}

	return CreateThread();
}

void Listener::Update()
{
	while (m_IsRun == true)
	{
		DWORD _dwNumberOfBytesTransferred = 0;
		const int _entryCount = 32;
		OVERLAPPED_ENTRY _entries[_entryCount];
		ULONG _numEnries = 0;	// ������ Entries�� ��
		SOverlapped* _psOverlapped = nullptr;
		SSocket* _psSocket = nullptr;

		std::unique_lock<std::shared_mutex> _ulk(m_WorkMutex);
		if (FALSE == GetQueuedCompletionStatusEx(m_hIOCP, _entries, _entryCount, &_numEnries, INFINITE, FALSE))
		{
			// �������� ����
			Sleep(0);
			continue;
		}

		// ������ �� ��ŭ �ݺ�
		for (ULONG i = 0; i < _numEnries; ++i)
		{
			_dwNumberOfBytesTransferred = _entries[i].dwNumberOfBytesTransferred;

			_psOverlapped = reinterpret_cast<SOverlapped*>(_entries[i].lpOverlapped);
			if (_psOverlapped == nullptr)
			{
				continue;
			}

			// Accept�� ���
			if (_psOverlapped->eType == SOverlapped::eIOType::eAccept)
			{
				ProcessAccept(_psOverlapped);
				continue;
			}

			// Disconnect�� ���
			if (_psOverlapped->eType == SOverlapped::eIOType::eDisconnect)
			{
				ProcessDisconnect(_psOverlapped);
				continue;
			}

			if (_dwNumberOfBytesTransferred == 0)
			{
				// Ŭ���̾�Ʈ�� �������� ��(������ ����) Disconnect
				DoDisconnect(_psOverlapped->socket, _psOverlapped);
				continue;
			}

			if (_dwNumberOfBytesTransferred == -1)
			{
				continue;
			}

			// SSocket Ȯ�� (Accept�� �ƴϸ� SSocket�� ����)
			_psSocket = reinterpret_cast<SSocket*>(_entries[i].lpCompletionKey);
			if (_psSocket == nullptr)
			{
				if (_psOverlapped->eType != SOverlapped::eIOType::eAccept)
				{
					// PQCS : GQCS�� ���Ͻ�ų �� �ִ� �Լ�, �ַ� ���ῡ ���δ�.
					PostQueuedCompletionStatus(m_hIOCP, 0, _entries[i].lpCompletionKey, _entries[i].lpOverlapped);
					continue;
				}
			}

			// IO_Type�� ���� ó�� ����
			if (_psOverlapped->eType == SOverlapped::eIOType::eReceive)
			{
				// Overlapped ����� ����� ���� ��� ������ ���� ī��Ʈ ����
				ProcessReceive(_dwNumberOfBytesTransferred, _psOverlapped, _psSocket);
				continue;
			}
			else if (_psOverlapped->eType == SOverlapped::eIOType::eSend)
			{
				// Overlapped ����� ����� ���� ��� ������ ���� ī��Ʈ ����
				ProcessSend(_dwNumberOfBytesTransferred, _psOverlapped, _psSocket);
				continue;
			}
		}

		Sleep(0);
	}
}

BOOL Listener::Stop()
{
	// ������ ���� �÷���
	m_IsRun = false;

	// ������ �����尡 �����Ǿ����� ���� ����
	if (nullptr == m_WorkThreadArray)
	{
		return TRUE;
	}

	// ������ ���� ���
	for (int i = 0; i < m_ThreadCount; i++)
	{
		m_WorkThreadArray[i]->join();
		delete m_WorkThreadArray[i];
	}

	delete[] m_WorkThreadArray;
	m_WorkThreadArray = nullptr;

	return TRUE;
}

void Listener::ProcessAccept(SOverlapped* psOverlapped)
{
	// Ŭ���̾�Ʈ ���� Ȯ��
	int _addrLen = sizeof(sockaddr_in) + 16;
	sockaddr_in* _pLocal = nullptr;
	sockaddr_in* _pRemote = nullptr;

	// ���� ���� ��������
	m_lpfnGetAcceptExSockAddrs(
		psOverlapped->buffer,
		0,
		sizeof(sockaddr_in) + 16,
		sizeof(sockaddr_in) + 16,
		(sockaddr**)&_pLocal,
		&_addrLen,
		(sockaddr**)&_pRemote,
		&_addrLen);

	char _buf[32] = { 0, };
	inet_ntop(AF_INET, &_pRemote->sin_addr, _buf, sizeof(_buf));

	// Ŭ���̾�Ʈ ���� �� ���
	SSocket* _psSocket = m_sSocketPool.GetUsableObject();
	_psSocket->socket = psOverlapped->socket;
	_psSocket->ip = std::string(_buf);
	_psSocket->port = ntohs(_pRemote->sin_port);

	// IOCP ���� �� ���� ���
	CreateIoCompletionPort(reinterpret_cast<HANDLE>(_psSocket->socket), m_hIOCP, reinterpret_cast<ULONG_PTR>(_psSocket), 0);

	// ���� �ο� �ʰ����� Ȯ��
	if (!AddUserList(_psSocket))
	{
		// ���� �ִ� ���� �� �ʰ�
		DoDisconnect(psOverlapped->socket, psOverlapped);
		return;
	}

	// ���ú� �ɱ�
	DoReceive(_psSocket->socket, psOverlapped);
}

void Listener::ProcessDisconnect(SOverlapped* psOverlapped)
{
	// Ŭ���̾�Ʈ ���� ��Ͽ��� ����
	for (auto iter = m_UserVec.begin(); iter != m_UserVec.end(); iter++)
	{
		SSocket* _nowClient = *iter;
		if (psOverlapped->socket == _nowClient->socket)
		{
			m_UserVec.erase(iter);
			m_sSocketPool.ReturnObject(_nowClient);
			break;
		}
	}

	// ���� ��Ȱ���� ���� �ٽ� Accept�� �Ǵ�.
	psOverlapped->eType = SOverlapped::eIOType::eAccept;

	// AcceptEx ����
	DoAccept(psOverlapped);
}

void Listener::ProcessSend(DWORD dwNumberOfBytesTransferred, SOverlapped* psOverlapped, SSocket* psSocket)
{
	m_sOverlappedPool.ReturnObject(psOverlapped);
}

void Listener::ProcessReceive(DWORD dwNumberOfBytesTransferred, SOverlapped* psOverlapped, SSocket* psSocket)
{
	psOverlapped->iDataSize += dwNumberOfBytesTransferred;

	while (psOverlapped->iDataSize > 0)
	{
		static const unsigned short _cusHeaderSize = 2;

		// HeaderSize ��ŭ ��Ŷ�� �ȵ���
		if (psOverlapped->iDataSize < _cusHeaderSize)
		{
			break;
		}

		unsigned short _usBodySize = *reinterpret_cast<unsigned short*>(psOverlapped->buffer);
		unsigned short _usPacketSize = _cusHeaderSize + _usBodySize;

		// ������ ��Ŷ�� �ȵ���
		if (_usPacketSize > psOverlapped->iDataSize)
		{
			break;
		}

		// �����͵��� �̹��� ó���Ѹ�ŭ ����.
		memcpy_s(psOverlapped->buffer, psOverlapped->iDataSize,
			psOverlapped->buffer + _usPacketSize, psOverlapped->iDataSize - _usPacketSize);

		PacketHeader* _packet = reinterpret_cast<PacketHeader*>(psOverlapped->buffer);

		// �޼��� ť�� ���� �޼��� �߰�
		AddNetworkMessage(psSocket, _packet);

		// ó���� ��Ŷ ũ�⸸ŭ ó���ҷ� ����
		psOverlapped->iDataSize -= _usPacketSize;
	}

	// �ٽ� ���� �ɾ� �α�
	DoReceive(psOverlapped->socket, psOverlapped);
}

BOOL Listener::DoDisconnect(SOCKET socket, SOverlapped* psOverlapped)
{
	if (psOverlapped == nullptr)
	{
		psOverlapped = m_sOverlappedPool.GetUsableObject();
	}
	psOverlapped->eType = SOverlapped::eIOType::eDisconnect;
	psOverlapped->socket = socket;

	// Disconnect ����(���� ��Ȱ�� �ɼ�)
	if (m_lpfnDisconnectEx(psOverlapped->socket, &psOverlapped->wsaOverlapped, TF_REUSE_SOCKET, 0) == false)
	{
		if (WSAGetLastError() != ERROR_IO_PENDING)
		{
			//throw ErrorCode::Listener_DisconnectEx;
			return FALSE;
		}

		// ����� ���� �õ� ����
		// m_WaitSocketVec.push_back(std::make_pair(GetTickCount64(), FindUserSocket(socket)));
	}

	return TRUE;
}

void Listener::DoAccept(SOverlapped* psOverlapped)
{
	// AcceptEx ����
	DWORD _dwByte;
	if (m_lpfnAcceptEx(
		m_ListenSocket, psOverlapped->socket, &psOverlapped->buffer,
		0, sizeof(SOCKADDR_IN) + 16, sizeof(SOCKADDR_IN) + 16,
		&_dwByte, &psOverlapped->wsaOverlapped))
	{
		// AcceptEx Success
	}
	else if (WSAGetLastError() == ERROR_IO_PENDING)
	{
		DWORD numBytes;
		if (GetOverlappedResult((HANDLE)psOverlapped->socket, &(psOverlapped->wsaOverlapped), &numBytes, TRUE) == false)
		{
			//throw ErrorCode::Listener_AcceptEx;
			return;
		}
	}
	else
	{
		//throw ErrorCode::Listener_AcceptEx;
		return;
	}
}

void Listener::DoReceive(SOCKET socket, SOverlapped* psOverlapped)
{
	// psOverlapped�� �����Ǿ����� ������ ����(�����Ǿ��ִٸ� ��Ȱ��)
	if (psOverlapped == nullptr)
	{
		psOverlapped = m_sOverlappedPool.GetUsableObject();
	}

	psOverlapped->eType = SOverlapped::eIOType::eReceive;
	psOverlapped->socket = socket;

	WSABUF _wsaBuffer;
	DWORD dwNumberOfBytesRecvd = 0;
	DWORD dwFlag = 0;

	_wsaBuffer.buf = psOverlapped->buffer + psOverlapped->iDataSize;
	_wsaBuffer.len = sizeof(psOverlapped->buffer) - psOverlapped->iDataSize;

	// WSARecv
	if (SOCKET_ERROR == WSARecv(
		psOverlapped->socket,
		&_wsaBuffer,
		1,
		&dwNumberOfBytesRecvd,
		&dwFlag,
		&psOverlapped->wsaOverlapped,
		nullptr))
	{
		if (WSAGetLastError() != WSA_IO_PENDING)
		{
			//throw ErrorCode::Listener_WSAReceive;
			return;
		}
	}

	// DoReceive Success
	return;
}

void Listener::AddNetworkMessage(SSocket* psSocket, PacketHeader* packet)
{
	m_NetworkMessageQueue.push(std::make_pair(psSocket, packet));
}

BOOL Listener::AddUserList(SSocket* psSocket)
{
	// ���� �ִ� ���� ���� �� ���� ������ ���� ����
	if (m_HeadCount <= m_UserVec.size())
	{
		return FALSE;
	}

	m_UserVec.push_back(psSocket);
	return TRUE;
}

SSocket* Listener::FindUserSocket(SOCKET socket)
{
	SSocket* _result = nullptr;

	for (auto user : m_UserVec)
	{
		if (user->socket == socket)
		{
			_result = user;
			break;
		}
	}

	return _result;
}

//SSocket* Listener::FindUserSocket(std::string id)
//{
//	SSocket* _result = nullptr;
//	for (auto user : m_UserVec)
//	{
//		if (user.first == id)
//		{
//			_result = user.second;
//			break;
//		}
//	}
//	return _result;
//}
//
//std::string Listener::FindUserId(SSocket* psSocket)
//{
//	std::string _result = std::string();
//	for (auto user : m_UserVec)
//	{
//		if (user.second == psSocket)
//		{
//			_result = user.first;
//			break;
//		}
//	}
//	return _result;
//}
//
//std::string Listener::FindUserId(SOCKET socket)
//{
//	std::string _result = std::string();
//	for (auto user : m_UserVec)
//	{
//		if (user.second->socket == socket)
//		{
//			_result = user.first;
//			break;
//		}
//	}
//	return _result;
//}

//void Listener::CheckTimeWaitSocket()
//{
//	while (m_IsTimeWait == true)
//	{
//		// TIME_WAIT�� ��ٸ��� ������ ������ continue;
//		if (IsTimeWaitSocketExist() == TRUE)
//		{
//			std::unique_lock<std::shared_mutex> _ulk(m_TimeWaitMutex);
//
//			static const DWORD _defaultWaitTime = 240 * 1000;
//			DWORD _nowTime = GetTickCount64();
//
//			// Time Wait �ð��� ���� ������ ����Ǯ�� �ǵ����ֱ�
//			for (auto _iter = m_WaitSocketVec.begin(); _iter != m_WaitSocketVec.end();)
//			{
//				DWORD _waitTime = _nowTime - _iter->first;
//				if ((_waitTime) >= _defaultWaitTime)
//				{
//					// Overlapped ���� �� AcceptEx ����
//					SOverlapped* _psOverlapped = m_sOverlappedPool.GetUsableObject();
//					_psOverlapped->eType = SOverlapped::eIOType::eAccept;
//					_psOverlapped->socket = _iter->second->socket;
//					DoAccept(_psOverlapped);
//					m_WaitSocketVec.erase(_iter++);
//					continue;
//				}
//
//				++_iter;
//			}
//		}
//
//		Sleep(1);
//	}
//}
//
//void Listener::CreateTimeWaitThread()
//{
//	// �̹� ������ ������ �� ��� ����
//	if (m_IsTimeWait == true)
//	{
//		return;
//	}
//
//	// ������ ����
//	m_IsTimeWait = true;
//	m_TimeWaitThread = std::thread(&Listener::CheckTimeWaitSocket, this);
//}
//
//BOOL Listener::IsTimeWaitSocketExist()
//{
//	std::shared_lock<std::shared_mutex> _slk(m_TimeWaitMutex);
//	return !m_WaitSocketVec.empty();
//}
