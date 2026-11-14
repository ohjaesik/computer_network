#include "pch.h"
#include "StaticRouterDlg.h"
#include "NetworkAddress.h"

namespace {
    const int DEVICE_IDS[] = {IDC_ROUTER_INTERNAL_DEVICE,IDC_ROUTER_EXTERNAL_DEVICE};
    const int MAC_IDS[] = {IDC_ROUTER_INTERNAL_MAC,IDC_ROUTER_EXTERNAL_MAC};
    const int IP_IDS[] = {IDC_ROUTER_INTERNAL_IP,IDC_ROUTER_EXTERNAL_IP};
    const int MASK_IDS[] = {IDC_ROUTER_INTERNAL_MASK,IDC_ROUTER_EXTERNAL_MASK};
    CString PortName(int port) {
        return port == 0 ? CString(_T("Port 1 (내부)")) : CString(_T("Port 2 (외부)"));
    }
    uint64_t MacKey(const unsigned char* mac) {
        uint64_t key = 0; for (int i = 0; i < 6; ++i) key = (key << 8) | mac[i]; return key;
    }
    CRect WorkArea(HWND window) {
        MONITORINFO info = {}; info.cbSize = sizeof(info);
        if (::GetMonitorInfo(::MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST),&info)) return CRect(info.rcWork);
        RECT rect = {}; ::SystemParametersInfo(SPI_GETWORKAREA,0,&rect,0); return CRect(rect);
    }
    void RestoreTop(CListCtrl& list, int top) {
        if (list.GetItemCount()) list.EnsureVisible((std::min)(top,list.GetItemCount()-1),FALSE);
    }
}

BEGIN_MESSAGE_MAP(CRouterProxyEntryDlg,CDialogEx)
    ON_CBN_SELCHANGE(IDC_ROUTER_PROXY_DEVICE,&CRouterProxyEntryDlg::OnDeviceChanged)
END_MESSAGE_MAP()
CRouterProxyEntryDlg::CRouterProxyEntryDlg(CARPLayer* first, CARPLayer* second, CWnd* parent)
    : CDialogEx(IDD_ROUTER_PROXY_ENTRY,parent), m_arps{first,second} {}
BOOL CRouterProxyEntryDlg::OnInitDialog()
{
    CDialogEx::OnInitDialog();
    auto* devices = static_cast<CComboBox*>(GetDlgItem(IDC_ROUTER_PROXY_DEVICE));
    devices->AddString(PortName(0)); devices->AddString(PortName(1)); devices->SetCurSel(0);
    static_cast<CEdit*>(GetDlgItem(IDC_ROUTER_PROXY_IP))->SetLimitText(15);
    OnDeviceChanged(); return TRUE;
}
void CRouterProxyEntryDlg::OnDeviceChanged()
{
    const int port = static_cast<CComboBox*>(GetDlgItem(IDC_ROUTER_PROXY_DEVICE))->GetCurSel();
    if (port < 0 || port >= ROUTER_MAX_INTERFACES || !m_arps[port]) return;
    unsigned char mac[6]; m_arps[port]->GetEffectiveMac(mac);
    SetDlgItemText(IDC_ROUTER_PROXY_MAC,NetworkAddress::FormatMac(mac));
}
void CRouterProxyEntryDlg::OnOK()
{
    const int port = static_cast<CComboBox*>(GetDlgItem(IDC_ROUTER_PROXY_DEVICE))->GetCurSel();
    CString text; uint32_t ip; GetDlgItemText(IDC_ROUTER_PROXY_IP,text);
    if (port < 0 || port >= ROUTER_MAX_INTERFACES || !m_arps[port] || !NetworkAddress::ParseIp(text,ip) ||
        !NetworkPackets::IsUnicastIp(ip)) { AfxMessageBox(_T("Device와 Proxy IP를 확인하세요.")); return; }
    // [assignment7][static-router] Proxy 표는 대리 응답만 등록한다. 이 Add로 수동 Routing Table에 숨은 /32 경로를 만들지 않는다.
    if (!m_arps[port]->AddProxy(ip,PortName(port))) {
        AfxMessageBox(_T("Proxy 등록 실패: 연결/주소 검사, 중복, 자기 IP, network/broadcast 주소를 확인하세요.")); return;
    }
    CDialogEx::OnOK();
}

