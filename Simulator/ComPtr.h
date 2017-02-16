#pragma once

template<typename T> class ComPtr
{
	T* m_ComPtr;
public:
	ComPtr()
		: m_ComPtr(nullptr)
	{
		static_assert (std::is_base_of<IUnknown, T>::value, "T needs to be IUnknown based");
	}

	explicit ComPtr(T* lComPtr, bool addRef = true)
		: m_ComPtr(lComPtr)
	{
		static_assert (std::is_base_of<IUnknown, T>::value, "T needs to be IUnknown based");
		if ((m_ComPtr != nullptr) && addRef)
			m_ComPtr->AddRef();
	}

	ComPtr(nullptr_t np)
		: m_ComPtr(nullptr)
	{ }

	ComPtr(const ComPtr<T>& lComPtrObj)
	{
		static_assert (std::is_base_of<IUnknown, T>::value, "T needs to be IUnknown based");
		m_ComPtr = lComPtrObj.m_ComPtr;
		if (m_ComPtr)
			m_ComPtr->AddRef();
	}

	ComPtr(ComPtr<T>&& lComPtrObj)
	{
		m_ComPtr = lComPtrObj.m_ComPtr;
		lComPtrObj.m_ComPtr = nullptr;
	}

	_Check_return_ HRESULT CoCreateInstance(
		_In_ REFCLSID rclsid,
		_Inout_opt_ LPUNKNOWN pUnkOuter = NULL,
		_In_ DWORD dwClsContext = CLSCTX_ALL) throw()
	{
		rassert(m_ComPtr == NULL);
		return ::CoCreateInstance(rclsid, pUnkOuter, dwClsContext, __uuidof(T), (void**)&m_ComPtr);
	}

	T* operator=(T* lComPtr)
	{
		if (m_ComPtr)
			m_ComPtr->Release();

		m_ComPtr = lComPtr;

		if (m_ComPtr)
			m_ComPtr->AddRef();

		return m_ComPtr;
	}

	T* operator=(const ComPtr<T>& lComPtrObj)
	{
		if (m_ComPtr)
			m_ComPtr->Release();

		m_ComPtr = lComPtrObj.m_ComPtr;

		if (m_ComPtr)
			m_ComPtr->AddRef();

		return m_ComPtr;
	}

	~ComPtr()
	{
		static_assert (std::is_base_of<IUnknown, T>::value, "T needs to be IUnknown based");
		if (m_ComPtr)
		{
			m_ComPtr->Release();
			m_ComPtr = nullptr;
		}
	}

	// Attach to an interface (does not call AddRef)
	void Attach(T* ptr)
	{
		assert(ptr != nullptr);

		if (m_ComPtr != nullptr)
			m_ComPtr->Release();

		m_ComPtr = ptr;
	}

	operator T*() const
	{
		return m_ComPtr;
	}

	T* Get() const
	{
		return m_ComPtr;
	}

	T* const* GetInterfacePtr() const
	{
		return &m_ComPtr;
	}

	T** operator&()
	{
		//The assert on operator& usually indicates a bug. Could be a potential memory leak.
		// If this really what is needed, however, use GetInterface() explicitly.
		assert(nullptr == m_ComPtr);
		return &m_ComPtr;
	}

	T* operator->() const
	{
		return m_ComPtr;
	}

	bool operator==(const ComPtr<T>& other) const
	{
		return this->m_ComPtr == other.m_ComPtr;
	}

	bool operator==(nullptr_t np) const
	{
		return this->m_ComPtr == np;
	}

	template <typename I>
	HRESULT QueryInterface(I **interfacePtr)
	{
		return m_ComPtr->QueryInterface(IID_PPV_ARGS(interfacePtr));
	}
};


template<typename T> class ComTaskMemPtr
{
	T* _ptr;

public:
	ComTaskMemPtr()
		: _ptr(nullptr)
	{ }

	ComTaskMemPtr(const ComTaskMemPtr&) = delete;
	ComTaskMemPtr(ComTaskMemPtr&&) = delete;
	ComTaskMemPtr& operator= (const ComTaskMemPtr&) = delete;
	ComTaskMemPtr& operator= (ComTaskMemPtr&&) = delete;

	~ComTaskMemPtr()
	{
		if (_ptr != nullptr)
		{
			CoTaskMemFree(_ptr);
			_ptr = nullptr;
		}
	}

	T** operator&()
	{
		//The assert on operator& usually indicates a bug. Could be a potential memory leak.
		// If this really what is needed, however, use GetPtr() explicitly.
		assert(_ptr == nullptr);
		return &_ptr;
	}

	T* GetPtr() const
	{
		return _ptr;
	}
};
