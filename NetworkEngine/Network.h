#pragma once
#include <string>
#include <unordered_map>
#include <WS2tcpip.h>
#include <MSWSock.h>
#include "NetworkStructDefine.h"

// 전방 선언
class Connector;
class Listener;

/// <summary>
/// [2021-12-06][최민경]
/// 커넥터와 리스너를 관리하는 네트워크 클래스
/// </summary>
class Network
{
public:
	Network();
	~Network();

#pragma region 외부에서 Network 클래스를 사용할 때 제공할 API
public:
	// name : 연결할 커넥터를 저장할 이름, port : 연결할 커넥터의 포트 번호, ip : 연결할 커넥터의 ip 주소
	BOOL CreateConnector(std::string connectorName, unsigned short port, std::string ip);
	// name : 오픈할 리스너를 저장할 이름, port : 오픈할 리스너의 포트 번호, headCount : 리스너에서 수용할 인원 제한
	BOOL CreateListener(std::string listenerName, unsigned short port, unsigned int headCount);

	// connectorName : 불러올 커넥터의 이름, packet : 보낼 패킷의 포인터
	BOOL ConnectorSend(std::string connectorName, PacketHeader* packet);
	// connectorName : 불러올 커넥터의 이름, networkMessageArray : <SSocket*, packet>의 배열, arraySize : 배열의 크기
	BOOL ConnectorReceive(std::string connectorName, PacketHeader**& networkMessageArray, int& arraySize);
	// connectorName : 연결을 종료할 커넥터의 이름
	BOOL ConnectorDisconnect(std::string connectorName);

	// listenerName : 불러올 리스너의 이름, psSocket : 대상 socket, packet : 보낼 패킷의 포인터
	BOOL ListenerSend(std::string listenerName, SSocket* psSocket, PacketHeader* packet);
	// listenerName : 불러올 리스너의 이름, networkMessageArray : <SSocket*, packet>의 배열, arraySize : 배열의 크기
	BOOL ListenerReceive(std::string listenerName, std::pair<SSocket*, PacketHeader*>*& networkMessageArray, int& arraySize);
	// listenerName : 연결을 종료할 리스너의 이름, socket : 대상 socket
	BOOL ListenerDisconnect(std::string listenerName, SOCKET socket);
	BOOL ListenerDisconnect(std::string listenerName, SSocket* psSocket);
	
	Listener*	FindListener(std::string name);
	Connector*	FindConnector(std::string name);
#pragma endregion

#pragma region Network 클래스 내부에서 사용할 멤버 함수
public:

#pragma endregion


#pragma region Network 클래스에서 필요한 멤버 변수
private:
	WSADATA		m_WSAData;		// 윈속 데이터 (단 한번만 생성 및 해제)

	std::unordered_map<std::string, Listener*>		m_ListenerUMap;
	std::unordered_map<std::string, Connector*>		m_ConnectorUMap;
#pragma endregion

};

