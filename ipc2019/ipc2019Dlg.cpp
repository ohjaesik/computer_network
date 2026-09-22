// ipc2019Dlg.cpp: Assignment 3 IPC code extended with Assignment 4 Ethernet I/O.

#include "pch.h"
#include "framework.h"
#include "ipc2019.h"
#include "ipc2019Dlg.h"
#include "afxdialogex.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

class CAboutDlg : public CDialogEx
{
public:
    CAboutDlg();

#ifdef AFX_DESIGN_TIME
    enum { IDD = IDD_ABOUTBOX };
#endif

protected:
    virtual void DoDataExchange(CDataExchange* pDX);
    DECLARE_MESSAGE_MAP()
};

CAboutDlg::CAboutDlg()
    : CDialogEx(IDD_ABOUTBOX)
{
}

void CAboutDlg::DoDataExchange(CDataExchange* pDX)
{
    CDialogEx::DoDataExchange(pDX);
}

BEGIN_MESSAGE_MAP(CAboutDlg, CDialogEx)
END_MESSAGE_MAP()

UINT nRegSendMsg = 0;
UINT nRegAckMsg = 0;

Cipc2019Dlg::Cipc2019Dlg(CWnd* pParent)
    : CDialogEx(IDD_IPC2019_DIALOG, pParent),
      CBaseLayer("ChatDlg"),
      m_ChatApp(NULL),
      m_FileApp(NULL),
      m_Ethernet(NULL),
      m_NILayer(NULL),
      m_bSendReady(FALSE),
      m_nAckReady(-1),
      m_unSrcAddr(0),
      m_unDstAddr(0),
      m_wParam(0),
      m_lParam(0),
      m_stMessage(_T("")),
      m_strSourceMac(_T("")),
      m_strDestinationMac(_T("")),
      m_strFilePath(_T(""))
{
    m_hIcon = AfxGetApp()->LoadIcon(IDR_MAINFRAME);

    // Every protocol object derives from CBaseLayer. The legacy File layer is
    // retained but is not part of the active Assignment 4 network path.
    m_LayerMgr.AddLayer(new CNILayer("NI"));
    m_LayerMgr.AddLayer(new CEthernetLayer("Ethernet"));
    m_LayerMgr.AddLayer(new CChatAppLayer("ChatApp"));
    m_LayerMgr.AddLayer(new CFileAppLayer("FileApp"));
    m_LayerMgr.AddLayer(new CFileLayer("File"));
    m_LayerMgr.AddLayer(this, FALSE);

    // Active path:
    // ChatDlg <-> ChatApp/FileApp <-> Ethernet <-> NI
    m_LayerMgr.ConnectLayers(
        "NI ( *Ethernet ( *ChatApp ( *ChatDlg ) *FileApp ( *ChatDlg ) ) )");

    m_NILayer =
        static_cast<CNILayer*>(m_LayerMgr.GetLayer("NI"));
    m_Ethernet =
        static_cast<CEthernetLayer*>(m_LayerMgr.GetLayer("Ethernet"));
    m_ChatApp =
        static_cast<CChatAppLayer*>(m_LayerMgr.GetLayer("ChatApp"));
    m_FileApp =
        static_cast<CFileAppLayer*>(m_LayerMgr.GetLayer("FileApp"));
}

Cipc2019Dlg::~Cipc2019Dlg()
{
}

void Cipc2019Dlg::DoDataExchange(CDataExchange* pDX)
{
    CDialogEx::DoDataExchange(pDX);
    DDX_Text(pDX, IDC_EDIT_SRC, m_strSourceMac);
    DDX_Text(pDX, IDC_EDIT_DST, m_strDestinationMac);
    DDX_Text(pDX, IDC_EDIT_MSG, m_stMessage);
    DDX_Text(pDX, IDC_EDIT_FILE_PATH, m_strFilePath);
    DDX_Control(pDX, IDC_LIST_CHAT, m_ListChat);
    DDX_Control(pDX, IDC_COMBO_ADAPTER, m_AdapterCombo);
    DDX_Control(pDX, IDC_PROGRESS_FILE, m_FileProgress);
}

