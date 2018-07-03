#include "stdafx.h"
#include "Downloader.h"
#include <io.h>  
#include <curl/curl.h>
#include <curl/easy.h>
#include <wininet.h>
#include <strsafe.h>
#include <atltime.h>
#ifdef _DEBUG
	#pragma comment(lib,"libcurld.lib")
#else
	#pragma comment(lib,"libcurl.lib")
#endif // _DEBUG

#pragma comment ( lib, "ws2_32.lib" )
#pragma comment ( lib, "winmm.lib" )
#pragma comment ( lib, "wldap32.lib" )

#include <atlcomcli.h>
#include <urlmon.h>
#include "lib/json/lib_json/json/value.h"
#include "lib/json/lib_json/json/reader.h"
#include <corecrt_wstdio.h>
#include "Utils.h"
#include <algorithm>
#pragma comment(lib, "UrlMon.lib")

#define DAY_FORMAT					"%d-%d-%d"
#define POST_PARAM					L"src=%s&date=%s"
#define FILE_NAME					"%d-%d-%d.zip"
#define DUMP_URL_TAG				"dump_file_url"
#define DUMP_FOLDER_APPENDIX		"dump\\"
#define DUMP_ANALYZE_CMD			"-z \"%s\" -c \".ecxr;k;q\""//!sym noisy;
#define DUMP_ANALYZE_EXE_NAME		"cdb.exe"
#define DUMP_ANALYZE_EXE_PATH		L"D:\\softbag\\windbg\\cdb.exe"
#define DUMP_INFO_GAP				"***"
#define RESULT_FILE_NAME			"00000000000000000000000_result.txt"
#define MAX_SHOW_RANKS				50
#define MAX_SHOW_LOCAL_PATH_COUNTS	5

CDumpAnalyze::CDumpAnalyze(void)
{
	curl_global_init(CURL_GLOBAL_ALL);
}

