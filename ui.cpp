#include "stdafx.h"
#include "resource.h"

_NT_BEGIN

#include "symsrv.h"

int ShowErrorBox(HWND hwnd, HRESULT dwError, PCWSTR pzCaption, UINT uType)
{
	PWSTR psz;
	ULONG dwFlags;
	HMODULE hmod;	

	if ((dwError & FACILITY_NT_BIT) || (0 > dwError && HRESULT_FACILITY(dwError) == FACILITY_NULL))
	{
		dwError &= ~FACILITY_NT_BIT;
		static HMODULE s_hmod;
		if (!s_hmod)
		{
			s_hmod = GetModuleHandle(L"ntdll");
		}
		hmod = s_hmod;
		dwFlags = FORMAT_MESSAGE_IGNORE_INSERTS | FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_HMODULE;
	}
	else
	{
		hmod = 0;
		dwFlags = FORMAT_MESSAGE_IGNORE_INSERTS | FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM;
	}

	int r = IDCANCEL;
	if (FormatMessageW(dwFlags, hmod, dwError, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (PWSTR)&psz, 0, 0))
	{
		r = MessageBoxW(hwnd, psz, pzCaption, uType);
		LocalFree(psz);
	}
	else
	{
		WCHAR sz[32];
		if (0 < swprintf_s(sz, _countof(sz), L"Error %08X", dwError))
		{
			r = MessageBoxW(hwnd, sz, pzCaption, uType);
		}
	}

	return r;
}

void ShowLog(HWND hwnd, PCWSTR Caption, WLog& log)
{
	if (log.IsEmpty())
	{
		return ;
	}
	if (hwnd = CreateWindowExW(0, WC_EDIT, Caption, 
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

		log >> hwnd;

		ShowWindow(hwnd, SW_SHOWNORMAL);
	}
}

void OnBrowse(_In_ HWND hwndDlg, 
			  _In_ UINT nIDDlgItem, 
			  _In_ UINT cFileTypes, 
			  _In_ const COMDLG_FILTERSPEC *rgFilterSpec, 
			  _In_ UINT iFileType = 0)
{
	IFileDialog *pFileOpen;

	if (0 <= CoCreateInstance(__uuidof(FileOpenDialog), NULL, CLSCTX_ALL, IID_PPV_ARGS(&pFileOpen)))
	{
		pFileOpen->SetOptions(FOS_NOVALIDATE|FOS_NOTESTFILECREATE|
			FOS_NODEREFERENCELINKS|FOS_DONTADDTORECENT|FOS_FORCESHOWHIDDEN);

		if (0 <= pFileOpen->SetFileTypes(cFileTypes, rgFilterSpec) && 
			0 <= pFileOpen->SetFileTypeIndex(1 + iFileType) && 
			0 <= pFileOpen->Show(hwndDlg))
		{
			IShellItem *pItem;

			if (0 <= pFileOpen->GetResult(&pItem))
			{
				PWSTR pszFilePath;
				if (0 <= pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszFilePath))
				{
					SetDlgItemTextW(hwndDlg, nIDDlgItem, pszFilePath);
					CoTaskMemFree(pszFilePath);
				}
				pItem->Release();
			}
		}

		pFileOpen->Release();
	}
}

void OnBrowse(_In_ HWND hwndDlg, _In_ UINT nIDDlgItem, _In_ PCWSTR lpszTitle)
{
	WCHAR buf[MAX_PATH];

	BROWSEINFO bi = { 
		0, 0, 0, lpszTitle, BIF_DONTGOBELOWDOMAIN|BIF_NEWDIALOGSTYLE|BIF_RETURNONLYFSDIRS
	};

	if (PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi))
	{
		if (SHGetPathFromIDListEx(pidl, buf, _countof(buf), GPFIDL_DEFAULT))
		{
			SetDlgItemTextW(hwndDlg, nIDDlgItem, buf);
		}

		CoTaskMemFree(pidl);
	}
}

void FireWindow(HWND hwnd)
{
	AnimateWindow(hwnd, 200, AW_HIDE|AW_CENTER);
	AnimateWindow(hwnd, 200, AW_ACTIVATE|AW_CENTER);
	SetFocus(hwnd);
}

class CDialog 
{
	DownloadContext* _context = 0;
	ULONG _editId = IDC_EDIT1;

	void OnInitDialog(HWND hwnd)
	{
		SendMessage(hwnd, WM_SETICON, ICON_BIG, (LPARAM)LoadImage((HINSTANCE)&__ImageBase, 
			MAKEINTRESOURCE(IDI_ICON1), IMAGE_ICON, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_SHARED));

		SendMessage(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)LoadImage((HINSTANCE)&__ImageBase, 
			MAKEINTRESOURCE(IDI_ICON1), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_SHARED));

		static PCWSTR Servers[] = {
			L"nvidia", // https://driver-symbols.nvidia.com/
			L"amd", // https://download.amd.com/dir/bin
			L"intel", // https://software.intel.com/sites/downloads/symbols/
			L"mozilla", // https://symbols.mozilla.org/
			L"google", // https://chromium-browser-symsrv.commondatastorage.googleapis.com/
			L"microsoft", // https://msdl.microsoft.com/download/symbols/
		};

