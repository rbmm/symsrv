#include "stdafx.h"
#include "internal.h"

EXTERN_C_START

NTSYSAPI
PVOID
NTAPI
RtlImageDirectoryEntryToData(
							 _In_ PVOID Base,
							 _In_ BOOLEAN MappedAsImage,
							 _In_ USHORT DirectoryEntry,
							 _Out_ PULONG Size
							 );


WINBASEAPI
BOOL CALLBACK SymbolServerSetOptionsW(
									 _In_ UINT_PTR options,
									 _In_ ULONG64  data
									 );

WINBASEAPI
BOOL CALLBACK SymbolServerW(
							_In_ PCWSTR params,
							_In_ PCWSTR filename,
							_In_ PVOID  id,
							_In_ DWORD  two,
							_In_ DWORD  three,
							_Out_ PWSTR  path
							);

PVOID __imp_SymbolServerSetOptionsW, __imp_SymbolServerW;

EXTERN_C_END

inline ULONG BOOL_TO_ERROR(BOOL f)
{
	return f ? NOERROR : GetLastError();
}

void OnEvent(PIMAGEHLP_CBA_EVENTW p, DownloadContext* context)
{
	PWSTR desc = const_cast<PWSTR>(p->desc);
	while (WCHAR c = *desc)
	{
		if (c == '\b')
		{
			*desc = ' ';
		}
		desc++;
	}

	context->_log(L"%x %08x %s\r\n", p->severity, p->code, desc = const_cast<PWSTR>(p->desc));

	if (!wcschr(desc, '\n'))
	{
		if (desc = _wcsdup(desc))
		{
			if (!PostMessageW(context->_hwnd, e_status, (WPARAM)desc, (LPARAM)context))
			{
				free(desc);
			}
		}
	}
}

void OnXml(PWSTR psz, DownloadContext* context)
{
	static const WCHAR Progress_begin[] = L"<Progress percent=\"";
	static const WCHAR Progress_end[] = L"\"/>";

	if (!memcmp(psz, Progress_begin, sizeof(Progress_begin) - sizeof(WCHAR)))
	{
		ULONG n = wcstoul(psz + _countof(Progress_begin) - 1, &psz, 10);

		if (!memcmp(psz, Progress_end, sizeof(Progress_end) - sizeof(WCHAR)))
		{
			PostMessageW(context->_hwnd, e_progress, (WPARAM)n, (LPARAM)context);
		}
	}
}

BOOL CALLBACK symsrvCallback(UINT_PTR action, ULONG64 data, ULONG64 context)
{
	switch (action)
	{
	case SSRVACTION_EVENTW:
		OnEvent(reinterpret_cast<PIMAGEHLP_CBA_EVENTW>(data), reinterpret_cast<DownloadContext*>(context));
		break;

	case SSRVACTION_QUERYCANCEL:
		*(ULONG64*)data = reinterpret_cast<DownloadContext*>(context)->_bCancel;
		break;

	case SSRVACTION_SIZE:
		reinterpret_cast<DownloadContext*>(context)->_log(L"SIZE: %I64u\r\n", *(ULONG64*)data);
		break;

	case SSRVACTION_HTTPSTATUS:
		reinterpret_cast<DownloadContext*>(context)->_log(L"HTTP STATUS: %I64u\r\n", *(ULONG*)data);
		break;

	case SSRVACTION_TRACE:
	case SSRVACTION_XMLOUTPUT:
		OnXml((PWSTR)data, reinterpret_cast<DownloadContext*>(context));
		break;

	default: 
		reinterpret_cast<DownloadContext*>(context)->_log(L"!! action[%x] data=%I64x\r\n", action, data);
	}
	return TRUE;
}

ULONG GetPdbforPE(_In_ HMODULE hmod, _In_ BOOLEAN bMappedAsImage, _Out_ DownloadContext* context)
{
	ULONG dwError = ERROR_NOT_FOUND;
	ULONG cb;

	PIMAGE_DEBUG_DIRECTORY pidd = (PIMAGE_DEBUG_DIRECTORY)RtlImageDirectoryEntryToData(
		hmod, bMappedAsImage, IMAGE_DIRECTORY_ENTRY_DEBUG, &cb);

	if (pidd && cb && !(cb % sizeof IMAGE_DEBUG_DIRECTORY))
	{
		do 
		{
			struct CV_INFO_PDB 
			{
				ULONG CvSignature;
				GUID Signature;
				ULONG Age;
				char PdbFileName[];
			};

			if (pidd->Type == IMAGE_DEBUG_TYPE_CODEVIEW && pidd->SizeOfData > sizeof(CV_INFO_PDB))
			{
				if (ULONG PointerToRawData = bMappedAsImage ? pidd->AddressOfRawData : pidd->PointerToRawData)
				{
					CV_INFO_PDB* lpcvh = (CV_INFO_PDB*)RtlOffsetToPointer(PAGE_ALIGN(hmod), PointerToRawData);

					if (lpcvh->CvSignature == 'SDSR')
					{
						PCSTR PdbFileName = lpcvh->PdbFileName, c = strrchr(PdbFileName, L'\\');
						if (c)
						{
							PdbFileName = c + 1;
						}

						ULONG cch = 0;
						PWSTR filename = 0;
						while (cch = MultiByteToWideChar(CP_UTF8, 0, PdbFileName, MAXULONG, filename, cch))
						{
							if (filename)
							{
								context->Signature = lpcvh->Signature;
								context->Age = lpcvh->Age;
								context->_PdbFileName = filename;
								return NOERROR;
							}

							if (cch > MAXSHORT)
							{
								return ERROR_DS_NAME_TOO_LONG;
							}

							if (!(filename = new WCHAR[cch]))
							{
								return ERROR_OUTOFMEMORY;
							}
						}

						dwError = GetLastError();

						if (filename)
						{
							delete [] filename;
						}
					}
				}
			}

		} while (pidd++, cb -= sizeof IMAGE_DEBUG_DIRECTORY);
	}

	return dwError;
}

