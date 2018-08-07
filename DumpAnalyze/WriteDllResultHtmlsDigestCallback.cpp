#include "stdafx.h"
#include "WriteDllResultHtmlsDigestCallback.h"





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

WriteDllResultHtmlsDigestCallback::WriteDllResultHtmlsDigestCallback()
{
	m_dwIndexCall = -1;
	m_strNowDll = "?";
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
			CStringA strText;
			strText.Format("<html>\n<head> dll-overview.html </head>\n"
				"<table border=\"1\"width=\"200\">\n"
				"<tr> <th>&nbsp;</th>  <th nowrap>Module name (%u)</th>  <th nowrap>Crash version info</th> </tr>\n",
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

		strText.Format(
			"<tr> <td rowspan=\"%u\" nowrap>%u</td> <td rowspan=\"%u\" nowrap>\
			<a href=\"./%s\">%s</a> (%u)</td> <td nowrap>\n", 
			m_dwVerCount, 
			m_dwIndexCall, 
			m_dwVerCount, 
			(LPCSTR)m_strDllHtml,
			(LPCSTR)(m_strDll.IsEmpty() ? ToHtmlStringA("<unknown>") : m_strDll), 
			m_dwDllDumpCount);
	}
	else
	{
		strText = ("<tr> <td nowrap>\n");
	}
	m_ofDigestStream << (LPCSTR)strText;

	//°æ±¾ÐÅÏ¢
	strText.Format("<a href=\"../../\">%s</a> (%u)\n",
		(LPCSTR)m_strVer, 
		m_dwVerDumpCount);
	m_ofDigestStream << (LPCSTR)strText;

	m_ofDigestStream << "</td></tr>\n" << endl;
}
