#include "pch.h"
#include "ipc2019Dlg.h"
#include "NetworkAddress.h"

// [assignment6] 기존 Dialog 이벤트/코드는 유지하고 IP/ARP 화면 연결 부분만 별도 구현 파일로 분리한다.
void Cipc2019Dlg::InitIpUi()
{
    if (!m_IP || !m_ARP) return;
    CRect page(10, 44, 610, 368); MapDialogRect(&page);
    // [assignment6] 기존 채팅/파일 컨트롤(이름 없는 정적 라벨 포함)을 본문 영역 기준으로 기억한다.
    // [assignment6] 아직 ARP 자식창을 만들기 전이므로 ARP 화면 자체가 이 목록에 들어가지 않는다.
    for (CWnd* child = GetWindow(GW_CHILD); child; child = child->GetNextWindow()) {
        CRect rect; child->GetWindowRect(&rect); ScreenToClient(&rect);
        if (rect.top >= page.top && rect.bottom <= page.bottom && rect.left >= page.left && rect.right <= page.right)
            m_chatControls.push_back(child->GetSafeHwnd());
    }
    m_arpDialog.Attach(m_ARP,m_ARP2);
    if (!m_arpDialog.Create(IDD_ARP_DIALOG, this)) {
        AfxMessageBox(_T("ARP 화면 생성 실패")); GetDlgItem(IDC_PAGE_ARP)->EnableWindow(FALSE); return;
    }
    m_arpDialog.MoveWindow(page);
    m_ARP->SetNotifyWindow(m_hWnd); m_IP->SetNotifyWindow(m_hWnd);
    m_ARP2->SetNotifyWindow(m_hWnd); m_IP2->SetNotifyWindow(m_hWnd); m_router.SetNotifyWindow(m_hWnd);
    ((CEdit*)GetDlgItem(IDC_EDIT_SOURCE_IP))->SetLimitText(15);
    ((CEdit*)GetDlgItem(IDC_EDIT_DST))->SetLimitText(15);
    ((CEdit*)GetDlgItem(IDC_EDIT_SOURCE_IP2))->SetLimitText(15);
    SetDlgItemText(IDC_EDIT_SUBNET_MASK,_T("255.255.255.0"));
    SetDlgItemText(IDC_EDIT_SUBNET_MASK2,_T("255.255.255.0"));
    if (m_NI2->LoadAdapters()) {
        for (int i = 0; i < m_NI2->GetAdapterCount(); ++i) m_AdapterCombo2.AddString(m_NI2->GetAdapterName(i));
        m_AdapterCombo2.SetCurSel(m_NI2->GetAdapterCount() > 1 ? 1 : 0);
        m_AdapterCombo2.SetDroppedWidth(m_AdapterCombo.GetDroppedWidth());
    }
    ShowIpPage(false); RefreshIpControls();
}
void Cipc2019Dlg::ShowIpPage(bool arp)
{
    for (HWND window : m_chatControls) ::ShowWindow(window, arp ? SW_HIDE : SW_SHOW);
    if (m_arpDialog.GetSafeHwnd()) m_arpDialog.ShowWindow(arp ? SW_SHOW : SW_HIDE);
    GetDlgItem(IDC_PAGE_CHAT)->EnableWindow(arp);
    GetDlgItem(IDC_PAGE_ARP)->EnableWindow(!arp && m_arpDialog.GetSafeHwnd());
    if (arp) m_arpDialog.RefreshTables();
}
void Cipc2019Dlg::OnShowChatPage() { ShowIpPage(false); }
void Cipc2019Dlg::OnShowArpPage() { ShowIpPage(true); }

