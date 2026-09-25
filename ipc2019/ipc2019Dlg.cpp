
// ipc2019Dlg.cpp: 구현 파일
//

#include "pch.h"
#include <shellapi.h>  // [assignment4] 수신 파일이 저장된 폴더를 탐색기로 연다.
#include "framework.h"
#include "ipc2019.h"
#include "ipc2019Dlg.h"
#include "afxdialogex.h"
#include <afxdlgs.h> // [assignment4] 파일 선택 창(CFileDialog) 선언

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif


// 응용 프로그램 정보에 사용되는 CAboutDlg 대화 상자입니다.

class CAboutDlg : public CDialogEx
{
public:
	CAboutDlg();

// 대화 상자 데이터입니다.
#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_ABOUTBOX };
#endif

	protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV 지원입니다.

// 구현입니다.
protected:
	DECLARE_MESSAGE_MAP()
};

CAboutDlg::CAboutDlg() : CDialogEx(IDD_ABOUTBOX)
{
}

void CAboutDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialogEx::DoDataExchange(pDX);
}

BEGIN_MESSAGE_MAP(CAboutDlg, CDialogEx)
END_MESSAGE_MAP()


// Cipc2019Dlg 대화 상자



Cipc2019Dlg::Cipc2019Dlg(CWnd* pParent /*=nullptr*/)
	: CDialogEx(IDD_IPC2019_DIALOG, pParent)
	, CBaseLayer("ChatDlg")
	, m_bSendReady(FALSE)
	, m_nAckReady( -1 )

	, m_unSrcAddr(0)
	, m_unDstAddr(0)
	, m_stMessage(_T(""))
{
	//대화상자 멤버 변수 초기화
	//  m_unDstAddr = 0;
	//  unSrcAddr = 0;
	//  m_stMessage = _T("");
	//대화 상자 멤버 초기화 완료

	m_hIcon = AfxGetApp()->LoadIcon(IDR_MAINFRAME);

	//Protocol Layer Setting
	m_LayerMgr.AddLayer(new CChatAppLayer("ChatApp"));
	m_LayerMgr.AddLayer(new CEthernetLayer("Ethernet"));
	#if USE_NPCAP_STACK
	// [assignment4] NI가 실제 전송을 맡고 FileApp이 파일 내용을 분할한다. 기존 FileLayer와는 역할이 다르다.
	m_LayerMgr.AddLayer(new CNILayer("NI"));
	m_LayerMgr.AddLayer(new CFileAppLayer("FileApp"));
#else
	m_LayerMgr.AddLayer(new CFileLayer("File"));
#endif
	m_LayerMgr.AddLayer(this, FALSE); // [assignment4] InitInstance에서 만든 Dialog는 LayerManager가 삭제하지 않도록 소유권을 제외한다.

	// 레이어를 연결한다. (레이어 생성)
#if USE_NPCAP_STACK
	// [assignment4] Dialog의 단일 Under 포인터는 ChatApp에 둔다. 파일 송신은 m_FileApp으로 호출하므로
	// [assignment4] FileApp -> Dialog 연결에는 '+'를 써서 기존 Under 포인터를 덮어쓰지 않는다.
	m_LayerMgr.ConnectLayers("NI ( *Ethernet ( *ChatApp ( *ChatDlg ) *FileApp ( +ChatDlg ) ) )");
	m_NI = (CNILayer*)m_LayerMgr.GetLayer("NI");
	m_Ethernet = (CEthernetLayer*)m_LayerMgr.GetLayer("Ethernet");
	m_FileApp = (CFileAppLayer*)m_LayerMgr.GetLayer("FileApp");
#else
	m_LayerMgr.ConnectLayers("File ( *Ethernet ( *ChatApp ( *ChatDlg ) ) )");
#endif

	m_ChatApp = (CChatAppLayer*)m_LayerMgr.GetLayer("ChatApp");
	//Protocol Layer Setting
}

void Cipc2019Dlg::DoDataExchange(CDataExchange* pDX)
{
	CDialogEx::DoDataExchange(pDX);
#if USE_NPCAP_STACK
	// [assignment4] MAC 주소·파일 경로·어댑터 목록·송수신 진행 컨트롤을 Dialog 멤버와 연결한다.
	DDX_Text(pDX, IDC_EDIT_SRC, m_sourceMac);
	DDX_Text(pDX, IDC_EDIT_DST, m_destinationMac);
	DDX_Text(pDX, IDC_EDIT_FILE_PATH, m_filePath);
	DDX_Control(pDX, IDC_COMBO_ADAPTER, m_AdapterCombo);
	DDX_Control(pDX, IDC_PROGRESS_FILE, m_FileProgress);
    DDX_Control(pDX, IDC_PROGRESS_FILE_RECEIVE, m_FileReceiveProgress);
#else
	DDX_Text(pDX, IDC_EDIT_SRC, m_unSrcAddr);
	DDX_Text(pDX, IDC_EDIT_DST, m_unDstAddr);
#endif
	DDX_Text(pDX, IDC_EDIT_MSG, m_stMessage);
	DDX_Control(pDX, IDC_LIST_CHAT, m_ListChat);
}

// 레지스트리에 등록하기 위한 변수
UINT nRegSendMsg;
UINT nRegAckMsg;
// 레지스트리에 등록하기 위한 변수


