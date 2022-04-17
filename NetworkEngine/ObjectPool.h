#pragma once
/// <summary>
/// [2021-11-17][최민경]
/// 다중 스레드에 안전한 오브젝트 풀
/// </summary>
template <typename T>
class ObjectPool
{
#pragma region CObject
private:
	template<typename T>
	class CObject
	{
	public:
		CObject()
		{
			object = T();
			isUsable = true;
		}
		~CObject()
		{
		}

	public:
		T			object;			// 오브젝트 원본
		bool		isUsable;		// 사용 가능 여부(참조 여부)
	};
#pragma endregion

public:
	ObjectPool(int num = 100, bool isExpand = false);
	~ObjectPool();

public:
	// 사용되고 있지 않은 오브젝트를 탐색하여 가져오는 함수
	T* GetUsableObject();
	// 사용이 끝난 오브젝트를 풀에 돌려주는 함수
	void ReturnObject(T* object);

private:
	// num개만큼 오브젝트 풀에 오브젝트를 생성하는 함수
	void CreateObjectPool(int num);

private:
	mutable std::shared_mutex		m_SharedMutex;
	std::vector<CObject<T>>			m_ObjectVec;
	bool							m_isExpand;			// 사용 가능한 오브젝트가 없을 때 추가 확장 여부
};

template<typename T>
inline ObjectPool<T>::ObjectPool(int num, bool isExpand)
	: m_isExpand(isExpand)
{
	CreateObjectPool(num);
}

template<typename T>
inline ObjectPool<T>::~ObjectPool()
{
	//// 오브젝트 풀 내의 모든 오브젝트 찾아서 해제하기
	//for (CObject<T>*& obj : m_ObjectVec)
	//{
	//	T* _tempObject = obj->object;
	//	_tempObject->~T();
	//}
}

template<typename T>
inline T* ObjectPool<T>::GetUsableObject()
{
	std::unique_lock<std::shared_mutex> _ulk(m_SharedMutex);

	T* _usableObject = nullptr;

	do
	{
		// 오브젝트 풀에서 사용 가능한 오브젝트 찾기
		for (CObject<T>& obj : m_ObjectVec)
		{
			if (obj.isUsable == true)
			{
				_usableObject = &(obj.object);
				obj.isUsable = false;
				break;
			}
		}

		// 확장 옵션이 켜져있으면 오브젝트 풀 늘리기
		if (m_isExpand == true)
		{
			if (_usableObject == nullptr)
			{
				CreateObjectPool(m_ObjectVec.size());
			}
		}
		else
		{
			break;
		}

	} while (_usableObject == nullptr);

	return _usableObject;
}

template<typename T>
inline void ObjectPool<T>::ReturnObject(T* object)
{
	// 오브젝트 풀에서 해당 오브젝트 찾아서 다시 준비 시키기
	for (CObject<T>& obj : m_ObjectVec)
	{
		if (&(obj.object) == object)
		{
			obj.isUsable = true;
		}
	}
}

template<typename T>
inline void ObjectPool<T>::CreateObjectPool(int num)
{
	for (int i = 0; i < num; i++)
	{
		m_ObjectVec.push_back(CObject<T>());
	}
}