BEGIN_MESSAGE_MAP(Cipc2019Dlg, CDialogEx)
    ON_WM_SYSCOMMAND()
    ON_WM_PAINT()
    ON_WM_QUERYDRAGICON()
    ON_WM_DESTROY()
    ON_WM_TIMER()
    ON_BN_CLICKED(IDC_BUTTON_ADDR, &Cipc2019Dlg::OnBnClickedButtonAddr)
    ON_BN_CLICKED(IDC_BUTTON_SEND, &Cipc2019Dlg::OnBnClickedButtonSend)
    ON_BN_CLICKED(IDC_CHECK_TOALL, &Cipc2019Dlg::OnBnClickedCheckToall)
    ON_BN_CLICKED(
        IDC_BUTTON_FILE_BROWSE,
        &Cipc2019Dlg::OnBnClickedButtonFileBrowse)
    ON_BN_CLICKED(
        IDC_BUTTON_FILE_SEND,
        &Cipc2019Dlg::OnBnClickedButtonFileSend)
    ON_MESSAGE(WM_APP_CHAT_RECEIVED, &Cipc2019Dlg::OnChatReceived)
    ON_MESSAGE(WM_APP_FILE_STATUS, &Cipc2019Dlg::OnFileStatus)
    ON_REGISTERED_MESSAGE(nRegSendMsg, OnRegSendMsg)
    ON_REGISTERED_MESSAGE(nRegAckMsg, OnRegAckMsg)
END_MESSAGE_MAP()

BOOL Cipc2019Dlg::OnInitDialog()
{
    CDialogEx::OnInitDialog();

    ASSERT((IDM_ABOUTBOX & 0xFFF0) == IDM_ABOUTBOX);
    ASSERT(IDM_ABOUTBOX < 0xF000);

    CMenu* systemMenu = GetSystemMenu(FALSE);
    if (systemMenu != NULL)
    {
        CString aboutText;
        if (aboutText.LoadString(IDS_ABOUTBOX) &&
            !aboutText.IsEmpty())
        {
            systemMenu->AppendMenu(MF_SEPARATOR);
            systemMenu->AppendMenu(
                MF_STRING,
                IDM_ABOUTBOX,
                aboutText);
        }
    }

    SetIcon(m_hIcon, TRUE);
    SetIcon(m_hIcon, FALSE);

    SetRegstryMessage();
    m_FileApp->SetNotifyWindow(m_hWnd);

    m_FileProgress.SetRange(0, 100);
    m_FileProgress.SetPos(0);

    CEdit* sourceEdit =
        static_cast<CEdit*>(GetDlgItem(IDC_EDIT_SRC));
    if (sourceEdit != NULL)
        sourceEdit->SetReadOnly(TRUE);

    if (m_NILayer->RefreshAdapterList())
    {
        for (int i = 0; i < m_NILayer->GetAdapterCount(); ++i)
            m_AdapterCombo.AddString(
                m_NILayer->GetAdapterDescription(i));

        if (m_AdapterCombo.GetCount() > 0)
            m_AdapterCombo.SetCurSel(0);

        SetDlgItemText(
            IDC_STATIC_FILE_STATUS,
            _T("어댑터와 목적지 MAC 주소를 설정하십시오."));
    }
    else
    {
        SetDlgItemText(
            IDC_STATIC_FILE_STATUS,
            m_NILayer->GetLastErrorMessage());
    }

    SetDlgState(IPC_INITIALIZING);
    return TRUE;
}

void Cipc2019Dlg::OnDestroy()
{
    EndofProcess();
    CDialogEx::OnDestroy();
}

void Cipc2019Dlg::EndofProcess()
{
    if (m_FileApp != NULL)
    {
        m_FileApp->SetNotifyWindow(NULL);
        m_FileApp->StopTransfer();
    }

    if (m_NILayer != NULL)
        m_NILayer->CloseAdapter();
}

void Cipc2019Dlg::OnSysCommand(UINT nID, LPARAM lParam)
{
    if ((nID & 0xFFF0) == IDM_ABOUTBOX)
    {
        CAboutDlg aboutDialog;
        aboutDialog.DoModal();
    }
    else
    {
        CDialogEx::OnSysCommand(nID, lParam);
    }
}

void Cipc2019Dlg::OnPaint()
{
    if (IsIconic())
    {
        CPaintDC dc(this);
        SendMessage(
            WM_ICONERASEBKGND,
            reinterpret_cast<WPARAM>(dc.GetSafeHdc()),
            0);

        const int iconWidth = GetSystemMetrics(SM_CXICON);
        const int iconHeight = GetSystemMetrics(SM_CYICON);
        CRect rectangle;
        GetClientRect(&rectangle);
        const int x = (rectangle.Width() - iconWidth + 1) / 2;
        const int y = (rectangle.Height() - iconHeight + 1) / 2;
        dc.DrawIcon(x, y, m_hIcon);
    }
    else
    {
        CDialogEx::OnPaint();
    }
}