CDumpAnalyze::~CDumpAnalyze(void)
{
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

class CUrlMonAdapter :
	public IBindStatusCallback,
	public IHttpNegotiate,
	public IAuthenticate
{
public:
	CUrlMonAdapter() : m_cRef(0), m_bEnd(FALSE), m_dwRetCode(200), m_hWaitEvent(NULL) {}
	virtual ~CUrlMonAdapter()
	{
		if (m_hWaitEvent)
		{
			CloseHandle(m_hWaitEvent);
			m_hWaitEvent = NULL;
		}
	}
	STDMETHOD_(ULONG, AddRef)() { return m_cRef++; }
	STDMETHOD_(ULONG, Release)() { if (--m_cRef == 0) { delete this; return 0; } return m_cRef; }
	STDMETHODIMP QueryInterface(REFIID riid, void** ppv)
	{
		if (riid == IID_IUnknown || riid == IID_IBindStatusCallback)
		{
			*ppv = (IBindStatusCallback*)this;
			AddRef();
			return S_OK;
		}
		else if (riid == IID_IHttpNegotiate)
		{
			*ppv = (IHttpNegotiate*)this;
			AddRef();
			return S_OK;
		}
		else if (riid == IID_IAuthenticate)
		{
			*ppv = (IAuthenticate*)this;
			AddRef();
			return S_OK;
		}
		else
		{
			*ppv = NULL;
			return E_NOINTERFACE;
		}
	}
	STDMETHODIMP BeginningTransaction(
		LPCWSTR szURL,
		LPCWSTR szHeaders,
		DWORD dwReserved,
		LPWSTR *pszAdditionalHeaders)
	{
		if (!pszAdditionalHeaders)
			return E_POINTER;
		*pszAdditionalHeaders = NULL;

		if (BINDVERB_POST == m_dwAction && m_hDataToPost)
		{
			LPCWSTR c_wszHeaders = L"Content-Type: application/x-www-form-urlencoded\r\n";
			int len = (wcslen(c_wszHeaders) + 1) * sizeof(WCHAR);
			LPWSTR wszAdditionalHeaders = (LPWSTR)CoTaskMemAlloc(len);
			if (!wszAdditionalHeaders)
			{
				return E_OUTOFMEMORY;
			}
			StringCbCopy(wszAdditionalHeaders, len, c_wszHeaders);
			*pszAdditionalHeaders = wszAdditionalHeaders;
		}
		return S_OK;
	}
	STDMETHODIMP OnResponse(
		DWORD dwResponseCode,
		LPCWSTR szResponseHeaders,
		LPCWSTR szRequestHeaders,
		PWSTR *pszAdditionalRequestHeaders)
	{
		m_dwRetCode = dwResponseCode;
		if (!pszAdditionalRequestHeaders)
			return E_POINTER;
		*pszAdditionalRequestHeaders = NULL;
		return S_OK;
	}
	STDMETHODIMP OnStartBinding(DWORD dwReserved, IBinding *pib)
	{
		return S_OK;
	}
	STDMETHODIMP GetPriority(LONG *pnPriority)
	{
		HRESULT hr = S_OK;
		if (pnPriority)
			*pnPriority = THREAD_PRIORITY_NORMAL;
		else
			hr = E_INVALIDARG;
		return hr;
	}
	STDMETHODIMP OnLowResource(DWORD reserved)
	{
		return S_OK;
	}
	STDMETHODIMP OnProgress(ULONG ulProgress, ULONG ulProgressMax, ULONG ulStatusCode, LPCWSTR szStatusText)
	{
		return S_OK;
	}
	STDMETHODIMP OnStopBinding(HRESULT hresult, LPCWSTR szError)
	{
		m_bEnd = TRUE;
		m_hFinalResult = hresult;
		if (m_hWaitEvent)
			SetEvent(m_hWaitEvent);

		m_spStream.Release();
		return S_OK;
	}
	STDMETHODIMP GetBindInfo(DWORD *grfBINDF, BINDINFO *pbindinfo)
	{
		if (pbindinfo == NULL || pbindinfo->cbSize == 0 || grfBINDF == NULL)
			return E_INVALIDARG;

		*grfBINDF = BINDF_ASYNCHRONOUS | BINDF_ASYNCSTORAGE | BINDF_PULLDATA | BINDF_FWD_BACK;

		ULONG cbSize = pbindinfo->cbSize;
		memset(pbindinfo, 0, cbSize);
		pbindinfo->cbSize = cbSize;
		pbindinfo->dwBindVerb = m_dwAction;

		if (m_dwAction == BINDVERB_POST)
		{
			pbindinfo->stgmedData.tymed = TYMED_HGLOBAL;
			pbindinfo->stgmedData.hGlobal = m_hDataToPost;
			pbindinfo->stgmedData.pUnkForRelease = (LPUNKNOWN)(LPBINDSTATUSCALLBACK)this;
			pbindinfo->cbstgmedData = m_cbDataToPost;
			AddRef();
		}
		return S_OK;
	}
	STDMETHODIMP OnObjectAvailable(REFIID riid, IUnknown *punk)
	{
		return S_OK;
	}
	BOOL StartDownload(LPCWSTR url, PBYTE pPostData, DWORD dwPostLen)
	{

		if (pPostData && dwPostLen)
		{
			m_cbDataToPost = dwPostLen;
			m_hDataToPost = GlobalAlloc(GPTR, m_cbDataToPost);
			if (!m_hDataToPost)
				return FALSE;
			memcpy(m_hDataToPost, pPostData, m_cbDataToPost);
			m_dwAction = BINDVERB_POST;
		}
		else
			m_dwAction = BINDVERB_GET;

		CComPtr<IBindCtx> spBindCtx;
		CComPtr<IMoniker> spMoniker;
		CComPtr<IStream> spStream;

		HRESULT hr = CreateURLMoniker(NULL, url, &spMoniker);
		if (SUCCEEDED(hr))
			hr = CreateBindCtx(0, &spBindCtx);
		if (SUCCEEDED(hr))
			hr = RegisterBindStatusCallback(spBindCtx, static_cast<IBindStatusCallback*>(this), NULL, 0);
		if (SUCCEEDED(hr))
			hr = spMoniker->BindToStorage(spBindCtx, 0, __uuidof(IStream), (void**)&spStream);
		return SUCCEEDED(hr);
	}

	BOOL Download(LPCWSTR url, PBYTE pPostData, DWORD dwPostLen, DWORD dwTimeout)
	{
		if (!m_hWaitEvent && !(m_hWaitEvent = ::CreateEvent(NULL, TRUE, FALSE, NULL)))
			return FALSE;

		::ResetEvent(m_hWaitEvent);
		if (!StartDownload(url, pPostData, dwPostLen))
			return FALSE;

		if (m_bEnd)
			return TRUE;

		BOOL bWaitOk = FALSE;
		DWORD dwRet = 0;
		while (!bWaitOk && (dwRet = MsgWaitForMultipleObjects(1, &m_hWaitEvent, FALSE, dwTimeout, QS_POSTMESSAGE | QS_SENDMESSAGE)) != WAIT_TIMEOUT)
		{
			// 停止事件
			if (dwRet == WAIT_OBJECT_0)
				break;

			MSG msg;
			while (!PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
				continue;

			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}

		return SUCCEEDED(m_hFinalResult);
	}
	DWORD GetRetCode()
	{
		return m_dwRetCode;
	}
	STDMETHODIMP Authenticate(
		HWND *phwnd,
		LPWSTR *pszUsername,
		LPWSTR *pszPassword)
	{
		return S_OK;
	}
protected:
	BOOL m_bEnd;
	HRESULT m_hFinalResult;
	DWORD m_cRef;
	BINDVERB m_dwAction;
	HGLOBAL m_hDataToPost;
	DWORD m_cbDataToPost;
	DWORD m_dwRetCode;
	HANDLE m_hWaitEvent;
protected:
	CComPtr<IStream> m_spStream;
};

class CUrlMonToBufAdapter : public CUrlMonAdapter
{
public:
	STDMETHODIMP OnDataAvailable(DWORD grfBSCF, DWORD dwSize, FORMATETC* pformatetc, STGMEDIUM* pstgmed)
	{
		HRESULT hr = S_OK;
		if (BSCF_FIRSTDATANOTIFICATION & grfBSCF)
		{
			if (!m_spStream && pstgmed->tymed == TYMED_ISTREAM)
				m_spStream = pstgmed->pstm;
		}
		if (m_spStream)
		{
			DWORD dwTotalRead = m_strBuf.GetLength();
			do
			{
				int iRead = dwSize - dwTotalRead;
				if (iRead < 0x1000)
					iRead = 0x1000;
				else if (iRead > 0xA00000)
					iRead = 0xA00000;
				BYTE *pBuffer = (BYTE *)m_strBuf.GetBuffer(dwTotalRead + iRead) + dwTotalRead;
				DWORD dwActuallyRead = 0;
				hr = m_spStream->Read(pBuffer, iRead, &dwActuallyRead);
				dwTotalRead += dwActuallyRead;
				m_strBuf.ReleaseBufferSetLength(dwTotalRead);
			} while (!(hr == E_PENDING || hr == S_FALSE) && SUCCEEDED(hr));
		}
		return hr;
	}
public:
	CStringA m_strBuf;
};

void CDumpAnalyze::Reset()
{
	m_hWorkThread = nullptr;
	m_nFailCounts = 0;
	m_mapDumpInfo.clear();
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
	std::string url;

	while (true)
	{
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
				url = pInfo->url;
				delete pInfo;
			}
		}

		m_currentCount++;

		UpdateProcess(PT_ANALYZING, m_currentCount);
		std::string name = PathFindFileNameA(url.c_str());
		std::string path = g_szWorkingFolder;
		path += "dump\\";

		if (!PathFileExistsA(path.c_str()))
		{
			CreateDirectoryA(path.c_str(), NULL);
		}

		path += name;
		std::string dumpPath = DumploadAndUnzipDump(url, path, g_szWorkingFolder + "dump\\");
		if (dumpPath.length() > 0 && PathFileExistsA(dumpPath.c_str()))
		{
			AnalyzeDump(dumpPath);
		}
	}

}