BEGIN_MESSAGE_MAP(Cipc2019Dlg, CDialogEx)
	ON_WM_SYSCOMMAND()
	ON_WM_PAINT()
	ON_WM_QUERYDRAGICON()
	ON_BN_CLICKED(IDC_BUTTON_ADDR, &Cipc2019Dlg::OnBnClickedButtonAddr)
	ON_BN_CLICKED(IDC_BUTTON_SEND, &Cipc2019Dlg::OnBnClickedButtonSend)
	ON_WM_TIMER()
	// [assignment4] 장치 선택·파일 버튼 이벤트와 작업 스레드가 보낸 결과 메시지를 UI 핸들러에 연결한다.
	ON_WM_DESTROY()
	ON_CBN_SELCHANGE(IDC_COMBO_ADAPTER, &Cipc2019Dlg::OnAdapterChanged)
	ON_BN_CLICKED(IDC_BUTTON_FILE_BROWSE, &Cipc2019Dlg::OnFileBrowse)
	ON_BN_CLICKED(IDC_BUTTON_FILE_SEND, &Cipc2019Dlg::OnFileSend)
    ON_BN_CLICKED(IDC_BUTTON_RECEIVED_FOLDER, &Cipc2019Dlg::OnOpenReceivedFolder)
	ON_MESSAGE(WM_CHAT_RECEIVED, &Cipc2019Dlg::OnChatReceived)
	ON_MESSAGE(WM_FILE_STATUS, &Cipc2019Dlg::OnFileStatus)

	ON_REGISTERED_MESSAGE(nRegSendMsg, OnRegSendMsg)
	//////////////////////// fill the blank ///////////////////////////////
		// Ack 레지스터 등록
	ON_REGISTERED_MESSAGE(nRegAckMsg, OnRegAckMsg)
	///////////////////////////////////////////////////////////////////////
	
	
	ON_BN_CLICKED(IDC_CHECK_TOALL, &Cipc2019Dlg::OnBnClickedCheckToall)
END_MESSAGE_MAP()


// Cipc2019Dlg 메시지 처리기

BOOL Cipc2019Dlg::OnInitDialog()
{
	CDialogEx::OnInitDialog();

	// 시스템 메뉴에 "정보..." 메뉴 항목을 추가합니다.

	// IDM_ABOUTBOX는 시스템 명령 범위에 있어야 합니다.
	ASSERT((IDM_ABOUTBOX & 0xFFF0) == IDM_ABOUTBOX);
	ASSERT(IDM_ABOUTBOX < 0xF000);

	CMenu* pSysMenu = GetSystemMenu(FALSE);
	if (pSysMenu != nullptr)
	{
		BOOL bNameValid;
		CString strAboutMenu;
		bNameValid = strAboutMenu.LoadString(IDS_ABOUTBOX);
		ASSERT(bNameValid);
		if (!strAboutMenu.IsEmpty())
		{
			pSysMenu->AppendMenu(MF_SEPARATOR);
			pSysMenu->AppendMenu(MF_STRING, IDM_ABOUTBOX, strAboutMenu);
		}
	}

	// 이 대화 상자의 아이콘을 설정합니다.  응용 프로그램의 주 창이 대화 상자가 아닐 경우에는
	//  프레임워크가 이 작업을 자동으로 수행합니다.
	SetIcon(m_hIcon, TRUE);			// 큰 아이콘을 설정합니다.
	SetIcon(m_hIcon, FALSE);		// 작은 아이콘을 설정합니다.

	// TODO: 여기에 추가 초기화 작업을 추가합니다.
	SetRegstryMessage();
    // [assignment4] 0은 OS가 지원하는 최대 텍스트 길이로 제한을 확장한다. 긴 단편 재조립 메시지도 표시한다.
    m_ListChat.SetLimitText(0);
#if USE_NPCAP_STACK
	// [assignment4] 현재 Dialog를 파일 상태 알림 대상으로 지정하고 송신·수신 진행창과 UI 타이머를 초기화한다.
	m_FileApp->SetNotifyWindow(m_hWnd);
	m_FileProgress.SetRange(0, 100);
    m_FileReceiveProgress.SetRange(0, 100);
    SetDlgItemText(IDC_STATIC_FILE_STATUS, _T("보낼 파일을 선택하세요."));
    SetDlgItemText(IDC_EDIT_FILE_RECEIVE_STATUS, _T("수신 대기 중"));
    SetTimer(FILE_UI_TIMER_ID, FILE_UI_REFRESH_MS, NULL);
	((CEdit*)GetDlgItem(IDC_EDIT_SRC))->SetReadOnly(TRUE);
	// [assignment4] MFC 편집창의 기본 입력 제한 때문에 MTU 초과 단편화를 시험하지 못하는 일을 막는다.
	((CEdit*)GetDlgItem(IDC_EDIT_MSG))->SetLimitText(CHAT_MAX_MESSAGE_SIZE);
	if (m_NI->LoadAdapters()) {
		// [assignment4] 오른쪽 설정 열은 좁게 유지하되, 목록을 펼쳤을 때는 긴 장치 설명도 읽을 수 있게 한다.
		// [assignment4] 고정 픽셀 값 대신 실제 컨트롤 폰트로 측정해 Windows 배율에 맞는 펼침 폭을 구한다.
		CClientDC adapterDC(&m_AdapterCombo);
		CFont* previousFont = adapterDC.SelectObject(m_AdapterCombo.GetFont());
		int dropWidth = 0;
		for (int i = 0; i < m_NI->GetAdapterCount(); ++i) {
			CString description = m_NI->GetAdapterName(i);
			m_AdapterCombo.AddString(description);
			dropWidth = (std::max)(dropWidth, static_cast<int>(adapterDC.GetTextExtent(description).cx));
		}
		adapterDC.SelectObject(previousFont);
		m_AdapterCombo.SetDroppedWidth(dropWidth + 2 * GetSystemMetrics(SM_CXVSCROLL));
		m_AdapterCombo.SetCurSel(0);
		OnAdapterChanged(); // [assignment4] 목적지 설정 전에도 자신의 MAC을 볼 수 있다.
	} else SetDlgItemText(IDC_STATIC_NETWORK_STATUS, m_NI->GetError());
#else
	// 기존 IPC 모드에서는 네트워크/파일 전송용 컨트롤을 사용하지 않는다.
	GetDlgItem(IDC_BUTTON_FILE_SEND)->EnableWindow(FALSE);
	GetDlgItem(IDC_BUTTON_FILE_BROWSE)->EnableWindow(FALSE);
	GetDlgItem(IDC_COMBO_ADAPTER)->EnableWindow(FALSE);
#endif
	SetDlgState(IPC_INITIALIZING);

	return TRUE;  // 포커스를 컨트롤에 설정하지 않으면 TRUE를 반환합니다.
}

