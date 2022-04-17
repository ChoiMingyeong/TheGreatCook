#include "pch.h"
#include "Connector.h"

Connector::Connector(unsigned short port, std::string ip)
	:m_ConnectorSocket(INVALID_SOCKET), m_hIOCP(nullptr), m_IsConnect(false),
	m_lpfnConnectEx(nullptr), m_WorkThreadArray(nullptr), m_IsRun(false),
	m_sOverlappedPool(10, true)
{
	// IOCP 핸들 생성
	m_hIOCP = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
	if (m_hIOCP == nullptr)
	{
		// 에러 발생 : IOCP 핸들 생성 실패
		//throw ErrorCode::Connector_hIOCPCreate;
		return;
	}

	// 임시 소켓 생성
	SOCKET _tempSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (_tempSocket == INVALID_SOCKET)
	{
		return;
	}

	// 확장 함수 포인터 메모리에 등록
	DWORD _dwByte;
	GUID _guid;

	// ConnectEx 함수 포인터 획득
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

	// 임시 소켓 클로징
	closesocket(_tempSocket);



	// 커넥트 소켓 생성
	m_ConnectorSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (m_ConnectorSocket == INVALID_SOCKET)
	{
		//throw ErrorCode::Connector_CreateConnectSocket;
		return;
	}

	// 커넥트 소켓 바인딩
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

	// 커넥터 소켓을 IOCP에 연결
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
	// 커넥트 소켓 제거
	if (INVALID_SOCKET != m_ConnectorSocket)
	{
		closesocket(m_ConnectorSocket);
		m_ConnectorSocket = INVALID_SOCKET;
	}

	// 실행중인 스레드 종료
	//Stop();

	// IOCP 핸들 닫음
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

	// 패킷 복사
	if (packet->packetSize + 2 >= BUFSIZ)
	{
		return FALSE;
	}

	// 걸어놓을 SOverlapped 설정
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
	// 이미 실행중인 스레드가 생성되어있는 상태
	if (m_WorkThreadArray != nullptr)
	{
		return TRUE;
	}

	return CreateThread();
}

BOOL Connector::Stop()
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

void Connector::Update()
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

			// Connect인 경우
			if (_psOverlapped->eType == SOverlapped::eIOType::eConnect)
			{
				ProcessConnect(_psOverlapped);
				continue;
			}

			if (_dwNumberOfBytesTransferred == 0)
			{
				// 클라이언트가 종료했을 때(비정상 종료) Disconnect
				//Stop();
				break;
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
				ProcessReceive(_dwNumberOfBytesTransferred, _psOverlapped);
			}
			else if (_psOverlapped->eType == SOverlapped::eIOType::eSend)
			{
				// Overlapped 결과가 제대로 나온 경우 소켓의 참조 카운트 감소
				ProcessSend(_dwNumberOfBytesTransferred, _psOverlapped);
			}
		}

		Sleep(0);
	}
}

BOOL Connector::CreateThread()
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
		AddNetworkMessage(_packet);

		// 처리한 패킷 크기만큼 처리할량 감소
		psOverlapped->iDataSize -= _usPacketSize;
	}

	// 다시 수신 걸어 두기
	DoReceive(psOverlapped);
}

void Connector::DoReceive(SOverlapped* psOverlapped)
{
	// psOverlapped가 지정되어있지 않으면 생성(지정되어있다면 재활용)
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