BEGIN_MESSAGE_MAP(CStaticRouterDlg,CDialogEx)
    ON_BN_CLICKED(IDC_ROUTER_START,&CStaticRouterDlg::OnStart)
    ON_BN_CLICKED(IDC_ROUTER_END,&CStaticRouterDlg::OnEnd)
    ON_CBN_SELCHANGE(IDC_ROUTER_INTERNAL_DEVICE,&CStaticRouterDlg::OnInternalChanged)
    ON_CBN_SELCHANGE(IDC_ROUTER_EXTERNAL_DEVICE,&CStaticRouterDlg::OnExternalChanged)
    ON_BN_CLICKED(IDC_ROUTER_ROUTE_ADD,&CStaticRouterDlg::OnRouteAdd)
    ON_BN_CLICKED(IDC_ROUTER_ROUTE_DELETE,&CStaticRouterDlg::OnRouteDelete)
    ON_BN_CLICKED(IDC_ROUTER_ARP_DELETE,&CStaticRouterDlg::OnArpDelete)
    ON_BN_CLICKED(IDC_ROUTER_PROXY_ADD,&CStaticRouterDlg::OnProxyAdd)
    ON_BN_CLICKED(IDC_ROUTER_PROXY_DELETE,&CStaticRouterDlg::OnProxyDelete)
    ON_MESSAGE(WM_ARP_CHANGED,&CStaticRouterDlg::OnArpChanged)
    ON_MESSAGE(WM_NETWORK_EVENT,&CStaticRouterDlg::OnNetworkEvent)
    ON_WM_TIMER()
    ON_WM_DESTROY()
    ON_WM_SIZE()
    ON_WM_GETMINMAXINFO()
END_MESSAGE_MAP()

