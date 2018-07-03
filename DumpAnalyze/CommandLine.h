#pragma once

#include <string>
#include <vector>
#include <sstream>
#include <map>


using std::wstring;
using std::vector;
using std::wstringstream;
using std::map;

class CCommandLine
{
public:
	static CCommandLine & getInstance()
	{
		static CCommandLine cmdline;
		return cmdline;
	}

	virtual ~CCommandLine();

	BOOL parse(const wstring &strCmdLine);

	// support the other args
	BOOL pack(wstring &strCmdLine, BOOL bAppendOtherArgs = FALSE) const;

	BOOL isEmpty() const;
	BOOL hasOption(const wstring &strOption) const;
	BOOL getOption(const wstring &strOption, wstring &strOptionValue) const;
	BOOL setOption(const wstring &strOption, const wstring &strOptionValue);
	BOOL getAllOptions(std::map<wstring, wstring> &vOptions) const;

	BOOL appendOtherArg(const wstring &strArg);
	BOOL getOtherArgs(vector<wstring> &strArgs) const;

    BOOL getSwitchOption(const wstring &strOption, BOOL bDefaultValue) const;

protected:
	BOOL parseOption(const wstring &strCmdLine, INT nBegin, INT nEnd, INT nEqualPos);

private:
	CCommandLine();
	CCommandLine(const wstring &strCmdLine);

private:
	std::map<wstring, wstring> m_vOptions;
	vector<wstring> m_vOtherArgs;
};