HCURSOR Cipc2019Dlg::OnQueryDragIcon()
{
    return static_cast<HCURSOR>(m_hIcon);
}

void Cipc2019Dlg::OnBnClickedButtonSend()
{
    UpdateData(TRUE);

    if (!m_bSendReady)
    {
        AfxMessageBox(_T("먼저 네트워크 어댑터와 MAC 주소를 설정하십시오."));
        return;
    }

    if (m_stMessage.IsEmpty())
        return;

    SendData();

#if ENABLE_LEGACY_IPC_ACK
    // Assignment 3 registered-message/ACK path is preserved but disabled for
    // Assignment 4 because this protocol does not define an ACK frame.
    SetTimer(1, 2000, NULL);
    m_nAckReady = 0;
    ::SendMessage(HWND_BROADCAST, nRegSendMsg, 0, 0);
#endif

    m_stMessage.Empty();
    UpdateData(FALSE);

    CWnd* messageEdit = GetDlgItem(IDC_EDIT_MSG);
    if (messageEdit != NULL)
        messageEdit->SetFocus();
}

void Cipc2019Dlg::SendData()
{
    CStringA encoded = EncodeUtf8(m_stMessage);

    if (encoded.IsEmpty() ||
        encoded.GetLength() > CHAT_MAX_MESSAGE_SIZE)
    {
        AfxMessageBox(
            _T("채팅 메시지는 UTF-8 기준 1~65535 bytes여야 합니다."));
        return;
    }

    if (!m_ChatApp->Send(
            reinterpret_cast<unsigned char*>(
                const_cast<LPSTR>(static_cast<LPCSTR>(encoded))),
            encoded.GetLength()))
    {
        AfxMessageBox(_T("채팅 frame 전송에 실패했습니다."));
        return;
    }

    unsigned char source[ETHERNET_ADDRESS_SIZE] = { 0 };
    unsigned char destination[ETHERNET_ADDRESS_SIZE] = { 0 };
    m_Ethernet->GetSourceAddress(source);
    m_Ethernet->GetDestinAddress(destination);

    CString line;
    line.Format(
        _T("[%s -> %s] %s"),
        static_cast<LPCTSTR>(FormatMacAddress(source)),
        static_cast<LPCTSTR>(FormatMacAddress(destination)),
        static_cast<LPCTSTR>(m_stMessage));
    m_ListChat.AddString(line);
}

BOOL Cipc2019Dlg::Receive(unsigned char* ppayload)
{
    if (ppayload == NULL || !IsWindow(m_hWnd))
        return FALSE;

    CString* line =
        new CString(reinterpret_cast<LPCTSTR>(ppayload));

    if (!PostMessage(
            WM_APP_CHAT_RECEIVED,
            0,
            reinterpret_cast<LPARAM>(line)))
    {
        delete line;
        return FALSE;
    }

    return TRUE;
}

BOOL Cipc2019Dlg::Receive(
    unsigned char* ppayload,
    int nlength,
    const unsigned char* sourceAddress,
    const unsigned char* destinationAddress)
{
    if (ppayload == NULL ||
        nlength <= 0 ||
        sourceAddress == NULL ||
        destinationAddress == NULL ||
        !IsWindow(m_hWnd))
        return FALSE;

    const CString message = DecodeUtf8(ppayload, nlength);
    CString* line = new CString;
    line->Format(
        _T("[%s -> %s] %s"),
        static_cast<LPCTSTR>(FormatMacAddress(sourceAddress)),
        static_cast<LPCTSTR>(FormatMacAddress(destinationAddress)),
        static_cast<LPCTSTR>(message));

    if (!PostMessage(
            WM_APP_CHAT_RECEIVED,
            0,
            reinterpret_cast<LPARAM>(line)))
    {
        delete line;
        return FALSE;
    }

    return TRUE;
}

LRESULT Cipc2019Dlg::OnChatReceived(
    WPARAM wParam,
    LPARAM lParam)
{
    UNREFERENCED_PARAMETER(wParam);

    CString* line = reinterpret_cast<CString*>(lParam);
    if (line != NULL)
    {
        m_ListChat.AddString(*line);
        delete line;
    }

    return 0;
}