void CDumpAnalyze::WorkImpl()
{
	UpdateProcess(PT_BEGIN);

	std::wstring from;
	if (!CCommandLine::getInstance().getOption(L"from", from))
	{
		from = L"ldstray";
	}
	
	if (!CCommandLine::getInstance().getOption(L"date", date_))
	{
		try
		{
			CTime tcurrent(CTime::GetCurrentTime());
			CTimeSpan span(1,0,0,0);

			CTime yesterday = tcurrent - span;
			std::wostringstream oss;
			oss << yesterday.GetYear() << L"-" << yesterday.GetMonth() << L"-" << yesterday.GetDay();
			date_ = oss.str();
		}
		catch (...)
		{
		}
	}

	CString strPost;
	strPost.Format(POST_PARAM, from.c_str(), date_.c_str());
	
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

	m_currentCount = 0;
	if (!m_lstDumpInfo.empty())
	{
		UpdateProcess(PT_GET_DUMP_DONE, m_lstDumpInfo.size());
		
		std::string foler;
		InitDownloadFolder(foler, date_.c_str(), from);
		g_szWorkingFolder = foler;

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

// 		CStringA strDumpResultPath;
// 		strDumpResultPath.Format("%s\\%s", foler.c_str(), RESULT_FILE_NAME);
// 		WriteResult(strDumpResultPath);

		CStringA strHtmResult;
		strHtmResult.Format("%s\\%s", foler.c_str(), "result.html");
		WriteResultHtml(strHtmResult);
		UpdateProcess(PT_DONE, 0);
	}
	else
	{
		UpdateProcess(PT_GET_DUMP_DONE,0);
	}
	return;
}

