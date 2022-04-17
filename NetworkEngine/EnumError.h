#pragma once
/// <summary>
/// [2021-12-07][최민경]
/// 에러 발생 시에 throw되는 에러코드의 정의 모음
/// </summary>
enum class ErrorCode
{
	ObjectPool_NumberIsZero,		// 오브젝트풀 생성시에 생성 개수가 0, 혹은 0 이하의 값이 들어왔을 때

	Network_WSAStartup,				// Network - WSAStartup 오류
	Network_AlreadyExistName,		// Network - 이미 등록된 이름의 커넥터/리스너
	Network_NotExist,				// Network - 등록되지 않은 커넥터/리스너 찾기

	Listener_hIOCPCreate,			// Listener - IOCP 핸들 생성 오류
	Listener_CreateListenSocket,	// Listener - 리슨 소켓 생성 오류
	Listener_BindListenSocket,		// Listener - 리슨 소켓 바인딩 오류
	Listener_GetExFunction,			// Listener - Ex 함수를 가져오는데에 실패
	Listener_ArrayCreate,			// Listener - 네트워크 메세지 배열 생성 실패
	Listener_SocketCreate,			// Listener - 소켓 생성 오류
	Listener_AcceptEx,				// Listener - AcceptEx 실패
	Listener_DisconnectEx,			// Listener - DisconnectEx 실패
	Listener_WSAReceive,			// Listener - WSAReceive 오류
	Listener_WSASend,				// Listener - WSASend 오류
	Listener_Listen,				// Listener - Listen 오류
	Listener_OverBuffer,			// Listener - Buffer보다 큰 패킷 전송 시도

	Connector_hIOCPCreate,			// Connector - IOCP 핸들 생성 오류
	Connector_ArrayCreate,			// Connector - 네트워크 메세지 배열 생성 실패
	Connector_CreateConnectSocket,	// Connector - 커넥터 소켓 생성 오류
	Connector_BindConnectSocket,	// Connector - 커넥터 소켓 바인딩 오류
	Connector_GetExFunction,		// Connector - Ex 함수를 가져오는데에 실패
};