LRESULT Cipc2019Dlg::OnFileStatus(
    WPARAM wParam,
    LPARAM lParam)
{
    UNREFERENCED_PARAMETER(wParam);

    FILE_STATUS_EVENT* eventData =
        reinterpret_cast<FILE_STATUS_EVENT*>(lParam);

    if (eventData == NULL)
        return 0;

    m_FileProgress.SetPos(eventData->progress);
    SetDlgItemText(
        IDC_STATIC_FILE_STATUS,
        eventData->message);

    if (eventData->finished)
    {
        CWnd* sendButton =
            GetDlgItem(IDC_BUTTON_FILE_SEND);
        if (sendButton != NULL)
            sendButton->EnableWindow(m_bSendReady);
    }

    delete eventData;
    return 0;
}

BOOL Cipc2019Dlg::PreTranslateMessage(MSG* pMsg)
{
    if (pMsg->message == WM_KEYDOWN)
    {
        if (pMsg->wParam == VK_RETURN &&
            ::GetDlgCtrlID(::GetFocus()) == IDC_EDIT_MSG)
        {
            OnBnClickedButtonSend();
            return TRUE;
        }

        if (pMsg->wParam == VK_ESCAPE)
            return TRUE;
    }

    return CDialogEx::PreTranslateMessage(pMsg);
}

void Cipc2019Dlg::SetDlgState(int state)
{
    CWnd* sendButton = GetDlgItem(IDC_BUTTON_SEND);
    CWnd* messageEdit = GetDlgItem(IDC_EDIT_MSG);
    CWnd* fileBrowseButton =
        GetDlgItem(IDC_BUTTON_FILE_BROWSE);
    CWnd* fileSendButton =
        GetDlgItem(IDC_BUTTON_FILE_SEND);
    CWnd* setAddressButton =
        GetDlgItem(IDC_BUTTON_ADDR);
    CWnd* destinationEdit =
        GetDlgItem(IDC_EDIT_DST);
    CWnd* broadcastCheck =
        GetDlgItem(IDC_CHECK_TOALL);

    switch (state)
    {
    case IPC_INITIALIZING:
        if (sendButton != NULL)
            sendButton->EnableWindow(FALSE);
        if (messageEdit != NULL)
            messageEdit->EnableWindow(FALSE);
        if (fileBrowseButton != NULL)
            fileBrowseButton->EnableWindow(TRUE);
        if (fileSendButton != NULL)
            fileSendButton->EnableWindow(FALSE);
        break;

    case IPC_READYTOSEND:
        if (sendButton != NULL)
            sendButton->EnableWindow(TRUE);
        if (messageEdit != NULL)
            messageEdit->EnableWindow(TRUE);
        if (fileBrowseButton != NULL)
            fileBrowseButton->EnableWindow(TRUE);
        if (fileSendButton != NULL)
            fileSendButton->EnableWindow(TRUE);
        break;

    case IPC_UNICASTMODE:
        if (!m_bSendReady && destinationEdit != NULL)
            destinationEdit->EnableWindow(TRUE);
        break;

    case IPC_BROADCASTMODE:
        m_strDestinationMac = _T("FF-FF-FF-FF-FF-FF");
        if (destinationEdit != NULL)
            destinationEdit->EnableWindow(FALSE);
        UpdateData(FALSE);
        break;

    case IPC_ADDR_SET:
        if (setAddressButton != NULL)
            setAddressButton->SetWindowText(_T("재설정(&R)"));
        m_AdapterCombo.EnableWindow(FALSE);
        if (destinationEdit != NULL)
            destinationEdit->EnableWindow(FALSE);
        if (broadcastCheck != NULL)
            broadcastCheck->EnableWindow(FALSE);
        break;

    case IPC_ADDR_RESET:
        if (setAddressButton != NULL)
            setAddressButton->SetWindowText(_T("설정(&O)"));
        m_AdapterCombo.EnableWindow(TRUE);
        if (broadcastCheck != NULL)
            broadcastCheck->EnableWindow(TRUE);
        if (destinationEdit != NULL)
        {
            CButton* check =
                static_cast<CButton*>(broadcastCheck);
            destinationEdit->EnableWindow(
                check == NULL || check->GetCheck() == BST_UNCHECKED);
        }
        break;

    case IPC_WAITFORACK:
    case IPC_ERROR:
    default:
        break;
    }
}