void Cipc2019Dlg::OnSysCommand(UINT nID, LPARAM lParam)
{
	if ((nID & 0xFFF0) == IDM_ABOUTBOX)
	{
		CAboutDlg dlgAbout;
		dlgAbout.DoModal();
	}
	else
	{
		CDialogEx::OnSysCommand(nID, lParam);
	}
}

// 대화 상자에 최소화 단추를 추가할 경우 아이콘을 그리려면
//  아래 코드가 필요합니다.  문서/뷰 모델을 사용하는 MFC 애플리케이션의 경우에는
//  프레임워크에서 이 작업을 자동으로 수행합니다.

void Cipc2019Dlg::OnPaint()
{
	if (IsIconic())
	{
		CPaintDC dc(this); // 그리기를 위한 디바이스 컨텍스트입니다.

		SendMessage(WM_ICONERASEBKGND, reinterpret_cast<WPARAM>(dc.GetSafeHdc()), 0);

		// 클라이언트 사각형에서 아이콘을 가운데에 맞춥니다.
		int cxIcon = GetSystemMetrics(SM_CXICON);
		int cyIcon = GetSystemMetrics(SM_CYICON);
		CRect rect;
		GetClientRect(&rect);
		int x = (rect.Width() - cxIcon + 1) / 2;
		int y = (rect.Height() - cyIcon + 1) / 2;

		// 아이콘을 그립니다.
		dc.DrawIcon(x, y, m_hIcon);
	}
	else
	{
		CDialogEx::OnPaint();
	}
}

// 사용자가 최소화된 창을 끄는 동안에 커서가 표시되도록 시스템에서
//  이 함수를 호출합니다.
HCURSOR Cipc2019Dlg::OnQueryDragIcon()
{
	return static_cast<HCURSOR>(m_hIcon);
}




void Cipc2019Dlg::OnBnClickedButtonSend()
{
#if USE_NPCAP_STACK
	// [assignment4] 네트워크 채팅 송신을 호출한다. 현재 프로토콜에는 ACK가 없어 IPC ACK 타이머는 시작하지 않는다.
	SendData();
	return;
#endif
	UpdateData(TRUE);

	if (!m_stMessage.IsEmpty())
	{
		SetTimer(1, 2000, NULL);
		m_nAckReady = 0;

		SendData();
		m_stMessage = "";

		(CEdit*)GetDlgItem(IDC_EDIT3)->SetFocus();

		//////////////////////// fill the blank ///////////////////////////////
				// Send 신호를 브로드캐스트로 알림
		::SendMessage(HWND_BROADCAST, nRegSendMsg, 0, 0);
		///////////////////////////////////////////////////////////////////////
	}

	UpdateData(FALSE);
}

void Cipc2019Dlg::SetRegstryMessage()
{
	nRegSendMsg = RegisterWindowMessage(_T("Send IPC Message"));
	//////////////////////// fill the blank ///////////////////////////////
		// Ack 레지스트리의 메시지를 설정
	nRegAckMsg = RegisterWindowMessage(_T("Ack IPC Message"));
	///////////////////////////////////////////////////////////////////////
}

void Cipc2019Dlg::SendData()
{
#if USE_NPCAP_STACK
	// [assignment4] 기존 SendData 진입점에서 네트워크용 UTF-8 채팅 송신 함수로 분기한다.
	SendNetworkChat();
	return;
#endif
	CString MsgHeader;
	if (m_unDstAddr == (unsigned int)0xff)
		MsgHeader.Format(_T("[%d:BROADCAST] "), m_unSrcAddr);
	else
		MsgHeader.Format(_T("[%d:%d] "), m_unSrcAddr, m_unDstAddr);

	AppendChatMessage(MsgHeader + m_stMessage);

	//////////////////////// fill the blank ///////////////////////////////
	// 입력한 메시지를 파일로 저장
	int nlength = m_stMessage.GetLength();
	unsigned char* ppayload = new unsigned char[nlength + 1];
	memcpy(ppayload, (unsigned char*)(LPCTSTR)m_stMessage, nlength);
	ppayload[nlength] = '\0';


	// 보낼 data와 메시지 길이를 Send함수로 넘겨준다.
	m_ChatApp->Send(ppayload, nlength);
	///////////////////////////////////////////////////////////////////////
}

BOOL Cipc2019Dlg::Receive(unsigned char* ppayload)
{
	if (m_nAckReady == -1)
	{
		//////////////////////// fill the blank ///////////////////////////////
				// 현재 과제에서는 쓰이지 않음.
				// 여기서 FALSE처리를 해도 다음 함수들에서 TRUE처리가 되므로
				// return은 의미 없다.
				// 다음 과제에서 Receive시 Ack 메시지를 받은 경우
				// 어떠한 처리 과정에 쓰일 것으로 추정.
		///////////////////////////////////////////////////////////////////////
	}

	AppendChatMessage((LPCTSTR)ppayload);
	return TRUE;
}

BOOL Cipc2019Dlg::PreTranslateMessage(MSG* pMsg)
{
	// TODO: Add your specialized code here and/or call the base class
	switch (pMsg->message)
	{
	case WM_KEYDOWN:
		switch (pMsg->wParam)
		{
		case VK_RETURN:
			if (::GetDlgCtrlID(::GetFocus()) == IDC_EDIT3)
				OnBnClickedButtonSend();
			return TRUE; // [assignment4] Enter가 기본 IDOK로 전달되어 Dialog가 닫히는 것을 방지한다.
		case VK_ESCAPE: return FALSE;
		}
		break;
	}

	return CDialog::PreTranslateMessage(pMsg);
}