CStaticRouterDlg::CStaticRouterDlg(CWnd* parent) : CDialogEx(IDD_STATIC_ROUTER_MAIN,parent)
{
    // [assignment7][static-router] NIC마다 NI/Ethernet/IP/ARP를 만들고 두 IP 계층이 같은 라우터를 사용한다.
    // [assignment7][static-router] 이 화면은 라우터 PC에서만 실행한다. Host A/B는 OS의 ping을 사용한다.
    m_layers.AddLayer(new CNILayer("NI")); m_layers.AddLayer(new CEthernetLayer("Ethernet"));
    m_layers.AddLayer(new CIPLayer("IP")); m_layers.AddLayer(new CARPLayer("ARP"));
    m_layers.AddLayer(new CNILayer("NI2")); m_layers.AddLayer(new CEthernetLayer("Ethernet2"));
    m_layers.AddLayer(new CIPLayer("IP2")); m_layers.AddLayer(new CARPLayer("ARP2"));
    m_layers.ConnectLayers("NI ( *Ethernet ( *IP *ARP ) )");
    m_layers.ConnectLayers("NI2 ( *Ethernet2 ( *IP2 *ARP2 ) )");
    const char* names[2][4] = {{"NI","Ethernet","IP","ARP"},{"NI2","Ethernet2","IP2","ARP2"}};
    for (int i = 0; i < ROUTER_MAX_INTERFACES; ++i) {
        m_ports[i].ni = static_cast<CNILayer*>(m_layers.GetLayer(names[i][0]));
        m_ports[i].ethernet = static_cast<CEthernetLayer*>(m_layers.GetLayer(names[i][1]));
        m_ports[i].ip = static_cast<CIPLayer*>(m_layers.GetLayer(names[i][2]));
        m_ports[i].arp = static_cast<CARPLayer*>(m_layers.GetLayer(names[i][3]));
        m_ports[i].ethernet->SetProtocolLayers(m_ports[i].ip,m_ports[i].arp);
    }
    m_router.SetStaticRouting(true);
}
CStaticRouterDlg::~CStaticRouterDlg()
{
    // [assignment7][static-router] Dialog 생성 실패 때도 worker/계층을 정리한다. OnDestroy 뒤 재호출되어도 안전하다.
    StopNetwork(); m_layers.DeAllocLayer();
}
void CStaticRouterDlg::DoDataExchange(CDataExchange* dx)
{
    CDialogEx::DoDataExchange(dx);
    DDX_Control(dx,IDC_ROUTER_INTERNAL_DEVICE,m_ports[0].devices);
    DDX_Control(dx,IDC_ROUTER_EXTERNAL_DEVICE,m_ports[1].devices);
    DDX_Control(dx,IDC_ROUTER_ROUTES,m_routes); DDX_Control(dx,IDC_ROUTER_ARP,m_cache);
    DDX_Control(dx,IDC_ROUTER_PROXIES,m_proxies);
}
BOOL CStaticRouterDlg::OnInitDialog()
{
    CDialogEx::OnInitDialog(); InitColumns();
    // [assignment7][static-router] 두 ComboBox는 같은 Npcap 장치 목록에서 내부/외부 NIC를 각각 고른다.
    // [assignment7][static-router] NIC를 선택하면 MAC만 미리 읽고, IP는 사용자가 입력한다. Host의 OS 설정을 자동 변경하지 않는다.
    for (int i = 0; i < ROUTER_MAX_INTERFACES; ++i) {
        static_cast<CEdit*>(GetDlgItem(IP_IDS[i]))->SetLimitText(15);
        static_cast<CEdit*>(GetDlgItem(MASK_IDS[i]))->SetLimitText(15);
        SetDlgItemText(MASK_IDS[i],_T("255.255.255.0"));
        m_ports[i].arp->SetNotifyWindow(m_hWnd); m_ports[i].ip->SetNotifyWindow(m_hWnd);
        if (!m_ports[i].ni->LoadAdapters()) { SetStatus(m_ports[i].ni->GetError()); continue; }
        CClientDC dc(&m_ports[i].devices); CFont* old = dc.SelectObject(m_ports[i].devices.GetFont());
        int width = 0;
        for (int device = 0; device < m_ports[i].ni->GetAdapterCount(); ++device) {
            const CString name = m_ports[i].ni->GetAdapterName(device);
            m_ports[i].devices.AddString(name); width = (std::max)(width,static_cast<int>(dc.GetTextExtent(name).cx));
        }
        dc.SelectObject(old); m_ports[i].devices.SetDroppedWidth(width+2*GetSystemMetrics(SM_CXVSCROLL));
        m_ports[i].devices.SetCurSel(i < m_ports[i].ni->GetAdapterCount() ? i : 0);
    }
    m_router.SetNotifyWindow(m_hWnd); m_ready = true;
    m_timerActive = SetTimer(ROUTER_UI_TIMER_ID,ROUTER_UI_REFRESH_MS,NULL) != 0;
    SetStatus(m_timerActive ? _T("내부/외부 유선 NIC와 각 IP/Mask를 지정하고 Routing Table을 입력한 뒤 Start하세요.") :
        _T("상태 타이머 시작 실패: 프로그램을 다시 실행하세요."));
    PreviewAdapter(0); PreviewAdapter(1);
    if (m_ports[0].ni->GetAdapterCount() < 2 || m_ports[1].ni->GetAdapterCount() < 2)
        SetStatus(_T("라우터에는 서로 다른 유선 NIC 2개가 필요합니다. Npcap 설치와 USB Ethernet 연결을 확인하세요."));
    RememberLayout(); FitToWorkArea(); RefreshControls(); RefreshTables(); RefreshStatus();
    return TRUE;
}
void CStaticRouterDlg::PreviewAdapter(int port)
{
    if (m_connected || !m_ready) return;
    auto& current = m_ports[port];
    if (!current.ni->OpenAdapter(current.devices.GetCurSel())) {
        SetDlgItemText(MAC_IDS[port],_T("")); SetStatus(current.ni->GetError()); return;
    }
    unsigned char mac[6]; current.ni->GetMacAddress(mac);
    SetDlgItemText(MAC_IDS[port],NetworkAddress::FormatMac(mac)); current.ni->CloseAdapter();
}
void CStaticRouterDlg::OnInternalChanged() { PreviewAdapter(0); }
void CStaticRouterDlg::OnExternalChanged() { PreviewAdapter(1); }