void Cipc2019Dlg::OnBnClickedButtonAddr()
{
    if (m_bSendReady)
    {
        if (m_FileApp->IsSending())
        {
            AfxMessageBox(
                _T("파일 전송이 끝난 뒤 네트워크 설정을 변경하십시오."));
            return;
        }

        m_NILayer->CloseAdapter();
        m_bSendReady = FALSE;
        m_strSourceMac.Empty();
        SetDlgState(IPC_ADDR_RESET);
        SetDlgState(IPC_INITIALIZING);
        SetDlgItemText(
            IDC_STATIC_FILE_STATUS,
            _T("네트워크 설정이 해제되었습니다."));
        UpdateData(FALSE);
        return;
    }

    UpdateData(TRUE);

    unsigned char destination[ETHERNET_ADDRESS_SIZE] = { 0 };
    CButton* broadcast =
        static_cast<CButton*>(GetDlgItem(IDC_CHECK_TOALL));

    if (broadcast != NULL &&
        broadcast->GetCheck() == BST_CHECKED)
    {
        memset(destination, 0xff, sizeof(destination));
        m_strDestinationMac =
            _T("FF-FF-FF-FF-FF-FF");
    }
    else if (!ParseMacAddress(
                 m_strDestinationMac,
                 destination))
    {
        AfxMessageBox(
            _T("목적지 MAC 주소를 00-11-22-33-44-55 형식으로 입력하십시오."));
        return;
    }

    const int adapterIndex = m_AdapterCombo.GetCurSel();
    if (!m_NILayer->OpenAdapter(adapterIndex))
    {
        AfxMessageBox(m_NILayer->GetLastErrorMessage());
        return;
    }

    unsigned char source[ETHERNET_ADDRESS_SIZE] = { 0 };
    if (!m_NILayer->GetMacAddress(source))
    {
        m_NILayer->CloseAdapter();
        AfxMessageBox(_T("선택한 어댑터의 MAC 주소를 얻지 못했습니다."));
        return;
    }

    m_Ethernet->SetSourceAddress(source);
    m_Ethernet->SetDestinAddress(destination);

    if (!m_NILayer->StartReceive())
    {
        const CString error = m_NILayer->GetLastErrorMessage();
        m_NILayer->CloseAdapter();
        AfxMessageBox(error);
        return;
    }

    m_strSourceMac = FormatMacAddress(source);
    m_strDestinationMac = FormatMacAddress(destination);
    m_bSendReady = TRUE;

    SetDlgState(IPC_ADDR_SET);
    SetDlgState(IPC_READYTOSEND);
    SetDlgItemText(
        IDC_STATIC_FILE_STATUS,
        _T("네트워크 연결 준비 완료"));
    UpdateData(FALSE);
}

void Cipc2019Dlg::OnBnClickedCheckToall()
{
    CButton* check =
        static_cast<CButton*>(GetDlgItem(IDC_CHECK_TOALL));

    if (check != NULL &&
        check->GetCheck() == BST_CHECKED)
        SetDlgState(IPC_BROADCASTMODE);
    else
        SetDlgState(IPC_UNICASTMODE);
}

void Cipc2019Dlg::OnBnClickedButtonFileBrowse()
{
    CFileDialog dialog(
        TRUE,
        NULL,
        NULL,
        OFN_FILEMUSTEXIST | OFN_HIDEREADONLY,
        _T("모든 파일 (*.*)|*.*||"),
        this);

    if (dialog.DoModal() == IDOK)
    {
        m_strFilePath = dialog.GetPathName();
        UpdateData(FALSE);
    }
}

void Cipc2019Dlg::OnBnClickedButtonFileSend()
{
    UpdateData(TRUE);

    if (!m_bSendReady)
    {
        AfxMessageBox(_T("먼저 네트워크 설정을 완료하십시오."));
        return;
    }

    if (m_strFilePath.IsEmpty())
    {
        AfxMessageBox(_T("전송할 파일을 선택하십시오."));
        return;
    }

    m_FileProgress.SetPos(0);
    SetDlgItemText(
        IDC_STATIC_FILE_STATUS,
        _T("파일 전송 준비 중..."));

    CWnd* sendButton =
        GetDlgItem(IDC_BUTTON_FILE_SEND);
    if (sendButton != NULL)
        sendButton->EnableWindow(FALSE);

    if (!m_FileApp->StartSendFile(m_strFilePath))
    {
        if (sendButton != NULL)
            sendButton->EnableWindow(TRUE);
    }
}

