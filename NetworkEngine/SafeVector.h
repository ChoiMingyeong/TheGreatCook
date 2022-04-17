#pragma once
/// <summary>
/// [2021-12-16][�ֹΰ�]
/// shared_mutex(C++ 17)�� ����� ���� �����忡 ������ ����
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
	// TODO: ���⿡ return ���� �����մϴ�.
}