void CStaticRouterDlg::OnStart()
{
    if (m_connected || !m_timerActive) return;
    // [assignment7][static-router] 유효한 host IP/연속 mask, 서로 다른 유선 NIC, 겹치지 않는 subnet을 먼저 검사한다.
    // [assignment7][static-router] 주소를 소스에 고정하지 않으므로 시연 현장에서 IP/Mask/라우팅 항목을 입력할 수 있다.
    uint32_t addresses[2] = {}, masks[2] = {}; unsigned char macs[2][6] = {};
    for (int i = 0; i < ROUTER_MAX_INTERFACES; ++i) {
        CString text; GetDlgItemText(IP_IDS[i],text);
        if (!NetworkAddress::ParseIp(text,addresses[i])) { AfxMessageBox(_T("내부/외부 IPv4 주소를 입력하세요.")); return; }
        GetDlgItemText(MASK_IDS[i],text);
        if (!NetworkAddress::ParseIp(text,masks[i]) || !NetworkPackets::IsHostOnSubnet(addresses[i],addresses[i],masks[i])) {
            AfxMessageBox(_T("IP와 연속된 Mask(/1~ /30)를 확인하세요. network/broadcast 주소는 사용할 수 없습니다.")); return;
        }
        if (!m_ports[i].ni->CheckWiredAdapter(m_ports[i].devices.GetCurSel())) { AfxMessageBox(m_ports[i].ni->GetError()); return; }
    }
    if (m_ports[0].ni->GetAdapterId(m_ports[0].devices.GetCurSel()) == m_ports[1].ni->GetAdapterId(m_ports[1].devices.GetCurSel())) {
        AfxMessageBox(_T("내부와 외부에는 서로 다른 유선 NIC를 선택하세요.")); return;
    }
    if ((addresses[0]&masks[0]) == (addresses[1]&masks[0]) || (addresses[0]&masks[1]) == (addresses[1]&masks[1])) {
        AfxMessageBox(_T("내부/외부 IP와 Mask는 서로 겹치지 않는 네트워크여야 합니다.")); return;
    }
    for (int i = 0; i < ROUTER_MAX_INTERFACES; ++i) {
        auto& port = m_ports[i];
        if (!port.ni->OpenAdapter(port.devices.GetCurSel())) { AfxMessageBox(port.ni->GetError()); StopNetwork(); return; }
        port.ni->GetMacAddress(macs[i]); port.ethernet->SetSourceAddress(macs[i]);
        port.ip->Configure(addresses[i],port.arp); port.arp->Configure(addresses[i],macs[i],port.ip);
        m_router.BindInterface(i,addresses[i],masks[i],port.arp,port.ethernet);
        port.ip->SetRouter(&m_router,i); port.arp->SetRouter(&m_router,i);
        SetDlgItemText(MAC_IDS[i],NetworkAddress::FormatMac(macs[i]));
    }
    CString error;
    if (!m_router.ValidateStaticRoutes(error)) { AfxMessageBox(error); StopNetwork(); return; }
    // [assignment7][static-router] 양쪽 계층/표를 준비한 뒤 수신을 시작한다. 한쪽 시작 실패 시 두 링크 모두 정리한다.
    for (int i = 0; i < ROUTER_MAX_INTERFACES; ++i) {
        if (!m_ports[i].arp->BeginAddressCheck(macs[i]) || !m_ports[i].ni->StartReceive()) {
            AfxMessageBox(_T("주소 검사 또는 NIC 수신 시작 실패")); StopNetwork(); return;
        }
    }
    m_connected = true; SetStatus(_T("주소 충돌 검사 중입니다. 완료 후 호스트에서 ping을 보내세요."));
    RefreshControls(); RefreshTables();
}
void CStaticRouterDlg::StopNetwork()
{
    // [assignment7][static-router] 교차 송신을 먼저 멈추고 NI worker를 join한 뒤 주소/캐시를 초기화한다. 수동 경로 표는 보존한다.
    m_router.Suspend();
    for (auto& port : m_ports) if (port.ni) port.ni->CloseAdapter();
    m_router.Reset();
    for (auto& port : m_ports) { if (port.ip) port.ip->Reset(); if (port.arp) port.arp->Reset(); }
    m_connected = false;
    if (m_ready && !m_closing && ::IsWindow(GetSafeHwnd())) { RefreshControls(); RefreshTables(); }
}
void CStaticRouterDlg::OnEnd()
{
    StopNetwork(); SetStatus(_T("라우터 중지. 수동 경로는 유지되며 IP/NIC를 변경할 수 있습니다."));
}
void CStaticRouterDlg::RefreshControls()
{
    if (!m_ready || m_closing) return;
    for (int i = 0; i < ROUTER_MAX_INTERFACES; ++i) {
        m_ports[i].devices.EnableWindow(!m_connected);
        GetDlgItem(IP_IDS[i])->EnableWindow(!m_connected); GetDlgItem(MASK_IDS[i])->EnableWindow(!m_connected);
    }
    GetDlgItem(IDC_ROUTER_START)->EnableWindow(!m_connected && m_timerActive &&
        m_ports[0].ni->GetAdapterCount() >= 2 && m_ports[1].ni->GetAdapterCount() >= 2);
    GetDlgItem(IDC_ROUTER_END)->EnableWindow(m_connected);
    GetDlgItem(IDC_ROUTER_ARP_DELETE)->EnableWindow(m_connected);
    GetDlgItem(IDC_ROUTER_PROXY_ADD)->EnableWindow(m_connected && m_ports[0].arp->IsUsable() && m_ports[1].arp->IsUsable());
    GetDlgItem(IDC_ROUTER_PROXY_DELETE)->EnableWindow(m_connected);
}
void CStaticRouterDlg::OnRouteAdd()
{
    CRouteEntryDlg editor(&m_router,this);
    if (editor.DoModal() == IDOK) { RefreshTables(); SetStatus(_T("Routing entry를 추가했습니다.")); }
}
void CStaticRouterDlg::OnRouteDelete()
{
    const int row = m_routes.GetNextItem(-1,LVNI_SELECTED);
    if (row >= 0) { m_router.DeleteStaticRoute(static_cast<uint32_t>(m_routes.GetItemData(row))); RefreshTables(); }
}
void CStaticRouterDlg::OnArpDelete()
{
    const int row = m_cache.GetNextItem(-1,LVNI_SELECTED);
    if (row >= 0 && row < static_cast<int>(m_cacheRows.size())) {
        const auto key = m_cacheRows[row]; m_ports[key.port].arp->DeleteEntry(key.ip); RefreshTables();
    }
}
void CStaticRouterDlg::OnProxyAdd()
{
    CRouterProxyEntryDlg editor(m_ports[0].arp,m_ports[1].arp,this);
    if (editor.DoModal() == IDOK) { RefreshTables(); SetStatus(_T("Proxy ARP 항목을 추가했습니다. 일반 정적 라우팅에는 Proxy 등록이 필요하지 않습니다.")); }
}
void CStaticRouterDlg::OnProxyDelete()
{
    const int row = m_proxies.GetNextItem(-1,LVNI_SELECTED);
    if (row >= 0 && row < static_cast<int>(m_proxyRows.size())) {
        const auto key = m_proxyRows[row]; m_ports[key.port].arp->DeleteProxy(key.ip); RefreshTables();
    }
}
void CStaticRouterDlg::OnTimer(UINT_PTR id)
{
    if (id != ROUTER_UI_TIMER_ID) { CDialogEx::OnTimer(id); return; }
    if (m_connected) {
        for (int i = 0; i < ROUTER_MAX_INTERFACES; ++i) {
            if (!m_ports[i].ni->IsReceiveRunning()) {
                const CString reason = m_ports[i].ni->GetIoError();
                CString message; message.Format(_T("Port %d 수신 중단: %s. 장치를 확인하고 Start로 재연결하세요."),
                    i+1,static_cast<LPCTSTR>(reason));
                StopNetwork(); SetStatus(message); RefreshStatus(); return;
            }
        }
        const ULONGLONG now = GetTickCount64();
        for (auto& port : m_ports) { port.arp->Tick(now); port.ip->Tick(now); }
        m_router.Tick(now);
    }
    RefreshControls(); RefreshTables(); RefreshStatus();
}
void CStaticRouterDlg::SetStatus(const CString& text)
{
    m_status = text; if (m_ready && !m_closing) RefreshStatus();
}
void CStaticRouterDlg::RefreshStatus()
{
    const auto stats = m_router.GetStats(); CString text;
    text.Format(_T("전달 %llu / 대기 %llu / 폐기 %llu\r\n%s"),
        static_cast<unsigned long long>(stats.forwarded),static_cast<unsigned long long>(stats.waiting),
        static_cast<unsigned long long>(stats.dropped),static_cast<LPCTSTR>(m_status));
    if (m_connected) for (int i = 0; i < ROUTER_MAX_INTERFACES; ++i) {
        const CString error = m_ports[i].ni->GetIoError();
        if (!error.IsEmpty()) {
            CString detail; detail.Format(_T("\r\n[Port %d 최근 NIC 오류] %s"),i+1,static_cast<LPCTSTR>(error));
            text += detail;
        }
    }
    if (text != m_displayedStatus) { SetDlgItemText(IDC_ROUTER_MESSAGE,text); m_displayedStatus = text; }
}
LRESULT CStaticRouterDlg::OnArpChanged(WPARAM, LPARAM) { RefreshTables(); return 0; }
LRESULT CStaticRouterDlg::OnNetworkEvent(WPARAM, LPARAM message)
{
    // [assignment7][static-router] NI worker는 UI를 직접 갱신하지 않는다. 받은 CString의 소유권은 UI에서 해제한다.
    CString* text = reinterpret_cast<CString*>(message);
    if (text) { SetStatus(*text); delete text; } return 0;
}
void CStaticRouterDlg::OnDestroy()
{
    m_closing = true; KillTimer(ROUTER_UI_TIMER_ID);
    m_router.SetNotifyWindow(NULL);
    for (auto& port : m_ports) { port.ip->SetNotifyWindow(NULL); port.arp->SetNotifyWindow(NULL); }
    StopNetwork();
    MSG message;
    while (::PeekMessage(&message,m_hWnd,WM_NETWORK_EVENT,WM_NETWORK_EVENT,PM_REMOVE)) delete reinterpret_cast<CString*>(message.lParam);
    while (::PeekMessage(&message,m_hWnd,WM_ARP_CHANGED,WM_ARP_CHANGED,PM_REMOVE)) {}
    CDialogEx::OnDestroy();
}

