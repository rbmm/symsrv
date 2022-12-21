#pragma once

enum { e_completed = WM_APP, e_status};

#include "log.h"

struct DownloadContext
{
	WLog _log, _xml;
	PWSTR _PdbFileName = 0, _PdbFilePath = 0;
	PWSTR _params;
	GUID Signature;
	HWND _hwnd;
	ULONG Age;
	BOOL _bCancel = FALSE;
	LONG _dwRefCount = 1;

	void AddRef()
	{
		InterlockedIncrementNoFence(&_dwRefCount);
	}

	void Release()
	{
		if (!InterlockedDecrement(&_dwRefCount))
		{
			delete this;
		}
	}

	DownloadContext(_In_ PWSTR params, _In_ HWND hwnd) : _params(params), _hwnd(hwnd)
	{
	}

	~DownloadContext()
	{
		PWSTR psz;

		if (psz = _PdbFilePath)
		{
			delete [] psz;
		}

		if (psz = _PdbFileName)
		{
			delete [] psz;
		}

		if (psz = _params)
		{
			delete [] psz;
		}
	}
};

ULONG GetPdbforPE(_In_ PCWSTR lpszName, _Out_ DownloadContext* context);

ULONG InitSymSrv();

ULONG CALLBACK WorkThread(_In_ DownloadContext* context);

BOOL IsValidPDBExist(_In_ PCWSTR pszFile, _In_ const GUID* Signature, _In_ DWORD Age);