void Cipc2019Dlg::SetRegstryMessage()
{
    nRegSendMsg =
        RegisterWindowMessage(_T("Send IPC Message"));
    nRegAckMsg =
        RegisterWindowMessage(_T("Ack IPC Message"));
}

LRESULT Cipc2019Dlg::OnRegSendMsg(
    WPARAM wParam,
    LPARAM lParam)
{
    UNREFERENCED_PARAMETER(wParam);
    UNREFERENCED_PARAMETER(lParam);

#if ENABLE_LEGACY_IPC_ACK
    if (m_nAckReady)
    {
        CBaseLayer* legacyFile =
            m_LayerMgr.GetLayer("File");
        if (legacyFile != NULL && legacyFile->Receive())
            ::SendMessage(HWND_BROADCAST, nRegAckMsg, 0, 0);
    }
#endif

    return 0;
}

LRESULT Cipc2019Dlg::OnRegAckMsg(
    WPARAM wParam,
    LPARAM lParam)
{
    UNREFERENCED_PARAMETER(wParam);
    UNREFERENCED_PARAMETER(lParam);

    if (!m_nAckReady)
    {
        m_nAckReady = -1;
        KillTimer(1);
    }

    return 0;
}

void Cipc2019Dlg::OnTimer(UINT nIDEvent)
{
    if (nIDEvent == 1)
    {
        m_ListChat.AddString(
            _T(">> The last message was time-out.."));
        m_nAckReady = -1;
        KillTimer(1);
    }

    CDialogEx::OnTimer(nIDEvent);
}

BOOL Cipc2019Dlg::ParseMacAddress(
    const CString& input,
    unsigned char address[ETHERNET_ADDRESS_SIZE])
{
    if (address == NULL)
        return FALSE;

    CString text(input);
    text.Trim();
    text.Replace(_T('-'), _T(':'));

    unsigned int value[ETHERNET_ADDRESS_SIZE] = { 0 };
    TCHAR trailing = 0;

    const int converted = _stscanf_s(
        text,
        _T("%x:%x:%x:%x:%x:%x%c"),
        &value[0],
        &value[1],
        &value[2],
        &value[3],
        &value[4],
        &value[5],
        &trailing,
        1);

    if (converted != ETHERNET_ADDRESS_SIZE)
        return FALSE;

    for (int i = 0; i < ETHERNET_ADDRESS_SIZE; ++i)
    {
        if (value[i] > 0xff)
            return FALSE;
        address[i] = static_cast<unsigned char>(value[i]);
    }

    return TRUE;
}

CString Cipc2019Dlg::FormatMacAddress(
    const unsigned char* address)
{
    if (address == NULL)
        return _T("");

    CString result;
    result.Format(
        _T("%02X-%02X-%02X-%02X-%02X-%02X"),
        address[0],
        address[1],
        address[2],
        address[3],
        address[4],
        address[5]);
    return result;
}

CString Cipc2019Dlg::DecodeUtf8(
    const unsigned char* data,
    int length)
{
    if (data == NULL || length <= 0)
        return _T("");

    UINT codePage = CP_UTF8;
    DWORD flags = MB_ERR_INVALID_CHARS;
    int characterCount = MultiByteToWideChar(
        codePage,
        flags,
        reinterpret_cast<LPCCH>(data),
        length,
        NULL,
        0);

    if (characterCount == 0)
    {
        codePage = CP_ACP;
        flags = 0;
        characterCount = MultiByteToWideChar(
            codePage,
            flags,
            reinterpret_cast<LPCCH>(data),
            length,
            NULL,
            0);
    }

    if (characterCount <= 0)
        return _T("");

    CString result;
    LPWSTR buffer = result.GetBuffer(characterCount);
    MultiByteToWideChar(
        codePage,
        flags,
        reinterpret_cast<LPCCH>(data),
        length,
        buffer,
        characterCount);
    result.ReleaseBuffer(characterCount);
    return result;
}

CStringA Cipc2019Dlg::EncodeUtf8(const CString& text)
{
    if (text.IsEmpty())
        return CStringA();

    const int byteCount = WideCharToMultiByte(
        CP_UTF8,
        0,
        text,
        text.GetLength(),
        NULL,
        0,
        NULL,
        NULL);

    CStringA result;
    LPSTR buffer = result.GetBuffer(byteCount);
    WideCharToMultiByte(
        CP_UTF8,
        0,
        text,
        text.GetLength(),
        buffer,
        byteCount,
        NULL,
        NULL);
    result.ReleaseBuffer(byteCount);
    return result;
}