void CStaticRouterDlg::InitColumns()
{
    for (CListCtrl* list : {&m_routes,&m_cache,&m_proxies})
        list->SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
    const TCHAR* route[] = {_T("Destination"),_T("Netmask"),_T("Gateway"),_T("Flag"),_T("Interface"),_T("Metric")};
    const TCHAR* cache[] = {_T("IP Address"),_T("Ethernet Address"),_T("IF"),_T("State")};
    const TCHAR* proxy[] = {_T("Device"),_T("IP Address"),_T("Ethernet Address")};
    for (int i = 0; i < 6; ++i) m_routes.InsertColumn(i,route[i],LVCFMT_LEFT,60);
    for (int i = 0; i < 4; ++i) m_cache.InsertColumn(i,cache[i],LVCFMT_LEFT,60);
    for (int i = 0; i < 3; ++i) m_proxies.InsertColumn(i,proxy[i],LVCFMT_LEFT,60);
}
void CStaticRouterDlg::SizeColumns()
{
    CRect rect; m_routes.GetClientRect(&rect); int width = rect.Width()-GetSystemMetrics(SM_CXVSCROLL)-6;
    const int route[] = {24,24,24,8,12,8};
    for (int i = 0; i < 6; ++i) m_routes.SetColumnWidth(i,(std::max)(30,width*route[i]/100));
    m_cache.GetClientRect(&rect); width = rect.Width()-GetSystemMetrics(SM_CXVSCROLL)-6;
    const int cache[] = {30,37,13,20};
    for (int i = 0; i < 4; ++i) m_cache.SetColumnWidth(i,(std::max)(30,width*cache[i]/100));
    m_proxies.GetClientRect(&rect); width = rect.Width()-GetSystemMetrics(SM_CXVSCROLL)-6;
    const int proxy[] = {20,34,46};
    for (int i = 0; i < 3; ++i) m_proxies.SetColumnWidth(i,(std::max)(30,width*proxy[i]/100));
}
void CStaticRouterDlg::RefreshTables()
{
    if (!m_ready || m_closing) return;
    std::vector<CIPRouter::STATIC_ROUTE> routes; m_router.GetStaticRoutes(routes);
    std::vector<ARP_CACHE_ENTRY> caches[2]; std::vector<ARP_PROXY_ENTRY> proxies[2];
    std::vector<uint64_t> routeState, cacheState, proxyState;
    unsigned char macs[2][6] = {};
    for (const auto& row : routes) for (uint64_t value : {uint64_t(row.id),uint64_t(row.destination),uint64_t(row.mask),
        uint64_t(row.gateway),uint64_t(row.outgoing),uint64_t(row.flags),uint64_t(row.metric)}) routeState.push_back(value);
    for (int port = 0; port < ROUTER_MAX_INTERFACES; ++port) {
        m_ports[port].arp->GetSnapshot(caches[port],proxies[port]); m_ports[port].arp->GetEffectiveMac(macs[port]);
        for (const auto& row : caches[port]) for (uint64_t value : {uint64_t(port),uint64_t(row.ip),MacKey(row.mac),uint64_t(row.complete)}) cacheState.push_back(value);
        for (const auto& row : proxies[port]) for (uint64_t value : {uint64_t(port),uint64_t(row.ip),MacKey(macs[port])}) proxyState.push_back(value);
    }
    // [assignment7][static-router] 표의 실제 내용이 달라질 때만 다시 그린다. 250ms마다 선택 행/스크롤이 초기화되지 않게 한다.
    if (routeState != m_routeState) {
        const int selected = m_routes.GetNextItem(-1,LVNI_SELECTED), top = m_routes.GetTopIndex();
        const uint32_t id = selected < 0 ? 0 : static_cast<uint32_t>(m_routes.GetItemData(selected));
        m_routes.SetRedraw(FALSE); m_routes.DeleteAllItems();
        for (const auto& entry : routes) {
            int row = m_routes.InsertItem(m_routes.GetItemCount(),NetworkAddress::FormatIp(entry.destination));
            m_routes.SetItemText(row,1,NetworkAddress::FormatIp(entry.mask));
            m_routes.SetItemText(row,2,(entry.flags&ROUTE_FLAG_GATEWAY) ? NetworkAddress::FormatIp(entry.gateway) : CString(_T("-")));
            CString flags;
            if (entry.flags&ROUTE_FLAG_UP) flags += _T("U");
            if (entry.flags&ROUTE_FLAG_GATEWAY) flags += _T("G");
            if (entry.flags&ROUTE_FLAG_HOST) flags += _T("H");
            m_routes.SetItemText(row,3,flags.IsEmpty() ? CString(_T("Down")) : flags);
            CString value; value.Format(_T("Port %d"),entry.outgoing+1); m_routes.SetItemText(row,4,value);
            value.Format(_T("%u"),entry.metric); m_routes.SetItemText(row,5,value); m_routes.SetItemData(row,entry.id);
            if (entry.id == id) m_routes.SetItemState(row,LVIS_SELECTED,LVIS_SELECTED);
        }
        RestoreTop(m_routes,top); m_routes.SetRedraw(TRUE); m_routes.Invalidate(); m_routeState.swap(routeState);
    }
    auto selectedKey = [](CListCtrl& list, const std::vector<ROW_KEY>& rows) {
        const int selected = list.GetNextItem(-1,LVNI_SELECTED);
        return selected >= 0 && selected < static_cast<int>(rows.size()) ? rows[selected] : ROW_KEY();
    };
    if (cacheState != m_cacheState) {
        const auto selected = selectedKey(m_cache,m_cacheRows); const int top = m_cache.GetTopIndex();
        m_cache.SetRedraw(FALSE); m_cache.DeleteAllItems(); m_cacheRows.clear();
        for (int port = 0; port < ROUTER_MAX_INTERFACES; ++port) for (const auto& entry : caches[port]) {
            int row = m_cache.InsertItem(m_cache.GetItemCount(),NetworkAddress::FormatIp(entry.ip));
            m_cache.SetItemText(row,1,entry.complete ? NetworkAddress::FormatMac(entry.mac) : CString(_T("-")));
            CString name; name.Format(_T("Port %d"),port+1); m_cache.SetItemText(row,2,name);
            m_cache.SetItemText(row,3,entry.complete ? _T("Complete") : _T("Incomplete"));
            ROW_KEY key; key.port = port; key.ip = entry.ip; m_cacheRows.push_back(key);
            if (selected.port == port && selected.ip == entry.ip) m_cache.SetItemState(row,LVIS_SELECTED,LVIS_SELECTED);
        }
        RestoreTop(m_cache,top); m_cache.SetRedraw(TRUE); m_cache.Invalidate(); m_cacheState.swap(cacheState);
    }
    if (proxyState != m_proxyState) {
        const auto selected = selectedKey(m_proxies,m_proxyRows); const int top = m_proxies.GetTopIndex();
        m_proxies.SetRedraw(FALSE); m_proxies.DeleteAllItems(); m_proxyRows.clear();
        for (int port = 0; port < ROUTER_MAX_INTERFACES; ++port) for (const auto& entry : proxies[port]) {
            CString name; name.Format(_T("Port %d"),port+1);
            int row = m_proxies.InsertItem(m_proxies.GetItemCount(),name);
            m_proxies.SetItemText(row,1,NetworkAddress::FormatIp(entry.ip)); m_proxies.SetItemText(row,2,NetworkAddress::FormatMac(macs[port]));
            ROW_KEY key; key.port = port; key.ip = entry.ip; m_proxyRows.push_back(key);
            if (selected.port == port && selected.ip == entry.ip) m_proxies.SetItemState(row,LVIS_SELECTED,LVIS_SELECTED);
        }
        RestoreTop(m_proxies,top); m_proxies.SetRedraw(TRUE); m_proxies.Invalidate(); m_proxyState.swap(proxyState);
    }
}

