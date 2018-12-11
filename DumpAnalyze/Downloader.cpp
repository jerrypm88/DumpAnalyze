#include "stdafx.h"
#include "Downloader.h"
#include <io.h>  
#include <curl/curl.h>
#include <curl/easy.h>
#include <wininet.h>
#include <strsafe.h>
#include <atltime.h>
#include <atlcomcli.h>
#include <urlmon.h>
#include "lib/json/lib_json/json/value.h"
#include "lib/json/lib_json/json/reader.h"
#include <corecrt_wstdio.h>
#include "Utils.h"
#include <algorithm>
#include "Common/VersionHelpers.h"
#include "UrlMonAdapter.h"
#include <fstream>
#include "md5_ex.h"
#include <atlfile.h>
#include <imagehlp.h>
using namespace ATL;



#ifdef _DEBUG
#pragma comment(lib,"libcurld.lib")
#pragma comment(lib,"jsoncpp_d.lib")
#pragma comment(lib,"zlib_d.lib")
#else
#pragma comment(lib,"libcurl.lib")
#pragma comment(lib,"jsoncpp.lib")
#pragma comment(lib,"zlib.lib")
#endif // _DEBUG

#pragma comment ( lib, "ws2_32.lib" )
#pragma comment ( lib, "winmm.lib" )
#pragma comment ( lib, "wldap32.lib" )
#pragma comment ( lib, "Version.lib" )
#pragma comment ( lib, "sqlite3.lib" )
#pragma comment(lib, "UrlMon.lib")
#pragma comment(lib, "DbgHelp.lib")





#define DAY_FORMAT					"%d-%d-%d"
#define POST_PARAM					L"src=%s&date=%s"
#define FILE_NAME					"%d-%d-%d.zip"
#define DUMP_URL_TAG				"dump_file_url"
#define DUMP_FOLDER_APPENDIX		"dump\\"
#define DUMP_ANALYZE_CMD			"-z \"%s\" -c \".ecxr;k;q\""
#define DUMP_ANALYZE_EXE_NAME		"cdb.exe"
#define DUMP_ANALYZE_EXE_PATH		L"D:\\softbag\\windbg\\cdb.exe"
#define DUMP_INFO_GAP				"***"
#define RESULT_FILE_NAME			"00000000000000000000000_result.txt"
#define MAX_SHOW_RANKS				50
#define MAX_SHOW_LOCAL_PATH_COUNTS	5

CDumpAnalyze::CDumpAnalyze(void)
{
	m_pSqliteDb = NULL;
	curl_global_init(CURL_GLOBAL_ALL);
}

CDumpAnalyze::~CDumpAnalyze(void)
{
	if( m_pSqliteDb )
	{
		sqlite3_close(m_pSqliteDb);
		m_pSqliteDb = NULL;
	}
	curl_global_cleanup();
}

size_t CDumpAnalyze::WriteFunc(char *str, size_t size, size_t nmemb, void *stream)
{
	return fwrite(str, size, nmemb, (FILE*)stream);
}

size_t CDumpAnalyze::ProgressFunc(
	double* pFileLen,
	double t,// 下载时总大小    
	double d, // 已经下载大小    
	double ultotal, // 上传是总大小    
	double ulnow)   // 已经上传大小    
{
	if (t == 0) return 0;
	*pFileLen = d;
	return 0;
}

int CDumpAnalyze::StartWorkThread(HWND hWnd)
{
	m_hWnd = hWnd;
	if ( !m_hWorkThread || WaitForSingleObject(m_hWorkThread,0) == WAIT_OBJECT_0 )
	{
		m_hWorkThread = (HANDLE)_beginthreadex(NULL, 0, WorkProc, this, 0, NULL);
		return 0;
	}
	return -1;
}

void CDumpAnalyze::Reset()
{
	m_hWorkThread = nullptr;
	m_nFailCounts = 0;
	m_mapDumpResult.clear();
	m_lstDumpInfo.clear();
	m_mapCallStack.clear();
}

int GetProcessCount(void)
{
	SYSTEM_INFO info = {};
	GetSystemInfo(&info);

	return info.dwNumberOfProcessors;
}

void AnalyzeDumpThreadProc(void *pThis)
{
	if (pThis)
	{
		CDumpAnalyze *p = (CDumpAnalyze*)pThis;
		p->AnalyzeDumpThread();
	}
}

void CDumpAnalyze::AnalyzeDumpThread()
{
	DUMP_INFO dump_info;

	while (true)
	{
		do
		{
			std::lock_guard<std::mutex> lock(m_lstDumpUrlsMutex);
			if (m_lstDumpInfo.empty())
			{
				LOG << "thread quit." << std::this_thread::get_id();
				return;
			}
			
			PDUMP_INFO pInfo = m_lstDumpInfo.front();
			m_lstDumpInfo.pop_front();
			if (pInfo)
			{
				dump_info = *pInfo;
				delete pInfo;
			}
		} while (0);

		m_currentCount++;

		UpdateProcess(PT_ANALYZING, m_currentCount);
		std::string name = PathFindFileNameA(dump_info.url.c_str());
		std::string path = g_strWorkingFolder;
		path += "dump\\";

		if (!PathFileExistsA(path.c_str()))
		{
			CreateDirectoryA(path.c_str(), NULL);
		}

		path += name;
		std::string dumpPath = DumploadAndUnzipDump(dump_info, path, g_strWorkingFolder + "dump\\");
		if (dumpPath.length() > 0 && PathFileExistsA(dumpPath.c_str()))
		{
			AnalyzeDump(dump_info, dumpPath);
		}
	}
}

