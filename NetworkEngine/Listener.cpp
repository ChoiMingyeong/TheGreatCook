#include "pch.h"
#include "Listener.h"

Listener::Listener(unsigned short port, unsigned int headCount)
	: m_ListenSocket(INVALID_SOCKET), m_hIOCP(nullptr), m_HeadCount(headCount),
	m_sSocketPool(headCount + headCount / 2), m_sOverlappedPool(headCount + 10, true),
	m_IsRun(false), m_WorkThreadArray(nullptr)/*,
	m_IsTimeWait(false)*/
{
	// IOCP 핸들 생성
	m_hIOCP = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
	if (m_hIOCP == nullptr)
	{
		// 에러 발생 : IOCP 핸들 생성 실패
		//throw ErrorCode::Listener_hIOCPCreate;
		return;
	}

	// 리슨 소켓 생성
	m_ListenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (m_ListenSocket == INVALID_SOCKET)
	{
		// 에러 발생 : ListenSocket 생성 오류
		//throw ErrorCode::Listener_CreateListenSocket;
		return;
	}

	// 리슨 소켓 바인딩
	if (BindListenSocket(port) == FALSE)
	{
		closesocket(m_ListenSocket);
		m_ListenSocket = INVALID_SOCKET;
		//throw ErrorCode::Listener_BindListenSocket;
		return;
	}

	// 리슨 소켓을 IOCP에 연결
	CreateIoCompletionPort(reinterpret_cast<HANDLE>(m_ListenSocket), m_hIOCP, 0, 0);

	// 리슨 시작
	if (listen(m_ListenSocket, SOMAXCONN) == SOCKET_ERROR)
	{
		closesocket(m_ListenSocket);
		m_ListenSocket = INVALID_SOCKET;
		//throw ErrorCode::Listener_Listen;
		return;
	}

	// 확장 함수 포인터 메모리에 등록
	DWORD _dwByte;
	GUID _guid;

	// AcceptEx 함수 포인터 획득
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

	// DisconnectEx 함수 포인터 획득
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

	// GetAcceptExSockAddrs 함수 포인터 획득
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

	// Accept 걸어놓기
	for (int i = 0; i < m_HeadCount; i++)
	{
		// Overlapped 생성 및 AcceptEx 실행
		SOverlapped* _psOverlapped = m_sOverlappedPool.GetUsableObject();
		_psOverlapped->eType = SOverlapped::eIOType::eAccept;

		// 소켓 생성
		_psOverlapped->socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

		if (_psOverlapped->socket == INVALID_SOCKET)
		{
			//throw ErrorCode::Listener_SocketCreate;
			return;
		}

		// 즉석 종료 옵션
		LINGER _linger;
		_linger.l_onoff = 1;
		_linger.l_linger = 0;
		setsockopt(_psOverlapped->socket, SOL_SOCKET, SO_LINGER, (char*)&_linger, sizeof(_linger));

		// Accept 실행
		DoAccept(_psOverlapped);
	}

	// 스레드 실행
	Run();
}

Listener::~Listener()
{
	// 리슨 소켓 제거
	if (INVALID_SOCKET != m_ListenSocket)
	{
		closesocket(m_ListenSocket);
		m_ListenSocket = INVALID_SOCKET;
	}

	// 실행중인 스레드 종료
	//Stop();

	// IOCP 핸들 닫음
	if (nullptr != m_hIOCP)
	{
		CloseHandle(m_hIOCP);
	}
}

BOOL Listener::Send(SSocket* psSocket, PacketHeader* packet)
{
	// 패킷 복사
	if (packet->packetSize + 2 >= BUFSIZ)
	{
		return FALSE;
	}
	// 걸어놓을 SOverlapped 설정
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

	// WSABUF 셋팅
	WSABUF _wsaBuffer;
	_wsaBuffer.buf = _psOverlapped->buffer;
	_wsaBuffer.len = _psOverlapped->iDataSize;

	// WSASend() 오버랩드 걸기
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
			// WSASend() 에러
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

	// GetAcceptExSockAddrs 함수 포인터 획득
	if (SOCKET_ERROR == WSAIoctl(
		m_ListenSocket, SIO_GET_EXTENSION_FUNCTION_POINTER,
		&guid, sizeof(guid),
		&lpfnEx, sizeof(lpfnEx),
		&_dwByte, NULL, NULL))
	{
		// GetAcceptExSockAddrs 생성 오류
		//throw ErrorCode::Listener_GetExFunction;
		return FALSE;
	}

	return TRUE;
}

