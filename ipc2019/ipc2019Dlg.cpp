
// ipc2019Dlg.cpp: 구현 파일
//

#include "pch.h"
#include "framework.h"
#include "ipc2019.h"
#include "ipc2019Dlg.h"
#include "afxdialogex.h"
#include <afxdlgs.h> // 파일 선택 창(CFileDialog) 선언

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
	// NI가 실제 전송을 맡고 FileApp이 파일 내용을 분할한다. 기존 FileLayer와는 역할이 다르다.
	m_LayerMgr.AddLayer(new CNILayer("NI"));
	m_LayerMgr.AddLayer(new CFileAppLayer("FileApp"));
#else
	m_LayerMgr.AddLayer(new CFileLayer("File"));
#endif
	m_LayerMgr.AddLayer(this, FALSE); // main에서 생성한 Dialog는 LayerManager 소유가 아니다.

	// 레이어를 연결한다. (레이어 생성)
#if USE_NPCAP_STACK
	// Dialog의 단일 Under 포인터는 ChatApp에 둔다. 파일 송신은 m_FileApp으로 호출하므로
	// FileApp -> Dialog 연결에는 '+'를 써서 기존 Under 포인터를 덮어쓰지 않는다.
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
	DDX_Text(pDX, IDC_EDIT_SRC, m_sourceMac);
	DDX_Text(pDX, IDC_EDIT_DST, m_destinationMac);
	DDX_Text(pDX, IDC_EDIT_FILE_PATH, m_filePath);
	DDX_Control(pDX, IDC_COMBO_ADAPTER, m_AdapterCombo);
	DDX_Control(pDX, IDC_PROGRESS_FILE, m_FileProgress);
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
	ON_WM_DESTROY()
	ON_CBN_SELCHANGE(IDC_COMBO_ADAPTER, &Cipc2019Dlg::OnAdapterChanged)
	ON_BN_CLICKED(IDC_BUTTON_FILE_BROWSE, &Cipc2019Dlg::OnFileBrowse)
	ON_BN_CLICKED(IDC_BUTTON_FILE_SEND, &Cipc2019Dlg::OnFileSend)
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
#if USE_NPCAP_STACK
	m_FileApp->SetNotifyWindow(m_hWnd);
	m_FileProgress.SetRange(0, 100);
	((CEdit*)GetDlgItem(IDC_EDIT_SRC))->SetReadOnly(TRUE);
	// MFC 편집창의 기본 입력 제한 때문에 MTU 초과 단편화를 시험하지 못하는 일을 막는다.
	((CEdit*)GetDlgItem(IDC_EDIT_MSG))->SetLimitText(CHAT_MAX_MESSAGE_SIZE);
	if (m_NI->LoadAdapters()) {
		// 오른쪽 설정 열은 좁게 유지하되, 목록을 펼쳤을 때는 긴 장치 설명도 읽을 수 있게 한다.
		// 고정 픽셀 값 대신 실제 컨트롤 폰트로 측정해 Windows 배율에 맞는 펼침 폭을 구한다.
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
		OnAdapterChanged(); // 목적지 설정 전에도 자신의 MAC을 볼 수 있다.
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
	// A4 raw Ethernet 프로토콜에는 ACK가 없으므로 아래 IPC 타이머를 시작하지 않는다.
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
	SendNetworkChat();
	return;
#endif
	CString MsgHeader;
	if (m_unDstAddr == (unsigned int)0xff)
		MsgHeader.Format(_T("[%d:BROADCAST] "), m_unSrcAddr);
	else
		MsgHeader.Format(_T("[%d:%d] "), m_unSrcAddr, m_unDstAddr);

	m_ListChat.AddString(MsgHeader + m_stMessage);

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

	m_ListChat.AddString((LPCTSTR)ppayload);
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
			return TRUE; // Enter가 기본 IDOK로 전달되어 Dialog가 닫히는 것을 방지한다.
		case VK_ESCAPE: return FALSE;
		}
		break;
	}

	return CDialog::PreTranslateMessage(pMsg);
}


