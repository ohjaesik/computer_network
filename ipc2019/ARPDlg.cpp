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
END_MESSAGE_MAP()

void CARPDlg::DoDataExchange(CDataExchange* dx)
{
    CDialogEx::DoDataExchange(dx);
    DDX_Control(dx, IDC_ARP_CACHE, m_cache);
    DDX_Control(dx, IDC_PROXY_CACHE, m_proxy);
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
    m_proxy.InsertColumn(0, _T("Device"), LVCFMT_LEFT, proxyWidth * 20 / 100);
    m_proxy.InsertColumn(1, _T("IP Address"), LVCFMT_LEFT, proxyWidth * 34 / 100);
    m_proxy.InsertColumn(2, _T("Ethernet Address"), LVCFMT_LEFT, proxyWidth * 46 / 100);
    SetConnection(false, _T(""), _T(""));
    return TRUE;
}
void CARPDlg::SetConnection(bool connected, const CString& device, const CString& mac)
{
    m_connected = connected; m_device = device; m_mac = mac;
    if (!GetSafeHwnd()) return;
    const int ids[] = {IDC_ARP_SEND, IDC_ARP_DELETE, IDC_ARP_CLEAR, IDC_PROXY_ADD,
        IDC_PROXY_DELETE, IDC_GARP_SEND, IDC_ARP_TARGET, IDC_PROXY_IP, IDC_GARP_MAC};
    for (int id : ids) GetDlgItem(id)->EnableWindow(connected);
    SetDlgItemText(IDC_GARP_MAC, mac);
    SetStatus(connected ? _T("ARP 요청 대기 중") : _T("공통 설정에서 어댑터와 내 IP를 설정하고 연결하세요."));
    RefreshTables();
}
void CARPDlg::RefreshTables()
{
    if (!GetSafeHwnd()) return;
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
    if (!m_layer->AddProxy(ip, m_device)) AfxMessageBox(_T("Proxy 등록 실패: 내 IP, 중복 항목 또는 표 크기를 확인하세요."));
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
    if (!m_layer || !m_connected) return;
    CString text; GetDlgItemText(IDC_GARP_MAC, text);
    unsigned char mac[6];
    if (!NetworkAddress::ParseMac(text, mac) || !NetworkPackets::IsUnicastMac(mac)) {
        AfxMessageBox(_T("유효한 unicast MAC 주소를 입력하세요.")); return;
    }
    // [assignment6] 이 입력은 광고 패킷의 MAC이다. 실제 NIC 설정/채팅·파일의 출발지 MAC은 변경하지 않는다.
    m_layer->SendGratuitous(mac);
}
