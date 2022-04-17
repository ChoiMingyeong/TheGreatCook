#include "pch.h"
#include "Network.h"
#include "Connector.h"
#include "Listener.h"

Network::Network()
{
	// ��� Network Ŭ�������� �� �ѹ��� �ʱ�ȭ
	static bool _isInit = false;
	if (_isInit == false)
	{
		// ���� ����
		if (WSAStartup(MAKEWORD(2, 2), &m_WSAData) != 0)
		{
			// ���� �߻� : WSAStartup ����
			//throw ErrorCode::Network_WSAStartup;
			return;
		}
		_isInit = true;
	}
}

Network::~Network()
{
	// Listener ����
	for (auto _listener : m_ListenerUMap)
	{
		delete _listener.second;
		_listener.second = nullptr;
	}
	m_ListenerUMap.clear();

	// Connector ����
	for (auto _connector : m_ConnectorUMap)
	{
		delete _connector.second;
		_connector.second = nullptr;
	}
	m_ConnectorUMap.clear();

	// ���� ����
	WSACleanup();
}

BOOL Network::CreateConnector(std::string name, unsigned short port, std::string ip)
{
	// �̹� ��ϵ� �̸��� Ŀ���Ͷ�� FALSE ����
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
	// �̹� ��ϵ� �̸��� �����ʶ�� FALSE ����
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
	// ��ϵ��� ���� Ŀ���͸� FALSE ����
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
	// ��ϵ��� ���� Ŀ���͸� FALSE ����
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
	// ��ϵ��� ���� Ŀ���͸� FALSE ����
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
	// ��ϵ��� ���� �����ʸ� FALSE ����
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
	// ��ϵ��� ���� �����ʸ� FALSE ����
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
	// ��ϵ��� ���� Ŀ���͸� FALSE ����
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