void CStaticRouterDlg::RememberLayout()
{
    CRect design(0,0,640,340); MapDialogRect(&design); m_designSize = design.Size();
    for (CWnd* child = GetWindow(GW_CHILD); child; child = child->GetNextWindow()) {
        CRect rect; child->GetWindowRect(&rect); ScreenToClient(&rect);
        const int id = child->GetDlgCtrlID();
        m_layout.push_back({child->GetSafeHwnd(),rect,id == DEVICE_IDS[0] || id == DEVICE_IDS[1]});
    }
}
void CStaticRouterDlg::FitToWorkArea()
{
    const CRect work = WorkArea(m_hWnd); CRect window; GetWindowRect(&window);
    const int width = (std::min)(window.Width(),(std::max)(1,work.Width()-16));
    const int height = (std::min)(window.Height(),(std::max)(1,work.Height()-16));
    SetWindowPos(nullptr,work.left+(work.Width()-width)/2,work.top+(work.Height()-height)/2,width,height,SWP_NOZORDER | SWP_NOACTIVATE);
    CRect client; GetClientRect(&client); LayoutControls(client.Width(),client.Height());
}
void CStaticRouterDlg::LayoutControls(int cx, int cy)
{
    if (!m_ready || m_layout.empty() || cx <= 0 || cy <= 0) return;
    // [assignment7][static-router] 별도 페이지나 창 전체 스크롤 없이 자료의 한 화면 배치를 현재 작업 영역에 맞춘다.
    for (const auto& child : m_layout) {
        const int left = static_cast<int>(static_cast<long long>(child.rect.left)*cx/m_designSize.cx);
        const int right = static_cast<int>(static_cast<long long>(child.rect.right)*cx/m_designSize.cx);
        const int top = static_cast<int>(static_cast<long long>(child.rect.top)*cy/m_designSize.cy);
        const int bottom = static_cast<int>(static_cast<long long>(child.rect.bottom)*cy/m_designSize.cy);
        // [assignment7][static-router] ComboBox의 전체 높이는 펼친 목록 크기다. 입력 행 높이로 줄이면 목록이 잘리므로 여유를 둔다.
        const int height = (std::max)(1,bottom-top) + (child.combo ? (std::max)(100,cy/3) : 0);
        ::MoveWindow(child.window,left,top,(std::max)(1,right-left),height,FALSE);
    }
    SizeColumns(); RedrawWindow(nullptr,nullptr,RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
}
void CStaticRouterDlg::OnSize(UINT type, int cx, int cy)
{
    CDialogEx::OnSize(type,cx,cy); if (type != SIZE_MINIMIZED) LayoutControls(cx,cy);
}
void CStaticRouterDlg::OnGetMinMaxInfo(MINMAXINFO* info)
{
    CDialogEx::OnGetMinMaxInfo(info);
    if (!m_ready) return;
    CRect minimum(0,0,480,250); MapDialogRect(&minimum);
    ::AdjustWindowRectEx(&minimum,GetStyle(),FALSE,GetExStyle()); const CRect work = WorkArea(m_hWnd);
    info->ptMinTrackSize.x = (std::min)(minimum.Width(),work.Width());
    info->ptMinTrackSize.y = (std::min)(minimum.Height(),work.Height());
}
