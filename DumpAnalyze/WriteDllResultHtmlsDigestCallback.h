#pragma once
#include <fstream>
#include <iomanip>
#include <string>
#include <map>
using namespace std;





CStringA ToHtmlStringA(LPCSTR szText);

#define ToHtmlLPCSTR(szText)   ((LPCSTR)ToHtmlStringA((szText)))


typedef map<CStringA, CStringA>  DbFromTimestampMap;




struct WriteDllResultHtmlsCallback
{
	virtual void OnInfoCallback(BOOL bEndCallback)
	{
		//Nothing
	}

	explicit WriteDllResultHtmlsCallback()
	{
		m_dwTotalDumpCount = 0;
		m_dwDllDumpCount = 0;
		m_dwVerCount = 0;
		m_dwVerDumpCount = 0;
		m_dwTagDumpCount = 0;
	}

	DWORD     m_dwTotalDumpCount;
	CStringA  m_strDllHtml;
	CStringA  m_strDll;
	DWORD     m_dwDllDumpCount;
	DWORD     m_dwVerCount;
	CStringA  m_strVer;
	DWORD     m_dwVerDumpCount;
	CStringA  m_strTag; 
	DWORD     m_dwTagDumpCount;
};


struct WriteDllResultHtmlsDigestCallback : public WriteDllResultHtmlsCallback
{
	virtual void OnInfoCallback(BOOL bEndCallback);
	void OnInfoCallback_Internal();
	void CommitAndReset();

	explicit WriteDllResultHtmlsDigestCallback();

	DbFromTimestampMap  m_mapDbFromTimestamp;

	DWORD     m_dwIndexCall;
	ofstream  m_ofDigestStream;
	CStringA  m_strNowDll;
};