void Cipc2019Dlg::SetDlgState(int state)
{
#if USE_NPCAP_STACK
	// [assignment4] 네트워크 모드의 주소 잠금·브로드캐스트·송신 가능 상태를 전용 UI 처리 함수로 전달한다.
	SetNetworkDlgState(state);
	return;
#endif
	UpdateData(TRUE);

	CButton* pChkButton = (CButton*)GetDlgItem(IDC_CHECK1);

	CButton* pSendButton = (CButton*)GetDlgItem(bt_send);
	CButton* pSetAddrButton = (CButton*)GetDlgItem(bt_setting);
	CEdit* pMsgEdit = (CEdit*)GetDlgItem(IDC_EDIT3);
	CEdit* pSrcEdit = (CEdit*)GetDlgItem(IDC_EDIT1);
	CEdit* pDstEdit = (CEdit*)GetDlgItem(IDC_EDIT2);

	switch (state)
	{
	case IPC_INITIALIZING:
		pSendButton->EnableWindow(FALSE);
		pMsgEdit->EnableWindow(FALSE);
		m_ListChat.EnableWindow(FALSE);
		break;
	case IPC_READYTOSEND:
		pSendButton->EnableWindow(TRUE);
		pMsgEdit->EnableWindow(TRUE);
		m_ListChat.EnableWindow(TRUE);
		break;
	case IPC_WAITFORACK:	break;
	case IPC_ERROR:		break;
	case IPC_UNICASTMODE:
		m_unDstAddr = 0x0;
		pDstEdit->EnableWindow(TRUE);
		break;
	case IPC_BROADCASTMODE:
		m_unDstAddr = 0xff;
		pDstEdit->EnableWindow(FALSE);
		break;
	case IPC_ADDR_SET:
		pSetAddrButton->SetWindowText(_T("재설정(&R)"));
		pSrcEdit->EnableWindow(FALSE);
		pDstEdit->EnableWindow(FALSE);
		//pChkButton->EnableWindow(FALSE);
		break;
	case IPC_ADDR_RESET:
		pSetAddrButton->SetWindowText(_T("설정(&O)"));
		pSrcEdit->EnableWindow(TRUE);
		if (!pChkButton->GetCheck())
			pDstEdit->EnableWindow(TRUE);
		pChkButton->EnableWindow(TRUE);
		break;
	}

	UpdateData(FALSE);
}


void Cipc2019Dlg::EndofProcess()
{
#if USE_NPCAP_STACK
	// [assignment4] 하위 객체를 지우기 전에 두 worker를 join한다. 종료 중에도 PostMessage는
	// [assignment4] 큐에 남을 수 있으므로 OnDestroy에서 해당 데이터의 소유권을 마저 정리한다.
	if (m_FileApp) m_FileApp->StopTransfer();
	if (m_NI) m_NI->CloseAdapter();
	if (m_FileApp) m_FileApp->SetNotifyWindow(NULL);
#endif
	m_LayerMgr.DeAllocLayer();
}

// Send메시지 레지스트리가 켜졌을 때
LRESULT Cipc2019Dlg::OnRegSendMsg(WPARAM wParam, LPARAM lParam)
{
#if USE_NPCAP_STACK
	return 0; // [assignment4] 기존 IPC 알림을 수신해도 A4 스택의 파일/타이머 상태를 바꾸지 않는다.
#endif
	//////////////////////// fill the blank ///////////////////////////////
	if (m_nAckReady) {
		// File 레이어에서 상대방이 전송한 메시지가 담긴 파일을 가져옴
		if (m_LayerMgr.GetLayer("File")->Receive())
		{
			// 메시지를 받았다면 Ack 신호를 브로드캐스트로 날린다.
			::SendMessage(HWND_BROADCAST, nRegAckMsg, 0, 0);
		}
	}
	///////////////////////////////////////////////////////////////////////
	return 0;
}

LRESULT Cipc2019Dlg::OnRegAckMsg(WPARAM wParam, LPARAM lParam)
{
	if (!m_nAckReady) { // Ack 신호를 받으면 타이머를 멈춘다.
		m_nAckReady = -1;
		KillTimer(1);
	}

	return 0;
}

void Cipc2019Dlg::OnTimer(UINT_PTR nIDEvent)
{
#if USE_NPCAP_STACK
    // [assignment4] 새 UI 타이머를 기존 ACK 타임아웃 처리와 분리한다. 패킷이 없어도 대기 시간이 증가한다.
    if (nIDEvent == FILE_UI_TIMER_ID) {
        RefreshFileView(m_sendView, m_FileProgress, IDC_STATIC_FILE_STATUS);
        RefreshFileView(m_receiveView, m_FileReceiveProgress, IDC_EDIT_FILE_RECEIVE_STATUS);
        return;
    }
#endif
    if (nIDEvent != 1) { CDialogEx::OnTimer(nIDEvent); return; }
	// TODO: Add your message handler code here and/or call default
	AppendChatMessage(_T(">> The last message was time-out.."));
	m_nAckReady = -1;
	KillTimer(1);

	CDialog::OnTimer(nIDEvent);
}


void Cipc2019Dlg::OnBnClickedButtonAddr()
{
#if USE_NPCAP_STACK
	// [assignment4] 주소 설정 버튼을 MAC 기반 네트워크 설정·해제 동작에 연결한다.
	SetNetworkAddress();
	return;
#endif
	UpdateData(TRUE);

	if (!m_unDstAddr ||
		!m_unSrcAddr)
	{
		AfxMessageBox(_T("주소를 설정 오류발생"),
			MB_OK | MB_ICONSTOP);

		return;
	}

	if (m_bSendReady) {
		SetDlgState(IPC_ADDR_RESET);
		SetDlgState(IPC_INITIALIZING);
	}
	else {
		m_ChatApp->SetSourceAddress(m_unSrcAddr);
		m_ChatApp->SetDestinAddress(m_unDstAddr);

		SetDlgState(IPC_ADDR_SET);
		SetDlgState(IPC_READYTOSEND);
	}

	m_bSendReady = !m_bSendReady;
}

