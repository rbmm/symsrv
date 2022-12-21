#include "stdafx.h"
#include "resource.h"

EXTERN_C_START

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

_NT_BEGIN
#include "symsrv.h"

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
		reinterpret_cast<DownloadContext*>(context)->_xml << (PCWSTR)data << L"\r\n";
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
		context->_xml.Init(0x10000);
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

_NT_END