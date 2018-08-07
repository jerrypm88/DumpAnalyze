// MainDlg.cpp : implementation of the CMainDlg class
//
/////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "resource.h"

#include "aboutdlg.h"
#include "MainDlg.h"

BOOL CMainDlg::PreTranslateMessage(MSG* pMsg)
{
	return CWindow::IsDialogMessage(pMsg);
}

BOOL CMainDlg::OnIdle()
{
	UIUpdateChildWindows();
	return FALSE;
}

LRESULT CMainDlg::OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
	// center the dialog on the screen
	CenterWindow();

	// set icons
	HICON hIcon = AtlLoadIconImage(IDR_MAINFRAME, LR_DEFAULTCOLOR, ::GetSystemMetrics(SM_CXICON), ::GetSystemMetrics(SM_CYICON));
	SetIcon(hIcon, TRUE);
	HICON hIconSmall = AtlLoadIconImage(IDR_MAINFRAME, LR_DEFAULTCOLOR, ::GetSystemMetrics(SM_CXSMICON), ::GetSystemMetrics(SM_CYSMICON));
	SetIcon(hIconSmall, FALSE);

	// register object for message filtering and idle updates
	CMessageLoop* pLoop = _Module.GetMessageLoop();
	ATLASSERT(pLoop != NULL);
	pLoop->AddMessageFilter(this);
	pLoop->AddIdleHandler(this);

	UIAddChildWindowContainer(m_hWnd);

	m_infoList = (CListBox)GetDlgItem(IDC_LIST_ACITON);

	CCommandLine::getInstance().parse(GetCommandLine());

	const BOOL bHasUrl = 
		(CCommandLine::getInstance().hasOption(L"from") &&
		CCommandLine::getInstance().hasOption(L"url"));
	if(bHasUrl || CCommandLine::getInstance().hasOption(L"digest"))
	{
		Start();
	}

	return TRUE;
}

LRESULT CMainDlg::OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
	// unregister message filtering and idle updates
	CMessageLoop* pLoop = _Module.GetMessageLoop();
	ATLASSERT(pLoop != NULL);
	pLoop->RemoveMessageFilter(this);
	pLoop->RemoveIdleHandler(this);

	return 0;
}

LRESULT CMainDlg::OnUpdateProcess(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& /*bHandled*/)
{
	PROCESS_TYPE type = (PROCESS_TYPE)wParam;
	int param = (int)lParam;
	UpdateProcessImpl(type, param);
	return S_OK;
}

LRESULT CMainDlg::OnOK(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
{
	// TODO: Add validation code 
	//CloseDialog(wID);
	Start();

	return 0;
}

void CMainDlg::Start()
{
	m_downloader.StartWorkThread(m_hWnd);
}

LRESULT CMainDlg::OnCancel(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
{
	CloseDialog(wID);
	return 0;
}

void CMainDlg::CloseDialog(int nVal)
{
	DestroyWindow();
	::PostQuitMessage(nVal);
}

void CMainDlg::UpdateProcessImpl(PROCESS_TYPE type, int nPrama)
{
	CString strOutput;
	switch (type)
	{
	case PT_BEGIN:
		strOutput = _T("开始分析dump...");
		break;
	case PT_GET_DUMP_INFO:
		strOutput = _T("开始获取dump下载地址...");
		break;
	case PT_GET_DUMP_DONE:
		if (nPrama == 0)
		{
			strOutput = _T("获取dump下载地址失败");
		}
		else
		{
			m_nTotalCounts = nPrama;
			strOutput.Format(_T("获取dump下载地址成功，共有%d个dump"), nPrama);
		}
		break;
	case PT_ANALYZING:
		strOutput.Format(_T("%d/%d 分析中"), nPrama, m_nTotalCounts);
		break;
	case PT_DONE:
		strOutput.Format(_T("dump分析结束"), nPrama);
		break;
	default:
		break;
	}
	if (m_infoList)
	{
		m_infoList.InsertString(m_infoList.GetCount(),strOutput);
	}

	if (type == PT_DONE && CCommandLine::getInstance().hasOption(L"from"))
	{
		PostMessage(WM_CLOSE);
	}
}
