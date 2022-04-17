#include "pch.h"
#include "Connector.h"

Connector::Connector(unsigned short port, std::string ip)
	:m_ConnectorSocket(INVALID_SOCKET), m_hIOCP(nullptr), m_IsConnect(false),
	m_lpfnConnectEx(nullptr), m_WorkThreadArray(nullptr), m_IsRun(false),
	m_sOverlappedPool(10, true)
{
	// IOCP �ڵ� ����
	m_hIOCP = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
	if (m_hIOCP == nullptr)
	{
		// ���� �߻� : IOCP �ڵ� ���� ����
		//throw ErrorCode::Connector_hIOCPCreate;
		return;
	}

	// �ӽ� ���� ����
	SOCKET _tempSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (_tempSocket == INVALID_SOCKET)
	{
		return;
	}

	// Ȯ�� �Լ� ������ �޸𸮿� ���
	DWORD _dwByte;
	GUID _guid;

	// ConnectEx �Լ� ������ ȹ��
	_guid = WSAID_CONNECTEX;
	if (SOCKET_ERROR == WSAIoctl(
		_tempSocket, SIO_GET_EXTENSION_FUNCTION_POINTER,
		&_guid, sizeof(_guid),
		&m_lpfnConnectEx, sizeof(m_lpfnConnectEx),
		&_dwByte, NULL, NULL))
	{
		//throw ErrorCode::Listener_GetExFunction;
		closesocket(_tempSocket);
		_tempSocket = INVALID_SOCKET;
		return;
	}

	// �ӽ� ���� Ŭ��¡
	closesocket(_tempSocket);



	// Ŀ��Ʈ ���� ����
	m_ConnectorSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (m_ConnectorSocket == INVALID_SOCKET)
	{
		//throw ErrorCode::Connector_CreateConnectSocket;
		return;
	}

	// Ŀ��Ʈ ���� ���ε�
	SOCKADDR_IN _sockaddr;
	ZeroMemory(&_sockaddr, sizeof(_sockaddr));
	_sockaddr.sin_family = PF_INET;
	_sockaddr.sin_addr.s_addr = htonl(INADDR_ANY);

	if (bind(m_ConnectorSocket, reinterpret_cast<sockaddr*>(&_sockaddr), sizeof(_sockaddr)) == SOCKET_ERROR)
	{
		closesocket(m_ConnectorSocket);
		m_ConnectorSocket = INVALID_SOCKET;
		//throw ErrorCode::Connector_BindConnectSocket;
		return;
	}

	// Ŀ���� ������ IOCP�� ����
	CreateIoCompletionPort(reinterpret_cast<HANDLE>(m_ConnectorSocket), m_hIOCP, 0, 0);

	// connect
	SOverlapped* _psOverlapped = m_sOverlappedPool.GetUsableObject();
	_psOverlapped->eType = SOverlapped::eIOType::eConnect;
	_psOverlapped->socket = m_ConnectorSocket;

	SOCKADDR_IN _addr;
	ZeroMemory(&_addr, sizeof(_addr));
	_addr.sin_family = AF_INET;
	inet_pton(AF_INET, ip.c_str(), &_addr.sin_addr);
	_addr.sin_port = htons(port);

	if (m_lpfnConnectEx(
		m_ConnectorSocket, reinterpret_cast<SOCKADDR*>(&_addr), sizeof(_addr),
		nullptr, 0,
		&_dwByte, &_psOverlapped->wsaOverlapped) == SOCKET_ERROR)
	{
		closesocket(m_ConnectorSocket);
		return;
	}

	setsockopt(m_ConnectorSocket, SOL_SOCKET, SO_UPDATE_CONNECT_CONTEXT, NULL, 0);

	Run();
}

Connector::~Connector()
{
	// Ŀ��Ʈ ���� ����
	if (INVALID_SOCKET != m_ConnectorSocket)
	{
		closesocket(m_ConnectorSocket);
		m_ConnectorSocket = INVALID_SOCKET;
	}

	// �������� ������ ����
	//Stop();

	// IOCP �ڵ� ����
	if (nullptr != m_hIOCP)
	{
		CloseHandle(m_hIOCP);
	}
}

