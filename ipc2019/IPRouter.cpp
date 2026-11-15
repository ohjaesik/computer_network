#include "pch.h"
#include "IPRouter.h"
#include "ARPLayer.h"
#include "EthernetLayer.h"

using namespace NetworkPackets;

void CIPRouter::SetStaticRouting(bool enabled)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_staticRouting == enabled) return;
    // [assignment7][static-router] 모드는 UI에서 연결 전에 선택한다. 이전 모드의 ARP 대기 데이터도 취소한다.
    m_pending.clear(); m_pendingBytes = 0; m_staticRouting = enabled;
}
bool CIPRouter::IsStaticRouting()
{
    std::lock_guard<std::mutex> lock(m_mutex); return m_staticRouting;
}
bool CIPRouter::RouteFitsInterface(const STATIC_ROUTE& route) const
{
    if (!ValidInterface(route.outgoing)) return false;
    const auto& port = m_interfaces[route.outgoing];
    if (route.flags & ROUTE_FLAG_GATEWAY) {
        // [assignment7][static-router] 게이트웨이는 선택한 출력 NIC에서 직접 ARP 가능한 실제 호스트여야 한다.
        if (!IsHostOnSubnet(route.gateway,port.ip,port.mask)) return false;
        for (const auto& local : m_interfaces) if (route.gateway == local.ip) return false;
        return true;
    }
    // [assignment7][static-router] U는 다음 홉=최종 목적지다. 직접 연결된 subnet 또는 그 안의 더 구체적인 경로만 허용한다.
    return route.mask >= port.mask && (route.destination & port.mask) == (port.ip & port.mask) &&
        (!(route.flags & ROUTE_FLAG_HOST) || (IsHostOnSubnet(route.destination,port.ip,port.mask) && route.destination != port.ip));
}
bool CIPRouter::AddStaticRoute(const STATIC_ROUTE& input, CString& error)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    error.Empty(); STATIC_ROUTE route = input;
    const uint32_t inverse = ~route.mask;
    // [assignment7][static-router] NIC mask(/1~ /30)와 달리 route mask는 기본 경로 /0과 호스트 경로 /32도 받는다.
    if ((inverse & (inverse + 1)) != 0 || (route.destination & route.mask) != route.destination) {
        error = _T("Netmask는 연속된 비트여야 하며 Destination의 host bit는 0이어야 합니다."); return false;
    }
    if (route.outgoing < 0 || route.outgoing >= ROUTER_MAX_INTERFACES || route.metric > ROUTE_MAX_METRIC ||
        (route.flags & ~(ROUTE_FLAG_UP | ROUTE_FLAG_GATEWAY | ROUTE_FLAG_HOST))) {
        error = _T("Interface(Port 0/1), Flag(U/G/H), Metric(0~65535)을 확인하세요."); return false;
    }
    if ((route.flags & ROUTE_FLAG_HOST) && route.mask != 0xffffffffu) {
        error = _T("H(Host) 경로의 Netmask는 255.255.255.255이어야 합니다."); return false;
    }
    if (route.mask == 0xffffffffu) route.flags |= ROUTE_FLAG_HOST;
    if ((route.destination || route.mask == 0xffffffffu) && !IsUnicastIp(route.destination)) {
        error = _T("Destination은 unicast 호스트/네트워크 또는 기본 경로 0.0.0.0이어야 합니다."); return false;
    }
    if (route.flags & ROUTE_FLAG_GATEWAY) {
        if (!IsUnicastIp(route.gateway)) { error = _T("G 경로에는 유효한 Gateway IP가 필요합니다."); return false; }
    } else route.gateway = 0; // [assignment7][static-router] 강의 표에 NIC 자신의 IP가 적혀 있어도 G가 없으면 사용하지 않는다.
    if (m_running && !RouteFitsInterface(route)) {
        error = _T("직접 경로는 출력 NIC subnet 안에, Gateway는 출력 NIC subnet의 다른 호스트로 지정하세요."); return false;
    }
    for (const auto& old : m_staticRoutes) {
        if (old.destination == route.destination && old.mask == route.mask && old.gateway == route.gateway &&
            old.outgoing == route.outgoing && old.flags == route.flags) {
            error = _T("동일한 경로가 이미 있습니다. 변경하려면 기존 행을 삭제한 뒤 추가하세요."); return false;
        }
    }
    if (m_staticRoutes.size() >= ROUTE_TABLE_MAX_ENTRIES || !m_nextRouteId) {
        error = _T("Routing Table의 최대 항목 수를 초과했습니다."); return false;
    }
    route.id = m_nextRouteId++; m_staticRoutes.push_back(route);
    DiscardChangedPendingLocked(); return true;
}
void CIPRouter::DeleteStaticRoute(uint32_t id)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_staticRoutes.erase(std::remove_if(m_staticRoutes.begin(),m_staticRoutes.end(),
        [id](const STATIC_ROUTE& route) { return route.id == id; }),m_staticRoutes.end());
    DiscardChangedPendingLocked();
}
void CIPRouter::GetStaticRoutes(std::vector<STATIC_ROUTE>& routes)
{
    std::lock_guard<std::mutex> lock(m_mutex); routes = m_staticRoutes;
}
bool CIPRouter::ValidateStaticRoutes(CString& error)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    error.Empty();
    for (const auto& route : m_staticRoutes) if (!RouteFitsInterface(route)) {
        error.Format(_T("경로 ID %u: 현재 Interface IP/Mask와 직접 경로/Gateway가 맞지 않습니다."),route.id);
        return false;
    }
    return true;
}
void CIPRouter::DiscardChangedPendingLocked()
{
    // [assignment7][static-router] 편집으로 next hop/출력 NIC가 바뀐 대기 패킷은 취소한다. 늦은 ARP Reply로 구 경로 송신을 막는다.
    if (!m_staticRouting) return;
    for (auto it = m_pending.begin(); it != m_pending.end();) {
        int outgoing; uint32_t nextHop;
        if (!FindRoute(ReadIp(it->original.data()+16),outgoing,nextHop) || outgoing != it->outgoing || nextHop != it->nextHop) {
            ++m_stats.dropped; ErrorLocked(*it,3,0); m_pendingBytes -= it->packet.size(); it = m_pending.erase(it);
        } else ++it;
    }
}

