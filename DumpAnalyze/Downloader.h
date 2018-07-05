#pragma once

#include <curl/curl.h> 
#include <atlstr.h>  
#include <string>
#include <map>
#include <list>
#include <mutex>
#include <thread>
#include <atomic>

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


	typedef std::map<CStringA, list<CStringA> >  TAG_DUMP_RESULT;
	typedef std::map<CStringA, list<CStringA> >::iterator TAG_DUMP_RESULT_ITERATOR;

	typedef std::map<std::string, TAG_DUMP_RESULT> VER_TAG_RESULT;
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
	BOOL AnalyzeDump(const DUMP_INFO & dump_info, std::string& path);
	void InitDownloadFolder(std::string& strFloder, const wchar_t* strAppendix, std::wstring& from);
	void ArrangeDumpInfo(CStringA strTag, CStringA strCallStack, CStringA strPath, const DUMP_INFO & dump_info);
	void OutputResult(const std::string & folder);
	void WriteResultHtml(CStringA strPath, TAG_DUMP_RESULT & tag_result);
	void UpdateProcess(PROCESS_TYPE e, int nParam = 0);
private:
	HANDLE m_hWorkThread = nullptr;

	std::wstring date_;
	std::list<PDUMP_INFO> m_lstDumpInfo;
	std::mutex             m_lstDumpUrlsMutex;
	std::atomic_int        m_currentCount;

	int m_nFailCounts = 0;
	

	VER_TAG_RESULT ver_tag_result_;

	std::map<CStringA, list<CStringA>>		m_mapDumpResult;

	std::map<CStringA, CStringA>			m_mapCallStack;
	std::mutex                              m_resultMutex;
	HWND	m_hWnd = NULL;			
	
};