void Cipc2019Dlg::RefreshIpControls()
{
    const BOOL busy = (m_FileApp && m_FileApp->IsSending()) || !m_pendingFile.IsEmpty();
    const BOOL usable = m_bSendReady && m_ARP && m_ARP->IsUsable();
    const BOOL second = ((CButton*)GetDlgItem(IDC_ENABLE_ROUTING))->GetCheck();
    const BOOL broadcast = ((CButton*)GetDlgItem(IDC_CHECK_TOALL))->GetCheck();
    m_AdapterCombo.EnableWindow(!m_bSendReady);
    GetDlgItem(IDC_EDIT_SOURCE_IP)->EnableWindow(!m_bSendReady);
    GetDlgItem(IDC_EDIT_SUBNET_MASK)->EnableWindow(!m_bSendReady);
    GetDlgItem(IDC_ENABLE_ROUTING)->EnableWindow(!m_bSendReady);
    m_AdapterCombo2.EnableWindow(!m_bSendReady && second);
    GetDlgItem(IDC_EDIT_SOURCE_IP2)->EnableWindow(!m_bSendReady && second);
    GetDlgItem(IDC_EDIT_SUBNET_MASK2)->EnableWindow(!m_bSendReady && second);
    GetDlgItem(IDC_EDIT_DST)->EnableWindow(!busy && !broadcast);
    GetDlgItem(IDC_CHECK_TOALL)->EnableWindow(!busy);
    GetDlgItem(IDC_BUTTON_SEND)->EnableWindow(usable);
    GetDlgItem(IDC_EDIT_MSG)->EnableWindow(usable);
    GetDlgItem(IDC_BUTTON_FILE_SEND)->EnableWindow(usable && !busy);
    m_arpDialog.SetGarpAllowed(!busy);
    SetDlgItemText(IDC_BUTTON_ADDR, m_bSendReady ? _T("연결 해제") : _T("연결(&O)"));
}
void Cipc2019Dlg::OnRoutingModeChanged() { RefreshIpControls(); if (!m_bSendReady) OnSecondAdapterChanged(); }
void Cipc2019Dlg::OnSecondAdapterChanged()
{
    if (!m_NI2 || m_bSendReady || !((CButton*)GetDlgItem(IDC_ENABLE_ROUTING))->GetCheck()) return;
    if (!m_NI2->OpenAdapter(m_AdapterCombo2.GetCurSel())) { SetDlgItemText(IDC_STATIC_NETWORK_STATUS,m_NI2->GetError()); return; }
    unsigned char mac[6]; m_NI2->GetMacAddress(mac); SetDlgItemText(IDC_EDIT_SOURCE_MAC2,FormatMac(mac));
}