void CDumpAnalyze::InitDownloadFolder(std::string& strFloder, const wchar_t* strAppendix, std::wstring& from)
{
	wchar_t szFolder[MAX_PATH] = { 0 };
	GetModuleFileName(NULL, szFolder, MAX_PATH);
	PathRemoveFileSpec(szFolder);
	PathAppend(szFolder, from.c_str());

	PathAddBackslash(szFolder);

	CString strFolder;

	SYSTEMTIME time;
	GetLocalTime(&time);
	
	strFolder.Format(L"%s%04d-%02d-%02d %02d-%02d-%02d", szFolder, time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond);
	
	strFolder += L"\\";
	SHCreateDirectory(NULL, strFolder);
	strFloder = (CW2A)strFolder;
}

std::string CDumpAnalyze::DumploadAndUnzipDump(std::string& url, std::string& path,std::string& folder)
{
	LOG << "Begin down:" << path.c_str();
	CURL *pCurl = NULL;
	FILE* pFile = fopen(path.c_str(), "wb");
	pCurl = curl_easy_init();
	if (pCurl != NULL && pFile != NULL)
	{
		curl_easy_setopt(pCurl, CURLOPT_WRITEDATA, (void*)pFile);
		curl_easy_setopt(pCurl, CURLOPT_WRITEFUNCTION, WriteFunc);
		curl_easy_setopt(pCurl, CURLOPT_URL, url.c_str());
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

BOOL CDumpAnalyze::AnalyzeDump(std::string& path)
{
	std::wstring cdb_path = DUMP_ANALYZE_EXE_PATH;
	CCommandLine::getInstance().getOption(L"cdb", cdb_path);
	std::string cdb_path_a = CW2A(cdb_path.c_str());

	CStringA strCmdLine;
	strCmdLine.Format(DUMP_ANALYZE_CMD, path.c_str());

	std::wstring symbol_path;
	CCommandLine::getInstance().getOption(L"symbol", symbol_path);
	if (!symbol_path.empty())
	{
		CStringA szSymbolCmd;
		szSymbolCmd.Format(" -y \"%s\"", CW2A(symbol_path.c_str()));
		strCmdLine += szSymbolCmd;
	}

	CStringA strRet = Util::Process::CreateProcessForOutput(cdb_path_a.c_str(), strCmdLine);

	LOG << "----------------" << m_currentCount << "----------------" << "\r\n" << strRet;

	int nStartPos = -1;
	if ( strRet.Find("ChildEBP RetAddr") == -1 || (nStartPos = strRet.Find(DUMP_INFO_GAP)) == -1 )		//"*"
		return FALSE;

	int nQuitPos = strRet.Find("quit");
	strRet = strRet.Left(nQuitPos);

	CStringA strCallStack = strRet;
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
	strCallStack.Trim();

	std::list<CStringA> listLines = Util::STRING::spliterString(strCallStack,"\n");
	std::list<CStringA>::iterator iter = listLines.begin();
	while (iter != listLines.end())
	{
		CStringA strLine = *iter;
		if (strLine.Left(9).CompareNoCase("WARNING: ") == 0 || strLine.Left(3).CompareNoCase("***") == 0 || strLine.Left(9).CompareNoCase("quit") == 0)
		{
			iter = listLines.erase(iter);
			continue;
		}
		else 
		{
			iter++;
		}
	}

	CStringA strTag;
	std::list<CStringA>::iterator iter2 = listLines.begin();
	while (iter2 != listLines.end())
	{
		CStringA strLine = *iter2;
		strTag += strLine.Mid(18);
		strTag += "\r\n";
		iter2++;
	}
	//CStringA strTag = strCallStack.Left(18);
	//if (strTag.Left(1).CompareNoCase("0") != 0)
	//{
	//	ATLASSERT(FALSE);
	//}
	//else
	//{
	//	strTag = strCallStack;
	//}
	ArrangeDumpInfo(strTag, strCallStack, path.c_str());
	return TRUE;
}

void CDumpAnalyze::ArrangeDumpInfo(CStringA strTag, CStringA strCallStack, CStringA strPath)
{
	std::lock_guard<std::mutex> lock(m_resultMutex);
	//第一个写tag，方便分类
	if (m_mapDumpInfo.find(strTag) == m_mapDumpInfo.end())
		m_mapDumpInfo[strTag].push_back(strTag);
	m_mapDumpInfo[strTag].push_back(strPath);
	if (m_mapCallStack.find(strTag) == m_mapCallStack.end())
		m_mapCallStack[strTag] = strCallStack;
}

bool SortByCount(list<CStringA>* pLeft, list<CStringA>* pRight)
{
	return pLeft->size() > pRight->size();
}

void CDumpAnalyze::WriteResult(CStringA strPath)
{
	int nDumpCounts = 0;
	std::list<list<CStringA>*> lstToSort;
	std::map<CStringA, list<CStringA>>::iterator iter = m_mapDumpInfo.begin();
	while (iter != m_mapDumpInfo.end())
	{
		lstToSort.push_back(&(iter->second));
		nDumpCounts += (iter->second.size() - 1);
		iter++;
	}
	lstToSort.sort(SortByCount);
	FILE* pFile = fopen(strPath, "wb");
	if (pFile != NULL)
	{
		//1.counts
		CStringA strCounts;
		strCounts.Format("Analyzed  %d  dump...\r\n\r\n", nDumpCounts);
		fwrite(strCounts, sizeof(char), strCounts.GetLength(), pFile);
		//2 top20
		int i = 0;
		std::list<list<CStringA>*>::iterator iter = lstToSort.begin();
		while (iter != lstToSort.end() && i < MAX_SHOW_RANKS)
		{
			list<CStringA>* pList = *iter;

			CStringA strDesc;
			strDesc.Format("Top %d: total counts = %d\r\n\r\n", i + 1, pList->size() - 1);
			fwrite(strDesc, sizeof(char), strDesc.GetLength(), pFile);
			if (pList)
			{
				list<CStringA>::iterator iterDumpInfo = pList->begin();
				CStringA strTag;
				CStringA strCallStack;
				int nIndex = 0;
				while (iterDumpInfo != pList->end())
				{
					if (nIndex == 0)
					{ 
						strTag = *iterDumpInfo;
						strCallStack = m_mapCallStack[strTag];
						fwrite(strCallStack, sizeof(char), strCallStack.GetLength(), pFile);
					}
					else
					{
						if (nIndex > MAX_SHOW_LOCAL_PATH_COUNTS)
							break;
						CStringA strLocalPath = *iterDumpInfo;
						fwrite(strLocalPath, sizeof(char), strLocalPath.GetLength(), pFile);
					}
					fwrite("\r\n", sizeof(char), 1, pFile);
					iterDumpInfo++;
					nIndex++;
				}
			}
			fwrite("\r\n\r\n", sizeof(char), 2, pFile);
			iter++;
			i++;
		}
		fclose(pFile);
	}
	return;
}

#define HTML_HEAD "<!DOCTYPE html>\
<html>\
<head>\
<meta charset = \"utf-8\">\
<title>%s</title>\
</head>"

#define BODY_HEAD_END "</body></html>"

void CDumpAnalyze::WriteResultHtml(CStringA strPath)
{
	int nDumpCounts = 0;
	std::list<list<CStringA>*> lstToSort;
	std::map<CStringA, list<CStringA>>::iterator iter = m_mapDumpInfo.begin();
	while (iter != m_mapDumpInfo.end())
	{
		lstToSort.push_back(&(iter->second));
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
		szFrom += date_;
		szFrom += L" dump auto analyze result";

		CStringA szHead;
		szHead.Format(HTML_HEAD, CW2A(szFrom.c_str()));
		fwrite(szHead, sizeof(char), szHead.GetLength(), pFile);

		//1.counts
		CStringA strCounts;
		strCounts.Format("<body><h2>Analyzed  %d  dump<h2>", nDumpCounts);
		fwrite(strCounts, sizeof(char), strCounts.GetLength(), pFile);
		//2 top20
		int i = 0;
		std::list<list<CStringA>*>::iterator iter = lstToSort.begin();
		while (iter != lstToSort.end() && i < MAX_SHOW_RANKS)
		{
			list<CStringA>* pList = *iter;

			CStringA strDesc;
			strDesc.Format("<h4>Top %d: total counts = %d<h4>", i + 1, pList->size() - 1);
			fwrite(strDesc, sizeof(char), strDesc.GetLength(), pFile);
			if (pList)
			{
				list<CStringA>::iterator iterDumpInfo = pList->begin();
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

BOOL CDumpAnalyze::ParseDumpUrls(std::string s)
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