void Cipc2019Dlg::OnBnClickedCheckToall()
{
	CButton* pChkButton = (CButton*)GetDlgItem(IDC_CHECK_TOALL);

	if (pChkButton->GetCheck()) {
		SetDlgState(IPC_BROADCASTMODE);
	}
	else {
		SetDlgState(IPC_UNICASTMODE);
	}
}


// [assignment4] 어댑터 선택이 바뀌면 해당 장치를 열어 조회한 MAC 주소를 Source 입력창에 표시한다.
void Cipc2019Dlg::OnAdapterChanged()
{
	if (!m_NI || m_bSendReady) return;
	// [assignment4] 어댑터 선택의 결과는 오른쪽 네트워크 상태에만 표시한다.
	// [assignment4] 아래 파일 상태 컨트롤은 OnFileStatus()가 송수신 진행률과 결과를 표시할 때 사용한다.
	if (!m_NI->OpenAdapter(m_AdapterCombo.GetCurSel())) {
		SetDlgItemText(IDC_STATIC_NETWORK_STATUS, m_NI->GetError());
		m_sourceMac.Empty();
	} else {
		unsigned char address[ETHERNET_ADDRESS_SIZE];
		m_NI->GetMacAddress(address);
		m_sourceMac = FormatMac(address);
		SetDlgItemText(IDC_STATIC_NETWORK_STATUS, _T("상대 PC의 MAC 주소를 입력하고 설정을 누르십시오."));
	}
	// [assignment4] UpdateData(FALSE)로 사용자가 입력 중인 목적지/채팅까지 덮어쓰지 않는다.
	SetDlgItemText(IDC_EDIT_SRC, m_sourceMac);
}

// [assignment4] 목적지 MAC 또는 브로드캐스트 주소를 설정하고 조회한 Source MAC을 Ethernet에 적용한 뒤 수신을 시작한다.
// [assignment4] 재설정 시에는 수신 스레드를 종료한 다음 미완성 조각과 파일 상태를 정리한다.
void Cipc2019Dlg::SetNetworkAddress()
{
	if (m_bSendReady) {
		if (m_FileApp->IsSending()) {
			AfxMessageBox(_T("파일 송신이 끝난 뒤 주소를 재설정하십시오.")); return;
		}
		m_NI->CloseAdapter();
		m_ChatApp->ResetNetworkReceive();
		m_FileApp->ResetReceive();
        // [assignment4] NI를 종료한 뒤 큐의 마지막 상태부터 처리해 진행 중 수신이 영원히 남지 않게 한다.
        MSG pending;
        while (::PeekMessage(&pending, m_hWnd, WM_FILE_STATUS, WM_FILE_STATUS, PM_REMOVE))
            OnFileStatus(pending.wParam, pending.lParam);
        if (m_receiveView.hasStatus && !m_receiveView.latest.finished) {
            m_receiveView.latest.finished = TRUE;
            m_receiveView.latest.percent = 0;
            m_receiveView.latest.message = _T("설정 해제로 파일 수신 중단");
            m_receiveView.latest.reportedAtMs = GetTickCount64();
            RefreshFileView(m_receiveView, m_FileReceiveProgress, IDC_EDIT_FILE_RECEIVE_STATUS);
        }
		m_bSendReady = FALSE;
		SetDlgState(IPC_ADDR_RESET);
		SetDlgState(IPC_INITIALIZING);
		SetDlgItemText(IDC_STATIC_NETWORK_STATUS, _T("네트워크 설정 해제"));
		return;
	}
	UpdateData(TRUE);
	unsigned char destination[ETHERNET_ADDRESS_SIZE];
	if (((CButton*)GetDlgItem(IDC_CHECK_TOALL))->GetCheck())
		memset(destination, 0xff, sizeof(destination));
	else if (!ParseMac(m_destinationMac, destination)) {
		AfxMessageBox(_T("목적지 MAC을 00-11-22-33-44-55 형식으로 입력하십시오.")); return;
	}
	if (!m_NI->OpenAdapter(m_AdapterCombo.GetCurSel())) { AfxMessageBox(m_NI->GetError()); return; }
	unsigned char source[ETHERNET_ADDRESS_SIZE];
	m_NI->GetMacAddress(source);
	m_Ethernet->SetSourceAddress(source);
	m_Ethernet->SetDestinAddress(destination);
	if (!m_NI->StartReceive()) { AfxMessageBox(m_NI->GetError()); m_NI->CloseAdapter(); return; }
	m_sourceMac = FormatMac(source);
	m_destinationMac = FormatMac(destination);
	m_bSendReady = TRUE;
	SetDlgState(IPC_ADDR_SET);
	SetDlgState(IPC_READYTOSEND);
	SetDlgItemText(IDC_STATIC_NETWORK_STATUS, _T("채팅/파일 송수신 준비 완료"));
	UpdateData(FALSE);
}

