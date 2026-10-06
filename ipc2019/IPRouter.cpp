#include "pch.h"
#include "IPRouter.h"
#include "ARPLayer.h"
#include "EthernetLayer.h"

using namespace NetworkPackets;

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
    // [assignment6] /32 proxy 정적 경로가 connected subnet보다 구체적이므로 먼저 확인한다.
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
    // [assignment6] 이미 next-hop 큐에 들어간 해당 /32 데이터도 취소해 삭제한 경로로 뒤늦게 보내지 않는다.
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
    // [assignment6] 도달 경로가 없거나 요청이 들어온 NIC와 출력 NIC가 같으면 대리 Reply하지 않는다.
    return m_running && ValidInterface(incoming) && !IsDirectedBroadcast(target) && FindRoute(target,outgoing,nextHop) &&
        outgoing != incoming && m_interfaces[incoming].arp->IsUsable() && m_interfaces[outgoing].arp->IsUsable();
}
bool CIPRouter::IsLocalAddress(uint32_t address)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    // [assignment6] 두 IP 계층은 같은 앱/UI를 공유한다. 반대 NIC로 들어온 중계 PC 자신의 응답도 로컬 수신한다.
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
    // [assignment6] 중계 PC 자체의 Chat/File도 출력 경로를 사용한다. 자체 생성이므로 TTL을 감소시키지 않는다.
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
    if (output.arp->SendRequest(nextHop)) return TRUE;
    m_pendingBytes -= m_pending.back().packet.size(); m_pending.pop_back(); return FALSE;
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
    if (!FindRoute(destination,pending.outgoing,pending.nextHop) || pending.outgoing == incoming ||
        !m_interfaces[pending.outgoing].arp->IsUsable()) {
        ++m_stats.dropped; ErrorLocked(pending,3,0); return FALSE;
    }
    // [assignment6] 기존 source/destination/protocol/ID/fragment 필드를 보존하고 TTL만 1 감소시킨다.
    // [assignment6] IPv4 조각도 각각 전달할 수 있으나 router가 재조립하지 않는다. IP 옵션은 미지원이다.
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
    // [assignment6] ARP SendRequest는 ARP lock을 해제한 후 Ethernet을 호출한다. ARP 수신 콜백도 반대로 lock을 놓는다.
    if (output.arp->SendRequest(nextHop)) return TRUE;
    ++m_stats.dropped; ErrorLocked(m_pending.back(),3,1);
    m_pendingBytes -= m_pending.back().packet.size(); m_pending.pop_back(); return FALSE;
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
    // [assignment6] 캐시 갱신과 큐 등록의 경쟁/한 번 놓친 Reply도 정기 조회로 복구한다.
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
    if (offset & 0x1fff) return; // [assignment6] 첫 IPv4 조각이 아닌 경우에는 ICMP 오류를 보내지 않는다.
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
    // [assignment6] 오류는 유입 NIC의 이전 홉 MAC으로 반환한다. quote에는 TTL 감소 전 원본을 넣는다.
    m_interfaces[packet.incoming].ethernet->SendTo(response.data(),static_cast<int>(response.size()),
        ETHERNET_TYPE_IPV4,packet.previousMac);
}
void CIPRouter::Report(const CString& message) const
{
    if (!m_window) return;
    CString* text = new CString(message);
    if (!::PostMessage(m_window,WM_NETWORK_EVENT,0,reinterpret_cast<LPARAM>(text))) delete text;
}
