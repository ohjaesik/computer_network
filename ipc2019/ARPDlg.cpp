#include "pch.h"
#include "ARPDlg.h"
#include "NetworkAddress.h"

BEGIN_MESSAGE_MAP(CARPDlg, CDialogEx)
    ON_BN_CLICKED(IDC_ARP_SEND, &CARPDlg::OnRequest)
    ON_BN_CLICKED(IDC_ARP_DELETE, &CARPDlg::OnDelete)
    ON_BN_CLICKED(IDC_ARP_CLEAR, &CARPDlg::OnClear)
    ON_BN_CLICKED(IDC_PROXY_ADD, &CARPDlg::OnProxyAdd)
    ON_BN_CLICKED(IDC_PROXY_DELETE, &CARPDlg::OnProxyDelete)
    ON_BN_CLICKED(IDC_GARP_SEND, &CARPDlg::OnGarp)
    ON_CBN_SELCHANGE(IDC_ARP_INTERFACE, &CARPDlg::OnInterfaceChanged)
END_MESSAGE_MAP()

void CARPDlg::DoDataExchange(CDataExchange* dx)
{
    CDialogEx::DoDataExchange(dx);
    DDX_Control(dx, IDC_ARP_CACHE, m_cache);
    DDX_Control(dx, IDC_PROXY_CACHE, m_proxy);
    DDX_Control(dx, IDC_ARP_INTERFACE, m_interfaceCombo);
    DDX_Control(dx, IDC_PROXY_OUTGOING, m_outgoingCombo);
}
BOOL CARPDlg::OnInitDialog()
{
    CDialogEx::OnInitDialog();
    m_cache.SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
    m_proxy.SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
    CRect rect; m_cache.GetClientRect(&rect);
    const int width = rect.Width() - GetSystemMetrics(SM_CXVSCROLL) - 6;
    m_cache.InsertColumn(0, _T("IP Address"), LVCFMT_LEFT, width * 31 / 100);
    m_cache.InsertColumn(1, _T("Ethernet Address"), LVCFMT_LEFT, width * 43 / 100);
    m_cache.InsertColumn(2, _T("Status"), LVCFMT_LEFT, width * 26 / 100);
    m_proxy.GetClientRect(&rect);
    const int proxyWidth = rect.Width() - GetSystemMetrics(SM_CXVSCROLL) - 6;
    m_proxy.InsertColumn(0, _T("출력 NIC"), LVCFMT_LEFT, proxyWidth * 18 / 100);
    m_proxy.InsertColumn(1, _T("Proxy IP"), LVCFMT_LEFT, proxyWidth * 24 / 100);
    m_proxy.InsertColumn(2, _T("Reply MAC"), LVCFMT_LEFT, proxyWidth * 34 / 100);
    m_proxy.InsertColumn(3, _T("Next hop"), LVCFMT_LEFT, proxyWidth * 24 / 100);
    for (int i = 0; i < 2; ++i) {
        CString label; label.Format(_T("NIC %d"),i+1);
        m_interfaceCombo.AddString(label); m_outgoingCombo.AddString(label);
    }
    m_interfaceCombo.SetCurSel(0); m_outgoingCombo.SetCurSel(1);
    SetConnection(false, _T(""), _T(""));
    return TRUE;
}
void CARPDlg::SetConnection(bool connected, const CString& device, const CString& mac)
{
    SetConnection(0,connected,device,mac);
}
void CARPDlg::SetConnection(int index, bool connected, const CString& device, const CString& mac)
{
    if (index < 0 || index >= 2) return;
    m_connections[index] = connected; m_devices[index] = device; m_macs[index] = mac;
    if (!GetSafeHwnd()) return;
    if (index == m_selected) OnInterfaceChanged();
}
void CARPDlg::OnInterfaceChanged()
{
    m_selected = (std::max)(0,m_interfaceCombo.GetCurSel());
    m_layer = m_layers[m_selected]; m_connected = m_connections[m_selected];
    m_device = m_devices[m_selected]; m_mac = m_macs[m_selected];
    SetDlgItemText(IDC_GARP_MAC,m_mac); m_outgoingCombo.SetCurSel(1-m_selected);
    SetStatus(m_connected ? _T("선택 NIC의 ARP/GARP 상태를 표시합니다.") : _T("선택 NIC가 연결되지 않았습니다."));
    RefreshTables();
}
void CARPDlg::RefreshControls()
{
    const bool usable = m_connected && m_layer && m_layer->IsUsable();
    const int ids[] = {IDC_ARP_DELETE,IDC_ARP_CLEAR,IDC_PROXY_DELETE,IDC_ARP_TARGET,
        IDC_PROXY_IP,IDC_GARP_MAC,IDC_PROXY_OUTGOING,IDC_PROXY_NEXT_HOP};
    for (int id : ids) GetDlgItem(id)->EnableWindow(m_connected);
    GetDlgItem(IDC_ARP_SEND)->EnableWindow(usable);
    GetDlgItem(IDC_PROXY_ADD)->EnableWindow(usable && m_connections[1-m_selected] && m_layers[1-m_selected]->IsUsable());
    GetDlgItem(IDC_GARP_SEND)->EnableWindow(m_connected && m_garpAllowed && m_layer &&
        m_layer->GetAddressState() != CARPLayer::ADDRESS_STATE::Probing);
}
void CARPDlg::RefreshTables()
{
    if (!GetSafeHwnd()) return;
    RefreshControls();
    if (m_connected && m_layer) {
        unsigned char mac[6]; m_layer->GetEffectiveMac(mac);
        const CString effective = NetworkAddress::FormatMac(mac);
        if (m_mac != effective) { m_mac = effective; m_macs[m_selected] = effective; SetDlgItemText(IDC_GARP_MAC,effective); }
    }
    // [assignment6] 행을 갱신해도 IP 키로 선택 상태를 복원하여 수신 중 선택 삭제가 엉뚱한 행에 적용되지 않게 한다.
    int selected = m_cache.GetNextItem(-1, LVNI_SELECTED);
    uint32_t selectedIp = selected < 0 ? 0 : static_cast<uint32_t>(m_cache.GetItemData(selected));
    selected = m_proxy.GetNextItem(-1, LVNI_SELECTED);
    uint32_t proxyIp = selected < 0 ? 0 : static_cast<uint32_t>(m_proxy.GetItemData(selected));
    std::vector<ARP_CACHE_ENTRY> cache; std::vector<ARP_PROXY_ENTRY> proxies;
    if (m_layer) m_layer->GetSnapshot(cache, proxies);
    m_cache.SetRedraw(FALSE); m_proxy.SetRedraw(FALSE);
    m_cache.DeleteAllItems(); m_proxy.DeleteAllItems();
    for (const auto& entry : cache) {
        int row = m_cache.InsertItem(m_cache.GetItemCount(), NetworkAddress::FormatIp(entry.ip));
        m_cache.SetItemText(row, 1, entry.complete ? NetworkAddress::FormatMac(entry.mac) : CString(_T("????????????")));
        m_cache.SetItemText(row, 2, entry.complete ? _T("Complete") : _T("Incomplete"));
        m_cache.SetItemData(row, static_cast<DWORD_PTR>(entry.ip));
        if (entry.ip == selectedIp) m_cache.SetItemState(row, LVIS_SELECTED, LVIS_SELECTED);
    }
    for (const auto& entry : proxies) {
        int row = m_proxy.InsertItem(m_proxy.GetItemCount(), entry.device);
        m_proxy.SetItemText(row, 1, NetworkAddress::FormatIp(entry.ip));
        m_proxy.SetItemText(row, 2, m_mac);
        m_proxy.SetItemText(row, 3, entry.nextHop ? NetworkAddress::FormatIp(entry.nextHop) : CString(_T("Direct")));
        m_proxy.SetItemData(row, static_cast<DWORD_PTR>(entry.ip));
        if (entry.ip == proxyIp) m_proxy.SetItemState(row, LVIS_SELECTED, LVIS_SELECTED);
    }
    m_cache.SetRedraw(TRUE); m_proxy.SetRedraw(TRUE); m_cache.Invalidate(); m_proxy.Invalidate();
}
void CARPDlg::SetStatus(const CString& message)
{
    if (GetSafeHwnd()) SetDlgItemText(IDC_ARP_STATUS, message);
}
bool CARPDlg::ReadTarget(int id, uint32_t& ip)
{
    if (!m_layer || !m_connected) return false;
    CString text; GetDlgItemText(id, text);
    if (!NetworkAddress::ParseIp(text, ip) || !NetworkPackets::IsUnicastIp(ip)) {
        AfxMessageBox(_T("유효한 IPv4 주소를 입력하세요. 예: 192.168.10.2")); return false;
    }
    return true;
}
void CARPDlg::OnRequest()
{
    uint32_t ip; if (!ReadTarget(IDC_ARP_TARGET, ip)) return;
    SetStatus(m_layer->SendRequest(ip, true) ? _T("ARP Request 송신: Reply를 기다립니다.") : _T("ARP 요청 실패: 내 IP 또는 연결 상태를 확인하세요."));
    RefreshTables();
}
void CARPDlg::OnDelete()
{
    int row = m_cache.GetNextItem(-1, LVNI_SELECTED);
    if (m_layer && row >= 0) m_layer->DeleteEntry(static_cast<uint32_t>(m_cache.GetItemData(row)));
    RefreshTables();
}
void CARPDlg::OnClear()
{
    if (m_layer) m_layer->ClearCache(); RefreshTables();
}
void CARPDlg::OnProxyAdd()
{
    uint32_t ip; if (!ReadTarget(IDC_PROXY_IP, ip)) return;
    const int outgoing = m_outgoingCombo.GetCurSel();
    CString text; GetDlgItemText(IDC_PROXY_NEXT_HOP,text); text.Trim(); uint32_t nextHop = 0;
    if (!text.IsEmpty() && (!NetworkAddress::ParseIp(text,nextHop) || !NetworkPackets::IsUnicastIp(nextHop))) {
        AfxMessageBox(_T("다음 홉 IP를 확인하세요. 직접 연결된 대상이면 빈칸으로 둡니다.")); return;
    }
    CString device; device.Format(_T("NIC %d"),outgoing+1);
    // [assignment6] 표에 적는 MAC은 요청이 들어오는 NIC의 MAC, 선택하는 Device는 실제 출력 NIC다.
    if (!m_layer->AddProxy(ip,device,outgoing,nextHop))
        AfxMessageBox(_T("Proxy 등록 실패: 두 NIC 연결, 서로 다른 출력 NIC, next-hop subnet, 중복/방송/내 IP를 확인하세요."));
    RefreshTables();
}
void CARPDlg::OnProxyDelete()
{
    int row = m_proxy.GetNextItem(-1, LVNI_SELECTED);
    if (m_layer && row >= 0) m_layer->DeleteProxy(static_cast<uint32_t>(m_proxy.GetItemData(row)));
    RefreshTables();
}
void CARPDlg::OnGarp()
{
    if (!m_layer || !m_connected || !m_garpAllowed) return;
    CString text; GetDlgItemText(IDC_GARP_MAC, text);
    unsigned char mac[6];
    if (!NetworkAddress::ParseMac(text, mac) || !NetworkPackets::IsUnicastMac(mac)) {
        AfxMessageBox(_T("유효한 unicast MAC 주소를 입력하세요.")); return;
    }
    // [assignment6] NIC/OS 설정은 바꾸지 않지만 검사를 통과하면 raw 앱의 실제 MAC/필터도 변경된다.
    // [assignment6] 물리 NIC가 다른 unicast MAC을 허용해야 하므로 유선 어댑터의 promiscuous 캡처로 실습한다.
    if (!m_layer->SendGratuitous(mac)) SetStatus(_T("GARP 시작 실패: 진행 중 검사/연결/충돌 rate limit을 확인하세요."));
}
