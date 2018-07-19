
#pragma once
//#include "LdsBaseDef.h"




typedef BOOL (WINAPI * PFN_Wow64DisableWow64FsRedirection)(PVOID * ppOldValue);
typedef BOOL (WINAPI * PFN_Wow64RevertWow64FsRedirection)(PVOID pOlValue);



class CDisableWow64FsRedirection
{
public:
	explicit CDisableWow64FsRedirection()
	{
		HMODULE hKernel32 = GetModuleHandleW(L"kernel32.dll");
		if (hKernel32)
		{
			m_pfnWow64DisableWow64FsRedirection = (PFN_Wow64DisableWow64FsRedirection)GetProcAddress(hKernel32, "Wow64DisableWow64FsRedirection");
			m_pfnWow64RevertWow64FsRedirection = (PFN_Wow64RevertWow64FsRedirection)GetProcAddress(hKernel32, "Wow64RevertWow64FsRedirection");
		}
		if( m_pfnWow64DisableWow64FsRedirection && m_pfnWow64RevertWow64FsRedirection )
			m_pfnWow64DisableWow64FsRedirection( &m_lpOldValue );
	}

	~CDisableWow64FsRedirection()
	{
		if( m_pfnWow64DisableWow64FsRedirection && m_pfnWow64RevertWow64FsRedirection )
			m_pfnWow64RevertWow64FsRedirection( m_lpOldValue );
	}

protected:
	PVOID                               m_lpOldValue;
	PFN_Wow64DisableWow64FsRedirection  m_pfnWow64DisableWow64FsRedirection;
	PFN_Wow64RevertWow64FsRedirection   m_pfnWow64RevertWow64FsRedirection;
};



#define DISABLED_WOW64_FS_REDIRECTION()     CDisableWow64FsRedirection  _wow64Helper