// [assignment4] 설정 완료 여부에 따라 채팅·파일 전송 버튼을 활성화하고 송수신 중에는 어댑터·주소 변경을 제한한다.
void Cipc2019Dlg::SetNetworkDlgState(int state)
{
	BOOL broadcast = ((CButton*)GetDlgItem(IDC_CHECK_TOALL))->GetCheck();
	if (state == IPC_INITIALIZING || state == IPC_READYTOSEND) {
		BOOL ready = state == IPC_READYTOSEND;
		GetDlgItem(IDC_BUTTON_SEND)->EnableWindow(ready);
		GetDlgItem(IDC_EDIT_MSG)->EnableWindow(ready);
		GetDlgItem(IDC_BUTTON_FILE_SEND)->EnableWindow(ready);
	} else if (state == IPC_ADDR_SET || state == IPC_ADDR_RESET) {
		BOOL locked = state == IPC_ADDR_SET;
		m_AdapterCombo.EnableWindow(!locked);
		GetDlgItem(IDC_EDIT_DST)->EnableWindow(!locked && !broadcast);
		GetDlgItem(IDC_CHECK_TOALL)->EnableWindow(!locked);
		SetDlgItemText(IDC_BUTTON_ADDR, locked ? _T("재설정(&R)") : _T("설정(&O)"));
	} else if (state == IPC_BROADCASTMODE || state == IPC_UNICASTMODE) {
		GetDlgItem(IDC_EDIT_DST)->EnableWindow(!broadcast);
		m_destinationMac = broadcast ? _T("FF-FF-FF-FF-FF-FF") : _T("");
		SetDlgItemText(IDC_EDIT_DST, m_destinationMac);
	}
}

// [assignment4] 입력 문자열을 UTF-8 바이트로 바꿔 ChatApp에 전달하고 로컬 송신 로그를 표시한다.
// [assignment4] 헤더 길이 제한은 화면의 글자 수가 아니라 변환된 UTF-8 바이트 수로 검사한다.
void Cipc2019Dlg::SendNetworkChat()
{
	UpdateData(TRUE);
	if (!m_bSendReady || m_stMessage.IsEmpty()) return;
	// [assignment4] 문자열 문자 수와 네트워크 바이트 수는 다르다. 한글도 UTF-8 바이트로 바꾼 뒤
	// [assignment4] 그 바이트 길이를 헤더에 넣고, 수신 시 전체를 모은 다음 한 번만 역변환한다.
	CStringA utf8(CT2A(m_stMessage, CP_UTF8));
	if (utf8.GetLength() > CHAT_MAX_MESSAGE_SIZE) {
		AfxMessageBox(_T("과제 헤더의 전체 길이는 UTF-8 기준 65535 bytes까지입니다.")); return;
	}
	if (!m_ChatApp->Send(reinterpret_cast<unsigned char*>(const_cast<char*>(static_cast<LPCSTR>(utf8))), utf8.GetLength())) {
		AfxMessageBox(_T("채팅 프레임 송신 실패")); return;
	}
	CString line;
	line.Format(_T("[송신 %s -> %s]\r\n%s"), static_cast<LPCTSTR>(m_sourceMac),
		static_cast<LPCTSTR>(m_destinationMac), static_cast<LPCTSTR>(m_stMessage));
	AppendChatMessage(line); // [assignment4] 이 표시는 로컬 송신 표시이며 상대 수신 ACK가 아니다.
	m_stMessage.Empty();
	SetDlgItemText(IDC_EDIT_MSG, m_stMessage);
	GetDlgItem(IDC_EDIT_MSG)->SetFocus();
}

// [assignment4] ChatApp에서 완성한 UTF-8 메시지를 문자열로 복원하고 송신자 MAC과 함께 UI 메시지 큐로 전달한다.
BOOL Cipc2019Dlg::Receive(unsigned char* payload, int length, const unsigned char* source)
{
	if (!payload || !source || length <= 0) return FALSE;
	CStringA utf8(reinterpret_cast<const char*>(payload), length);
	CString message(CA2T(utf8, CP_UTF8));
	CString sender = FormatMac(source);
	CString* line = new CString;
	line->Format(_T("[수신 %s]\r\n%s"), static_cast<LPCTSTR>(sender), static_cast<LPCTSTR>(message));
	// [assignment4] NI 작업 스레드는 채팅 컨트롤을 직접 수정하지 않고 출력할 문자열만 복사해 전달한다.
    // [assignment4] 기존 MFC UI 스레드가 WM_CHAT_RECEIVED를 받아 CEdit에 표시하고 문자열 메모리를 해제한다.
	if (!PostMessage(WM_CHAT_RECEIVED, 0, reinterpret_cast<LPARAM>(line))) { delete line; return FALSE; }
	return TRUE;
}

// [assignment4] 기존 MFC UI 스레드에서 수신 문자열을 채팅창에 추가하고 전달받은 힙 메모리를 해제한다.
LRESULT Cipc2019Dlg::OnChatReceived(WPARAM wParam, LPARAM lParam)
{
	CString* line = reinterpret_cast<CString*>(lParam);
	if (line) { AppendChatMessage(*line); delete line; }
	return 0;
}

// [assignment4] 파일 선택 대화상자에서 전송할 파일의 전체 경로를 받아 경로 표시창에 반영한다.
void Cipc2019Dlg::OnFileBrowse()
{
	CFileDialog dialog(TRUE, NULL, NULL, OFN_FILEMUSTEXIST | OFN_HIDEREADONLY, _T("All files (*.*)|*.*||"), this);
	if (dialog.DoModal() == IDOK) {
		m_filePath = dialog.GetPathName();
		SetDlgItemText(IDC_EDIT_FILE_PATH, m_filePath);
	}
}

// [assignment4] 주소 설정과 중복 송신 여부를 확인하고 송신 표시만 초기화한 뒤 FileApp 작업 스레드를 시작한다.
void Cipc2019Dlg::OnFileSend()
{
	if (!m_FileApp || !m_bSendReady || m_FileApp->IsSending()) return;
	if (m_filePath.IsEmpty()) { AfxMessageBox(_T("전송할 파일을 선택하십시오.")); return; }
	GetDlgItem(IDC_BUTTON_FILE_SEND)->EnableWindow(FALSE);
	m_FileProgress.SetPos(0);
    m_sendView = FILE_VIEW();
    SetDlgItemText(IDC_STATIC_FILE_STATUS, _T("파일 송신 준비 중"));
	if (!m_FileApp->StartSendFile(m_filePath)) {
		GetDlgItem(IDC_BUTTON_FILE_SEND)->EnableWindow(TRUE);
        SetDlgItemText(IDC_STATIC_FILE_STATUS, _T("파일 송신 시작 실패"));
		AfxMessageBox(_T("파일 전송 스레드를 시작하지 못했습니다."));
	}
}