BOOL Listener::BindListenSocket(unsigned short port)
{
	// 서버 정보 설정
	SOCKADDR_IN _serverAddr;
	ZeroMemory(&_serverAddr, sizeof(_serverAddr));
	_serverAddr.sin_family = AF_INET;
	_serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);
	_serverAddr.sin_port = htons(port);

	// 바인딩
	if (SOCKET_ERROR == ::bind(m_ListenSocket, reinterpret_cast<SOCKADDR*>(&_serverAddr), sizeof(_serverAddr)))
	{
		return FALSE;
	}

	return TRUE;
}

BOOL Listener::CreateThread()
{
	// 스레드 개수 설정
	SYSTEM_INFO systemInfo;
	GetSystemInfo(&systemInfo);
	m_ThreadCount = systemInfo.dwNumberOfProcessors * 2;

	// 스레드 생성
	m_WorkThreadArray = new std::thread * [m_ThreadCount];
	if (m_WorkThreadArray == nullptr)
	{
		return FALSE;
	}

	// 스레드 실행 플래그
	m_IsRun = true;

	for (int i = 0; i < m_ThreadCount; i++)
	{
		m_WorkThreadArray[i] = new std::thread(&Listener::Update, this);
	}

	return TRUE;
}

BOOL Listener::Run()
{
	// 이미 실행중인 스레드가 생성되어있는 상태
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
		ULONG _numEnries = 0;	// 가져온 Entries의 수
		SOverlapped* _psOverlapped = nullptr;
		SSocket* _psSocket = nullptr;

		std::unique_lock<std::shared_mutex> _ulk(m_WorkMutex);
		if (FALSE == GetQueuedCompletionStatusEx(m_hIOCP, _entries, _entryCount, &_numEnries, INFINITE, FALSE))
		{
			// 가져오기 실패
			Sleep(0);
			continue;
		}

		// 가져온 수 만큼 반복
		for (ULONG i = 0; i < _numEnries; ++i)
		{
			_dwNumberOfBytesTransferred = _entries[i].dwNumberOfBytesTransferred;

			_psOverlapped = reinterpret_cast<SOverlapped*>(_entries[i].lpOverlapped);
			if (_psOverlapped == nullptr)
			{
				continue;
			}

			// Accept인 경우
			if (_psOverlapped->eType == SOverlapped::eIOType::eAccept)
			{
				ProcessAccept(_psOverlapped);
				continue;
			}

			// Disconnect인 경우
			if (_psOverlapped->eType == SOverlapped::eIOType::eDisconnect)
			{
				ProcessDisconnect(_psOverlapped);
				continue;
			}

			if (_dwNumberOfBytesTransferred == 0)
			{
				// 클라이언트가 종료했을 때(비정상 종료) Disconnect
				DoDisconnect(_psOverlapped->socket, _psOverlapped);
				continue;
			}

			if (_dwNumberOfBytesTransferred == -1)
			{
				continue;
			}

			// SSocket 확인 (Accept가 아니면 SSocket이 담긴다)
			_psSocket = reinterpret_cast<SSocket*>(_entries[i].lpCompletionKey);
			if (_psSocket == nullptr)
			{
				if (_psOverlapped->eType != SOverlapped::eIOType::eAccept)
				{
					// PQCS : GQCS를 리턴시킬 수 있는 함수, 주로 종료에 쓰인다.
					PostQueuedCompletionStatus(m_hIOCP, 0, _entries[i].lpCompletionKey, _entries[i].lpOverlapped);
					continue;
				}
			}

			// IO_Type에 따라 처리 선택
			if (_psOverlapped->eType == SOverlapped::eIOType::eReceive)
			{
				// Overlapped 결과가 제대로 나온 경우 소켓의 참조 카운트 감소
				ProcessReceive(_dwNumberOfBytesTransferred, _psOverlapped, _psSocket);
				continue;
			}
			else if (_psOverlapped->eType == SOverlapped::eIOType::eSend)
			{
				// Overlapped 결과가 제대로 나온 경우 소켓의 참조 카운트 감소
				ProcessSend(_dwNumberOfBytesTransferred, _psOverlapped, _psSocket);
				continue;
			}
		}

		Sleep(0);
	}
}

