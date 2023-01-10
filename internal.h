#pragma once

#include "symsrv.h"
#include "log.h"

struct DownloadContext : public IDownloadContext
{
	WLog _log;
	PWSTR _PdbFileName = 0, _PdbFilePath = 0;
	PWSTR _params = 0;
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

	virtual void Cancel()
	{
		_bCancel = TRUE;
	}

	virtual PCWSTR GetPdbFileName()
	{
		return _PdbFileName;
	}

	virtual PCWSTR GetPdbFilePath()
	{
		return _PdbFilePath;
	}

	virtual HRESULT Start();

	virtual void ShowLog(_In_opt_ HWND hWndParent);

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
			free(psz);
		}
	}
};
