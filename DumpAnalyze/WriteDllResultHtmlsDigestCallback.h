#pragma once
#include <fstream>
#include <iomanip>
#include <string>
#include <map>
using namespace std;
#include "lib/sqlite3/sqlite3.h"




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


#define OVERVIEW_FLAG      "\x20<<\x20"
#define OVERVIEW_TABLE     "t_dll_ver_overview_history_"

const DWORD OVERVIEW_FLAG_COUNT = 6;
const DWORD OVERVIEW_TABLE_SIZE = strlen(OVERVIEW_TABLE);

typedef map<CStringA, map<CStringA, CStringA> >   CStringAMapMap;

struct WriteDllResultHtmlsDigestCallback : public WriteDllResultHtmlsCallback
{
	virtual void OnInfoCallback(BOOL bEndCallback);
	void OnInfoCallback_Internal();
	void CommitAndReset();

	explicit WriteDllResultHtmlsDigestCallback(sqlite3 * pSqliteDb, LPCWSTR szFromPart);

	DbFromTimestampMap  m_mapDbFromTimestamp;

	DWORD     m_dwIndexCall;
	ofstream  m_ofDigestStream;
	CStringA  m_strNowDll;

private:
	void InitDllVerOverviewHistory();
	CStringA LookupDllVerOverviewHistory(const CStringA &strDll, const CStringA &strVer) const;
	void UpdateDllVerOverviewHistory(const CStringA &strDll, const CStringA &strVer, const CStringA &strOverview);

private:
	sqlite3       * m_pSqliteDb;
	CStringA        m_strDllVerOverviewTable;
	UINT64          m_u64LastTime;
	CStringAMapMap  m_mapDllVerOverviewHistory;
};