void CDumpAnalyze::WorkImpl()
{
	UpdateProcess(PT_BEGIN);
	const CTime tmCurrent(CTime::GetCurrentTime());

	std::wstring strDigest;
	CCommandLine::getInstance().getOption(L"digest", strDigest);

	std::wstring strClean;
	CCommandLine::getInstance().getOption(L"clean", strClean);
	LONG lCleanDays = wcstol(strClean.c_str(), NULL, 10);
	if( lCleanDays > 0 )
	{
		CTime tmCleanDays = tmCurrent - CTimeSpan(lCleanDays, 0, 0, 0);
		m_dateClean.Format("%04d-%02d-%02d", tmCleanDays.GetYear(), tmCleanDays.GetMonth(), tmCleanDays.GetDay());
	}

	std::wstring from;
	if (!CCommandLine::getInstance().getOption(L"from", from))
	{
		from = L"ldstray";
	}

	if (!CCommandLine::getInstance().getOption(L"date", m_dateProcess))
	{
		try
		{
			CTimeSpan span( (strDigest.empty() ? 1 : 0), 0,0,0);
			CTime yesterday = tmCurrent - span;
			CStringW strDate;
			strDate.Format(L"%04d-%02d-%02d", yesterday.GetYear(), yesterday.GetMonth(), yesterday.GetDay());
			m_dateProcess = (LPCWSTR)strDate;
		}
		catch (...)
		{
		}
	}

	std::string foler;
	InitWrokingFolder( !strDigest.empty(), foler, m_dateProcess.c_str(), from);
	g_strWorkingFolder = foler;
	InitDbAndFlagDlls(strDigest.empty());

	if( strDigest.empty() )
	{
		CString strPost;
		strPost.Format(POST_PARAM, from.c_str(), m_dateProcess.c_str());

		CStringA szPostA = CW2A(strPost);

		UpdateProcess(PT_GET_DUMP_INFO);
		CStringA strRet;
		CComPtr<CUrlMonToBufAdapter> pAdapter = new CUrlMonToBufAdapter();

		std::wstring url;

		CCommandLine::getInstance().getOption(L"url", url);
		BOOL bRet = pAdapter->Download(url.c_str(), (LPBYTE)(LPSTR)(LPCSTR)szPostA, szPostA.GetLength(), 60*1000);
		if (bRet) 
		{
			strRet = pAdapter->m_strBuf;
			ParseDumpUrls((LPCSTR)strRet);
		}
	}
	else //仅仅是聚合之前的结果
	{
		DoDigestResultHtmls(strDigest);
		DoCleanDb();
		UpdateProcess(PT_DONE,0);
		return;
	}

	m_currentCount = 0;
	if (!m_lstDumpInfo.empty())
	{
		UpdateProcess(PT_GET_DUMP_DONE, m_lstDumpInfo.size());

		BOOL bUseTransaction = TRUE;
		if(CCommandLine::getInstance().hasOption(L"no_use_transaction"))
			bUseTransaction = FALSE;

		if (m_pSqliteDb && bUseTransaction)
		{
			int iRet = sqlite3_exec(m_pSqliteDb, "BEGIN TRANSACTION", NULL, NULL, NULL);
		}

		int nThreadCount = 1;
		std::wstring thread_count_cmd;
		CCommandLine::getInstance().getOption(L"thread", thread_count_cmd);
		if (!thread_count_cmd.empty())
		{
			nThreadCount = _ttoi(thread_count_cmd.c_str());
		}
		else
		{
			nThreadCount = GetProcessCount() * 8;
		}

		std::vector<std::thread> all_threads;
		all_threads.reserve(nThreadCount);
		for (int n = 0; n < nThreadCount; n++)
		{
			all_threads.push_back(std::thread(AnalyzeDumpThreadProc, this));
		}

		for (std::vector<std::thread>::iterator it = all_threads.begin(); it != all_threads.end(); ++it)
		{
			std::thread & t = *it;
			t.join();
		}

		if (m_pSqliteDb && bUseTransaction)
		{
			int iRet = sqlite3_exec(m_pSqliteDb, "COMMIT", NULL, NULL, NULL);
		}

		OutputResult();
	}

	DoCleanDb();
	UpdateProcess(PT_DONE, 0);
	return;
}

void CDumpAnalyze::InitWrokingFolder(BOOL bIsDigest, std::string& strFloder, const wchar_t* strAppendix, const std::wstring& from)
{
	wchar_t szFolder[MAX_PATH] = { 0 };
	GetModuleFileName(NULL, szFolder, MAX_PATH);
	PathRemoveFileSpec(szFolder);
	m_strSqliteDb = szFolder;
	m_strSqliteDb += L"\\DumpAnalyze.db3";

	m_strFromPart = (bIsDigest ? L"digest" : from.c_str());
	PathAppend(szFolder, m_strFromPart);
	PathAddBackslash(szFolder);

	CString strFolder;
	SYSTEMTIME time;
	GetLocalTime(&time);
	
	strFolder.Format(L"%s%04d-%02d-%02d_%02d-%02d-%02d", szFolder, time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond);
	m_strSqliteTbl.Format("t_%ls_%04d-%02d-%02d_%02d-%02d-%02d", (LPCWSTR)m_strFromPart, time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond);
	
	strFolder += L"\\";
	SHCreateDirectory(NULL, strFolder);
	strFloder = (CW2A)strFolder;
}

std::string CDumpAnalyze::DumploadAndUnzipDump(const DUMP_INFO & dump_info, std::string& path,std::string& folder)
{
	LOG << "Begin down:" << path.c_str();
	CURL *pCurl = NULL;
	FILE* pFile = fopen(path.c_str(), "wb");
	pCurl = curl_easy_init();
	if (pCurl != NULL && pFile != NULL)
	{
		curl_easy_setopt(pCurl, CURLOPT_WRITEDATA, (void*)pFile);
		curl_easy_setopt(pCurl, CURLOPT_WRITEFUNCTION, WriteFunc);
		curl_easy_setopt(pCurl, CURLOPT_URL, dump_info.url.c_str());
		curl_easy_perform(pCurl);
		curl_easy_cleanup(pCurl);

		fflush(pFile);
		int nRet = fclose(pFile);
		LOG << "End down:" << path.c_str() << ":close ret:" << nRet;
	}
	if (PathFileExistsA(path.c_str()))
	{
		if (Util::File::UnzipFile(path, folder))
		{
			std::vector<std::string> vecFile;
			if (Util::File::GetFileList(path, vecFile, true) && vecFile.size() != 0)
			{
				std::string singlePath = folder;
				singlePath += vecFile[0];
				LOG << "Delete file:" << path.c_str();
				if (!::DeleteFileA(path.c_str()))
				{
					DWORD dwError = GetLastError();
					LOG << "Delete File Failed: error=" << dwError << ",file:" << path.c_str();
				}
				return singlePath;
			}
		}
	}
	return "";
}

