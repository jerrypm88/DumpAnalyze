#include "stdafx.h"
#include "Utils.h"
#include <atlmisc.h>
#include <strsafe.h>
#include "lib\zlib\unzip.h"

BOOL Util::File::ExtractCurrentFile(unzFile z, LPCSTR strOutFileName, unz_file_info& file_info)
{
	if (!strOutFileName)
		return FALSE;

	CString strFileDir = strOutFileName;
	PathAppend(strFileDir.GetBuffer(MAX_PATH), L"..\\");
	if (!::PathFileExists(strFileDir))
		SHCreateDirectoryEx(NULL, strFileDir, NULL);

	int nFileLength = file_info.uncompressed_size;
	if (nFileLength < 0)
		return FALSE;

	// 0 byte file
	if (nFileLength == 0)
	{
		HANDLE   hFile = CreateFileA(strOutFileName, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
		CloseHandle(hFile);
		return TRUE;
	}

	HANDLE   hFile = CreateFileA(strOutFileName, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hFile == INVALID_HANDLE_VALUE)
	{
		DWORD dwError = GetLastError();
		return FALSE;
	}

	// extract file
	BOOL   bRet = FALSE;
	if (unzOpenCurrentFile(z) == UNZ_OK)
	{
		// prepare temp buffer to read very big file
		int    nLen = 32 * 1024;
		void   * pBuf = malloc(nLen);

		// loop read file
		while (true)
		{
			int   nResult = unzReadCurrentFile(z, pBuf, nLen);
			if (nResult > 0)
			{
				DWORD   nWrite;
				::WriteFile(hFile, pBuf, nResult, &nWrite, NULL);
			}
			else if (nResult == 0)
			{
				bRet = TRUE; // end of file
				break;
			}
			else
			{
				break; // error
			}
		}

		free(pBuf);
	}
	unzCloseCurrentFile(z);
	CloseHandle(hFile);
	return bRet;
}

BOOL Util::File::DeleteDirectoryEx(LPCWSTR szDirname)
{
	if (!::PathFileExists(szDirname))
		return FALSE;

	CString strDirName = szDirname;
	if (strDirName.Right(1) == _T("\\"))
		strDirName = strDirName.Left(strDirName.GetLength() - 1);

	CFindFile find;
	TCHAR tempFileFind[MAX_PATH];

	StringCbPrintf(tempFileFind, sizeof(tempFileFind), _T("%s\\*.*"), strDirName);
	BOOL IsFinded = find.FindFile(tempFileFind);
	while (IsFinded)
	{
		if (!find.IsDots())
		{
			TCHAR foundFileName[MAX_PATH];
			_tcsncpy_s(foundFileName, MAX_PATH, find.GetFileName(), MAX_PATH - 1);

			if (find.IsDirectory())
			{
				CString strDir;
				strDir.Format(_T("%s\\%s"), strDirName, foundFileName);

				DeleteDirectoryEx(strDir);
			}
			else if (_tcslen(foundFileName) > 0)
			{
				CString strPathFile;
				strPathFile.Format(_T("%s\\%s"), strDirName, foundFileName);
				DeleteFileReadOnly(strPathFile);
			}
		}
		IsFinded = find.FindNextFile();
	}

	find.Close();
	if (!::RemoveDirectory(strDirName))
	{
		return FALSE;
	}

	return TRUE;
}

BOOL Util::File::DeleteFileReadOnly(CString& strFile)
{
	BOOL bRet = FALSE;
	if (!strFile.IsEmpty() && ::PathFileExists(strFile))
	{
		DWORD fileAttributes = GetFileAttributes(strFile);
		fileAttributes &= ~FILE_ATTRIBUTE_READONLY;
		SetFileAttributes(strFile, fileAttributes);
		bRet = ::DeleteFile(strFile);
		if (!bRet)
		{
			CString strFileNew = GetTmpRenameFile(strFile);
			if (rename(CT2A(strFile), CT2A(strFileNew)) == 0)
			{
				strFile = strFileNew;
			}

			MoveFileEx(strFile, NULL, MOVEFILE_DELAY_UNTIL_REBOOT);
		}

	}
	return bRet;
}

CString Util::File::GetTmpRenameFile(CString strFile)
{
	CString strFileNew = strFile;

	int nIndex = 0;
	while (::PathFileExists(strFileNew))
	{
		nIndex++;
		strFileNew.Format(_T("%s.tmp%d"), strFile, nIndex);
	}
	return strFileNew;
}

BOOL Util::File::UnzipFile(std::string& strZipFile,std::string& folder)
{
	if (strZipFile.length() == 0 || folder.length() == 0)
		return FALSE;

	BOOL     bRet = FALSE;
	unzFile  z = unzOpen(strZipFile.c_str());
	if (z)
	{
		int   nResult = unzGoToFirstFile(z);
		unz_file_info   file_info;
		char   strName[MAX_PATH + 2] = { 0 };

		while (nResult == UNZ_OK)
		{
			if (unzGetCurrentFileInfo(z, &file_info, strName, MAX_PATH, NULL, 0, NULL, 0) != UNZ_OK)
				break;

			// make full path file name
			char   strPath[MAX_PATH] = { 0 };
			StringCchCopyA(strPath, MAX_PATH,folder.c_str());
			PathAddBackslashA(strPath);
			StringCchCatA(strPath, MAX_PATH, strName);
			if (!ExtractCurrentFile(z, strPath, file_info))
				break;

			nResult = unzGoToNextFile(z);
			if (nResult == UNZ_END_OF_LIST_OF_FILE)
			{
				bRet = TRUE; // end of enum
				break;
			}
		}
		unzClose(z);
	}
	return bRet;
}

BOOL Util::File::GetFileList(std::string& strZipFile, vector<std::string>& vecFile, BOOL bGetAll)
{
	unzFile  z = unzOpen64(strZipFile.c_str());
	if (!z)
		return FALSE;

	int nResult = unzGoToFirstFile(z);
	while (nResult == UNZ_OK)
	{
		unz_file_info   file_info;
		ZeroMemory(&file_info, sizeof(unz_file_info));
		char  strName[MAX_PATH + 2] = { 0 };
		if (unzGetCurrentFileInfo(z, &file_info, strName, MAX_PATH, NULL, 0, NULL, 0) != UNZ_OK)
		{
			unzClose(z);
			return FALSE;
		}

		if (!bGetAll)
		{
			for (int n = 0; n < (int)vecFile.size(); n++)
			{
				if (vecFile[n].compare(strName) == 0)
				{
					unzClose(z);
					return TRUE;
				}
			}
		}
		else
		{
			vecFile.push_back(strName);
		}

		nResult = unzGoToNextFile(z);
		if (nResult == UNZ_END_OF_LIST_OF_FILE)
			break;
	}
	unzClose(z);
	return TRUE;
}

CStringA Util::Process::CreateProcessForOutput(BOOL bWaitForExit, LPCSTR lpFilePath, LPCSTR lpParameters, DWORD nTimeOut)
{
	if (NULL == lpFilePath)
	{
		return "";
	}
	char szCommand[1024] = { 0 };
	if (NULL != lpParameters)
	{
		::wsprintfA(szCommand, "%s %s", lpFilePath, lpParameters);
	}
	else
	{
		::wsprintfA(szCommand, "%s", lpFilePath);
	}
	CStringA strRet("");
	SECURITY_ATTRIBUTES sa;
	HANDLE hRead = nullptr;
	HANDLE hWrite = nullptr;
	sa.nLength = sizeof(SECURITY_ATTRIBUTES);
	sa.lpSecurityDescriptor = NULL;
	sa.bInheritHandle = TRUE;
	if (!CreatePipe(&hRead, &hWrite, &sa, 0)) {
		goto clear;
	}
	STARTUPINFOA si = {0};
	PROCESS_INFORMATION pi = {0};
	si.cb = sizeof(STARTUPINFO);
	GetStartupInfoA(&si);
	si.hStdError = hWrite;
	si.hStdOutput = hWrite;
	si.wShowWindow = SW_HIDE;
	si.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
	if (!::CreateProcessA(NULL, szCommand
		, NULL, NULL, TRUE, NULL, NULL, NULL, &si, &pi)) 
	{
		CloseHandle(hWrite);
		goto clear;
	}
	CloseHandle(hWrite);
	if (pi.hThread)
	{
		::CloseHandle(pi.hThread);
		pi.hThread = NULL;
	}

	struct ProcessWorkItem
	{
		static DWORD WINAPI ThreadProc(LPVOID lpParam)
		{
			ProcessWorkItem *p = (ProcessWorkItem *)lpParam;
			DWORD dwWaitResult = WaitForSingleObject(p->m_hProcess, p->m_nTimeOut);
			if (WAIT_OBJECT_0 != dwWaitResult)
			{
				TerminateProcess(p->m_hProcess, -1);
			}
			::CloseHandle(p->m_hProcess);
			delete p;
			return 0;
		}

		DWORD   m_nTimeOut;
		HANDLE  m_hProcess;
	};

	if (!bWaitForExit)  //建立监控线程防止同步读取匿名管道一直阻塞
	{
		ProcessWorkItem *p = new ProcessWorkItem;
		p->m_hProcess = pi.hProcess;
		p->m_nTimeOut = nTimeOut;
		HANDLE hThread = CreateThread(NULL, 0, &ProcessWorkItem::ThreadProc, p, 0, NULL);
		if (hThread)
		{
			CloseHandle(hThread);
			pi.hProcess = NULL;
		}
		else
		{
			delete p;
		}
	}

	char buffer[4096] = { 0 };
	DWORD bytesRead;
	while (true) {
		if (ReadFile(hRead, buffer, 4095, &bytesRead, NULL) == NULL)
			break;
		strRet += buffer;
		ZeroMemory(buffer, sizeof(buffer));
	}

clear:
	if (hRead)
	{
		CloseHandle(hRead);
		hRead = nullptr;
	}
	if( pi.hProcess )
	{
		DWORD dwWaitResult = WAIT_TIMEOUT;
		if(bWaitForExit)
			dwWaitResult = ::WaitForSingleObject(pi.hProcess, nTimeOut);
		else
			dwWaitResult = ::WaitForSingleObject(pi.hProcess, 0);

		if (WAIT_OBJECT_0 != dwWaitResult)
		{
			TerminateProcess(pi.hProcess, -1);
		}
		::CloseHandle(pi.hProcess);
	}
	return strRet;
}

std::list<CStringA> Util::STRING::spliterString(const CStringA & src, const CStringA & spliter)
{
	std::list<CStringA> ret;
	CStringA strTemp = src;

	LPSTR lpContext = NULL;
	for(LPSTR lp = strtok_s(strTemp.GetBuffer(), spliter, &lpContext); lp;
		lp = strtok_s(NULL, spliter, &lpContext))
	{
		ret.push_back( lp );
	}
	return std::move(ret);
}