BOOL Listener::Stop()
{
	// 스레드 종료 플래그
	m_IsRun = false;

	// 종료할 스레드가 생성되어있지 않은 상태
	if (nullptr == m_WorkThreadArray)
	{
		return TRUE;
	}

	// 스레드 종료 대기
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
	// 클라이언트 정보 확인
	int _addrLen = sizeof(sockaddr_in) + 16;
	sockaddr_in* _pLocal = nullptr;
	sockaddr_in* _pRemote = nullptr;

	// 소켓 정보 가져오기
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

	// 클라이언트 생성 및 등록
	SSocket* _psSocket = m_sSocketPool.GetUsableObject();
	_psSocket->socket = psOverlapped->socket;
	_psSocket->ip = std::string(_buf);
	_psSocket->port = ntohs(_pRemote->sin_port);

	// IOCP 생성 및 소켓 등록
	CreateIoCompletionPort(reinterpret_cast<HANDLE>(_psSocket->socket), m_hIOCP, reinterpret_cast<ULONG_PTR>(_psSocket), 0);

	// 수용 인원 초과인지 확인
	if (!AddUserList(_psSocket))
	{
		// 서버 최대 접속 수 초과
		DoDisconnect(psOverlapped->socket, psOverlapped);
		return;
	}

	// 리시브 걸기
	DoReceive(_psSocket->socket, psOverlapped);
}

void Listener::ProcessDisconnect(SOverlapped* psOverlapped)
{
	// 클라이언트 연결 목록에서 제거
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

	// 소켓 재활용을 위해 다시 Accept를 건다.
	psOverlapped->eType = SOverlapped::eIOType::eAccept;

	// AcceptEx 실행
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

		// HeaderSize 만큼 패킷이 안들어옴
		if (psOverlapped->iDataSize < _cusHeaderSize)
		{
			break;
		}

		unsigned short _usBodySize = *reinterpret_cast<unsigned short*>(psOverlapped->buffer);
		unsigned short _usPacketSize = _cusHeaderSize + _usBodySize;

		// 완전한 패킷이 안들어옴
		if (_usPacketSize > psOverlapped->iDataSize)
		{
			break;
		}

		// 데이터들을 이번에 처리한만큼 당긴다.
		memcpy_s(psOverlapped->buffer, psOverlapped->iDataSize,
			psOverlapped->buffer + _usPacketSize, psOverlapped->iDataSize - _usPacketSize);

		PacketHeader* _packet = reinterpret_cast<PacketHeader*>(psOverlapped->buffer);

		// 메세지 큐에 들어온 메세지 추가
		AddNetworkMessage(psSocket, _packet);

		// 처리한 패킷 크기만큼 처리할량 감소
		psOverlapped->iDataSize -= _usPacketSize;
	}

	// 다시 수신 걸어 두기
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

	// Disconnect 실행(소켓 재활용 옵션)
	if (m_lpfnDisconnectEx(psOverlapped->socket, &psOverlapped->wsaOverlapped, TF_REUSE_SOCKET, 0) == false)
	{
		if (WSAGetLastError() != ERROR_IO_PENDING)
		{
			//throw ErrorCode::Listener_DisconnectEx;
			return FALSE;
		}

		// 우아한 종료 시도 흔적
		// m_WaitSocketVec.push_back(std::make_pair(GetTickCount64(), FindUserSocket(socket)));
	}

	return TRUE;
}

void Listener::DoAccept(SOverlapped* psOverlapped)
{
	// AcceptEx 실행
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
	// psOverlapped가 지정되어있지 않으면 생성(지정되어있다면 재활용)
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
	// 서버 최대 수용 수가 다 차면 연결을 끊고 리턴
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
//		// TIME_WAIT를 기다리는 소켓이 없으면 continue;
//		if (IsTimeWaitSocketExist() == TRUE)
//		{
//			std::unique_lock<std::shared_mutex> _ulk(m_TimeWaitMutex);
//
//			static const DWORD _defaultWaitTime = 240 * 1000;
//			DWORD _nowTime = GetTickCount64();
//
//			// Time Wait 시간이 지난 소켓은 소켓풀로 되돌려주기
//			for (auto _iter = m_WaitSocketVec.begin(); _iter != m_WaitSocketVec.end();)
//			{
//				DWORD _waitTime = _nowTime - _iter->first;
//				if ((_waitTime) >= _defaultWaitTime)
//				{
//					// Overlapped 생성 및 AcceptEx 실행
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
//	// 이미 스레드 생성이 된 경우 리턴
//	if (m_IsTimeWait == true)
//	{
//		return;
//	}
//
//	// 스레드 생성
//	m_IsTimeWait = true;
//	m_TimeWaitThread = std::thread(&Listener::CheckTimeWaitSocket, this);
//}
//
//BOOL Listener::IsTimeWaitSocketExist()
//{
//	std::shared_lock<std::shared_mutex> _slk(m_TimeWaitMutex);
//	return !m_WaitSocketVec.empty();
//}