		ULONG i = _countof(Servers);
		HWND hwndCB = GetDlgItem(hwnd, IDC_COMBO1);
		do 
		{
			--i;
			ComboBox_SetItemData(hwndCB, i, ComboBox_AddString(hwndCB, Servers[i]));
		} while (i);
		ComboBox_SetCurSel(hwndCB, 0);

		WCHAR buf[MAX_PATH];

		if (ULONG cch = GetWindowsDirectory(buf, _countof(buf)))
		{
			if (!wcscpy_s(buf + cch, _countof(buf) - cch, L"\\symbols"))
			{
				SetDlgItemTextW(hwnd, IDC_EDIT2, buf);
			}
			if (!wcscpy_s(buf + cch, _countof(buf) - cch, L"\\system32\\"))
			{
				SetDlgItemTextW(hwnd, IDC_EDIT1, buf);
			}
		}
	}

	INT_PTR DlgProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
	{
		static const COMDLG_FILTERSPEC rgSpec[] =
		{ 
			{ L"Dll files", L"*.dll" },
			{ L"Exe files", L"*.exe" },
			{ L"Sys files", L"*.sys" },
			{ L"All files", L"*" },
		};

		DownloadContext* context;

		switch(uMsg)
		{
		case WM_DESTROY:
			if (context = _context) 
			{
				context->Release();
				_context = 0;
			}
			break;

		case WM_CLOSE:
			EndDialog(hwnd, 0);
			break;

		case WM_INITDIALOG:
			OnInitDialog(hwnd);
			break;

		case e_completed:
			if (context = _context) 
			{
				if ((LPARAM)context == lParam)
				{
					ShowLog(hwnd, L"LOG", context->_log);
					ShowLog(hwnd, L"XML", context->_xml);
					ToggleUI(hwnd, TRUE);
					SetDlgItemTextW(hwnd, IDC_STATIC1, 0);
					if (!(ULONG)wParam)
					{
						if (PIDLIST_ABSOLUTE pidl = ILCreateFromPath(context->_PdbFilePath))
						{
							SHOpenFolderAndSelectItems(pidl, 0, 0, 0);
							ILFree(pidl);
						}
					}
					ShowErrorBox(hwnd, (ULONG)wParam, context->_PdbFileName, (ULONG)wParam ? MB_ICONHAND : MB_ICONINFORMATION);
					context->Release();
					_context = 0;
				}
			}
			break;

		case e_status:
			if (context = _context) 
			{
				if ((LPARAM)context == lParam)
				{
					SetDlgItemTextW(hwnd, IDC_STATIC1, (PWSTR)wParam);
					free((PVOID)wParam);
				}
			}
			break;

		case WM_COMMAND:
			switch (wParam)
			{
			case MAKEWPARAM(IDC_EDIT1, EN_SETFOCUS):
				_editId = IDC_EDIT1;
				break;

			case MAKEWPARAM(IDC_EDIT2, EN_SETFOCUS):
				_editId = IDC_EDIT2;
				break;

			case MAKEWPARAM(IDOK, BN_CLICKED):
				if (!_context)
				{
					OnOk(hwnd);
				}
				break;

			case MAKEWPARAM(IDCANCEL, BN_CLICKED):
				if (context = _context) 
				{
					context->_bCancel = TRUE;
				}
				break;

			case MAKEWPARAM(IDC_BUTTON1, BN_CLICKED):
				switch (_editId)
				{
				case IDC_EDIT1:
					OnBrowse(hwnd, IDC_EDIT1, _countof(rgSpec), rgSpec);
					break;
				case IDC_EDIT2:
					OnBrowse(hwnd, IDC_EDIT2, L"Save PDB in this directory:");
					break;
				}
				break;
			}
			break;
		}
		return 0;
	}

	void OnOk(HWND hwnd)
	{
		static PCWSTR Servers[] = {
			L"msdl.microsoft.com/download/symbols", // 
			L"chromium-browser-symsrv.commondatastorage.googleapis.com", // 
			L"symbols.mozilla.org", // 
			L"software.intel.com/sites/downloads/symbols", // 
			L"download.amd.com/dir/bin", // 
			L"driver-symbols.nvidia.com", // 
		};
		
		int len;
		ULONG i = ComboBox_GetCurSel(GetDlgItem(hwnd, IDC_COMBO1));
		if (i < _countof(Servers))
		{
			len = (ULONG)wcslen(Servers[i]) + _countof(L"https://");
		}
		else
		{
			if (!(len = GetWindowTextLengthW(GetDlgItem(hwnd, IDC_COMBO1))))
			{
				FireWindow(GetDlgItem(hwnd, IDC_COMBO1));
				return ;
			}
			len++;
		}

		ULONG len2 = GetWindowTextLengthW(GetDlgItem(hwnd, IDC_EDIT2));

		if (!len2)
		{
			FireWindow(GetDlgItem(hwnd, IDC_EDIT2));
			return ;
		}

		if (PWSTR params = new WCHAR[len2+len+2] )
		{
			PWSTR pszFile;

			len2 = GetDlgItemTextW(hwnd, IDC_EDIT2, params, len2 + 1);

			NTSTATUS status;
			UNICODE_STRING ObjectName;
			if (0 <= (status = RtlDosPathNameToNtPathName_U_WithStatus(params, &ObjectName, 0, 0)))
			{
				IO_STATUS_BLOCK iosb;
				OBJECT_ATTRIBUTES oa = { sizeof(oa), 0, &ObjectName, OBJ_CASE_INSENSITIVE };
				status = NtOpenFile(&oa.RootDirectory, FILE_ADD_SUBDIRECTORY, &oa, &iosb, FILE_SHARE_VALID_FLAGS, FILE_DIRECTORY_FILE);
				RtlFreeUnicodeString(&ObjectName);
				if (0 <= status)
				{
					NtClose(oa.RootDirectory);
				}
			}

			if (0 > status)
			{
				ShowErrorBox(hwnd, HRESULT_FROM_NT(status), 0, MB_ICONHAND);
				FireWindow(GetDlgItem(hwnd, IDC_EDIT2));
				goto __fail;
			}

			params[len2] = '*';

			if (i < _countof(Servers))
			{
				len = swprintf_s(params + len2 + 1, len, L"https://%s", Servers[i]);
			}
			else
			{
				len = GetDlgItemTextW(hwnd, IDC_COMBO1, params + len2 + 1, len);
			}

			if (0 >= len)
			{
				goto __fail;
			}

			if (!(len = GetWindowTextLengthW(GetDlgItem(hwnd, IDC_EDIT1))))
			{
				FireWindow(GetDlgItem(hwnd, IDC_EDIT1));
				goto __fail;
			}

			++len;
			GetDlgItemTextW(hwnd, IDC_EDIT1, pszFile = (PWSTR)alloca(len * sizeof(WCHAR)), len);

			if (DownloadContext* context = new DownloadContext(params, hwnd))
			{
				params = 0;
				if (ULONG dwError = GetPdbforPE(pszFile, context))
				{
					ShowErrorBox(hwnd, dwError, L"Invalid PE", MB_ICONHAND);
					FireWindow(GetDlgItem(hwnd, IDC_EDIT1));
				}
				else
				{
					context->AddRef();

					if (HANDLE hThread = CreateThread(0, 0, (PTHREAD_START_ROUTINE)WorkThread, context, 0, 0))
					{
						NtClose(hThread);
						_context = context;

						ToggleUI(hwnd, FALSE);
						return ;
					}
					else
					{
						context->Release();
					}
				}
				context->Release();
			}

			if (params)
			{
__fail:
				delete [] params;
			}
		}
	}

	void ToggleUI(HWND hwnd, BOOL b)
	{
		static const UINT id[] = {
			IDCANCEL, IDOK, IDC_BUTTON1, IDC_EDIT1, IDC_EDIT2, IDC_COMBO1
		};

		ULONG i = _countof(id);
		do 
		{
			--i;
			EnableWindow(GetDlgItem(hwnd, id[i]), b ^ !i);
		} while (i);
	}

	static INT_PTR CALLBACK DlgProc_s(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
	{
		return reinterpret_cast<CDialog*>(GetWindowLongPtr(hwnd, DWLP_USER))->DlgProc(hwnd, uMsg, wParam, lParam);
	}

	static INT_PTR CALLBACK StartDlgProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
	{
		if (uMsg == WM_INITDIALOG)
		{
			SetWindowLongPtr(hwnd, DWLP_USER, lParam);
			SetWindowLongPtr(hwnd, DWLP_DLGPROC, (LONG_PTR)DlgProc_s);
			return reinterpret_cast<CDialog*>(GetWindowLongPtr(hwnd, DWLP_USER))->DlgProc(hwnd, uMsg, wParam, lParam);
		}

		return 0;
	}

public:

	INT_PTR Run()
	{
		return DialogBoxParam((HINSTANCE)&__ImageBase, MAKEINTRESOURCE(IDD_DIALOG1), HWND_DESKTOP, StartDlgProc, (LPARAM)this);
	}
};

void CALLBACK ep(void*)
{
	if (ULONG dwError = InitSymSrv())
	{
		ShowErrorBox(0, dwError, L"symsrv.dll", MB_ICONHAND);
		//Test(L"g:\\efi\\symbols*https://msdl.microsoft.com/download/symbols", L"c:\\windows\\system32\\ntdll.dll");
		//Test(L"D:\\WINDOWS\\symbols*https://software.intel.com/sites/downloads/symbols", 
		//	L"c:\\windows\\system32\\drivers\\IntcDAud.sys");
		//
	}
	else if (0 <= CoInitializeEx(0, COINIT_APARTMENTTHREADED|COINIT_DISABLE_OLE1DDE))
	{
		{
			CDialog dlg;
			dlg.Run();
		}

		CoUninitialize();
	}

	ExitProcess(0); 
}

_NT_END