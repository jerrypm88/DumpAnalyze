#ifndef _versionhelpers_
#define _versionhelpers_

#ifdef _MSC_VER
#pragma once
#endif  // _MSC_VER

#ifdef __cplusplus

#ifndef VERSIONHELPERAPI
#define VERSIONHELPERAPI inline BOOL
#endif

#else  // __cplusplus

#define VERSIONHELPERAPI FORCEINLINE BOOL

#endif // __cplusplus

namespace VersionHelper {

// forward define
VERSIONHELPERAPI IsWindows10OrGreater();

VERSIONHELPERAPI
IsWin64()
{
    /* 判断是否为64位系统 */
    typedef void (WINAPI *PGNSI)(LPSYSTEM_INFO);

    static BOOL g_bFirst = TRUE;
    static BOOL g_bResult = FALSE;

    if (g_bFirst)
    {
        // Call GetNativeSystemInfo if supported or GetSystemInfo otherwise.
        PGNSI pGNSI = (PGNSI) GetProcAddress(GetModuleHandle(_T("kernel32.dll")), "GetNativeSystemInfo");
        if(NULL != pGNSI)
        {
            SYSTEM_INFO si = {};
            pGNSI(&si);

            if (si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64 || 
                si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_IA64 )   
            {
                g_bResult = TRUE;	   
            }
        }

        g_bFirst = FALSE;
    }

    return g_bResult;
}

//VERSIONHELPERAPI
//IsWindowsVersionOrGreater(WORD wMajorVersion, WORD wMinorVersion, WORD wServicePackMajor)
//{
//    OSVERSIONINFOEXW osvi = {};
//    ZeroMemory(&osvi, sizeof(osvi));
//    osvi.dwOSVersionInfoSize = sizeof(osvi);
//    osvi.dwMajorVersion = wMajorVersion;
//    osvi.dwMinorVersion = wMinorVersion;
//    osvi.wServicePackMajor = wServicePackMajor;
//
//    DWORDLONG dwlConditionMask = 0;
//    VER_SET_CONDITION(dwlConditionMask, VER_MAJORVERSION, VER_GREATER_EQUAL);
//    VER_SET_CONDITION(dwlConditionMask, VER_MINORVERSION, VER_GREATER_EQUAL);
//    VER_SET_CONDITION(dwlConditionMask, VER_SERVICEPACKMAJOR, VER_GREATER_EQUAL);
//
//    return VerifyVersionInfoW(&osvi, VER_MAJORVERSION|VER_MINORVERSION|VER_SERVICEPACKMAJOR, dwlConditionMask) != FALSE;
//}
//
//VERSIONHELPERAPI
//IsWindowsVersion(WORD wMajorVersion, WORD wMinorVersion, WORD wServicePackMajor)
//{
//    OSVERSIONINFOEXW osvi = {};
//    ZeroMemory(&osvi, sizeof(osvi));
//    osvi.dwOSVersionInfoSize = sizeof(osvi);
//    osvi.dwMajorVersion = wMajorVersion;
//    osvi.dwMinorVersion = wMinorVersion;
//    osvi.wServicePackMajor = wServicePackMajor;
//
//    DWORDLONG dwlConditionMask = 0;
//    VER_SET_CONDITION(dwlConditionMask, VER_MAJORVERSION, VER_EQUAL);
//    VER_SET_CONDITION(dwlConditionMask, VER_MINORVERSION, VER_EQUAL);
//    VER_SET_CONDITION(dwlConditionMask, VER_SERVICEPACKMAJOR, VER_EQUAL);
//
//    return VerifyVersionInfoW(&osvi, VER_MAJORVERSION|VER_MINORVERSION|VER_SERVICEPACKMAJOR, dwlConditionMask) != FALSE;
//}

/************************************************************************/
/*    先从\\VarFileInfo\\Translation定位找文件版本，如果找不到，再从根"\\"开始查找
      直接从"\\"上找，在win10 x86上会出现查找文件版本错误
/************************************************************************/
VERSIONHELPERAPI
GetFileVersionQueryKey(const CString& strFileName, LPCWSTR szQueryKey, CString& strFileVerInfo)
{
    BOOL bResult = FALSE;

    DWORD dwHandle = 0;

    DWORD dwInfoSize = GetFileVersionInfoSize(strFileName, &dwHandle);
    if (dwInfoSize != 0)
    {
        LPBYTE lpBlock = new BYTE[dwInfoSize];
        if (lpBlock)
        {
            if (GetFileVersionInfo(strFileName, dwHandle, dwInfoSize, lpBlock))
            {
				//-----if (IsWindows10OrGreater() && !IsWin64())
				{
					struct LANGANDCODEPAGE {
						WORD wLanguage;
						WORD wCodePage;
					} *lpTranslate;

					UINT cbTranslate = 0;
					if (VerQueryValue(lpBlock, _T("\\VarFileInfo\\Translation"), (LPVOID*)&lpTranslate, &cbTranslate))
					{
						for (size_t i = 0; i < (cbTranslate / sizeof(struct LANGANDCODEPAGE)); ++i)
						{
							CString strSubBlock;
							strSubBlock.Format(_T("\\StringFileInfo\\%04x%04x\\%s"), lpTranslate[i].wLanguage, lpTranslate[i].wCodePage, szQueryKey);

							LPBYTE lpFileVersion = NULL;
							UINT cbFileVersion = 0;
							if (VerQueryValue(lpBlock, strSubBlock, (LPVOID*)&lpFileVersion, &cbFileVersion))
							{
								strFileVerInfo.SetString((LPCTSTR)lpFileVersion, cbFileVersion);
								bResult = TRUE;
								break;
							}
						}
					}
				}
            }

            delete[] lpBlock;
        }
    }

    return bResult;
}

/************************************************************************/
/*       RtlGetNtVersionNumbers不会随着兼容模式一起改变版本号                                                               
/************************************************************************/
VERSIONHELPERAPI
GetNtVersionNumbers(DWORD& dwMajorVer, DWORD& dwMinorVer, DWORD& dwBuildNumber)
{
    BOOL bRet = FALSE;
    HMODULE hModNtdll = NULL;
    if (hModNtdll = ::LoadLibraryW(L"ntdll.dll"))
    {
        typedef void (WINAPI *pfRTLGETNTVERSIONNUMBERS)(DWORD*,DWORD*, DWORD*);
        pfRTLGETNTVERSIONNUMBERS pfRtlGetNtVersionNumbers;
        pfRtlGetNtVersionNumbers = (pfRTLGETNTVERSIONNUMBERS)::GetProcAddress(hModNtdll, "RtlGetNtVersionNumbers");
        if (pfRtlGetNtVersionNumbers)
        {
            pfRtlGetNtVersionNumbers(&dwMajorVer, &dwMinorVer, &dwBuildNumber);
            dwBuildNumber &= 0x0ffff;
            bRet = TRUE;
        }

        ::FreeLibrary(hModNtdll);
        hModNtdll = NULL;
    }

    return bRet;
}

/************************************************************************/
/*          RtlGetVersion 和 GetVersionEx 会随着兼容模式一起改变版本，并且在win10 x86上版本判断会错误
/************************************************************************/
//VERSIONHELPERAPI
//GetOsVersion(OSVERSIONINFOEXW* pOsvi)
//{
//    BOOL bRet = FALSE;
//    HMODULE hModNtdll = NULL;
//    if (hModNtdll = ::LoadLibraryW(L"ntdll.dll"))
//    {
//        typedef LONG (WINAPI* pfRTLGETVERSION)(OSVERSIONINFOEXW*);
//        pfRTLGETVERSION pfRtlGetVersion = (pfRTLGETVERSION)GetProcAddress(hModNtdll, "RtlGetVersion");
//        if (pfRtlGetVersion)
//        {
//            LONG Status = pfRtlGetVersion(pOsvi);
//            bRet = (Status == 0); // STATUS_SUCCESS;
//        }
//
//        ::FreeLibrary(hModNtdll);
//        hModNtdll = NULL;
//    }
//
//    return bRet;
//}

VERSIONHELPERAPI
IsWindowsServer()
{
    OSVERSIONINFOEXW osvi = {};
    ZeroMemory(&osvi, sizeof(osvi));
    osvi.dwOSVersionInfoSize = sizeof(osvi);
    osvi.wProductType = VER_NT_SERVER;

    DWORDLONG dwlConditionMask = 0;
    VER_SET_CONDITION(dwlConditionMask, VER_PRODUCT_TYPE, VER_EQUAL);

    return !VerifyVersionInfoW(&osvi, VER_PRODUCT_TYPE, dwlConditionMask);
}

// XP
VERSIONHELPERAPI
IsWindowsXP()
{
    static BOOL g_bFirst = TRUE;
    static BOOL g_bResult = FALSE;

    if (g_bFirst)
    {
        DWORD dwMajorVer = 0, dwMinorVer = 0, dwBuildNumber = 0;
        if (GetNtVersionNumbers(dwMajorVer, dwMinorVer, dwBuildNumber))
        {
            if (dwMajorVer == 5 && dwMinorVer == 1)
            {
                g_bResult = TRUE;
            }
        }

        g_bFirst = FALSE;
    }

    return g_bResult;
}

VERSIONHELPERAPI
IsWindowsXPOrGreater()
{
    static BOOL g_bFirst = TRUE;
    static BOOL g_bResult = FALSE;

    if (g_bFirst)
    {
        DWORD dwMajorVer = 0, dwMinorVer = 0, dwBuildNumber = 0;
        if (GetNtVersionNumbers(dwMajorVer, dwMinorVer, dwBuildNumber))
        {
            if (dwMajorVer == 5 && dwMinorVer >= 1 || dwMajorVer > 5)
            {
                g_bResult = TRUE;
            }
        }

        g_bFirst = FALSE;
    }

    return g_bResult;
}

// Vista
VERSIONHELPERAPI
IsWindowsVista()
{
    static BOOL g_bFirst = TRUE;
    static BOOL g_bResult = FALSE;

    if (g_bFirst)
    {
        DWORD dwMajorVer = 0, dwMinorVer = 0, dwBuildNumber = 0;
        if (GetNtVersionNumbers(dwMajorVer, dwMinorVer, dwBuildNumber))
        {
            if (dwMajorVer == 6 && dwMinorVer == 0)
            {
                g_bResult = TRUE;
            }
        }

        g_bFirst = FALSE;
    }

    return g_bResult;
}

VERSIONHELPERAPI
IsWindowsVistaOrGreater()
{
    static BOOL g_bFirst = TRUE;
    static BOOL g_bResult = FALSE;

    if (g_bFirst)
    {
        DWORD dwMajorVer = 0, dwMinorVer = 0, dwBuildNumber = 0;
        if (GetNtVersionNumbers(dwMajorVer, dwMinorVer, dwBuildNumber))
        {
            if (dwMajorVer >= 6)
            {
                g_bResult = TRUE;
            }
        }

        g_bFirst = FALSE;
    }

    return g_bResult;
}

// Win7
VERSIONHELPERAPI
IsWindows7()
{
    static BOOL g_bFirst = TRUE;
    static BOOL g_bResult = FALSE;

    if (g_bFirst)
    {
        DWORD dwMajorVer = 0, dwMinorVer = 0, dwBuildNumber = 0;
        if (GetNtVersionNumbers(dwMajorVer, dwMinorVer, dwBuildNumber))
        {
            if (dwMajorVer == 6 && dwMinorVer == 1)
            {
                g_bResult = TRUE;
            }
        }

        g_bFirst = FALSE;
    }

    return g_bResult;
}

VERSIONHELPERAPI
IsWindows7OrGreater()
{
    static BOOL g_bFirst = TRUE;
    static BOOL g_bResult = FALSE;

    if (g_bFirst)
    {
        DWORD dwMajorVer = 0, dwMinorVer = 0, dwBuildNumber = 0;
        if (GetNtVersionNumbers(dwMajorVer, dwMinorVer, dwBuildNumber))
        {
            if (dwMajorVer == 6 && dwMinorVer >= 1 || dwMajorVer > 6)
            {
                g_bResult = TRUE;
            }
        }

        g_bFirst = FALSE;
    }

    return g_bResult;
}

// Win8
VERSIONHELPERAPI
IsWindows8()
{
    static BOOL g_bFirst = TRUE;
    static BOOL g_bResult = FALSE;

    if (g_bFirst)
    {
        DWORD dwMajorVer = 0, dwMinorVer = 0, dwBuildNumber = 0;
        if (GetNtVersionNumbers(dwMajorVer, dwMinorVer, dwBuildNumber))
        {
            if (dwMajorVer == 6 && dwMinorVer == 2)
            {
                g_bResult = TRUE;
            }
        }

        g_bFirst = FALSE;
    }

    return g_bResult;
}

VERSIONHELPERAPI
IsWindows8OrGreater()
{
    static BOOL g_bFirst = TRUE;
    static BOOL g_bResult = FALSE;

    if (g_bFirst)
    {
        DWORD dwMajorVer = 0, dwMinorVer = 0, dwBuildNumber = 0;
        if (GetNtVersionNumbers(dwMajorVer, dwMinorVer, dwBuildNumber))
        {
            if (dwMajorVer == 6 && dwMinorVer >= 2 || dwMajorVer > 6)
            {
                g_bResult = TRUE;
            }
        }

        g_bFirst = FALSE;
    }

    return g_bResult;
}

// Win8.1
VERSIONHELPERAPI
IsWindows8_1()
{
    static BOOL g_bFirst = TRUE;
    static BOOL g_bResult = FALSE;

    if (g_bFirst)
    {
        DWORD dwMajorVer = 0, dwMinorVer = 0, dwBuildNumber = 0;
        if (GetNtVersionNumbers(dwMajorVer, dwMinorVer, dwBuildNumber))
        {
            if (dwMajorVer == 6 && dwMinorVer == 3)
            {
                g_bResult = TRUE;
            }
        }

        g_bFirst = FALSE;
    }

    return g_bResult;
}

VERSIONHELPERAPI
IsWindows8_1OrGreater()
{
    static BOOL g_bFirst = TRUE;
    static BOOL g_bResult = FALSE;

    if (g_bFirst)
    {
        DWORD dwMajorVer = 0, dwMinorVer = 0, dwBuildNumber = 0;
        if (GetNtVersionNumbers(dwMajorVer, dwMinorVer, dwBuildNumber))
        {
            if (dwMajorVer == 6 && dwMinorVer >= 3 || dwMajorVer > 6)
            {
                g_bResult = TRUE;
            }
        }

        g_bFirst = FALSE;
    }

    return g_bResult;
}

// Win10
VERSIONHELPERAPI
IsWindows10()
{
    static BOOL g_bFirst = TRUE;
    static BOOL g_bResult = FALSE;

    if (g_bFirst)
    {
        DWORD dwMajorVer = 0, dwMinorVer = 0, dwBuildNumber = 0;
        if (GetNtVersionNumbers(dwMajorVer, dwMinorVer, dwBuildNumber))
        {
            if ((dwMajorVer == 6 && dwMinorVer == 4) || 
                (dwMajorVer == 10 && dwMinorVer == 0))
            {
                g_bResult = TRUE;
            }
        }

        g_bFirst = FALSE;
    }

    return g_bResult;
}

VERSIONHELPERAPI
IsWindows10OrGreater()
{
    static BOOL g_bFirst = TRUE;
    static BOOL g_bResult = FALSE;

    if (g_bFirst)
    {
        DWORD dwMajorVer = 0, dwMinorVer = 0, dwBuildNumber = 0;
        if (GetNtVersionNumbers(dwMajorVer, dwMinorVer, dwBuildNumber))
        {
            if ((dwMajorVer == 6 && dwMinorVer == 4) ||
                dwMajorVer >= 10)
            {
                g_bResult = TRUE;
            }
        }

        g_bFirst = FALSE;
    }

    return g_bResult;
}

}; // namespace VersionHelper

#endif // _VERSIONHELPERS_