// [assignment4] PostMessage로 받은 상태를 송신·수신별 UI 스냅샷에 복사하고 해당 진행창만 갱신한다.
// [assignment4] 완료·오류를 채팅 로그에 남기며 송신 작업이 끝난 경우에만 파일 전송 버튼을 다시 활성화한다.
LRESULT Cipc2019Dlg::OnFileStatus(WPARAM wParam, LPARAM lParam)
{
	FILE_STATUS* status = reinterpret_cast<FILE_STATUS*>(lParam);
	if (!status) return 0;
    FILE_VIEW& view = status->sending ? m_sendView : m_receiveView;
    if (!view.hasStatus || view.latest.progress.startedAtMs != status->progress.startedAtMs ||
        (view.latest.finished && !status->finished)) {
        view = FILE_VIEW();
        view.sampleAtMs = status->progress.startedAtMs;
    }
    view.latest = *status; // [assignment4] worker가 넘긴 스냅샷을 복사하고 원본은 아래에서 해제한다.
    view.hasStatus = TRUE;
    RefreshFileView(view, status->sending ? m_FileProgress : m_FileReceiveProgress,
        status->sending ? IDC_STATIC_FILE_STATUS : IDC_EDIT_FILE_RECEIVE_STATUS);
    // [assignment4] 완료 경로/오류 문구도 줄바꿈 채팅 영역에 남겨 상태창이 바뀐 뒤 다시 확인할 수 있게 한다.
    if (status->finished) AppendChatMessage(status->message);
	if (status->sending && status->finished)
		GetDlgItem(IDC_BUTTON_FILE_SEND)->EnableWindow(m_bSendReady);
	delete status;
	return 0;
}

// [assignment4] UI 갱신 타이머를 멈추고 송신·수신 스레드를 종료한 뒤 남은 메시지 데이터와 Layer를 정리한다.
void Cipc2019Dlg::OnDestroy()
{
    KillTimer(FILE_UI_TIMER_ID);
	EndofProcess();
	// [assignment4] worker를 모두 종료했으므로 이제 큐에 새 알림은 들어오지 않는다.
	// [assignment4] 아직 UI가 처리하지 못한 heap 데이터도 직접 해제하여 종료 시 누수를 막는다.
	MSG message;
	while (::PeekMessage(&message, m_hWnd, WM_CHAT_RECEIVED, WM_CHAT_RECEIVED, PM_REMOVE))
		delete reinterpret_cast<CString*>(message.lParam);
	while (::PeekMessage(&message, m_hWnd, WM_FILE_STATUS, WM_FILE_STATUS, PM_REMOVE))
		delete reinterpret_cast<FILE_STATUS*>(message.lParam);
	CDialogEx::OnDestroy();
}

// [assignment4] 하이픈 또는 콜론으로 구분된 MAC 문자열을 검증하여 Ethernet 주소 6바이트로 변환한다.
BOOL Cipc2019Dlg::ParseMac(const CString& input, unsigned char* address)
{
	CString text(input);
	text.Trim(); text.Replace(_T('-'), _T(':'));
	if (text.GetLength() != 17) return FALSE;
	// [assignment4] 정확히 6개의 2자리 16진수만 받는다. sscanf의 부호/공백/초과 문자 허용을 피한다.
	const CString digits = _T("0123456789ABCDEF");
	text.MakeUpper();
	for (int i = 0; i < ETHERNET_ADDRESS_SIZE; ++i) {
		int high = digits.Find(text[i * 3]), low = digits.Find(text[i * 3 + 1]);
		if (high < 0 || low < 0 || (i < 5 && text[i * 3 + 2] != _T(':'))) return FALSE;
		address[i] = static_cast<unsigned char>(high * 16 + low);
	}
	return TRUE;
}

// [assignment4] MAC 6바이트를 두 자리 대문자 16진수와 하이픈으로 구성된 표시 문자열로 변환한다.
CString Cipc2019Dlg::FormatMac(const unsigned char* address)
{
	CString text;
	text.Format(_T("%02X-%02X-%02X-%02X-%02X-%02X"),
		address[0], address[1], address[2], address[3], address[4], address[5]);
	return text;
}

// [assignment4] 수평 스크롤 없는 ES_MULTILINE 편집창이 창 너비에 맞춰 줄을 바꾼다.
// [assignment4] 읽기 전용이어도 ReplaceSel로 프로그램의 로그를 추가할 수 있고 사용자는 복사할 수 있다.
void Cipc2019Dlg::AppendChatMessage(const CString& message)
{
    CString text(message);
    text.Replace(_T("\r\n"), _T("\n"));
    text.Replace(_T("\r"), _T("\n"));
    text.Replace(_T("\n"), _T("\r\n"));
    text += _T("\r\n\r\n");
    int end = m_ListChat.GetWindowTextLength();
    m_ListChat.SetSel(end, end);
    m_ListChat.ReplaceSel(text, FALSE);
    m_ListChat.LineScroll(m_ListChat.GetLineCount());
}

// [assignment4] 파일 처리량과 전송 속도의 바이트 수를 B·KiB·MiB·GiB 단위의 표시 문자열로 변환한다.
CString Cipc2019Dlg::FormatFileSize(uint64_t bytes)
{
    CString text;
    if (bytes < 1024) text.Format(_T("%llu B"), static_cast<unsigned long long>(bytes));
    else if (bytes < 1024 * 1024) text.Format(_T("%.1f KiB"), bytes / 1024.0);
    else if (bytes < 1024ULL * 1024 * 1024) text.Format(_T("%.2f MiB"), bytes / (1024.0 * 1024));
    else text.Format(_T("%.2f GiB"), bytes / (1024.0 * 1024 * 1024));
    return text;
}

