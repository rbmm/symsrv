#pragma once

// return error codes defined in Winerror.h
ULONG InitSymSrv(_In_ PCWSTR lpLibFileName = L"symsrv.dll"); 

enum { 
	e_completed = WM_APP,	// wParam = final HRESULT, lParam = IDownloadContext
	e_status,				// wParam = wcsdup(psz), lParam = IDownloadContext
	e_progress				// wParam = percent, lParam = IDownloadContext
};

struct __declspec(novtable) IDownloadContext
{
	virtual void AddRef() = 0;
	virtual void Release() = 0;
	virtual HRESULT Start() = 0;
	virtual void Cancel() = 0;
	virtual PCWSTR GetPdbFileName() = 0;
	virtual PCWSTR GetPdbFilePath() = 0;

	virtual void ShowLog(_In_opt_ HWND hWndParent) = 0;
};

// params = folder*server;
// i.e. c:\windows\symbols*https://msdl.microsoft.com/download/symbols
HRESULT Create(_Out_ IDownloadContext** ppCtx, _In_ PCWSTR lpFileName, _In_ PWSTR params, _In_ HWND hwnd);

void OpenFolderAndSelectFile(_In_ PCWSTR lpFileName );

