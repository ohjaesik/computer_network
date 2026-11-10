#include "pch.h"
#include "RouterDlg.h"
#include "NetworkAddress.h"

BEGIN_MESSAGE_MAP(CRouterDlg, CDialogEx)
    ON_BN_CLICKED(IDC_STATIC_ROUTER_MODE,&CRouterDlg::OnModeChanged)
    ON_BN_CLICKED(IDC_ROUTE_ADD,&CRouterDlg::OnAdd)
    ON_BN_CLICKED(IDC_ROUTE_DELETE,&CRouterDlg::OnDelete)
    ON_BN_CLICKED(IDC_ROUTE_CACHE_DELETE,&CRouterDlg::OnCacheDelete)
END_MESSAGE_MAP()

BOOL CRouteEntryDlg::OnInitDialog()
{
    CDialogEx::OnInitDialog();
    for (int id : {IDC_ROUTE_DESTINATION,IDC_ROUTE_MASK,IDC_ROUTE_GATEWAY})
        static_cast<CEdit*>(GetDlgItem(id))->SetLimitText(15);
    static_cast<CEdit*>(GetDlgItem(IDC_ROUTE_METRIC))->SetLimitText(5);
    auto* ports = static_cast<CComboBox*>(GetDlgItem(IDC_ROUTE_INTERFACE));
    ports->AddString(_T("Port 0 (NIC 1)")); ports->AddString(_T("Port 1 (NIC 2)")); ports->SetCurSel(0);
    CheckDlgButton(IDC_ROUTE_UP,BST_CHECKED); SetDlgItemText(IDC_ROUTE_METRIC,_T("1"));
    return TRUE;
}
void CRouteEntryDlg::OnOK()
{
    CString text;
    GetDlgItemText(IDC_ROUTE_DESTINATION,text);
    if (!NetworkAddress::ParseIp(text,route.destination)) { AfxMessageBox(_T("Destination IPv4 주소를 입력하세요.")); return; }
    GetDlgItemText(IDC_ROUTE_MASK,text);
    if (!NetworkAddress::ParseIp(text,route.mask)) { AfxMessageBox(_T("Netmask를 입력하세요. /0은 0.0.0.0, /32는 255.255.255.255입니다.")); return; }
    route.flags = (IsDlgButtonChecked(IDC_ROUTE_UP) == BST_CHECKED ? ROUTE_FLAG_UP : 0) |
        (IsDlgButtonChecked(IDC_ROUTE_G) == BST_CHECKED ? ROUTE_FLAG_GATEWAY : 0) |
        (IsDlgButtonChecked(IDC_ROUTE_H) == BST_CHECKED ? ROUTE_FLAG_HOST : 0);
    GetDlgItemText(IDC_ROUTE_GATEWAY,text); text.Trim();
    route.gateway = 0;
    if ((route.flags & ROUTE_FLAG_GATEWAY) && !NetworkAddress::ParseIp(text,route.gateway)) {
        AfxMessageBox(_T("G를 체크했으면 Gateway IP가 필요합니다.")); return;
    }
    // [assignment7][static-router] 빈 문자열/음수/숫자 뒤 문자/범위 초과가 조용히 0으로 바뀌지 않도록 한 자리씩 검사한다.
    GetDlgItemText(IDC_ROUTE_METRIC,text); text.Trim();
    unsigned int metric = 0;
    if (text.IsEmpty()) { AfxMessageBox(_T("Metric(0~65535)을 입력하세요.")); return; }
    for (int i = 0; i < text.GetLength(); ++i) {
        if (text[i] < _T('0') || text[i] > _T('9') || metric > ROUTE_MAX_METRIC / 10) {
            AfxMessageBox(_T("Metric은 0~65535의 정수입니다.")); return;
        }
        metric = metric * 10 + (text[i] - _T('0'));
        if (metric > ROUTE_MAX_METRIC) { AfxMessageBox(_T("Metric은 0~65535의 정수입니다.")); return; }
    }
    route.metric = metric;
    route.outgoing = static_cast<CComboBox*>(GetDlgItem(IDC_ROUTE_INTERFACE))->GetCurSel();
    CString error;
    if (!m_router || !m_router->AddStaticRoute(route,error)) { AfxMessageBox(error); return; }
    CDialogEx::OnOK();
}

