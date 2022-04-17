#pragma once
#include <shared_mutex>
#include "ObjectPool.h"
#include "SafeQueue.h"
/// <summary>
/// [2021-12-14][최민경]
/// 다른 커넥터들의 연결을 받는 리스너
/// </summary>
class Listener
{
public:
	Listener(unsigned short port, unsigned int headCount);
	~Listener();

public:
	BOOL		Send(SSocket* psSocket, PacketHeader* packet);
	BOOL		Receive(std::pair<SSocket*, PacketHeader*>*& networkMessageArray, int& arraySize);
	BOOL		DoDisconnect(SOCKET socket, SOverlapped* psOverlapped = nullptr);			// 비정상 종료(강제 종료) 수행

private:
	BOOL		RegisterExFunctionPointer(GUID guid, LPVOID lpfnEx);
	BOOL		BindListenSocket(unsigned short port);

	BOOL		Run();
	BOOL		Stop();
	void		Update();			// Work Function
	BOOL		CreateThread();
	void		DoAccept(SOverlapped* psOverlapped);
	void		DoReceive(SOCKET socket, SOverlapped* psOverlapped = nullptr);

	void		AddNetworkMessage(SSocket* psSocket, PacketHeader* packet);
	BOOL		AddUserList(SSocket* psSocket);

	SSocket*	FindUserSocket(SOCKET socket);

	//void		CheckTimeWaitSocket();		// 비정상 종료 이후 TIME_WAIT가 끝나 다시 접속을 받을 수 있는 소켓 체크
	//void		CreateTimeWaitThread();
	//BOOL		IsTimeWaitSocketExist();	// 정상 종료를 기다리는 소켓이 있는지 확인하는 함수

protected:
	void		ProcessAccept(SOverlapped* psOverlapped);		// 접속 요청 수행
	void		ProcessDisconnect(SOverlapped* psOverlapped);	// 정상 종료 수행
	void		ProcessSend(DWORD dwNumberOfBytesTransferred, SOverlapped* psOverlapped, SSocket* psSocket);
	void		ProcessReceive(DWORD dwNumberOfBytesTransferred, SOverlapped* psOverlapped, SSocket* psSocket);
	
public:
	std::vector<SSocket*>	m_UserVec;	// socket 정보
	
private:
	SOCKET			m_ListenSocket;	// 리슨 소켓
	HANDLE			m_hIOCP;		// IOCP 핸들
	unsigned int	m_HeadCount;

	int							m_ThreadCount;
	bool						m_IsRun;				// 작업 스레드 실행 플래그
	std::thread**				m_WorkThreadArray;		// 작업 스레드 배열
	mutable std::shared_mutex	m_WorkMutex;

	LPFN_ACCEPTEX				m_lpfnAcceptEx;
	LPFN_DISCONNECTEX			m_lpfnDisconnectEx;
	LPFN_GETACCEPTEXSOCKADDRS	m_lpfnGetAcceptExSockAddrs;

	ObjectPool<SSocket>		m_sSocketPool;
	ObjectPool<SOverlapped>	m_sOverlappedPool;

	SafeQueue<std::pair<SSocket*, PacketHeader*>>	m_NetworkMessageQueue;
	
	/*std::thread							m_TimeWaitThread;
	std::vector<std::pair<DWORD, SSocket*>>	m_WaitSocketVec;
	bool									m_IsTimeWait;
	mutable std::shared_mutex				m_TimeWaitMutex;*/
};