ULONG GetPdbforPE(_In_ PCWSTR lpszName, _Out_ DownloadContext* context)
{
	if (HMODULE hmod = LoadLibraryExW(lpszName, 0, LOAD_LIBRARY_AS_DATAFILE))
	{
		ULONG dwError = GetPdbforPE(hmod, !((DWORD_PTR)hmod & (PAGE_SIZE - 1)), context);

		FreeLibrary(hmod);

		return dwError;
	}

	return GetLastError();
}

ULONG CALLBACK WorkThread(_In_ DownloadContext* context)
{
	// SSRVOPT_SETCONTEXT is own per thread !
	ULONG dwError = BOOL_TO_ERROR(SymbolServerSetOptionsW(SSRVOPT_SETCONTEXT, (ULONG_PTR)context));
	if (NOERROR == dwError)
	{
		context->_log.Init(0x10000);
		if (PWSTR buf = new WCHAR [MAX_PATH])
		{
			context->_PdbFilePath = buf;
			dwError = BOOL_TO_ERROR(SymbolServerW(context->_params, context->_PdbFileName, &context->Signature, context->Age, 0, buf));
		}
		else
		{
			dwError = ERROR_OUTOFMEMORY;
		}
	}

	PostMessageW(context->_hwnd, e_completed, dwError, (LPARAM)context);

	context->Release();

	return dwError;
}

ULONG InitSymSrv()
{
	if (HMODULE hmod = LoadLibraryW(L"symsrv.dll"))
	{
		if (
			(__imp_SymbolServerW = GetProcAddress(hmod, "SymbolServerW")) &&
			(__imp_SymbolServerSetOptionsW = GetProcAddress(hmod, "SymbolServerSetOptionsW")) &&
			SymbolServerSetOptionsW(SSRVOPT_CALLBACKW, (ULONG_PTR)symsrvCallback) &&
			SymbolServerSetOptionsW(SSRVOPT_PARAMTYPE, SSRVOPT_GUIDPTR) &&
			SymbolServerSetOptionsW(SSRVOPT_UNATTENDED, FALSE)
			)
		{
			return NOERROR;
		}

		ULONG dwError = GetLastError();

		FreeLibrary(hmod);

		return dwError;
	}

	return GetLastError();
}

HRESULT Create(_Out_ IDownloadContext** ppCtx, _In_ PCWSTR lpFileName, _In_ PWSTR params, _In_ HWND hwnd)
{
	HRESULT dwError = E_OUTOFMEMORY;

	if (params = _wcsdup(params))
	{
		if (DownloadContext* pCtx = new DownloadContext(params, hwnd))
		{
			if (NOERROR == (dwError = GetPdbforPE(lpFileName, pCtx)))
			{
				*ppCtx = pCtx;
				return S_OK;
			}

			pCtx->Release();
		}

		free(params);

		dwError = HRESULT_FROM_WIN32(dwError);
	}

	return dwError;
}

HRESULT DownloadContext::Start()
{
	AddRef();

	if (HANDLE hThread = CreateThread(0, 0, (PTHREAD_START_ROUTINE)WorkThread, this, 0, 0))
	{
		CloseHandle(hThread);
		return S_OK;
	}
	
	Release();

	return HRESULT_FROM_WIN32(GetLastError());
}

void OpenFolderAndSelectFile(_In_ PCWSTR lpFileName )
{
	if (PIDLIST_ABSOLUTE pidl = ILCreateFromPath(lpFileName))
	{
		SHOpenFolderAndSelectItems(pidl, 0, 0, 0);
		ILFree(pidl);
	}
}

void DownloadContext::ShowLog(_In_opt_ HWND hwnd)
{
	if (_log.IsEmpty())
	{
		return ;
	}

	if (hwnd = CreateWindowExW(0, WC_EDIT, _PdbFileName, 
		WS_OVERLAPPEDWINDOW|WS_HSCROLL|WS_VSCROLL|ES_MULTILINE,
		CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, hwnd, 0, 0, 0))
	{
		HFONT hFont = 0;
		NONCLIENTMETRICS ncm = { sizeof(NONCLIENTMETRICS) };
		if (SystemParametersInfo(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0))
		{
			wcscpy(ncm.lfMessageFont.lfFaceName, L"Courier New");
			ncm.lfMessageFont.lfHeight = -ncm.iMenuHeight;
			ncm.lfMessageFont.lfWeight = FW_NORMAL;
			ncm.lfMessageFont.lfQuality = CLEARTYPE_QUALITY;
			ncm.lfMessageFont.lfPitchAndFamily = FIXED_PITCH|FF_MODERN;
			ncm.lfMessageFont.lfHeight = -ncm.iMenuHeight;

			hFont = CreateFontIndirect(&ncm.lfMessageFont);
		}

		if (hFont) SendMessage(hwnd, WM_SETFONT, (WPARAM)hFont, 0);

		ULONG n = 8;
		SendMessage(hwnd, EM_SETTABSTOPS, 1, (LPARAM)&n);

		_log >> hwnd;

		ShowWindow(hwnd, SW_SHOWNORMAL);
	}
}

