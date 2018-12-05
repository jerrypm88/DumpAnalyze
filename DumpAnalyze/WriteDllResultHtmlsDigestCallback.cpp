#include "stdafx.h"
#include "WriteDllResultHtmlsDigestCallback.h"
#include <assert.h>




CStringA ToHtmlStringA(LPCSTR szText)
{
	CStringA strText(szText);
	strText.Replace("&", "&amp;");
	strText.Replace("<", "&lt;");
	strText.Replace(">", "&gt;");
	strText.Replace("\x20", "&nbsp;");
	strText.Replace("\"", "&quot;");
	return strText;
}

WriteDllResultHtmlsDigestCallback::WriteDllResultHtmlsDigestCallback(sqlite3 * pSqliteDb, LPCWSTR szFromPart)
{
	m_pSqliteDb = pSqliteDb;
	m_dwIndexCall = -1;
	m_strNowDll = "?";
	m_u64LastTime = (UINT64)time(NULL);
	m_strDllVerOverviewTable.Format( (OVERVIEW_TABLE "%ls"), szFromPart);
}

void WriteDllResultHtmlsDigestCallback::CommitAndReset()
{
	if (m_ofDigestStream.is_open())
	{
		m_ofDigestStream << "</table>\n</html>\n" << endl;
		m_ofDigestStream.close();
	}
	m_dwIndexCall = -1;
	m_strNowDll = "?";
}

void WriteDllResultHtmlsDigestCallback::OnInfoCallback(BOOL bEndCallback)
{
	if( bEndCallback )
	{
		CommitAndReset();
		return;
	}
	else if( -1 == m_dwIndexCall )
	{
		m_dwIndexCall = 0;
		string strDigestOverview = g_strWorkingFolder + "\\dll-overview.html";
		m_ofDigestStream.open( strDigestOverview, ios::out );
		if( m_ofDigestStream )
		{
			InitDllVerOverviewHistory();

			CStringA strText;
			strText.Format("<html>\n<head> dll-overview.html </head>\n"
				"<table border=\"1\"width=\"200\">\n"
				"<tr> <th>&nbsp;</th>  <th nowrap>Module name (%u)</th>"
				"<th nowrap>crash rate history</th>"
				"<th nowrap>Crash version list</th>"
				"<th nowrap>crash rate history</th>"
				"</tr>\n",
				m_dwTotalDumpCount);
			m_ofDigestStream << (LPCSTR)strText << endl;
		}
	}

	if( m_ofDigestStream )
	{
		OnInfoCallback_Internal();
	}
}

void WriteDllResultHtmlsDigestCallback::OnInfoCallback_Internal()
{
	CStringA strText;
	if( m_strNowDll.CompareNoCase(m_strDll) )
	{
		++m_dwIndexCall;
		m_strNowDll = m_strDll;

		CStringA strOverviewHistory = LookupDllVerOverviewHistory(m_strDll, "*");
		CStringA strOverviewNow;
		if( m_dwTotalDumpCount )
		{
			strOverviewNow.Format("%.1f%%", (100.0 * m_dwDllDumpCount)/m_dwTotalDumpCount );
			if( !strOverviewHistory.IsEmpty() )
			{
				strOverviewNow.Append(OVERVIEW_FLAG);
				strOverviewNow.Append(strOverviewHistory);
			}
			UpdateDllVerOverviewHistory(m_strDll, "*", strOverviewNow);
		}
		else
		{
			strOverviewNow = strOverviewHistory;
		}

		strText.Format(
			"<tr> <td rowspan=\"%u\" nowrap> %u</td> <td rowspan=\"%u\" nowrap>\
			<a href=\"./%s\"> %s</a> (%u)</td>\
			<td rowspan = \"%u\" nowrap> %s</td>\
			<td nowrap>\n", 
			m_dwVerCount, 
			m_dwIndexCall, 
			m_dwVerCount, 
			(LPCSTR)m_strDllHtml,
			(LPCSTR)(m_strDll.IsEmpty() ? ToHtmlStringA("<unknown>") : m_strDll), 
			m_dwDllDumpCount, 
			m_dwVerCount, 
			ToHtmlLPCSTR(strOverviewNow) );
	}
	else
	{
		strText = ("<tr> <td nowrap>\n");
	}
	m_ofDigestStream << (LPCSTR)strText;

	//°æ±¾ÐÅÏ¢
	do 
	{
		CStringA strOverviewHistory = LookupDllVerOverviewHistory(m_strDll, m_strVer);
		CStringA strOverviewNow;
		if( m_dwTotalDumpCount )
		{
			strOverviewNow.Format("%.1f%%", (100.0 * m_dwVerDumpCount)/m_dwTotalDumpCount );
			if( !strOverviewHistory.IsEmpty() )
			{
				strOverviewNow.Append(OVERVIEW_FLAG);
				strOverviewNow.Append(strOverviewHistory);
			}
			UpdateDllVerOverviewHistory(m_strDll, m_strVer, strOverviewNow);
		}
		else
		{
			strOverviewNow = strOverviewHistory;
		}

		strText.Format("<a href=\"../../\"> %s</a> (%u)</td> <td> %s\n",
			(LPCSTR)m_strVer, 
			m_dwVerDumpCount, 
			ToHtmlLPCSTR(strOverviewNow) );

	} while (0);
	m_ofDigestStream << (LPCSTR)strText;

	m_ofDigestStream << "</td></tr>\n" << endl;
}

