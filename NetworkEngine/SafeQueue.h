#pragma once
#include <queue>
/// <summary>
/// [2021-12-14][최민경]
/// shared_mutex(C++ 17)를 사용한 다중 스레드에 안전한 큐
/// </summary>
template<typename T>
class SafeQueue
{
public:
	SafeQueue();
	~SafeQueue();

public:
	T				front();
	void			pop();
	void			push(T);
	size_t			size();
	bool			empty();

private:
	mutable std::shared_mutex	m_SharedMutex;
	std::queue<T>				m_Queue;
};

template<typename T>
inline SafeQueue<T>::SafeQueue()
{
}

template<typename T>
inline SafeQueue<T>::~SafeQueue()
{
}

template<typename T>
inline T SafeQueue<T>::front()
{
	std::shared_lock<std::shared_mutex> _slk(m_SharedMutex);
	return m_Queue.front();
}

template<typename T>
inline void SafeQueue<T>::pop()
{
	std::unique_lock<std::shared_mutex> _ulk(m_SharedMutex);
	m_Queue.pop();
}

template<typename T>
inline void SafeQueue<T>::push(T t)
{
	std::unique_lock<std::shared_mutex> _ulk(m_SharedMutex);
	m_Queue.push(t);
}

template<typename T>
inline size_t SafeQueue<T>::size()
{
	std::shared_lock<std::shared_mutex> _slk(m_SharedMutex);
	return m_Queue.size();
}

template<typename T>
inline bool SafeQueue<T>::empty()
{
	std::shared_lock<std::shared_mutex> _slk(m_SharedMutex);
	return m_Queue.empty();
}
