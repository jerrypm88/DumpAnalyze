#include "StdAfx.h"
#include "CommandLine.h"

CCommandLine::CCommandLine()
{
}

CCommandLine::CCommandLine(const wstring &strCmdLine)
{
	parse(strCmdLine);
}

CCommandLine::~CCommandLine()
{

}

BOOL
CCommandLine::parse(const wstring &strCmdLine)
{
	INT i, nSepPos, nEqualPos = -1, nCmdLineSize = (INT)strCmdLine.length();

	BOOL bIsQuota = FALSE, bIsParsed = FALSE;
	WCHAR nLastChar = 0;
	for(i = 0; i < nCmdLineSize; ++i) {
		// 跳过空白区域
		if(strCmdLine[i] != _T(' ')) {
			// 找到下一个分割点
			for(nSepPos = i; nSepPos < nCmdLineSize; ++nSepPos) {
				if(strCmdLine[nSepPos] == _T(' ')) {
					if(!bIsQuota) {
						parseOption(strCmdLine, i, nSepPos - 1, nEqualPos);
						bIsParsed = TRUE;
						break;
					}
				} else if(nLastChar != _T('\\') && strCmdLine[nSepPos] == _T('"')) {
					if(bIsQuota) {
						bIsQuota = FALSE;
					} else {
						bIsQuota = TRUE;
					}
				} else if(strCmdLine[nSepPos] == _T('=') && nEqualPos == -1) {
					nEqualPos = nSepPos;
				}

				nLastChar = strCmdLine[nSepPos];
			}

			if(!bIsParsed) {
				parseOption(strCmdLine, i, nCmdLineSize - 1, nEqualPos);
			}

			nEqualPos = -1;
			bIsParsed = FALSE;

			i = nSepPos;
			nLastChar = strCmdLine[nSepPos];
		}
	}

	return TRUE;
}

BOOL
CCommandLine::pack(wstring &strCmdLine, BOOL bAppendOtherArgs) const
{
	try
	{
		strCmdLine = _T("");
		wstringstream oss;
		std::map<wstring, wstring>::const_iterator oOptionIt = m_vOptions.begin();
		while(oOptionIt != m_vOptions.end()) {
			oss << _T(" --") << oOptionIt->first << _T("=\"") << oOptionIt->second << _T("\"");
			++oOptionIt;
		}

		if (bAppendOtherArgs)
		{
			for(vector<wstring>::const_iterator iter = m_vOtherArgs.begin(); iter != m_vOtherArgs.end(); ++iter) 
			{
				oss << _T(" ") << *iter << _T(" ");
			}
		}

		strCmdLine = oss.str();
		return TRUE;
	}
	catch (std::bad_alloc&)
	{
		strCmdLine.clear();
		SetLastError(ERROR_OUTOFMEMORY);
		return FALSE;
	}
}

BOOL CCommandLine::isEmpty() const
{
	return m_vOptions.empty();
}

BOOL
CCommandLine::hasOption(const wstring &strOption) const
{
	if(m_vOptions.find(strOption) == m_vOptions.end()) {
		return FALSE;
	}

	return TRUE;
}

BOOL
CCommandLine::getOption(const wstring &strOption, wstring &strOptionValue) const
{
	strOptionValue = _T("");

	std::map<wstring, wstring>::const_iterator oOptionIt = m_vOptions.find(strOption);
	if(oOptionIt == m_vOptions.end()) {
		return FALSE;
	}

	strOptionValue = oOptionIt->second;

	return TRUE;
}

BOOL
CCommandLine::setOption(const wstring &strOption, const wstring &strOptionValue)
{
	m_vOptions[strOption] = strOptionValue;
	return TRUE;
}

BOOL
CCommandLine::getAllOptions(std::map<wstring, wstring> &vOptions) const
{
	vOptions = m_vOptions;
	return TRUE;
}

BOOL
CCommandLine::appendOtherArg(const wstring &strArg)
{
	try
	{
		m_vOtherArgs.push_back(strArg);
		return TRUE;
	}
	catch (std::bad_alloc&)
	{
		SetLastError(ERROR_OUTOFMEMORY);
		return FALSE;
	}
}

BOOL
CCommandLine::getOtherArgs(vector<wstring> &strArgs) const
{
	strArgs = m_vOtherArgs;
	return TRUE;
}

/*
 以下几种情况表示选项开
    --XXXX=true
    --XXXX=yes
    --XXXX
*/
BOOL 
CCommandLine::getSwitchOption(const wstring &strOption, BOOL bDefaultValue) const
{
    std::wstring optValue;
    bool hasOpt = false;
    if (hasOption(strOption))
    {
        hasOpt = true;
        getOption(strOption, optValue);
    }

    if (!hasOpt)
        return bDefaultValue;

    if (optValue.empty() || optValue == L"true" || optValue == L"yes" || optValue == L"1")
        return TRUE;

    return FALSE;
}

BOOL
CCommandLine::parseOption(const wstring &strCmdLine, INT nBegin, INT nEnd, INT nEqualPos)
{
	// 支持"-"和"--"开头的参数，至少有两位长
	BOOL bIsCommand = FALSE;
	wstring strKey, strValue;
	INT nRealBegin = nBegin;

	if(nBegin < nEnd) {
		if(strCmdLine[nRealBegin] == _T('-') || strCmdLine[nRealBegin] == _T('/')) {
			bIsCommand = TRUE;
			++nRealBegin;
			if(strCmdLine[nRealBegin] == _T('-')) {
				++nRealBegin;
			}
		}
	}

	// 只有控制字符，那么直接跳过
	if(nRealBegin > nEnd) {
		return TRUE;
	}

	if(!bIsCommand) {
		// 其他输入，放入其他参数中
		strValue.assign(&(strCmdLine[nBegin]), nEnd - nBegin + 1);
		m_vOtherArgs.push_back(strValue);
		return TRUE;
	}

	// 输入为选项
	if(nEqualPos == -1) {
		strKey.assign(&(strCmdLine[nRealBegin]), nEnd - nRealBegin + 1);
	} else {
		strKey.assign(&(strCmdLine[nRealBegin]), nEqualPos - nRealBegin);
		if(nEqualPos == nEnd) {
			//ASSERT(nEqualPos != nEnd) << "Option with equal symbol has no value.";
		} else {
			INT nValueStart = nEqualPos + 1;
			if(strCmdLine[nEqualPos + 1] == _T('"')
				&& strCmdLine[nEnd] == _T('"')) {
					++nValueStart;
					--nEnd;
			}

			strValue.assign(&(strCmdLine[nValueStart]), nEnd - nValueStart + 1);
		}
	}

	m_vOptions[strKey] = strValue;

	return TRUE;
}