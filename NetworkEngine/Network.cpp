#include "pch.h"
#include "Network.h"
#include "Connector.h"
#include "Listener.h"

Network::Network()
{
	// 모든 Network 클래스에서 단 한번만 초기화
	static bool _isInit = false;
	if (_isInit == false)
	{
		// 윈속 시작
		if (WSAStartup(MAKEWORD(2, 2), &m_WSAData) != 0)
		{
			// 에러 발생 : WSAStartup 실패
			//throw ErrorCode::Network_WSAStartup;
			return;
		}
		_isInit = true;
	}
}

Network::~Network()
{
	// Listener 비우기
	for (auto _listener : m_ListenerUMap)
	{
		delete _listener.second;
		_listener.second = nullptr;
	}
	m_ListenerUMap.clear();

	// Connector 비우기
	for (auto _connector : m_ConnectorUMap)
	{
		delete _connector.second;
		_connector.second = nullptr;
	}
	m_ConnectorUMap.clear();

	// 윈속 종료
	WSACleanup();
}

BOOL Network::CreateConnector(std::string name, unsigned short port, std::string ip)
{
	// 이미 등록된 이름의 커넥터라면 FALSE 리턴
	if (FindConnector(name) != nullptr)
	{
		//throw ErrorCode::Network_AlreadyExistName;
		return FALSE;
	}

	m_ConnectorUMap.insert(std::make_pair(name, new Connector(port, ip)));

	return TRUE;
}

BOOL Network::CreateListener(std::string name, unsigned short port, unsigned int headCount)
{
	// 이미 등록된 이름의 리스너라면 FALSE 리턴
	if (FindListener(name) != nullptr)
	{
		//throw ErrorCode::Network_AlreadyExistName;
		return FALSE;
	}

	m_ListenerUMap.insert(std::make_pair(name, new Listener(port, headCount)));

	return 0;
}

BOOL Network::ConnectorSend(std::string name, PacketHeader* packet)
{
	// 등록되지 않은 커넥터면 FALSE 리턴
	Connector* _pConnector = FindConnector(name);
	if (_pConnector == nullptr)
	{
		//throw ErrorCode::Network_NotExist;
		return FALSE;
	}

	return _pConnector->Send(packet);
}

BOOL Network::ConnectorReceive(std::string name, PacketHeader**& networkMessageArray, int& arraySize)
{
	// 등록되지 않은 커넥터면 FALSE 리턴
	Connector* _pConnector = FindConnector(name);
	if (_pConnector == nullptr)
	{
		//throw ErrorCode::Network_NotExist;
		return FALSE;
	}

	return _pConnector->Receive(networkMessageArray, arraySize);
}

BOOL Network::ConnectorDisconnect(std::string name)
{
	// 등록되지 않은 커넥터면 FALSE 리턴
	Connector* _pConnector = FindConnector(name);
	if (_pConnector == nullptr)
	{
		//throw ErrorCode::Network_NotExist;
		return FALSE;
	}

	return _pConnector->Disconnect();
}

BOOL Network::ListenerSend(std::string name, SSocket* psSocket, PacketHeader* packet)
{
	// 등록되지 않은 리스너면 FALSE 리턴
	Listener* _pListener = FindListener(name);
	if (_pListener == nullptr)
	{
		//throw ErrorCode::Network_NotExist;
		return FALSE;
	}

	return _pListener->Send(psSocket, packet);
}

BOOL Network::ListenerReceive(std::string name, std::pair<SSocket*, PacketHeader*>*& networkMessageArray, int& arraySize)
{
	// 등록되지 않은 리스너면 FALSE 리턴
	Listener* _pListener = FindListener(name);
	if (_pListener == nullptr)
	{
		//throw ErrorCode::Network_NotExist;
		return FALSE;
	}

	return _pListener->Receive(networkMessageArray, arraySize);
}

BOOL Network::ListenerDisconnect(std::string listenerName, SOCKET socket)
{
	// 등록되지 않은 커넥터면 FALSE 리턴
	Listener* _pListener = FindListener(listenerName);
	if (_pListener == nullptr)
	{
		//throw ErrorCode::Network_NotExist;
		return FALSE;
	}

	return _pListener->DoDisconnect(socket);
}

BOOL Network::ListenerDisconnect(std::string listenerName, SSocket* psSocket)
{
	return ListenerDisconnect(listenerName, psSocket->socket);
}

Listener* Network::FindListener(std::string name)
{
	auto _result = m_ListenerUMap.find(name);
	if (_result == m_ListenerUMap.end())
	{
		return nullptr;
	}
	return _result->second;
}

Connector* Network::FindConnector(std::string name)
{
	auto _result = m_ConnectorUMap.find(name);
	if (_result == m_ConnectorUMap.end())
	{
		return nullptr;
	}
	return _result->second;
}