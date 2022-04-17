#pragma once
/// <summary>
/// [2021-12-16][최민경]
/// shared_mutex(C++ 17)를 사용한 다중 스레드에 안전한 벡터
/// </summary>
template<typename T>
class SafeVector
{
public:
	SafeVector();
	~SafeVector();

public:
	T& operator[](int index);

private:
	mutable std::shared_mutex	m_SharedMutex;
	std::vector<T>				m_Vector;
};

template<typename T>
inline SafeVector<T>::SafeVector()
{
}

template<typename T>
inline SafeVector<T>::~SafeVector()
{
}

template<typename T>
inline T& SafeVector<T>::operator[](int index)
{
	// TODO: 여기에 return 문을 삽입합니다.
}
