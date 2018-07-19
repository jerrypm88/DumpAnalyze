#pragma once



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
			// Í£Ö¹ÊÂ¼þ
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