void Cipc2019Dlg::SetDlgState(int state)
{
#if USE_NPCAP_STACK
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
	// 하위 객체를 지우기 전에 두 worker를 join한다. 종료 중에도 PostMessage는
	// 큐에 남을 수 있으므로 OnDestroy에서 해당 데이터의 소유권을 마저 정리한다.
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
	return 0; // 기존 IPC 알림을 수신해도 A4 스택의 파일/타이머 상태를 바꾸지 않는다.
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
	// TODO: Add your message handler code here and/or call default
	m_ListChat.AddString(_T(">> The last message was time-out.."));
	m_nAckReady = -1;
	KillTimer(1);

	CDialog::OnTimer(nIDEvent);
}


void Cipc2019Dlg::OnBnClickedButtonAddr()
{
#if USE_NPCAP_STACK
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


// [과제 4 추가] 기존 UI/IPC 함수는 위에 보존하고 MAC 설정·파일 기능만 확장한다.
void Cipc2019Dlg::OnAdapterChanged()
{
	if (!m_NI || m_bSendReady) return;
	// 어댑터 선택의 결과는 오른쪽 네트워크 상태에만 표시한다.
	// 아래 파일 상태 컨트롤은 OnFileStatus()가 송수신 진행률과 결과를 표시할 때 사용한다.
	if (!m_NI->OpenAdapter(m_AdapterCombo.GetCurSel())) {
		SetDlgItemText(IDC_STATIC_NETWORK_STATUS, m_NI->GetError());
		m_sourceMac.Empty();
	} else {
		unsigned char address[ETHERNET_ADDRESS_SIZE];
		m_NI->GetMacAddress(address);
		m_sourceMac = FormatMac(address);
		SetDlgItemText(IDC_STATIC_NETWORK_STATUS, _T("상대 PC의 MAC 주소를 입력하고 설정을 누르십시오."));
	}
	// UpdateData(FALSE)로 사용자가 입력 중인 목적지/채팅까지 덮어쓰지 않는다.
	SetDlgItemText(IDC_EDIT_SRC, m_sourceMac);
}

void Cipc2019Dlg::SetNetworkAddress()
{
	if (m_bSendReady) {
		if (m_FileApp->IsSending()) {
			AfxMessageBox(_T("파일 송신이 끝난 뒤 주소를 재설정하십시오.")); return;
		}
		m_NI->CloseAdapter();
		m_ChatApp->ResetNetworkReceive();
		m_FileApp->ResetReceive();
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

void Cipc2019Dlg::SendNetworkChat()
{
	UpdateData(TRUE);
	if (!m_bSendReady || m_stMessage.IsEmpty()) return;
	// 문자열 문자 수와 네트워크 바이트 수는 다르다. 한글도 UTF-8 바이트로 바꾼 뒤
	// 그 바이트 길이를 헤더에 넣고, 수신 시 전체를 모은 다음 한 번만 역변환한다.
	CStringA utf8(CT2A(m_stMessage, CP_UTF8));
	if (utf8.GetLength() > CHAT_MAX_MESSAGE_SIZE) {
		AfxMessageBox(_T("과제 헤더의 전체 길이는 UTF-8 기준 65535 bytes까지입니다.")); return;
	}
	if (!m_ChatApp->Send(reinterpret_cast<unsigned char*>(const_cast<char*>(static_cast<LPCSTR>(utf8))), utf8.GetLength())) {
		AfxMessageBox(_T("채팅 프레임 송신 실패")); return;
	}
	CString line;
	line.Format(_T("[%s -> %s] %s"), static_cast<LPCTSTR>(m_sourceMac),
		static_cast<LPCTSTR>(m_destinationMac), static_cast<LPCTSTR>(m_stMessage));
	m_ListChat.AddString(line); // 이 표시는 로컬 송신 표시이며 상대 수신 ACK가 아니다.
	m_stMessage.Empty();
	SetDlgItemText(IDC_EDIT_MSG, m_stMessage);
	GetDlgItem(IDC_EDIT_MSG)->SetFocus();
}

BOOL Cipc2019Dlg::Receive(unsigned char* payload, int length, const unsigned char* source)
{
	if (!payload || !source || length <= 0) return FALSE;
	CStringA utf8(reinterpret_cast<const char*>(payload), length);
	CString message(CA2T(utf8, CP_UTF8));
	CString sender = FormatMac(source);
	CString* line = new CString;
	line->Format(_T("[%s] %s"), static_cast<LPCTSTR>(sender), static_cast<LPCTSTR>(message));
	// worker에서 CListBox를 직접 조작하지 않는다. UI가 이 문자열을 출력하고 해제한다.
	if (!PostMessage(WM_CHAT_RECEIVED, 0, reinterpret_cast<LPARAM>(line))) { delete line; return FALSE; }
	return TRUE;
}

LRESULT Cipc2019Dlg::OnChatReceived(WPARAM wParam, LPARAM lParam)
{
	CString* line = reinterpret_cast<CString*>(lParam);
	if (line) { m_ListChat.AddString(*line); delete line; }
	return 0;
}

void Cipc2019Dlg::OnFileBrowse()
{
	CFileDialog dialog(TRUE, NULL, NULL, OFN_FILEMUSTEXIST | OFN_HIDEREADONLY, _T("All files (*.*)|*.*||"), this);
	if (dialog.DoModal() == IDOK) {
		m_filePath = dialog.GetPathName();
		SetDlgItemText(IDC_EDIT_FILE_PATH, m_filePath);
	}
}

void Cipc2019Dlg::OnFileSend()
{
	if (!m_FileApp || !m_bSendReady || m_FileApp->IsSending()) return;
	if (m_filePath.IsEmpty()) { AfxMessageBox(_T("전송할 파일을 선택하십시오.")); return; }
	GetDlgItem(IDC_BUTTON_FILE_SEND)->EnableWindow(FALSE);
	m_FileProgress.SetPos(0);
	if (!m_FileApp->StartSendFile(m_filePath)) {
		GetDlgItem(IDC_BUTTON_FILE_SEND)->EnableWindow(TRUE);
		AfxMessageBox(_T("파일 전송 스레드를 시작하지 못했습니다."));
	}
}

LRESULT Cipc2019Dlg::OnFileStatus(WPARAM wParam, LPARAM lParam)
{
	FILE_STATUS* status = reinterpret_cast<FILE_STATUS*>(lParam);
	if (!status) return 0;
	m_FileProgress.SetPos(status->percent);
	SetDlgItemText(IDC_STATIC_FILE_STATUS, status->message);
	if (status->sending && status->finished)
		GetDlgItem(IDC_BUTTON_FILE_SEND)->EnableWindow(m_bSendReady);
	delete status;
	return 0;
}

void Cipc2019Dlg::OnDestroy()
{
	EndofProcess();
	// worker를 모두 종료했으므로 이제 큐에 새 알림은 들어오지 않는다.
	// 아직 UI가 처리하지 못한 heap 데이터도 직접 해제하여 종료 시 누수를 막는다.
	MSG message;
	while (::PeekMessage(&message, m_hWnd, WM_CHAT_RECEIVED, WM_CHAT_RECEIVED, PM_REMOVE))
		delete reinterpret_cast<CString*>(message.lParam);
	while (::PeekMessage(&message, m_hWnd, WM_FILE_STATUS, WM_FILE_STATUS, PM_REMOVE))
		delete reinterpret_cast<FILE_STATUS*>(message.lParam);
	CDialogEx::OnDestroy();
}

BOOL Cipc2019Dlg::ParseMac(const CString& input, unsigned char* address)
{
	CString text(input);
	text.Trim(); text.Replace(_T('-'), _T(':'));
	if (text.GetLength() != 17) return FALSE;
	// 정확히 6개의 2자리 16진수만 받는다. sscanf의 부호/공백/초과 문자 허용을 피한다.
	const CString digits = _T("0123456789ABCDEF");
	text.MakeUpper();
	for (int i = 0; i < ETHERNET_ADDRESS_SIZE; ++i) {
		int high = digits.Find(text[i * 3]), low = digits.Find(text[i * 3 + 1]);
		if (high < 0 || low < 0 || (i < 5 && text[i * 3 + 2] != _T(':'))) return FALSE;
		address[i] = static_cast<unsigned char>(high * 16 + low);
	}
	return TRUE;
}

CString Cipc2019Dlg::FormatMac(const unsigned char* address)
{
	CString text;
	text.Format(_T("%02X-%02X-%02X-%02X-%02X-%02X"),
		address[0], address[1], address[2], address[3], address[4], address[5]);
	return text;
}