void Cipc2019Dlg::SetIpNetworkAddress()
{
    if (!m_IP || !m_ARP || !m_NI) return;
    if (m_bSendReady) {
        if (m_FileApp->IsSending()) { AfxMessageBox(_T("파일 송신이 끝난 뒤 연결을 해제하세요.")); return; }
        // [assignment6] 설정값/캐시를 바꾸기 전에 NI를 join한다. 수신 중 파일은 기존 부분 파일 정리를 사용한다.
        m_router.Suspend(); m_NI->CloseAdapter(); m_NI2->CloseAdapter();
        m_router.Reset(); m_IP2->Reset(); m_ARP2->Reset(); m_secondConnected = false;
        m_pendingFile.Empty(); m_IP->Reset(); m_ARP->Reset();
        m_ChatApp->ResetNetworkReceive(); m_FileApp->ResetReceive();
        MSG pending;
        while (::PeekMessage(&pending, m_hWnd, WM_FILE_STATUS, WM_FILE_STATUS, PM_REMOVE))
            OnFileStatus(pending.wParam, pending.lParam);
        if (m_receiveView.hasStatus && !m_receiveView.latest.finished) {
            m_receiveView.latest.finished = TRUE;
            m_receiveView.latest.message = _T("연결 해제로 파일 수신 중단");
            m_receiveView.latest.reportedAtMs = GetTickCount64();
        }
        m_bSendReady = FALSE;
        // [assignment6] 이전 연결에서 큐에 남은 상태가 새 연결의 상태를 덮지 않도록 비운다.
        while (::PeekMessage(&pending, m_hWnd, WM_NETWORK_EVENT, WM_NETWORK_EVENT, PM_REMOVE))
            delete reinterpret_cast<CString*>(pending.lParam);
        m_arpDialog.SetConnection(false, _T(""), m_sourceMac);
        m_arpDialog.SetConnection(1,false,_T(""),_T(""));
        SetDlgItemText(IDC_STATIC_NETWORK_STATUS, _T("네트워크 연결 해제"));
        SetDlgItemText(IDC_STATIC_FILE_STATUS, _T("연결 해제: 송신 대기 없음"));
        RefreshIpControls(); return;
    }
    CString text; GetDlgItemText(IDC_EDIT_SOURCE_IP, text);
    uint32_t localIp;
    if (!NetworkAddress::ParseIp(text, localIp) || !NetworkPackets::IsUnicastIp(localIp)) {
        AfxMessageBox(_T("내 IPv4 주소를 입력하세요. 이 값은 프로그램에서 사용할 주소입니다.")); return;
    }
    uint32_t mask, secondIp = 0, secondMask = 0;
    GetDlgItemText(IDC_EDIT_SUBNET_MASK,text);
    if (!NetworkAddress::ParseIp(text,mask) || !NetworkPackets::IsHostOnSubnet(localIp,localIp,mask)) {
        AfxMessageBox(_T("NIC 1 mask(/1~ /30)와 host IP를 확인하세요. network/broadcast IP는 사용할 수 없습니다.")); return;
    }
    const bool second = ((CButton*)GetDlgItem(IDC_ENABLE_ROUTING))->GetCheck() != 0;
    if (second) {
        if (m_NI->GetAdapterId(m_AdapterCombo.GetCurSel()) == m_NI2->GetAdapterId(m_AdapterCombo2.GetCurSel())) {
            AfxMessageBox(_T("PARP 중계는 서로 다른 물리 NIC를 선택해야 합니다.")); return;
        }
        GetDlgItemText(IDC_EDIT_SOURCE_IP2,text);
        if (!NetworkAddress::ParseIp(text,secondIp)) { AfxMessageBox(_T("NIC 2 IP를 확인하세요.")); return; }
        GetDlgItemText(IDC_EDIT_SUBNET_MASK2,text);
        if (!NetworkAddress::ParseIp(text,secondMask) || !NetworkPackets::IsHostOnSubnet(secondIp,secondIp,secondMask) ||
            (localIp & mask) == (secondIp & mask) || (localIp & secondMask) == (secondIp & secondMask)) {
            AfxMessageBox(_T("두 NIC에는 서로 겹치지 않는 subnet의 host IP/mask를 지정하세요.")); return;
        }
    }
    if (!m_NI->OpenAdapter(m_AdapterCombo.GetCurSel())) { AfxMessageBox(m_NI->GetError()); return; }
    unsigned char mac[6]; m_NI->GetMacAddress(mac);
    m_Ethernet->SetSourceAddress(mac);
    m_IP->Configure(localIp, m_ARP);
    m_ARP->Configure(localIp, mac, m_IP);
    m_router.BindInterface(0,localIp,mask,m_ARP,m_Ethernet); m_ARP->SetRouter(&m_router,0); m_IP->SetRouter(&m_router,0);
    unsigned char secondMac[6] = {};
    if (second) {
        if (!m_NI2->OpenAdapter(m_AdapterCombo2.GetCurSel())) {
            AfxMessageBox(m_NI2->GetError()); m_NI->CloseAdapter(); m_router.Reset(); m_IP->Reset(); m_ARP->Reset(); return;
        }
        m_NI2->GetMacAddress(secondMac); m_Ethernet2->SetSourceAddress(secondMac);
        m_IP2->Configure(secondIp,m_ARP2); m_ARP2->Configure(secondIp,secondMac,m_IP2);
        m_router.BindInterface(1,secondIp,secondMask,m_ARP2,m_Ethernet2);
        m_ARP2->SetRouter(&m_router,1); m_IP2->SetRouter(&m_router,1);
    }
    // [assignment7] 캐시/주소/중계 객체를 준비한 뒤 Probe를 시작한다. 검사 완료 전 앱은 활성화하지 않는다.
    const bool checksStarted = m_ARP->BeginAddressCheck(mac) && (!second || m_ARP2->BeginAddressCheck(secondMac));
    if (!checksStarted || !m_NI->StartReceive() || (second && !m_NI2->StartReceive())) {
        AfxMessageBox(_T("NI 수신 시작 실패")); m_router.Suspend(); m_NI->CloseAdapter(); m_NI2->CloseAdapter();
        m_router.Reset(); m_IP->Reset(); m_ARP->Reset(); m_IP2->Reset(); m_ARP2->Reset(); return;
    }
    m_sourceIp = NetworkAddress::FormatIp(localIp); m_sourceMac = FormatMac(mac); m_bSendReady = TRUE;
    SetDlgItemText(IDC_EDIT_SOURCE_IP, m_sourceIp); SetDlgItemText(IDC_EDIT_SRC, m_sourceMac);
    CString device; device.Format(_T("NIC %d"), m_AdapterCombo.GetCurSel() + 1);
    m_arpDialog.SetConnection(true, device, m_sourceMac);
    m_secondConnected = second;
    m_arpDialog.SetConnection(1,second,_T("NIC 2"),second ? FormatMac(secondMac) : CString());
    if (second) { SetDlgItemText(IDC_EDIT_SOURCE_IP2,NetworkAddress::FormatIp(secondIp)); SetDlgItemText(IDC_EDIT_SOURCE_MAC2,FormatMac(secondMac)); }
    SetDlgItemText(IDC_STATIC_NETWORK_STATUS, _T("주소 충돌 검사 중: 약 4~7초 후 GARP 광고와 함께 사용 가능합니다."));
    RefreshIpControls();
}

