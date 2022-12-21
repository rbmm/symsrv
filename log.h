#pragma once

class WLog
{
	PVOID _BaseAddress = 0;
	ULONG _RegionSize = 0, _Ptr = 0;

	PWSTR _buf()
	{
		return (PWSTR)((ULONG_PTR)_BaseAddress + _Ptr);
	}

	ULONG _cch()
	{
		return (_RegionSize - _Ptr) / sizeof(WCHAR);
	}

public:
	void operator >> (HWND hwnd);

	ULONG Init(SIZE_T RegionSize);

	~WLog();

	WLog(WLog&&) = delete;
	WLog(WLog&) = delete;
	WLog(){}

	operator PCWSTR()
	{
		return (PCWSTR)_BaseAddress;
	}

	WLog& operator ()(PCWSTR format, ...);
	WLog& operator << (PCWSTR str);

	WLog& operator[](HRESULT dwError);

	BOOL IsEmpty()
	{
		return !_Ptr;
	}
};