void WriteDllResultHtmlsDigestCallback::InitDllVerOverviewHistory()
{
	if ( !m_pSqliteDb )
		return;

	CStringA strSql;
	strSql.Format(
		"create table if not exists [%s]"
		"( dll_name varchar(260), ver varchar(32), overview varchar(260), last_time integer, UNIQUE(dll_name, ver) )",
		(LPCSTR)m_strDllVerOverviewTable);
	int iRet = sqlite3_exec(m_pSqliteDb, strSql, NULL, NULL, NULL);
	if (SQLITE_OK != iRet)
		return;

	static UINT64 s_u64LastTime = 0;
	s_u64LastTime = m_u64LastTime;

	struct ExecDllVerOverviewHistoryCallback
	{
		static int Callback(void *para, int n_column, char **column_value, char **column_name)
		{
			CStringAMapMap *pMapDllVerOverviewHistory = (CStringAMapMap *)para;

			if(4 != n_column)
				return SQLITE_ERROR;

			if(column_value[0] && column_value[1])
			{
				CStringA & strOverviewHistory = (*pMapDllVerOverviewHistory)[ column_value[0] ][ column_value[1] ];
				if( column_value[2] )
				{
					LPCSTR lpEnd = FindEndFlagByCount( column_value[2], OVERVIEW_FLAG, OVERVIEW_FLAG_COUNT );
					strOverviewHistory.SetString( column_value[2], (lpEnd - column_value[2]) );

					if (!strOverviewHistory.IsEmpty() && column_value[3] )
					{
						UINT64 u64LastTime = _strtoui64(column_value[3], NULL, 10);
						if (u64LastTime && (u64LastTime < s_u64LastTime))
						{
							INT64 iDiffTime = (INT64)(s_u64LastTime - u64LastTime) / (3600 * 24);
							if (iDiffTime >= 2)
							{
								CStringA strBuff;
								strBuff.Format("(%u Days)%s", (INT)(iDiffTime - 1), OVERVIEW_FLAG);
								strOverviewHistory.Insert(0, strBuff);
							}
						}
					}
				}
			}
			return SQLITE_OK;
		}

		static LPCSTR FindEndFlagByCount(LPCSTR szText, LPCSTR szFlag, DWORD dwCount)
		{
			const size_t cchFlag = strlen(szFlag);
			LPCSTR lpEnd = szText;
			for(DWORD ii=0; ii<dwCount; ++ii)
			{
				LPCSTR lp = strstr(szText, szFlag);
				if (!lp)
				{
					lpEnd = (szText + strlen(szText));
					break;
				}
				lpEnd = lp;
				szText = (lp + cchFlag);
			}
			return lpEnd;
		}
	};

	strSql.Format("select * from [%s]", (LPCSTR)m_strDllVerOverviewTable);
	iRet = sqlite3_exec(m_pSqliteDb, strSql, 
		&ExecDllVerOverviewHistoryCallback::Callback, &m_mapDllVerOverviewHistory, NULL);
}

CStringA WriteDllResultHtmlsDigestCallback::LookupDllVerOverviewHistory( const CStringA &strDll, const CStringA &strVer ) const
{
	CStringAMapMap::const_iterator itrFind = m_mapDllVerOverviewHistory.find(strDll);
	if( m_mapDllVerOverviewHistory.end() == itrFind )
		return CStringA();

	map<CStringA, CStringA>::const_iterator itrSub = itrFind->second.find(strVer);
	if( itrFind->second.end() == itrSub )
		return CStringA();

	return itrSub->second;
}

void WriteDllResultHtmlsDigestCallback::UpdateDllVerOverviewHistory( const CStringA &strDll, const CStringA &strVer, const CStringA &strOverview )
{
	if( !m_pSqliteDb )
		return;

	CStringA strSql;
	strSql.Format(
		"replace into [%s] values(\"%s\", \"%s\", \"%s\", %I64u)",
		(LPCSTR)m_strDllVerOverviewTable,
		(LPCSTR)strDll, (LPCSTR)strVer, (LPCSTR)strOverview, m_u64LastTime );
	int iRet = sqlite3_exec(m_pSqliteDb, strSql, NULL, NULL, NULL);
	assert( SQLITE_OK == iRet );
}