bool Cipc2019Dlg::PrepareIpDestination()
{
    if (!m_IP || !m_bSendReady) return false;
    uint32_t destination;
    if (((CButton*)GetDlgItem(IDC_CHECK_TOALL))->GetCheck()) destination = IPV4_BROADCAST;
    else {
        CString text; GetDlgItemText(IDC_EDIT_DST, text);
        if (!NetworkAddress::ParseIp(text, destination) || !NetworkPackets::IsUnicastIp(destination)) {
            AfxMessageBox(_T("상대 IPv4 주소를 입력하세요.")); return false;
        }
    }
    // [assignment6] 파일 작업 중 같은 상대에게 채팅은 가능하지만 목적지를 바꿔 파일 조각을 분산시키지 않는다.
    if ((m_FileApp->IsSending() || !m_pendingFile.IsEmpty()) && destination != m_IP->GetDestination()) {
        AfxMessageBox(_T("파일 송신 중에는 상대 IP를 변경할 수 없습니다.")); return false;
    }
    if (!m_IP->SetDestination(destination)) {
        AfxMessageBox(_T("내 IP로는 전송할 수 없습니다. 이전 채팅의 ARP 대기 중에는 상대 변경도 제한됩니다."));
        return false;
    }
    return true;
}

void Cipc2019Dlg::SendIpFile()
{
    if (!m_FileApp || !m_bSendReady || m_FileApp->IsSending() || !m_pendingFile.IsEmpty()) return;
    if (m_filePath.IsEmpty()) { AfxMessageBox(_T("전송할 파일을 선택하세요.")); return; }
    if (!PrepareIpDestination()) return;
    if (m_IP->IsDestinationResolved()) { StartIpFile(m_filePath); return; }
    // [assignment6] ARP 응답을 기다리는 동안 UI를 막지 않고 파일 경로만 보관한다.
    // [assignment6] 실제 파일은 MAC 확인 후 worker가 열기 때문에 큰 파일을 메모리 큐에 넣지 않는다.
    m_pendingFile = m_filePath; m_fileResolveStarted = GetTickCount64();
    m_sendView = FILE_VIEW(); m_FileProgress.SetPos(0);
    SetDlgItemText(IDC_STATIC_FILE_STATUS, _T("상대 MAC 확인 중 (ARP Reply 대기)"));
    if (!m_IP->RequestDestination()) {
        m_pendingFile.Empty(); SetDlgItemText(IDC_STATIC_FILE_STATUS, _T("ARP Request 송신 실패"));
    }
    RefreshIpControls();
}
void Cipc2019Dlg::StartIpFile(const CString& path)
{
    m_FileProgress.SetPos(0); m_sendView = FILE_VIEW();
    SetDlgItemText(IDC_STATIC_FILE_STATUS, _T("주소 확인 완료 · 파일 송신 준비 중"));
    if (!m_FileApp->StartSendFile(path)) SetDlgItemText(IDC_STATIC_FILE_STATUS, _T("파일 송신 스레드 시작 실패"));
    RefreshIpControls();
}
void Cipc2019Dlg::PollIpNetwork()
{
    if (!m_bSendReady || !m_ARP || !m_IP) return;
    const ULONGLONG now = GetTickCount64();
    m_ARP->Tick(now); m_IP->Tick(now);
    if (m_secondConnected) { m_ARP2->Tick(now); m_IP2->Tick(now); }
    m_router.Tick(now); RefreshIpControls();
    // [assignment7] GARP로 논리 MAC이 바뀌면 공통 표시/Proxy Reply MAC도 같은 값으로 갱신한다.
    unsigned char mac[6]; m_ARP->GetEffectiveMac(mac); m_sourceMac = FormatMac(mac); SetDlgItemText(IDC_EDIT_SRC,m_sourceMac);
    if (m_secondConnected) { m_ARP2->GetEffectiveMac(mac); SetDlgItemText(IDC_EDIT_SOURCE_MAC2,FormatMac(mac)); }
    m_arpDialog.RefreshTables();
    const CIPRouter::STATS stats = m_router.GetStats();
    CString routerStatus; routerStatus.Format(_T("중계 송신 %llu / 대기 %llu / 폐기 %llu"),
        static_cast<unsigned long long>(stats.forwarded),static_cast<unsigned long long>(stats.waiting),
        static_cast<unsigned long long>(stats.dropped)); SetDlgItemText(IDC_ROUTER_STATUS,routerStatus);
    if (m_pendingFile.IsEmpty()) return;
    if (!m_ARP->IsUsable()) {
        m_pendingFile.Empty(); SetDlgItemText(IDC_STATIC_FILE_STATUS,_T("주소 검사/충돌로 파일 송신 대기 취소")); RefreshIpControls(); return;
    }
    if (now - m_fileResolveStarted >= IP_RESOLVE_TIMEOUT_MS) {
        m_pendingFile.Empty();
        SetDlgItemText(IDC_STATIC_FILE_STATUS, _T("ARP 응답 없음: 파일 송신을 시작하지 못했습니다."));
        RefreshIpControls();
    } else if (m_IP->IsDestinationResolved()) {
        CString path(m_pendingFile); m_pendingFile.Empty(); StartIpFile(path);
    } else m_IP->RequestDestination(); // [assignment6] ARP 계층에서 1초 간격으로 중복 요청을 제한한다.
}
LRESULT Cipc2019Dlg::OnArpChanged(WPARAM, LPARAM)
{
    m_arpDialog.RefreshTables(); return 0;
}
LRESULT Cipc2019Dlg::OnNetworkEvent(WPARAM, LPARAM lParam)
{
    CString* text = reinterpret_cast<CString*>(lParam);
    if (text) {
        SetDlgItemText(IDC_STATIC_NETWORK_STATUS, *text);
        m_arpDialog.SetStatus(*text); AppendChatMessage(*text); delete text;
    }
    return 0;
}
