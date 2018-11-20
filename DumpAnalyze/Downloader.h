#pragma once

#include <curl/curl.h> 
#include <atlstr.h>  
#include <string>
#include <map>
#include <list>
#include <mutex>
#include <thread>
#include <atomic>
#include <set>
#include <map>
using namespace std;
#include <memory>
#include <vector>
#include <algorithm>
#include "lib/sqlite3/sqlite3.h"
#include "WriteDllResultHtmlsDigestCallback.h"





#define MSG_UPDATE_PROCESS	(WM_USER + 5555)

#pragma warning(disable : 4503)

enum PROCESS_TYPE
{
	PT_BEGIN,
	PT_GET_DUMP_INFO,
	PT_GET_DUMP_DONE, 
	PT_ANALYZING,
	PT_DONE,
};


enum DLL_FLAG
{
	DLL_FLAG_NONE,
	DLL_FLAG_KNOWN,
	DLL_FLAG_USER,
};


struct DB_TABLE_INFO
{
	CStringA    strFrom;
	CStringA    strTableName;
};


class CDumpAnalyze
{
public:
	CDumpAnalyze(void);
	~CDumpAnalyze(void);
	int StartWorkThread(HWND hWnd);
	void AnalyzeDumpThread();
	
	//dump url and ver
	typedef struct tagDUMP_INFO
	{
		std::string url;
		std::string ver;
	}DUMP_INFO, *PDUMP_INFO;

	typedef struct tagDUMP_RESULT
	{
		std::string ver;
		std::string callstack;
		std::string dump_file_path;
	}DUMP_RESULT, *PDUMP_RESULT;

	typedef list<CStringA>                        CStringAList;
	typedef std::map<CStringA, CStringAList >  TAG_DUMP_RESULT;                    // map[tag]list[tag,path]
	typedef std::map<CStringA, CStringAList >::iterator TAG_DUMP_RESULT_ITERATOR;

	typedef std::map<std::string, TAG_DUMP_RESULT> VER_TAG_RESULT;                 // map[ver][tag]list[tag,path]
	typedef VER_TAG_RESULT::iterator VER_TAG_RESULT_ITERATOR;

protected:
	static unsigned int WINAPI WorkProc(LPVOID lpParameter);								//线程函数  
	static size_t WriteFunc(char *str, size_t size, size_t nmemb, void *stream);					//写入数据（回调函数）  
	static size_t ProgressFunc(double* fileLen, double t, double d, double ultotal, double ulnow);  //下载进度 

private:
	void Reset();
	void WorkImpl();
	BOOL ParseDumpUrls(std::string s);
	std::string DumploadAndUnzipDump(const DUMP_INFO & dump_info,std::string& path,std::string& folder);
	BOOL AnalyzeDump(const DUMP_INFO & dump_info, const std::string& path);
	void InitWrokingFolder(BOOL bIsDigest, std::string& strFloder, const wchar_t* strAppendix, const std::wstring& from);
	void ArrangeDumpInfo(__inout CStringA &strDll, const CStringA &strTag, const CStringA &strCallStack, const CStringA &strPath, const DUMP_INFO & dump_info);
	void OutputResult() const;
	void UpdateProcess(PROCESS_TYPE e, int nParam = 0);
	void PeekDllFromPureStack(LPCSTR szPureStack, CStringA &strDll);
	BOOL PeekDllFromPureStackInternal(CStringA &strDll);  //返回标志：是否需要继续搜索
	BOOL PeekCallStackFromTag(const CStringA &strTag, CStringA &strCallStack) const;
	void WriteResultHtml(
		const CStringA &strPath, 
		const TAG_DUMP_RESULT & tag_result) const;
	void WriteDllResultHtmls(WriteDllResultHtmlsCallback &vDllResultHtmlsCallback, 
		const CStringA &strSqliteTbl) const;
	void WriteDllResultHtml_Dll(WriteDllResultHtmlsCallback &vDllResultHtmlsCallback, 
		const CStringA &strSqliteTbl, const CStringA &strHtmResult, const CStringA &strDll, DWORD dwDumpCount, DWORD dwTotalDumpCount) const;
	void WriteDllResultHtml_Ver(WriteDllResultHtmlsCallback &vDllResultHtmlsCallback, 
		const CStringA &strSqliteTbl, CStringA &strHtmlBuffer, const CStringA &strDll, const CStringA &strVer) const;
	BOOL DoDigestResultHtmls(const std::wstring &strDigest);
	BOOL DoCleanDb();
	BOOL PeekDllVersionFromDump(__inout CStringA &strDll, const CStringA &strPath, __out CStringA &strVer) const;

private:
	HANDLE m_hWorkThread = nullptr;

	std::wstring           m_dateProcess;
	CStringA               m_dateClean;
	std::list<PDUMP_INFO>  m_lstDumpInfo;
	std::mutex             m_lstDumpUrlsMutex;
	std::atomic_int        m_currentCount;
	std::atomic_int        m_nFailCounts;
	HWND	               m_hWnd = NULL;

private:
	mutable std::mutex                    m_resultMutex;
	VER_TAG_RESULT                        m_mapVerTagResult;    //map[ver][tag][tag & dump]
	std::map<CStringA, CStringAList>	  m_mapDumpResult;      //map[tag][tag & dump]
	mutable std::map<CStringA, CStringA>  m_mapCallStack;       //map[tag][stack]
	
private:
	mutable std::mutex                    m_lockFlagDlls;
	map<CStringA, DLL_FLAG>               m_mapFlagDlls;
	sqlite3 *                             m_pSqliteDb;
	CStringW                              m_strSqliteDb;
	CStringA                              m_strSqliteTbl;
	multimap<CStringA, DB_TABLE_INFO>     m_mapDbTableListMap;

protected:
	void     InitDbAndFlagDlls(BOOL bCreateNewTable);
	void     InitDbTableListMap();

	template<class MyDbInfoMultiMap>
	void     EnumDbTableList(MyDbInfoMultiMap &mapDbTableListMap, LPCSTR szCondExt) const;

	DLL_FLAG GetDllFlag(const CStringA &strDll) const;
	bool     SetDllFlag(const CStringA &strDll, DLL_FLAG dllFlag);
};