// [assignment4] 밀리초 단위 측정 시간을 분:초 형식으로 바꿔 경과 시간과 예상 남은 시간에 사용한다.
CString Cipc2019Dlg::FormatDuration(ULONGLONG milliseconds)
{
    unsigned long long seconds = milliseconds / 1000;
    CString text;
    text.Format(_T("%llu:%02llu"), seconds / 60, seconds % 60);
    return text;
}

// [assignment4] 방향별 실제 처리량으로 진행률·최근 속도·경과 시간·예상 남은 시간을 계산한다.
// [assignment4] 기존 UI 스레드의 타이머로 정체 시간도 갱신하며 5초 정체 표시는 통신 실패나 ACK 판정으로 사용하지 않는다.
void Cipc2019Dlg::RefreshFileView(FILE_VIEW& view, CProgressCtrl& progress, int statusId)
{
    if (!view.hasStatus) return;
    const FILE_STATUS& status = view.latest;
    const FILE_PROGRESS& count = status.progress;
    ULONGLONG now = status.finished ? status.reportedAtMs : GetTickCount64();
    ULONGLONG elapsed = now >= count.startedAtMs ? now - count.startedAtMs : 0;
    ULONGLONG idle = now >= count.lastProgressAtMs ? now - count.lastProgressAtMs : 0;
    ULONGLONG sampleTime = now >= view.sampleAtMs ? now - view.sampleAtMs : 0;
    // [assignment4] 최근 약 1초의 실제 바이트 증가량으로 계산한다. 정체되면 다음 샘플은 0 B/s가 된다.
    if (sampleTime >= FILE_RATE_SAMPLE_MS) {
        uint64_t delta = count.completedBytes >= view.sampleBytes ? count.completedBytes - view.sampleBytes : 0;
        view.bytesPerSecond = static_cast<double>(delta) * 1000.0 / sampleTime;
        view.sampleAtMs = now;
        view.sampleBytes = count.completedBytes;
    }
    // [assignment4] 완료/오류 뒤에는 시간과 속도가 계속 변하지 않도록 해당 작업의 평균을 보여준다.
    double rate = status.finished ? (elapsed ? count.completedBytes * 1000.0 / elapsed : 0) : view.bytesPerSecond;
    double percent = count.totalBytes ? 100.0 * count.completedBytes / count.totalBytes :
        (status.finished && status.percent == 100 ? 100.0 : 0.0);
    progress.SetPos(static_cast<int>((std::min)(100.0, percent)));
    CString state = status.message;
    CString remaining;
    if (!status.finished && count.completedBytes == count.totalBytes && count.totalBytes) {
        // [assignment4] 100%는 데이터 바이트 기준이다. END 검증/이름 변경 전에는 수신 완료로 표시하지 않는다.
        state = status.sending ? _T("데이터 송신 완료 · 마무리 중") : _T("데이터 수신 완료 · 종료 확인 대기");
        remaining = _T("마무리 대기");
    } else if (status.finished) remaining = _T("작업 종료");
    else if (idle >= FILE_PROGRESS_STALL_MS || rate <= 0) remaining = _T("남은 시간 계산 대기");
    else {
        uint64_t left = count.totalBytes > count.completedBytes ? count.totalBytes - count.completedBytes : 0;
        ULONGLONG seconds = static_cast<ULONGLONG>(left / rate);
        if (seconds * rate < left) ++seconds;
        remaining = _T("약 ") + FormatDuration(seconds * 1000) + _T(" 남음");
    }
    if (!status.finished && idle >= FILE_PROGRESS_STALL_MS) {
        // [assignment4] ACK나 실패 판정이 아니다. 마지막 바이트 처리 이후의 정체 시간만 알려준다.
        CString waiting;
        waiting.Format(status.sending ? _T(" · %llu초 동안 추가 송신 없음") : _T(" · %llu초 동안 추가 수신 없음"),
            static_cast<unsigned long long>(idle / 1000));
        state += waiting;
    }
    CString text;
    text.Format(_T("%s\r\n%s\r\n%.1f%% · %s / %s\r\n%s %s/s · 경과 %s\r\n%s"),
        static_cast<LPCTSTR>(state), static_cast<LPCTSTR>(count.fileName), percent,
        static_cast<LPCTSTR>(FormatFileSize(count.completedBytes)), static_cast<LPCTSTR>(FormatFileSize(count.totalBytes)),
        status.finished ? _T("평균") : _T("속도"), static_cast<LPCTSTR>(FormatFileSize(static_cast<uint64_t>(rate))),
        static_cast<LPCTSTR>(FormatDuration(elapsed)), static_cast<LPCTSTR>(remaining));
    CString displayed;
    GetDlgItemText(statusId, displayed);
    // [assignment4] 완료 후에는 같은 내용을 반복 설정하지 않아 사용자가 긴 경로를 스크롤/복사할 수 있다.
    if (displayed != text) SetDlgItemText(statusId, text);
}

// [assignment4] FileApp이 사용하는 ReceivedFiles 경로를 확보한 뒤 탐색기로 열어 수신 파일을 확인하게 한다.
void Cipc2019Dlg::OnOpenReceivedFolder()
{
    CString directory = CFileAppLayer::GetReceiveDirectory();
    if (directory.IsEmpty() || (!CreateDirectory(directory, NULL) && GetLastError() != ERROR_ALREADY_EXISTS)) {
        AfxMessageBox(_T("수신 폴더를 준비하지 못했습니다.")); return;
    }
    HINSTANCE opened = ShellExecute(m_hWnd, _T("open"), directory, NULL, NULL, SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(opened) <= 32) AfxMessageBox(_T("수신 폴더를 열지 못했습니다."));
}