void CRouterDlg::Attach(CIPRouter* router, CARPLayer* first, CARPLayer* second)
{
    m_router = router; m_arps[0] = first; m_arps[1] = second;
}
void CRouterDlg::DoDataExchange(CDataExchange* dx)
{
    CDialogEx::DoDataExchange(dx);
    DDX_Control(dx,IDC_ROUTE_TABLE,m_routes); DDX_Control(dx,IDC_ROUTE_ARP_CACHE,m_cache);
}
BOOL CRouterDlg::OnInitDialog()
{
    CDialogEx::OnInitDialog();
    m_routes.SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
    m_cache.SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
    CRect rect; m_routes.GetClientRect(&rect);
    int width = rect.Width() - GetSystemMetrics(SM_CXVSCROLL) - 6;
    const TCHAR* names[] = {_T("Destination"),_T("Netmask"),_T("Gateway"),_T("Flag"),_T("Interface"),_T("Metric")};
    const int sizes[] = {24,24,24,8,12,8};
    for (int i = 0; i < 6; ++i) m_routes.InsertColumn(i,names[i],LVCFMT_LEFT,width * sizes[i] / 100);
    m_cache.GetClientRect(&rect); width = rect.Width() - GetSystemMetrics(SM_CXVSCROLL) - 6;
    const TCHAR* cacheNames[] = {_T("IP Address"),_T("Ethernet Address"),_T("IF"),_T("State")};
    const int cacheSizes[] = {30,39,8,23};
    for (int i = 0; i < 4; ++i) m_cache.InsertColumn(i,cacheNames[i],LVCFMT_LEFT,width * cacheSizes[i] / 100);
    SetConnection(false,false);
    SetStatus(_T("Static Router 모드를 선택하고 공통 설정에서 서로 다른 유선 NIC 2개와 IP/Mask를 입력하세요."));
    RefreshTables(); return TRUE;
}
void CRouterDlg::SetConnection(bool connected, bool staticMode)
{
    m_connected = connected;
    if (!GetSafeHwnd()) return;
    CheckDlgButton(IDC_STATIC_ROUTER_MODE,staticMode ? BST_CHECKED : BST_UNCHECKED);
    GetDlgItem(IDC_STATIC_ROUTER_MODE)->EnableWindow(!connected);
    GetDlgItem(IDC_ROUTE_CACHE_DELETE)->EnableWindow(connected);
}
void CRouterDlg::SetStatus(const CString& message)
{
    if (GetSafeHwnd()) SetDlgItemText(IDC_ROUTE_STATUS,message);
}
void CRouterDlg::OnModeChanged()
{
    if (m_connected || !GetParent()) return;
    GetParent()->SendMessage(WM_STATIC_ROUTER_MODE,IsDlgButtonChecked(IDC_STATIC_ROUTER_MODE) == BST_CHECKED);
}
void CRouterDlg::RefreshTables()
{
    if (!GetSafeHwnd() || !m_router) return;
    const int selected = m_routes.GetNextItem(-1,LVNI_SELECTED);
    const uint32_t selectedId = selected < 0 ? 0 : static_cast<uint32_t>(m_routes.GetItemData(selected));
    const int cacheSelected = m_cache.GetNextItem(-1,LVNI_SELECTED);
    const CACHE_ROW key = cacheSelected >= 0 && cacheSelected < static_cast<int>(m_cacheRows.size()) ?
        m_cacheRows[cacheSelected] : CACHE_ROW{-1,0};
    std::vector<CIPRouter::STATIC_ROUTE> routes; m_router->GetStaticRoutes(routes);
    // [assignment7][static-router] 두 표의 선택 키를 보존한다. 캐시는 IP가 같아도 인터페이스가 다르면 서로 다른 행이다.
    m_routes.SetRedraw(FALSE); m_cache.SetRedraw(FALSE);
    m_routes.DeleteAllItems(); m_cache.DeleteAllItems(); m_cacheRows.clear();
    for (const auto& route : routes) {
        int row = m_routes.InsertItem(m_routes.GetItemCount(),NetworkAddress::FormatIp(route.destination));
        m_routes.SetItemText(row,1,NetworkAddress::FormatIp(route.mask));
        m_routes.SetItemText(row,2,(route.flags & ROUTE_FLAG_GATEWAY) ? NetworkAddress::FormatIp(route.gateway) : CString(_T("-")));
        CString flags;
        if (route.flags & ROUTE_FLAG_UP) flags += _T("U");
        if (route.flags & ROUTE_FLAG_GATEWAY) flags += _T("G");
        if (route.flags & ROUTE_FLAG_HOST) flags += _T("H");
        m_routes.SetItemText(row,3,flags.IsEmpty() ? CString(_T("Down")) : flags);
        CString value; value.Format(_T("Port %d"),route.outgoing); m_routes.SetItemText(row,4,value);
        value.Format(_T("%u"),route.metric); m_routes.SetItemText(row,5,value);
        m_routes.SetItemData(row,route.id);
        if (route.id == selectedId) m_routes.SetItemState(row,LVIS_SELECTED,LVIS_SELECTED);
    }
    for (int port = 0; port < ROUTER_MAX_INTERFACES; ++port) {
        std::vector<ARP_CACHE_ENTRY> cache; std::vector<ARP_PROXY_ENTRY> proxies;
        if (m_arps[port]) m_arps[port]->GetSnapshot(cache,proxies);
        for (const auto& entry : cache) {
            int row = m_cache.InsertItem(m_cache.GetItemCount(),NetworkAddress::FormatIp(entry.ip));
            m_cache.SetItemText(row,1,entry.complete ? NetworkAddress::FormatMac(entry.mac) : CString(_T("-")));
            CString value; value.Format(_T("%d"),port); m_cache.SetItemText(row,2,value);
            m_cache.SetItemText(row,3,entry.complete ? _T("Complete") : _T("Incomplete"));
            m_cacheRows.push_back(CACHE_ROW{port,entry.ip});
            if (key.port == port && key.ip == entry.ip) m_cache.SetItemState(row,LVIS_SELECTED,LVIS_SELECTED);
        }
    }
    m_routes.SetRedraw(TRUE); m_cache.SetRedraw(TRUE); m_routes.Invalidate(); m_cache.Invalidate();
    const auto stats = m_router->GetStats(); CString counters;
    counters.Format(_T("Forwarded %llu  /  Waiting %llu  /  Dropped %llu"),
        static_cast<unsigned long long>(stats.forwarded),static_cast<unsigned long long>(stats.waiting),
        static_cast<unsigned long long>(stats.dropped));
    SetDlgItemText(IDC_ROUTE_COUNTERS,counters);
}
void CRouterDlg::OnAdd()
{
    if (!m_router) return;
    // [assignment7][static-router] 검증 오류 때 입력을 잃지 않게 modal 입력창은 유지하고 오류만 표시한다.
    CRouteEntryDlg editor(m_router,this);
    if (editor.DoModal() != IDOK) return;
    SetStatus(_T("수동 경로 추가 완료. U는 목적지 IP, UG는 Gateway IP의 MAC을 ARP로 확인합니다."));
    RefreshTables();
}
void CRouterDlg::OnDelete()
{
    const int row = m_routes.GetNextItem(-1,LVNI_SELECTED);
    if (row >= 0 && m_router) m_router->DeleteStaticRoute(static_cast<uint32_t>(m_routes.GetItemData(row)));
    RefreshTables();
}
void CRouterDlg::OnCacheDelete()
{
    const int row = m_cache.GetNextItem(-1,LVNI_SELECTED);
    if (row >= 0 && row < static_cast<int>(m_cacheRows.size())) {
        const auto key = m_cacheRows[row];
        if (m_arps[key.port]) m_arps[key.port]->DeleteEntry(key.ip);
    }
    RefreshTables();
}