void CIPRouter::BindInterface(int index, uint32_t ip, uint32_t mask, CARPLayer* arp, CEthernetLayer* ethernet)
{
    if (index < 0 || index >= ROUTER_MAX_INTERFACES) return;
    std::lock_guard<std::mutex> lock(m_mutex);
    m_interfaces[index].ip = ip; m_interfaces[index].mask = mask;
    m_interfaces[index].arp = arp; m_interfaces[index].ethernet = ethernet;
    m_running = true;
}
void CIPRouter::Suspend()
{
    std::lock_guard<std::mutex> lock(m_mutex); m_running = false;
    m_pending.clear(); m_pendingBytes = 0;
}
void CIPRouter::Reset()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& port : m_interfaces) port = ROUTER_PORT();
    m_routes.clear(); m_pending.clear(); m_pendingBytes = 0; m_stats = STATS();
    m_errorWindowAt = 0; m_errorsInWindow = 0; m_identification = 0;
    m_running = false;
}
bool CIPRouter::ValidInterface(int index) const
{
    return index >= 0 && index < ROUTER_MAX_INTERFACES && m_interfaces[index].ip &&
        m_interfaces[index].arp && m_interfaces[index].ethernet && IsSubnetMask(m_interfaces[index].mask);
}
bool CIPRouter::IsDirectedBroadcast(uint32_t ip) const
{
    for (const auto& port : m_interfaces) {
        if (!port.ip) continue;
        const uint32_t network = port.ip & port.mask;
        if (ip == network || ip == (network | ~port.mask)) return true;
    }
    return false;
}
bool CIPRouter::FindRoute(uint32_t destination, int& outgoing, uint32_t& nextHop) const
{
    if (m_staticRouting) {
        // [assignment7][static-router] 사용자 표만 조회한다. /32 > subnet > /0 순으로 가장 긴 prefix를 선택한다.
        // [assignment7][static-router] prefix가 같으면 낮은 Metric, 둘 다 같으면 먼저 입력한 행을 사용한다. U가 없으면 제외한다.
        const STATIC_ROUTE* best = nullptr;
        for (const auto& route : m_staticRoutes) {
            if (!(route.flags & ROUTE_FLAG_UP) || !RouteFitsInterface(route) ||
                (destination & route.mask) != route.destination) continue;
            if (!best || route.mask > best->mask || (route.mask == best->mask && route.metric < best->metric)) best = &route;
        }
        if (!best) return false; // [assignment7][static-router] connected subnet/Proxy 등록으로 자동 보완하지 않는다.
        outgoing = best->outgoing;
        nextHop = (best->flags & ROUTE_FLAG_GATEWAY) ? best->gateway : destination;
        return true;
    }
    // [assignment7] /32 proxy 정적 경로가 connected subnet보다 구체적이므로 먼저 확인한다.
    for (const auto& route : m_routes) if (route.target == destination && ValidInterface(route.outgoing)) {
        outgoing = route.outgoing; nextHop = route.nextHop ? route.nextHop : destination; return true;
    }
    int best = -1; uint32_t longestMask = 0;
    for (int i = 0; i < ROUTER_MAX_INTERFACES; ++i) {
        const auto& port = m_interfaces[i];
        if (ValidInterface(i) && IsHostOnSubnet(destination, port.ip, port.mask) && port.mask > longestMask) {
            best = i; longestMask = port.mask;
        }
    }
    if (best < 0) return false;
    outgoing = best; nextHop = destination; return true;
}
bool CIPRouter::IsValidProxyTarget(uint32_t target)
{
    // [assignment7] 출력 경로 유무와 독립된 주소 검사다. 단일 NIC 시연이 기존 주소 검사를 우회하지 않게 한다.
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_running || !IsUnicastIp(target) || IsDirectedBroadcast(target)) return false;
    for (const auto& port : m_interfaces) if (target == port.ip) return false;
    return true;
}
bool CIPRouter::InstallProxyRoute(int incoming, uint32_t target, int outgoing, uint32_t nextHop)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!ValidInterface(incoming) || !ValidInterface(outgoing) || incoming == outgoing ||
        !IsUnicastIp(target) || IsDirectedBroadcast(target)) return false;
    for (const auto& port : m_interfaces) if (target == port.ip) return false;
    const auto& output = m_interfaces[outgoing];
    if (!IsHostOnSubnet(nextHop ? nextHop : target, output.ip, output.mask) || nextHop == output.ip) return false;
    for (const auto& route : m_routes) if (route.target == target) return false;
    if (m_routes.size() >= ARP_CACHE_MAX_ENTRIES) return false;
    ROUTE route; route.owner = incoming; route.target = target; route.outgoing = outgoing; route.nextHop = nextHop;
    m_routes.push_back(route); return true;
}
void CIPRouter::RemoveProxyRoute(int incoming, uint32_t target)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_routes.erase(std::remove_if(m_routes.begin(), m_routes.end(), [incoming,target](const ROUTE& route) {
        return route.owner == incoming && route.target == target;
    }), m_routes.end());
    // [assignment7] 이미 next-hop 큐에 들어간 해당 /32 데이터도 취소해 삭제한 경로로 뒤늦게 보내지 않는다.
    for (auto it = m_pending.begin(); it != m_pending.end();) {
        if (ReadIp(it->original.data() + 16) == target && it->incoming == incoming) {
            ErrorLocked(*it, 3, 0); ++m_stats.dropped; m_pendingBytes -= it->packet.size(); it = m_pending.erase(it);
        } else ++it;
    }
}
bool CIPRouter::CanProxyReply(int incoming, uint32_t target)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    int outgoing; uint32_t nextHop;
    // [assignment7] 도달 경로가 없거나 요청이 들어온 NIC와 출력 NIC가 같으면 대리 Reply하지 않는다.
    return m_running && ValidInterface(incoming) && !IsDirectedBroadcast(target) && FindRoute(target,outgoing,nextHop) &&
        outgoing != incoming && m_interfaces[incoming].arp->IsUsable() && m_interfaces[outgoing].arp->IsUsable();
}
bool CIPRouter::IsLocalAddress(uint32_t address)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    // [assignment7] 두 IP 계층은 같은 앱/UI를 공유한다. 반대 NIC로 들어온 중계 PC 자신의 응답도 로컬 수신한다.
    if (!m_running) return false;
    for (int i = 0; i < ROUTER_MAX_INTERFACES; ++i)
        if (ValidInterface(i) && address == m_interfaces[i].ip && m_interfaces[i].arp->IsUsable()) return true;
    return false;
}
bool CIPRouter::UsesDifferentInterface(int sourceInterface, uint32_t destination)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    int output; uint32_t nextHop;
    return m_running && ValidInterface(sourceInterface) && FindRoute(destination,output,nextHop) && output != sourceInterface;
}
bool CIPRouter::IsDestinationResolved(int sourceInterface, uint32_t destination)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    int output; uint32_t nextHop; unsigned char mac[6];
    return m_running && ValidInterface(sourceInterface) && FindRoute(destination,output,nextHop) &&
        m_interfaces[output].arp->IsUsable() && m_interfaces[output].arp->Lookup(nextHop,mac);
}
BOOL CIPRouter::RequestDestination(int sourceInterface, uint32_t destination)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    int output; uint32_t nextHop;
    return m_running && ValidInterface(sourceInterface) && FindRoute(destination,output,nextHop) &&
        m_interfaces[output].arp->SendRequest(nextHop);
}
BOOL CIPRouter::SendOriginated(int sourceInterface, const unsigned char* packet, int length, bool allowQueue)
{
    if (!packet || length < IP_HEADER_SIZE || length > ETHER_MAX_DATA_SIZE) return FALSE;
    std::lock_guard<std::mutex> lock(m_mutex);
    PENDING pending; pending.incoming = sourceInterface; pending.originated = true;
    if (!m_running || !ValidInterface(sourceInterface) ||
        !FindRoute(ReadIp(packet+16),pending.outgoing,pending.nextHop) ||
        !m_interfaces[pending.outgoing].arp->IsUsable()) return FALSE;
    // [assignment7] 중계 PC 자체의 Chat/File도 출력 경로를 사용한다. 자체 생성이므로 TTL을 감소시키지 않는다.
    pending.packet.assign(packet,packet+length); pending.original = pending.packet;
    auto& output = m_interfaces[pending.outgoing]; unsigned char mac[6];
    if (output.arp->Lookup(pending.nextHop,mac)) {
        FlushLocked(pending.outgoing,pending.nextHop,mac,GetTickCount64());
        return output.ethernet->SendTo(pending.packet.data(),length,ETHERNET_TYPE_IPV4,mac);
    }
    if (!allowQueue || m_pendingBytes + pending.packet.size() > ROUTER_PENDING_MAX_BYTES) return FALSE;
    pending.expiresAt = GetTickCount64() + IP_RESOLVE_TIMEOUT_MS;
    const uint32_t nextHop = pending.nextHop;
    m_pendingBytes += pending.packet.size(); m_pending.push_back(std::move(pending));
    // 일시적인 ARP 주입 실패에도 큐/원래 만료 시각을 유지한다. Tick이 제한 시간 안에서 재시도한다.
    if (!output.arp->SendRequest(nextHop)) Report(_T("ARP 송신 실패: 대기 패킷을 보존하고 제한 시간 내 재시도합니다."));
    return TRUE; // 전송 완료가 아니라 대기 큐 접수
}
BOOL CIPRouter::Forward(int incoming, const unsigned char* packet, int length, const unsigned char* previousMac)
{
    if (!packet || !IsUnicastMac(previousMac) || length < IP_HEADER_SIZE || length > ETHER_MAX_DATA_SIZE) return FALSE;
    const int ihl = (packet[0] & 15) * 4, total = (packet[2] << 8) | packet[3];
    if ((packet[0] >> 4) != 4 || ihl != IP_HEADER_SIZE || total < ihl || total > length ||
        Checksum(packet,ihl) || (packet[6] & 0x80)) return FALSE;
    const uint32_t destination = ReadIp(packet+16), sender = ReadIp(packet+12);
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_running || !ValidInterface(incoming) || !m_interfaces[incoming].arp->IsUsable() ||
        !IsUnicastIp(sender) || !IsUnicastIp(destination) || IsDirectedBroadcast(sender) || IsDirectedBroadcast(destination)) return FALSE;
    for (const auto& port : m_interfaces) if (sender == port.ip || destination == port.ip) return FALSE;
    PENDING pending;
    pending.original.assign(packet,packet+total); pending.incoming = incoming;
    memcpy(pending.previousMac,previousMac,6);
    if (packet[8] <= 1) { ++m_stats.dropped; ErrorLocked(pending,11,0); return FALSE; }
    // [assignment7][static-router] 수동 라우팅은 입력과 같은 인터페이스로도 전달할 수 있다. PARP의 기존 반대 NIC 조건은 유지한다.
    if (!FindRoute(destination,pending.outgoing,pending.nextHop) || (pending.outgoing == incoming && !m_staticRouting) ||
        !m_interfaces[pending.outgoing].arp->IsUsable()) {
        ++m_stats.dropped; ErrorLocked(pending,3,0); return FALSE;
    }
    // [assignment7] 기존 source/destination/protocol/ID/fragment 필드를 보존하고 TTL만 1 감소시킨다.
    // [assignment7] IPv4 조각도 각각 전달할 수 있으나 router가 재조립하지 않는다. IP 옵션은 미지원이다.
    pending.packet = pending.original; --pending.packet[8]; FixIpChecksum(pending.packet.data(),ihl);
    auto& output = m_interfaces[pending.outgoing];
    unsigned char mac[6];
    if (output.arp->Lookup(pending.nextHop,mac)) {
        FlushLocked(pending.outgoing,pending.nextHop,mac,GetTickCount64());
        const BOOL sent = output.ethernet->SendTo(pending.packet.data(),total,ETHERNET_TYPE_IPV4,mac);
        if (sent) ++m_stats.forwarded; else { ++m_stats.dropped; ErrorLocked(pending,3,1); }
        return sent;
    }
    if (m_pendingBytes + pending.packet.size() > ROUTER_PENDING_MAX_BYTES) {
        ++m_stats.dropped; ErrorLocked(pending,3,1); Report(_T("IP 중계 대기 큐 부족: 패킷 폐기")); return FALSE;
    }
    pending.expiresAt = GetTickCount64() + IP_RESOLVE_TIMEOUT_MS;
    const uint32_t nextHop = pending.nextHop;
    m_pendingBytes += pending.packet.size(); m_pending.push_back(std::move(pending));
    // [assignment7] ARP SendRequest는 ARP lock을 해제한 후 Ethernet을 호출한다. ARP 수신 콜백도 반대로 lock을 놓는다.
    // 첫 ARP 주입 실패만으로 원본 IP를 폐기하지 않는다. 재시도/최종 실패는 Tick이 처리한다.
    if (!output.arp->SendRequest(nextHop)) Report(_T("ARP 송신 실패: 대기 패킷을 보존하고 제한 시간 내 재시도합니다."));
    return TRUE;
}
void CIPRouter::FlushLocked(int outgoing, uint32_t nextHop, const unsigned char* mac, ULONGLONG now)
{
    for (auto it = m_pending.begin(); it != m_pending.end();) {
        if (it->outgoing != outgoing || it->nextHop != nextHop) { ++it; continue; }
        int selected; uint32_t hop;
        const bool valid = now < it->expiresAt && m_interfaces[outgoing].arp->IsUsable() &&
            FindRoute(ReadIp(it->original.data()+16),selected,hop) && selected == outgoing && hop == nextHop;
        const BOOL sent = valid && m_interfaces[outgoing].ethernet->SendTo(it->packet.data(),
            static_cast<int>(it->packet.size()),ETHERNET_TYPE_IPV4,mac);
        if (sent) { if (!it->originated) ++m_stats.forwarded; }
        else { ++m_stats.dropped; ErrorLocked(*it,3,1); }
        m_pendingBytes -= it->packet.size(); it = m_pending.erase(it);
    }
}
void CIPRouter::OnArpResolved(int index, uint32_t ip)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    unsigned char mac[6];
    if (m_running && ValidInterface(index) && m_interfaces[index].arp->Lookup(ip,mac)) FlushLocked(index,ip,mac,GetTickCount64());
}
void CIPRouter::Tick(ULONGLONG now)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_running) return;
    for (auto it = m_pending.begin(); it != m_pending.end();) {
        if (now >= it->expiresAt || !ValidInterface(it->outgoing) || !m_interfaces[it->outgoing].arp->IsUsable()) {
            ++m_stats.dropped; ErrorLocked(*it,3,1); m_pendingBytes -= it->packet.size(); it = m_pending.erase(it);
        } else ++it;
    }
    // [assignment7] 캐시 갱신과 큐 등록의 경쟁/한 번 놓친 Reply도 정기 조회로 복구한다.
    for (int index = 0; index < ROUTER_MAX_INTERFACES; ++index) {
        std::vector<uint32_t> targets;
        for (const auto& packet : m_pending) if (packet.outgoing == index &&
            std::find(targets.begin(),targets.end(),packet.nextHop) == targets.end()) targets.push_back(packet.nextHop);
        for (uint32_t ip : targets) {
            unsigned char mac[6];
            if (m_interfaces[index].arp->Lookup(ip,mac)) FlushLocked(index,ip,mac,now);
            else m_interfaces[index].arp->SendRequest(ip);
        }
    }
}
CIPRouter::STATS CIPRouter::GetStats()
{
    std::lock_guard<std::mutex> lock(m_mutex); STATS result = m_stats; result.waiting = m_pending.size(); return result;
}
void CIPRouter::ErrorLocked(const PENDING& packet, unsigned char type, unsigned char code)
{
    if (packet.originated) { Report(_T("중계 PC 자체 송신 실패: 출력 NIC의 ARP/주소/경로를 확인하세요.")); return; }
    if (!ValidInterface(packet.incoming) || !m_interfaces[packet.incoming].arp->IsUsable() || packet.original.size() < IP_HEADER_SIZE) return;
    const auto& original = packet.original;
    const unsigned short offset = static_cast<unsigned short>((original[6] << 8) | original[7]);
    if (offset & 0x1fff) return; // [assignment7] 첫 IPv4 조각이 아닌 경우에는 ICMP 오류를 보내지 않는다.
    if (original[9] == IP_PROTOCOL_ICMP && original.size() > IP_HEADER_SIZE) {
        const unsigned char oldType = original[IP_HEADER_SIZE];
        if (oldType == 3 || oldType == 4 || oldType == 5 || oldType == 11 || oldType == 12) return;
    }
    const ULONGLONG now = GetTickCount64();
    if (!m_errorWindowAt || now - m_errorWindowAt >= 1000) { m_errorWindowAt = now; m_errorsInWindow = 0; }
    if (m_errorsInWindow++ >= ICMP_ERROR_RATE_PER_SECOND) return;
    const size_t quoted = (std::min)(original.size(),size_t(IP_HEADER_SIZE + 8));
    std::vector<unsigned char> response(IP_HEADER_SIZE + 8 + quoted,0);
    response[0] = 0x45; response[2] = uint8_t(response.size() >> 8); response[3] = uint8_t(response.size());
    response[4] = uint8_t(m_identification >> 8); response[5] = uint8_t(m_identification++);
    response[8] = IP_DEFAULT_TTL; response[9] = IP_PROTOCOL_ICMP;
    WriteIp(response.data()+12,m_interfaces[packet.incoming].ip); memcpy(response.data()+16,original.data()+12,4);
    response[20] = type; response[21] = code; memcpy(response.data()+28,original.data(),quoted);
    const uint16_t checksum = Checksum(response.data()+20,response.size()-20);
    response[22] = uint8_t(checksum >> 8); response[23] = uint8_t(checksum);
    FixIpChecksum(response.data(),IP_HEADER_SIZE);
    // [assignment7] 오류는 유입 NIC의 이전 홉 MAC으로 반환한다. quote에는 TTL 감소 전 원본을 넣는다.
    m_interfaces[packet.incoming].ethernet->SendTo(response.data(),static_cast<int>(response.size()),
        ETHERNET_TYPE_IPV4,packet.previousMac);
}
void CIPRouter::Report(const CString& message) const
{
    if (!m_window) return;
    CString* text = new CString(message);
    if (!::PostMessage(m_window,WM_NETWORK_EVENT,0,reinterpret_cast<LPARAM>(text))) delete text;
}