BOOL CDumpAnalyze::AnalyzeDump(const DUMP_INFO & dump_info, const std::string& path)
{
	std::wstring cdb_path = DUMP_ANALYZE_EXE_PATH;
	if( !CCommandLine::getInstance().getOption(L"cdb", cdb_path) || !PathFileExistsW(cdb_path.c_str()) )
		return FALSE;

	const BOOL bIsWinDbgUsed = !wcsicmp(PathFindFileNameW(cdb_path.c_str()), L"windbg.exe");
	std::string cdb_path_a = CW2A(cdb_path.c_str());

	CStringA strCmdLine;
	strCmdLine.Format(DUMP_ANALYZE_CMD, path.c_str());

	std::wstring symbol_path;
	CCommandLine::getInstance().getOption(L"symbol", symbol_path);
	if (!symbol_path.empty())
	{
		CStringA szSymbolCmd;
		szSymbolCmd.Format(" -y \"%s\"", (LPCSTR)CW2A(symbol_path.c_str()));
		strCmdLine += szSymbolCmd;
	}

	CStringA strLogA;
	if( bIsWinDbgUsed )
	{
		GetTempPathA( MAX_PATH, strLogA.GetBuffer(MAX_PATH) );
		PathRemoveBackslashA(strLogA.GetBuffer());
		strLogA.ReleaseBuffer();
		
		CStringA strBuff;
		strBuff.Format("%s\\%p_%04x_%04x.tmp", (LPCSTR)strLogA, GetTickCount(), rand(), rand() );
		strLogA = strBuff;

		strCmdLine += " -WX -Q -logo \"";
		strCmdLine += strLogA;
		strCmdLine += "\"";
	}

	CStringA strRet;
	strRet = Util::Process::CreateProcessForOutput(bIsWinDbgUsed, cdb_path_a.c_str(), strCmdLine);
	
	if( bIsWinDbgUsed && PathFileExistsA(strLogA) )
	{
		HANDLE hFile = CreateFileA(strLogA, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
		if(INVALID_HANDLE_VALUE != hFile)
		{
			CAtlFileMapping<> fileMap;
			HRESULT hr = fileMap.MapFile(hFile);
			if(SUCCEEDED(hr))
			{
				LPCSTR szFileData = (LPCSTR)fileMap.GetData();
				if( szFileData && fileMap.GetMappingSize() )
					strRet.SetString(szFileData, fileMap.GetMappingSize());
			}
			CloseHandle(hFile);
		}
		if( !DeleteFileA(strLogA) )
			MoveFileExA(strLogA, NULL, MOVEFILE_DELAY_UNTIL_REBOOT);
	}

	int iRetStart = strRet.Find("ChildEBP RetAddr");
	CStringA strCallStack;
	do
	{
		LOG << "----------------" << m_currentCount << "----------------" << "\r\n" << strRet << "\n";

		int nStartPos = -1;
		if ( iRetStart == -1 || (nStartPos = strRet.Find(DUMP_INFO_GAP)) == -1)	 //"*"
			return FALSE;

		int nQuitPos = strRet.Find("quit");
		strRet = strRet.Left(nQuitPos);

		strCallStack = strRet;
		while (nStartPos != -1)
		{
			strCallStack = strCallStack.Mid(nStartPos + strlen(DUMP_INFO_GAP));
			int nNexLine = strCallStack.Find("\n");
			if (nNexLine != -1)
			{
				strCallStack = strCallStack.Mid(nNexLine + 1);
			}
			nStartPos = strCallStack.Find(DUMP_INFO_GAP);
		}

		if (strCallStack.Find("Executable search path") >= 0)
		{
			int nNexLine = strCallStack.Find("\n");
			if (nNexLine != -1)
			{
				strCallStack = strCallStack.Mid(nNexLine + 1);
			}
		}
	} while (0);
	strCallStack.Trim();

	std::list<CStringA> listLines = Util::STRING::spliterString(strCallStack,"\n");
	std::list<CStringA>::iterator iter = listLines.begin();
	while (iter != listLines.end())
	{
		CStringA strLine = *iter;
		if (strLine.Left(9).CompareNoCase("WARNING: ") == 0 
			|| strLine.Left(3).CompareNoCase("***") == 0 
			|| strLine.Left(9).CompareNoCase("quit") == 0)
		{
			iter = listLines.erase(iter);
			continue;
		}
		else 
		{
			iter++;
		}
	}

	CStringA strDll;
	CStringA strTag;
	std::list<CStringA>::iterator iter2 = listLines.begin();
	while (iter2 != listLines.end())
	{
		const CStringA & strLine = *iter2;
		LPSTR pEndNum = NULL;
		strtoul(strLine, &pEndNum, 16);
		//
		if (pEndNum && 8==(pEndNum - (LPCSTR)strLine))
		{
			LPCSTR szPureStack = (LPCSTR)strLine + 18;
			strTag += szPureStack;
			if (strDll.IsEmpty())
				PeekDllFromPureStack(szPureStack, strDll);
		}
		else
		{
			strTag += strLine;
		}
		strTag += "\r\n";
		iter2++;
	}

	// tag 算一个哈希值，方便比较及存储。
	strTag = MD5( (LPCSTR)strTag ).toString().c_str();

	ArrangeDumpInfo(strDll, strTag, strCallStack, path.c_str(), dump_info);
	return TRUE;
}

void CDumpAnalyze::PeekDllFromPureStack(LPCSTR szPureStack, CStringA & strDll)
{
	while ('\x20' == szPureStack[0])
		++szPureStack;

	LPCSTR szFind = strpbrk(szPureStack, "!+");
	if (!szFind)
		return;

	strDll.SetString(szPureStack, (szFind - szPureStack));
	if (strDll.IsEmpty())
		return;

	strDll.MakeLower();
	const CStringA strDllTmp = strDll;
	
	if( !PeekDllFromPureStackInternal(strDll) )
		return;

	do
	{
		szFind = strrchr((LPCSTR)strDllTmp, '_');
		if (!szFind)
			break;

		LPSTR szFindEnd = NULL;
		if ( 0 == strtoul(szFind + 1, &szFindEnd, 16) || !szFindEnd || *szFindEnd )
			break;

		strDll = strDllTmp.Left( (szFind - (LPCSTR)(strDllTmp)) );
		if (!PeekDllFromPureStackInternal(strDll))
			return;

	} while (0);

	//非标准模块，用它
	strDll = strDllTmp;
	SetDllFlag(strDll, DLL_FLAG_USER);
}

BOOL CDumpAnalyze::PeekDllFromPureStackInternal(CStringA & strDll)
{
	DLL_FLAG dllFlag = GetDllFlag(strDll);
	if (DLL_FLAG_KNOWN == dllFlag)
	{
		strDll.Empty();
		return FALSE;
	}
	else if(DLL_FLAG_USER == dllFlag)
	{
		//用它
		return FALSE;
	}

	CA2W a2wDll(strDll, CP_ACP);
	WCHAR szBufferFile[MAX_PATH] = { 0 };
	if (0 == SearchPathW(NULL, a2wDll, L".dll", MAX_PATH, szBufferFile, NULL))
	{
		return TRUE;
	}

	CStringW strCompanyName;
	if (!VersionHelper::GetFileVersionQueryKey(szBufferFile, L"CompanyName", strCompanyName))
	{
		//视作未知模块，用它
		SetDllFlag(strDll, DLL_FLAG_USER);
		return FALSE;
	}

	if ( !StrStrIW(strCompanyName, L"Microsoft") )
	{
		//非微软模块，用它
		SetDllFlag(strDll, DLL_FLAG_USER);
		return FALSE;
	}

	SetDllFlag(strDll, DLL_FLAG_KNOWN);
	strDll.Empty();
	return FALSE;
}

void CDumpAnalyze::InitDbAndFlagDlls(BOOL bCreateNewTable)
{
	std::lock_guard<std::mutex> lock(m_lockFlagDlls);

	if( m_pSqliteDb )
		return;

	int iRet = sqlite3_open( CW2A(m_strSqliteDb, CP_UTF8), &m_pSqliteDb);
	if(SQLITE_OK != iRet)
	{
		if( m_pSqliteDb )
		{
			sqlite3_close(m_pSqliteDb);
			m_pSqliteDb = NULL;
		}
		return;
	}

	iRet = sqlite3_exec(m_pSqliteDb, 
		"create table if not exists [t_flag_dlls]( dll_name varchar(260) PRIMARY KEY, dll_flag integer, reserved varchar(260) )", 
		NULL, NULL, NULL);
	if(SQLITE_OK != iRet)
		return;

	iRet = sqlite3_exec(m_pSqliteDb, 
		"create table if not exists [t_tag_call_stack]( tag varchar(260) PRIMARY KEY, call_stack integer, tbl_name varchar(260) )", 
		NULL, NULL, NULL);
	if(SQLITE_OK != iRet)
		return;

	if( bCreateNewTable )
	{
		CStringA strSql;
		strSql.Format(
			"CREATE TABLE if not exists [%s]( dll_name varchar(260), ver varchar(32), tag varchar(260), dump_path varchar(260), reserved varchar(260) )",
			(LPCSTR)m_strSqliteTbl);
		iRet = sqlite3_exec(m_pSqliteDb, strSql, NULL, NULL, NULL);
		if(SQLITE_OK != iRet)
			return;
	}

	//初始化模块类型列表
	struct ExecFlagDllsCallback
	{
		static int Callback(void *para, int n_column, char **column_value, char **column_name)
		{
			map<CStringA, DLL_FLAG> * pMapFlagDlls = (map<CStringA, DLL_FLAG> *)para;
			
			if(2 != n_column)
				return SQLITE_ERROR;

			if(column_value[0] && column_value[1])
			{
				(*pMapFlagDlls)[ column_value[0] ] = (DLL_FLAG)strtol(column_value[1], NULL, 10);
			}
			return SQLITE_OK;
		}
	};

	iRet = sqlite3_exec(m_pSqliteDb, 
		"select DISTINCT dll_name, dll_flag from t_flag_dlls", 
		&ExecFlagDllsCallback::Callback, &m_mapFlagDlls, NULL);
}

DLL_FLAG CDumpAnalyze::GetDllFlag(const CStringA &strDll) const
{
	std::lock_guard<std::mutex> lock(m_lockFlagDlls);

	map<CStringA,DLL_FLAG>::const_iterator itrFind = m_mapFlagDlls.find(strDll);
	if(itrFind != m_mapFlagDlls.end())
		return itrFind->second;

	return DLL_FLAG_NONE;
}

bool CDumpAnalyze::SetDllFlag(const CStringA &strDll, DLL_FLAG dllFlag)
{
	if(DLL_FLAG_NONE == dllFlag)
		return false;

	std::lock_guard<std::mutex> lock(m_lockFlagDlls);

	bool bInsert = m_mapFlagDlls.insert( make_pair(strDll, dllFlag) ).second;
	if( bInsert && m_pSqliteDb )
	{
		CStringA strSql;
		strSql.Format(
			"insert or replace into t_flag_dlls values(\"%s\", %d, null )",
			(LPCSTR)strDll, dllFlag);
		int iRet = sqlite3_exec(m_pSqliteDb, strSql, NULL, NULL, NULL);
	}
	return bInsert;
}

void CDumpAnalyze::ArrangeDumpInfo(__inout CStringA &strDll, const CStringA &strTag, const CStringA &strCallStack, const CStringA &strPath, const DUMP_INFO & dump_info)
{
	std::lock_guard<std::mutex> lock(m_resultMutex);
	do
	{
		// 这个容器 m_mapVerTagResult 用作按版本号分类。。
		VER_TAG_RESULT_ITERATOR it_tag_result = m_mapVerTagResult.find(dump_info.ver);
		if (it_tag_result == m_mapVerTagResult.end()) {
			TAG_DUMP_RESULT tag_result;
			tag_result[strTag].push_back(strTag);
			tag_result[strTag].push_back(strPath);
			m_mapVerTagResult[dump_info.ver] = tag_result;
		}
		else {
			TAG_DUMP_RESULT &tag_result = it_tag_result->second;
			TAG_DUMP_RESULT_ITERATOR it = tag_result.find(strTag);
			if (it == tag_result.end()) {
				tag_result[strTag].push_back(strTag);
			}
			tag_result[strTag].push_back(strPath);
		}

		// 这个容器 m_mapDumpResult 用作展示整体情况，不区分版本
		// 第一个写tag，方便分类
		if (m_mapDumpResult.find(strTag) == m_mapDumpResult.end())
			m_mapDumpResult[strTag].push_back(strTag);
		m_mapDumpResult[strTag].push_back(strPath);

		// 这个容器 m_mapCallStack 只是用于根据 tag 取 stacks
		//<tag, callstack>
		if (m_mapCallStack.find(strTag) == m_mapCallStack.end())
			m_mapCallStack[strTag] = strCallStack;

	} while (0);

	if( m_pSqliteDb )
	{
		CStringA strVer = "v";             // 如果提取模块版本失败，则以 v+产品版本 表示。
		strVer += dump_info.ver.c_str();
		const BOOL bUnknownDll = strDll.IsEmpty();
		if( PeekDllVersionFromDump(strDll, strPath, strVer) && bUnknownDll )
		{
			strDll.Insert(0, "unknown_");  // unknown_LdsIeView etc.
			strDll.MakeLower();
		}

		if (!strnicmp(strDll, "flash32_", 8))
		{
			strDll = "flash32_x_x_x_x";    // Flash整合在一块
		}

		CStringA strSql;
		strSql.Format(
			"insert into [%s] values( \"%s\", \"%s\", \"%s\", \"%s\", \"%s\" )",
			(LPCSTR)m_strSqliteTbl, 
			(LPCSTR)strDll, 
			(LPCSTR)strVer, 
			(LPCSTR)strTag, 
			PathFindFileNameA(strPath), 
			dump_info.ver.c_str());
		int iRet = sqlite3_exec(m_pSqliteDb, strSql, NULL, NULL, NULL);

		CStringA strCallStackTmp = strCallStack;
		strCallStackTmp.Replace("\"", "\"\"");
		strSql.Format("insert or ignore into [t_tag_call_stack] values(\"%s\", \"%s\", \"%s\" )",
			(LPCSTR)strTag, (LPCSTR)strCallStackTmp, (LPCSTR)m_strSqliteTbl);
		iRet = sqlite3_exec(m_pSqliteDb, strSql, NULL, NULL, NULL);
	}
}


bool SortByCount(const list<CStringA>* pLeft, const list<CStringA>* pRight)
{
	return pLeft->size() > pRight->size();
}


void CDumpAnalyze::OutputResult() const
{
	//版本分类表
	VER_TAG_RESULT::const_iterator it = m_mapVerTagResult.begin();
	for (; it != m_mapVerTagResult.end(); ++it)
	{
		//3.0.0.1001-result.html
		CStringA strHtmResult;
		strHtmResult.Format("%s\\%s-%s", g_strWorkingFolder.c_str(), it->first.c_str(), "result.html");
		WriteResultHtml(strHtmResult, it->second);
	}
	
	//总表
	if (!m_mapDumpResult.empty())
	{
		CStringA strHtmResult;
		strHtmResult.Format("%s\\%s", g_strWorkingFolder.c_str(), "result.html");
		WriteResultHtml(strHtmResult, m_mapDumpResult);
	}

	//======================================================================
	//模块分类表
	WriteDllResultHtmlsDigestCallback vDllResultHtmlsCallback(m_pSqliteDb, m_strFromPart);
	WriteDllResultHtmls(vDllResultHtmlsCallback, m_strSqliteTbl);
}




#define HTML_HEAD "<!DOCTYPE html>\
<html>\
<head>\
<meta charset = \"utf-8\">\
<title>%s</title>\
</head>"

#define BODY_HEAD_END "</body></html>"

void CDumpAnalyze::WriteResultHtml(
	const CStringA &strPath, 
	const TAG_DUMP_RESULT & tag_result) const
{
	int nDumpCounts = 0;
	std::list< const list<CStringA>* > lstToSort;
	std::map<CStringA, CStringAList >::const_iterator iter = tag_result.begin();
	while (iter != tag_result.end())
	{
		lstToSort.push_back( &(iter->second));
		nDumpCounts += (iter->second.size() - 1);
		iter++;
	}
	lstToSort.sort(SortByCount);
	FILE* pFile = fopen(strPath, "wb");
	if (pFile != NULL)
	{
		//HEAD
		std::wstring szFrom;
		CCommandLine::getInstance().getOption(L"from", szFrom);
		szFrom += m_dateProcess;
		szFrom += L" dump auto analyze result";

		CStringA szHead;
		szHead.Format(HTML_HEAD, (LPCSTR)CW2A(szFrom.c_str()));
		fwrite(szHead, sizeof(char), szHead.GetLength(), pFile);

		//1.counts
		CStringA strCounts;
		strCounts.Format("<body><h2>Analyzed  %d  dump</h2>", nDumpCounts);
		fwrite(strCounts, sizeof(char), strCounts.GetLength(), pFile);
		//2 top20
		int i = 0;
		std::list<const list<CStringA>*>::const_iterator iter = lstToSort.begin();
		while (iter != lstToSort.end() && i < MAX_SHOW_RANKS)
		{
			const list<CStringA>* pList = *iter;

			CStringA strDesc;
			strDesc.Format("<h4>Top %d: total counts = %d</h4>", i + 1, pList->size() - 1);
			fwrite(strDesc, sizeof(char), strDesc.GetLength(), pFile);
			if (pList)
			{
				list<CStringA>::const_iterator iterDumpInfo = pList->begin();
				CStringA strTag;
				CStringA strCallStack;
				int nIndex = 0;
				while (iterDumpInfo != pList->end())
				{
					if (nIndex == 0)
					{
						strTag = *iterDumpInfo;
						strCallStack = m_mapCallStack[strTag];
						strCallStack = "<pre>" + strCallStack;
						strCallStack += "</pre><br>";
						fwrite(strCallStack, sizeof(char), strCallStack.GetLength(), pFile);
					}
					else
					{
						if (nIndex > MAX_SHOW_LOCAL_PATH_COUNTS)
							break;
						CStringA strLocalPath = *iterDumpInfo;
						CStringA fileName = PathFindFileNameA(strLocalPath);
						CStringA fileNameLnk;
						fileNameLnk.Format("<a href=%s>%s</a><br>", "./dump/"+fileName, fileName);
						fwrite(fileNameLnk, sizeof(char), fileNameLnk.GetLength(), pFile);
					}
					//fwrite("<br>", sizeof(char), 4, pFile);
					iterDumpInfo++;
					nIndex++;
				}
			}
			//fwrite("<br>", sizeof(char), 4, pFile);
			iter++;
			i++;
		}
		fwrite("<!--panming@copyright-->", sizeof(char), strlen("<!--panming@copyright-->"), pFile);
		fwrite(BODY_HEAD_END, sizeof(char), strlen(BODY_HEAD_END), pFile);
		fclose(pFile);
	}
	return;
}

void CDumpAnalyze::UpdateProcess(PROCESS_TYPE e, int nParam)
{
	::PostMessage(m_hWnd, MSG_UPDATE_PROCESS,(WPARAM)e,(LPARAM)nParam);
}

BOOL CDumpAnalyze::ParseDumpUrls(const std::string &s)
{
	std::wstring dump_count = L"0";
	std::wstring ver;
#ifdef DEBUG
	dump_count = L"10";
#endif // DEBUG

	CCommandLine::getInstance().getOption(L"count", dump_count);
	CCommandLine::getInstance().getOption(L"ver", ver);
	int nCount = _ttoi(dump_count.c_str());

	Json::Value root;
	Json::Reader jsonRead;
	if (!jsonRead.parse(s, root))
		return FALSE;

	Json::Value jError = root.get("errno", NULL);
	if (jError.type() != Json::intValue || jError.asInt() != 0)
		return FALSE;

	Json::Value jArrData = root.get("data", NULL);
	if (jArrData.type() != Json::arrayValue)
		return FALSE;

	Json::ValueIterator itDumpInfo = jArrData.begin();
	while (itDumpInfo != jArrData.end())
	{
		Json::Value & dumpInfo = *itDumpInfo;
		if (!dumpInfo.isNull() && dumpInfo.isObject())
		{
			bool valid = true;
			Json::Value jVer = dumpInfo["ver"];
			std::string dump_ver = jVer.asCString();
			std::string ver_cmd = CW2A(ver.c_str());
			if (!ver.empty() && ver_cmd != dump_ver)
			{
				valid = false;
			}

			if (valid)
			{
				Json::Value jUrl = dumpInfo["dump_file_url"];

				PDUMP_INFO pInfo = new DUMP_INFO();
				pInfo->url = jUrl.asCString();
				pInfo->ver = dump_ver;
				m_lstDumpInfo.push_back(pInfo);
			}
		}
		itDumpInfo++;

		//debug
		if (m_lstDumpInfo.size() >= nCount && nCount > 0) break;
	}
	return TRUE;
}

unsigned int WINAPI CDumpAnalyze::WorkProc(LPVOID lpParameter)
{
	CDumpAnalyze* pDownload = (CDumpAnalyze*)lpParameter;
	if (pDownload)
	{
		pDownload->WorkImpl();
	}
	pDownload->m_hWorkThread = nullptr;
	return 0;
}

void CDumpAnalyze::WriteDllResultHtmls(WriteDllResultHtmlsCallback &vDllResultHtmlsCallback, 
									   const CStringA &strSqliteTbl) const
{
	if( !m_pSqliteDb )
		return;

	typedef list<pair<CStringA, DWORD> >   CStringADwordMapList;

	//查询模块概览：map<dll_name, dump_count>
	CStringADwordMapList  mapDllDumps;

	struct ExecDllDumpsCallback
	{
		static int Callback(void *para, int n_column, char **column_value, char **column_name)
		{
			CStringADwordMapList * pMapDllDumps = (CStringADwordMapList *)para;

			if(2 != n_column)
				return SQLITE_ERROR;

			if(column_value[0] && column_value[1])
			{
				pMapDllDumps->push_back(CStringADwordMapList::value_type(column_value[0], strtoul(column_value[1], NULL, 10)) );
			}
			return SQLITE_OK;
		}
	};

	CStringA strSql;
	strSql.Format(
		"select dll_name, count(*) as c from [%s] group by dll_name order by c desc",
		(LPCSTR)strSqliteTbl);

	int iRet = sqlite3_exec(m_pSqliteDb, strSql, 
		&ExecDllDumpsCallback::Callback, &mapDllDumps, NULL);

	DWORD dwTotalDumpCount = 0;
	CStringADwordMapList::const_iterator itrDll = mapDllDumps.begin();
	for (; mapDllDumps.end() != itrDll; ++itrDll)
	{
		dwTotalDumpCount += itrDll->second;
	}
	vDllResultHtmlsCallback.m_dwTotalDumpCount = dwTotalDumpCount;

	//输出
	DWORD dwIndex = 0;
	itrDll = mapDllDumps.begin();
	for (; mapDllDumps.end() != itrDll; ++itrDll, ++dwIndex)
	{
		vDllResultHtmlsCallback.m_strDll = itrDll->first;
		vDllResultHtmlsCallback.m_dwDllDumpCount = itrDll->second;

		//dll-unknown-result.html
		LPCSTR szDll = (itrDll->first.IsEmpty() ? "unknown" : (LPCSTR)itrDll->first);
		vDllResultHtmlsCallback.m_strDllHtml.Format("dll-%03u-%u-%s-%s", dwIndex, itrDll->second, szDll, "result.html");

		CStringA strHtmResult = g_strWorkingFolder.c_str();
		strHtmResult += "\\";
		strHtmResult += vDllResultHtmlsCallback.m_strDllHtml;

		WriteDllResultHtml_Dll(vDllResultHtmlsCallback, strSqliteTbl, strHtmResult, itrDll->first, itrDll->second, dwTotalDumpCount);
	}

	vDllResultHtmlsCallback.OnInfoCallback(TRUE);
}

void CDumpAnalyze::WriteDllResultHtml_Dll( WriteDllResultHtmlsCallback &vDllResultHtmlsCallback, 
										  const CStringA &strSqliteTbl, const CStringA &strHtmResult, const CStringA &strDll, DWORD dwDumpCount, DWORD dwTotalDumpCount) const
{
	typedef list<pair<CStringA, DWORD> >   CStringADwordMapList;

	//查询版本概览：map<ver, dump_count>
	CStringADwordMapList  mapVerDumps;

	struct ExecVerDumpsCallback
	{
		static int Callback(void *para, int n_column, char **column_value, char **column_name)
		{
			CStringADwordMapList * pMapVerDumps = (CStringADwordMapList *)para;

			if(2 != n_column)
				return SQLITE_ERROR;

			if(column_value[0] && column_value[1])
			{
				pMapVerDumps->push_back( CStringADwordMapList::value_type(column_value[0], strtoul(column_value[1], NULL, 10)) );
			}
			return SQLITE_OK;
		}
	};

	CStringA strSql;
	strSql.Format(
		"select ver, count(*) as c from [%s] where dll_name=\"%s\" group by ver order by c desc",
		(LPCSTR)strSqliteTbl, (LPCSTR)strDll);

	int iRet = sqlite3_exec(m_pSqliteDb, strSql, 
		&ExecVerDumpsCallback::Callback, &mapVerDumps, NULL);

	//输出
	CStringA strHtmlBuffer;
	LPCSTR szHeadGmt = "<!DOCTYPE html>\
		<html>\
		<head><meta charset = \"utf-8\"><title>%s dump auto analyze result</title></head>\
		<body>\
		<h2>Crash in %s, Analyzed %u dumps, total processed %u, rate equals %.2f%% </h2><br><br>";
	strHtmlBuffer.Format(szHeadGmt, 
		ToHtmlLPCSTR(PathFindFileNameA(strHtmResult)),
		ToHtmlLPCSTR(strDll.IsEmpty() ? "<unknown>": (LPCSTR)strDll),
		dwDumpCount, dwTotalDumpCount, 
		(dwTotalDumpCount ? (double)dwDumpCount*100.0/dwTotalDumpCount : 0.0) );

	vDllResultHtmlsCallback.m_dwVerCount = mapVerDumps.size();

	CStringA strHtmlVerInfo;
	DWORD dwIndex = 0;
	CStringADwordMapList::const_iterator itrVer = mapVerDumps.begin();
	for (; mapVerDumps.end() != itrVer; ++itrVer, ++dwIndex)
	{
		vDllResultHtmlsCallback.m_strVer = itrVer->first;
		vDllResultHtmlsCallback.m_dwVerDumpCount = itrVer->second;

		strHtmlVerInfo.Format("<h4>Top %u: Version = %s, Total count = %u</h4><br>",
			(dwIndex+1), (LPCSTR)itrVer->first, itrVer->second);
		strHtmlBuffer += strHtmlVerInfo;
		WriteDllResultHtml_Ver(vDllResultHtmlsCallback, strSqliteTbl, strHtmlBuffer, strDll, itrVer->first);
		strHtmlBuffer += "<br>";
	}

	strHtmlBuffer += "</body></html>";

	ofstream ofHtmResult(strHtmResult, ios_base::out);
	if(ofHtmResult)
		ofHtmResult << (LPCSTR)strHtmlBuffer << endl;
}

void CDumpAnalyze::WriteDllResultHtml_Ver( WriteDllResultHtmlsCallback &vDllResultHtmlsCallback, 
										  const CStringA &strSqliteTbl, CStringA &strHtmlBuffer, const CStringA &strDll, const CStringA &strVer ) const
{
	//查询crash概览：map<tag, dump_count>
	const size_t MAX_QUERY_TAG_COUNT  = 10;
	const size_t MAX_QUERY_DUMP_COUNT = 3;

	struct ExecTagDumpsParam
	{
		DWORD           dwTagCount;
		CStringA        strCallStack;
		set<CStringA>   setDumpPaths;
	};

	typedef list<pair<CStringA, ExecTagDumpsParam> >   CStringAParamMapList;
	CStringAParamMapList  mapTagDumps;

	struct ExecTagDumpsCallback
	{
		static int Callback(void *para, int n_column, char **column_value, char **column_name)
		{
			CStringAParamMapList * pMapTagDumps = (CStringAParamMapList *)para;

			if(3 != n_column)
				return SQLITE_ERROR;

			ExecTagDumpsParam vParam;
			vParam.dwTagCount = 0;

			if(column_value[0] && column_value[1])
			{
				vParam.dwTagCount = strtoul(column_value[1], NULL, 10);
			}

			if (vParam.dwTagCount)
			{
				pMapTagDumps->push_back(CStringAParamMapList::value_type(column_value[0], vParam));

				if (column_value[2])
				{
					set<CStringA> &setDumpPaths = pMapTagDumps->back().second.setDumpPaths;

					CStringA strDumpPaths(column_value[2]);
					LPSTR lpContext = NULL;
					LPSTR lp = strtok_s(strDumpPaths.GetBuffer(), "|", &lpContext);
					for (size_t ii = 0; lp; lp = strtok_s(NULL, "|", &lpContext), ++ii)
					{
						if (ii >= MAX_QUERY_DUMP_COUNT)
							break;

						setDumpPaths.insert(lp);
					}
				}
			}

			return SQLITE_OK;
		}
	};

	CStringA strSql;
	strSql.Format(
		"select tag, count(*) as c, group_concat(dump_path, \"|\") from [%s] where dll_name=\"%s\" and ver=\"%s\" group by tag order by c desc limit %u",
		(LPCSTR)strSqliteTbl, (LPCSTR)strDll, strVer, MAX_QUERY_TAG_COUNT);

	int iRet = sqlite3_exec(m_pSqliteDb, strSql, 
		&ExecTagDumpsCallback::Callback, &mapTagDumps, NULL);

	//整理
	for (CStringAParamMapList::iterator itr = mapTagDumps.begin();
		mapTagDumps.end() != itr; ++itr)
	{
		vDllResultHtmlsCallback.m_strTag = itr->first;
		vDllResultHtmlsCallback.m_dwTagDumpCount = itr->second.dwTagCount;

		PeekCallStackFromTag(itr->first, itr->second.strCallStack);
		CStringA strTemp;
		strTemp.Format("<pre>dump count = %u\n\n", itr->second.dwTagCount);
		strHtmlBuffer += strTemp;
		if( itr->second.strCallStack.IsEmpty() )
		{
			strHtmlBuffer += "Call stack tag = ";
			strHtmlBuffer += itr->first;
			strHtmlBuffer += " not found! \r\n";
		}
		else
		{
			strHtmlBuffer += ToHtmlStringA(itr->second.strCallStack);
		}
		strHtmlBuffer += "</pre>";

		for(set<CStringA>::const_iterator itrDump = itr->second.setDumpPaths.begin();
			itr->second.setDumpPaths.end() != itrDump; ++itrDump)
		{
			LPCSTR szFileName = (LPCSTR)( *itrDump );
			CStringA strFileNameLnk;
			if( strchr(szFileName, '/') )
			{
				do 
				{
					if( 't'!=szFileName[0] || '_'!=szFileName[1] )
						break;

					szFileName += 2;

					LPCSTR lp = strchr(szFileName, '_');
					if( !lp )
						break;

					CStringA strFrom(szFileName, (lp - szFileName));
					strFileNameLnk.Format("<a href=\"../../%s/%s\">%s/%s</a><br>",
						(LPCSTR)(strFrom), ToHtmlLPCSTR(lp + 1), 
						(LPCSTR)(strFrom), ToHtmlLPCSTR(lp + 1) );
				} while (0);

				if( strFileNameLnk.IsEmpty() )
				{
					strFileNameLnk = (LPCSTR)( *itrDump );
					strFileNameLnk += "<br>";
				}
			}
			else
			{
				strFileNameLnk.Format("<a href=\"./dump/%s\">%s</a><br>",
					ToHtmlLPCSTR(szFileName), ToHtmlLPCSTR(szFileName) );
			}
			strHtmlBuffer += strFileNameLnk;
		}

		strHtmlBuffer += "<br>";
	}

	vDllResultHtmlsCallback.OnInfoCallback(FALSE);
}

BOOL CDumpAnalyze::PeekCallStackFromTag( const CStringA &strTag, CStringA &strCallStack ) const
{
	if( !m_pSqliteDb )
		return FALSE;

	BOOL bRet = FALSE;
	CStringA strSql;
	strSql.Format("select call_stack from [t_tag_call_stack] where tag=\"%s\" limit 1", (LPCSTR)strTag);
	LPSTR *ppTableResult = NULL;
	int nCol = 0, nRow = 0;
	if((SQLITE_OK == sqlite3_get_table(m_pSqliteDb, strSql, &ppTableResult, &nRow, &nCol, NULL)) && ppTableResult && (1 == nCol) && (1 == nRow) )
	{
		if( ppTableResult[nCol] )
			strCallStack = ppTableResult[nCol];
		else
			strCallStack.Empty();
		bRet = TRUE;
	}

	if(ppTableResult)
		sqlite3_free_table(ppTableResult);
	return bRet;
}

BOOL CDumpAnalyze::DoDigestResultHtmls(const std::wstring &strDigest)
{
	std::list<CStringA> listLines = Util::STRING::spliterString(strDigest.c_str(),",");
	if( m_dateProcess.empty() || listLines.empty() || m_strSqliteTbl.IsEmpty() )
		return FALSE;

	CStringA strCondExt = "and tbl_name like \"t_%_";
	strCondExt += m_dateProcess.c_str();
	strCondExt += "_%\" order by tbl_name desc";

	DbFromTimestampMap  mapDbFromTimestamp;
	multimap<CStringA, DB_TABLE_INFO> mapDbTableListMap;
	EnumDbTableList(mapDbTableListMap, strCondExt);
	multimap<CStringA, DB_TABLE_INFO>::iterator itr = mapDbTableListMap.begin();
	for(; mapDbTableListMap.end() != itr; )
	{
		BOOL bFound = FALSE;
		std::list<CStringA>::const_iterator iter = listLines.begin();
		for (; iter != listLines.end(); ++iter)
		{
			if( 0 == itr->second.strFrom.CompareNoCase( *iter ))
			{
				bFound = TRUE;
				break;
			}
		}

		if( bFound )
		{
			if( mapDbFromTimestamp.insert( make_pair(itr->second.strFrom, itr->first) ).second )
				++itr;
			else
				itr = mapDbTableListMap.erase(itr);
		}
		else
		{
			itr = mapDbTableListMap.erase(itr);
		}
	}

	if( mapDbTableListMap.empty() )
		return FALSE;

	CStringA strSqliteTblTemp = m_strSqliteTbl;
	strSqliteTblTemp += "_temp";

	CStringA strSql;
	strSql.Format(
		"CREATE temp TABLE [%s]( dll_name varchar(260), ver varchar(32), tag varchar(260), dump_path varchar(260), reserved varchar(260) )",
		(LPCSTR)strSqliteTblTemp);
	int iRet = sqlite3_exec(m_pSqliteDb, strSql, NULL, NULL, NULL);
	if(SQLITE_OK != iRet)
		return FALSE;

	iRet = sqlite3_exec(m_pSqliteDb, "BEGIN TRANSACTION", NULL, NULL, NULL);

	itr = mapDbTableListMap.begin();
	for(; mapDbTableListMap.end() != itr; ++itr )
	{
		strSql.Format("insert into [%s] select dll_name, ver, tag, \"%s/dump/\"||dump_path, reserved from [%s]", 
			(LPCSTR)strSqliteTblTemp, (LPCSTR)itr->second.strTableName, (LPCSTR)itr->second.strTableName);
		iRet = sqlite3_exec(m_pSqliteDb, strSql, NULL, NULL, NULL);
	}

	iRet = sqlite3_exec(m_pSqliteDb, "COMMIT", NULL, NULL, NULL);

	WriteDllResultHtmlsDigestCallback vDllResultHtmlsCallback(m_pSqliteDb, m_strFromPart);
	vDllResultHtmlsCallback.m_mapDbFromTimestamp.swap( mapDbFromTimestamp );

	WriteDllResultHtmls(vDllResultHtmlsCallback, strSqliteTblTemp);
	return TRUE;
}

BOOL CDumpAnalyze::DoCleanDb()
{
	if( m_dateClean.IsEmpty() || !m_pSqliteDb )
		return TRUE;

	InitDbTableListMap();

	CStringA strSql;
	multimap<CStringA, DB_TABLE_INFO>::const_iterator itrUpper = m_mapDbTableListMap.upper_bound( m_dateClean );
	multimap<CStringA, DB_TABLE_INFO>::const_iterator itr = m_mapDbTableListMap.begin();
	for(; itrUpper != itr; ++itr)
	{
		strSql.Format("drop table [%s]", (LPCSTR)itr->second.strTableName);
		int iRet = sqlite3_exec(m_pSqliteDb, strSql, NULL, NULL, NULL);
		LOG << "Drop table [" << (LPCSTR)itr->second.strTableName << "] " << (SQLITE_OK==iRet ? "ok" : "fail") << "\n";
	}

	m_mapDbTableListMap.clear();
	return TRUE;
}

template<class MyDbInfoMultiMap>
void CDumpAnalyze::EnumDbTableList(MyDbInfoMultiMap &mapDbTableListMap, LPCSTR szCondExt) const
{
	if( !m_pSqliteDb )
		return;

	struct ExecInitDbTableListMapCallback
	{
		static int Callback(void *para, int n_column, char **column_value, char **column_name)
		{
			MyDbInfoMultiMap * pMapDbTableListMap = (MyDbInfoMultiMap *)para;

			if(1 != n_column)
				return SQLITE_ERROR;

			LPCSTR szTblName = column_value[0];
			if(szTblName 
				&& 
				stricmp(szTblName, "t_flag_dlls") 
				&& 
				stricmp(szTblName, "t_tag_call_stack")
				&&
				strnicmp(szTblName, OVERVIEW_TABLE, OVERVIEW_TABLE_SIZE))
			{
				LPCSTR lp1 = strchr(szTblName+2, '_');
				if( !lp1 )
					return SQLITE_OK;
				LPCSTR lp2 = strchr(lp1+1, '_');
				if( !lp2 )
					return SQLITE_OK;

				CStringA strTime( (lp1+1), (lp2 - (lp1+1)) );
				DB_TABLE_INFO tblInfo;
				tblInfo.strFrom.SetString( (szTblName+2), (lp1 - (szTblName+2)) );
				tblInfo.strTableName = szTblName;
				pMapDbTableListMap->insert(MyDbInfoMultiMap::value_type(
					strTime, tblInfo
				) );
			}
			return SQLITE_OK;
		}
	};

	CStringA strSql = "select tbl_name from sqlite_master where type=\"table\" ";
	if( szCondExt )
		strSql += szCondExt;
	int iRet = sqlite3_exec(m_pSqliteDb, strSql, 
		&ExecInitDbTableListMapCallback::Callback, &mapDbTableListMap, NULL);
}

void CDumpAnalyze::InitDbTableListMap()
{
	m_mapDbTableListMap.clear();
	EnumDbTableList(m_mapDbTableListMap, NULL);
}

// 获取模块版本信息，如果 strDll 为空，则获取主模块版本信息
BOOL CDumpAnalyze::PeekDllVersionFromDump( __inout CStringA &strDll, const CStringA &strPath, __out CStringA &strVer ) const
{
	HANDLE hFile = CreateFileA( strPath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
	if(INVALID_HANDLE_VALUE == hFile)
		return FALSE;

	BOOL bRet = FALSE;
	CAtlFileMapping<> fileMap;
	HRESULT hr = fileMap.MapFile(hFile);
	if(SUCCEEDED(hr))
	{
		LPBYTE lpFileData = (LPBYTE)fileMap.GetData();
		if( lpFileData && fileMap.GetMappingSize() )
		{
			do 
			{
				PMINIDUMP_DIRECTORY   pModDir = NULL;
				PMINIDUMP_MODULE_LIST pModList = NULL;
				ULONG ulModListSize = 0;
				if( !MiniDumpReadDumpStream (lpFileData, ModuleListStream, &pModDir, (PVOID *)&pModList, &ulModListSize) )
					break;

				if( !pModList 
					|| ulModListSize<sizeof(MINIDUMP_MODULE_LIST) 
					|| pModList->NumberOfModules>=0x10000
					|| ulModListSize<=sizeof(MINIDUMP_MODULE)*pModList->NumberOfModules )
					break;

				const CStringW strDllW = CA2W(strDll, CP_ACP);

				for(UINT ii=0; ii<pModList->NumberOfModules; ++ii)
				{
					RVA rvaName = pModList->Modules[ii].ModuleNameRva;
					if(0 == rvaName)
						continue;

					// 有些dump文件格式非法或者被损坏，需作严格的校验
					PMINIDUMP_STRING pNameString = (PMINIDUMP_STRING)(lpFileData + rvaName);
					if (IsBadReadPtr(pNameString, sizeof(*pNameString))
						||
						IsBadReadPtr(pNameString->Buffer, pNameString->Length))
					{
						strDll = "bad_dump";
						break;
					}

					CStringW strNameW( pNameString->Buffer, pNameString->Length/sizeof(WCHAR) );

					if( strDllW.IsEmpty() )
					{
						LPCWSTR szExt = PathFindExtensionW(strNameW);

						if( _wcsicmp(szExt, L".exe") )
							continue;
					}
					else
					{
						PathRemoveExtensionW( strNameW.GetBuffer() );
						strNameW.ReleaseBuffer();
						LPCWSTR szName = PathFindFileNameW(strNameW);

						if( _wcsicmp(szName, strDllW) )
							continue;
					}

					const VS_FIXEDFILEINFO * lpInfo = &pModList->Modules[ii].VersionInfo;
					if (VS_FFI_SIGNATURE != lpInfo->dwSignature)
						break;

					strVer.Format("%d.%d.%d.%d", 
						HIWORD(lpInfo->dwFileVersionMS), LOWORD(lpInfo->dwFileVersionMS),
						HIWORD(lpInfo->dwFileVersionLS), LOWORD(lpInfo->dwFileVersionLS));

					if( strDllW.IsEmpty() )
					{
						PathRemoveExtensionW( strNameW.GetBuffer() );
						strNameW.ReleaseBuffer();
						LPCWSTR szName = PathFindFileNameW(strNameW);

						strDll = CW2A(szName, CP_ACP);
					}
					bRet = TRUE;
					break;
				}
			} while (0);
		}
		fileMap.Unmap();
	}
	CloseHandle(hFile);
	return bRet;
}