BOOL Connector::Send(PacketHeader* packet)
{
	if (m_ConnectorSocket == INVALID_SOCKET)
	{
		return FALSE;
	}

	// ��Ŷ ����
	if (packet->packetSize + 2 >= BUFSIZ)
	{
		return FALSE;
	}

	// �ɾ���� SOverlapped ����
	SOverlapped* _psOverlapped = m_sOverlappedPool.GetUsableObject();
	_psOverlapped->eType = SOverlapped::eIOType::eSend;
	_psOverlapped->socket = m_ConnectorSocket;

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
			m_sOverlappedPool.ReturnObject(_psOverlapped);
			//throw ErrorCode::Listener_WSASend;
			return FALSE;
		}
	}

	return TRUE;
}

BOOL Connector::Receive(PacketHeader**& networkMessageArray, int& arraySize)
{
	std::vector<PacketHeader*> _networkMessageVec;

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
	networkMessageArray = new PacketHeader * [arraySize];
	if (networkMessageArray == nullptr)
	{
		//throw ErrorCode::Connector_ArrayCreate;
		return FALSE;
	}

	for (int i = 0; i < arraySize; i++)
	{
		networkMessageArray[i] = _networkMessageVec[i];
	}

	return TRUE;
}

BOOL Connector::Disconnect()
{
	if (m_ConnectorSocket == INVALID_SOCKET)
	{
		return FALSE;
	}

	closesocket(m_ConnectorSocket);
	m_ConnectorSocket = INVALID_SOCKET;
	return TRUE;
}

BOOL Connector::Run()
{
	// �̹� �������� �����尡 �����Ǿ��ִ� ����
	if (m_WorkThreadArray != nullptr)
	{
		return TRUE;
	}

	return CreateThread();
}

BOOL Connector::Stop()
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

void Connector::Update()
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

			// Connect�� ���
			if (_psOverlapped->eType == SOverlapped::eIOType::eConnect)
			{
				ProcessConnect(_psOverlapped);
				continue;
			}

			if (_dwNumberOfBytesTransferred == 0)
			{
				// Ŭ���̾�Ʈ�� �������� ��(������ ����) Disconnect
				//Stop();
				break;
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
				ProcessReceive(_dwNumberOfBytesTransferred, _psOverlapped);
			}
			else if (_psOverlapped->eType == SOverlapped::eIOType::eSend)
			{
				// Overlapped ����� ����� ���� ��� ������ ���� ī��Ʈ ����
				ProcessSend(_dwNumberOfBytesTransferred, _psOverlapped);
			}
		}

		Sleep(0);
	}
}

BOOL Connector::CreateThread()
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
		m_WorkThreadArray[i] = new std::thread(&Connector::Update, this);
	}

	return TRUE;
}

void Connector::ProcessConnect(SOverlapped* psOverlapped)
{
	m_IsConnect = true;
	DoReceive(psOverlapped);
}

void Connector::ProcessSend(DWORD dwNumberOfBytesTransferred, SOverlapped* psOverlapped)
{
	m_sOverlappedPool.ReturnObject(psOverlapped);
}

void Connector::ProcessReceive(DWORD dwNumberOfBytesTransferred, SOverlapped* psOverlapped)
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
		AddNetworkMessage(_packet);

		// ó���� ��Ŷ ũ�⸸ŭ ó���ҷ� ����
		psOverlapped->iDataSize -= _usPacketSize;
	}

	// �ٽ� ���� �ɾ� �α�
	DoReceive(psOverlapped);
}

void Connector::DoReceive(SOverlapped* psOverlapped)
{
	// psOverlapped�� �����Ǿ����� ������ ����(�����Ǿ��ִٸ� ��Ȱ��)
	if (psOverlapped == nullptr)
	{
		psOverlapped = m_sOverlappedPool.GetUsableObject();
	}

	psOverlapped->eType = SOverlapped::eIOType::eReceive;
	psOverlapped->socket = m_ConnectorSocket;

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

void Connector::AddNetworkMessage(PacketHeader* packet)
{
	m_NetworkMessageQueue.push(packet);
}
