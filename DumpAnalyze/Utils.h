#pragma once
#include <atlstr.h>
#include <string>
#include <vector>
#include "lib/zlib/unzip.h"
#include <list>

namespace Util
{
	namespace File 
	{
		BOOL DeleteDirectoryEx(LPCWSTR szFolder);
		BOOL DeleteFileReadOnly(CString& strFile);
		CString GetTmpRenameFile(CString strFile);
		BOOL UnzipFile(std::string& zipPath,std::string& folder);
		BOOL ExtractCurrentFile(unzFile z, LPCSTR strOutFileName, unz_file_info& file_info);
		BOOL GetFileList(std::string& strZipFile, vector<std::string>& vecFile, BOOL bGetAll);
	}

	namespace Process
	{
		CStringA CreateProcessForOutput(BOOL bWaitForExit, LPCSTR lpFilePath, LPCSTR lpParameters, DWORD nTimeOut = 10 * 60 * 1000);
	}

	namespace STRING
	{
		std::list<CStringA> spliterString(const CStringA &src, const CStringA &spliter);
	}
}
