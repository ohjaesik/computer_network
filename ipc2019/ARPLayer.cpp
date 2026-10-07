#include "pch.h"
#include "ARPLayer.h"
#include "IPLayer.h"
#include "EthernetLayer.h"
#include "IPRouter.h"

using namespace NetworkPackets;

CARPLayer::CARPLayer(const char* name) : CBaseLayer(name) {}

void CARPLayer::Configure(uint32_t localIp, const unsigned char* localMac, CIPLayer* ipLayer)
{
    // [assignment6] 설정은 NI를 시작하기 전에 완료한다. 실행 중 주소 변경은 연결 해제 후에만 허용한다.
    Reset();
    m_localIp = localIp;
    memcpy(m_localMac, localMac, sizeof(m_localMac));
    memcpy(m_hardwareMac, localMac, 6); memcpy(m_candidateMac, localMac, 6);
    m_addressState = ADDRESS_STATE::Ready; // [assignment7] UI는 수신 시작 직전에 BeginAddressCheck로 Probe를 시작한다.
    m_ipLayer = ipLayer;
}

void CARPLayer::Reset()
{
    if (GetUnderLayer()) static_cast<CEthernetLayer*>(GetUnderLayer())->SetProbeAddress(nullptr);
    std::lock_guard<std::mutex> lock(m_mutex);
    m_cache.clear(); m_proxies.clear();
    m_localIp = 0; m_ipLayer = nullptr;
    memset(m_localMac, 0, sizeof(m_localMac));
    memset(m_lastGarpMac, 0, sizeof(m_lastGarpMac)); m_lastGarpAt = 0;
    memset(m_hardwareMac,0,6); memset(m_candidateMac,0,6);
    m_addressState = ADDRESS_STATE::Disabled; m_probesSent = m_announcementsSent = m_conflicts = 0;
    m_nextActionAt = m_lastDefendAt = m_lastAttemptAt = 0; m_router = nullptr;
    NotifyChanged();
}

bool CARPLayer::Lookup(uint32_t ip, unsigned char* mac)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    const ULONGLONG now = GetTickCount64();
    for (const auto& entry : m_cache) {
        if (entry.ip == ip && entry.complete && now < entry.expiresAt) {
            memcpy(mac, entry.mac, 6); return true;
        }
    }
    return false;
}

// [assignment6] Ethernet에는 목적지 MAC과 EtherType을 호출마다 전달한다.
// [assignment6] ARP 요청으로 공통 목적지 MAC을 broadcast로 덮어써 파일 송신에 영향을 주지 않는다.
BOOL CARPLayer::Transmit(unsigned short operation, uint32_t senderIp, const unsigned char* senderMac,
    uint32_t targetIp, const unsigned char* targetMac, const unsigned char* ethernetDestination)
{
    if (!GetUnderLayer()) return FALSE;
    ARP_PACKET packet = {};
    packet.hardwareType = htons(ARP_HARDWARE_ETHERNET);
    packet.protocolType = htons(ETHERNET_TYPE_IPV4);
    packet.hardwareLength = 6; packet.protocolLength = 4;
    packet.operation = htons(operation);
    memcpy(packet.senderMac, senderMac, 6); WriteIp(packet.senderIp, senderIp);
    memcpy(packet.targetMac, targetMac, 6); WriteIp(packet.targetIp, targetIp);
    return static_cast<CEthernetLayer*>(GetUnderLayer())->SendTo(
        reinterpret_cast<unsigned char*>(&packet), sizeof(packet), ETHERNET_TYPE_ARP,
        ethernetDestination, senderMac);
}

BOOL CARPLayer::SendRequest(uint32_t targetIp, bool force)
{
    if (!m_localIp || !IsUsable() || !IsUnicastIp(targetIp) || targetIp == m_localIp) return FALSE;
    const ULONGLONG now = GetTickCount64();
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto found = std::find_if(m_cache.begin(), m_cache.end(),
            [targetIp](const ARP_CACHE_ENTRY& entry) { return entry.ip == targetIp; });
        if (found != m_cache.end() && !force) {
            if (found->complete && now < found->expiresAt) return TRUE;
            if (found->lastRequestAt && now - found->lastRequestAt < ARP_REQUEST_INTERVAL_MS) return TRUE;
        }
        if (found == m_cache.end()) {
            if (m_cache.size() >= ARP_CACHE_MAX_ENTRIES) return FALSE;
            ARP_CACHE_ENTRY entry; entry.ip = targetIp;
            entry.expiresAt = now + ARP_INCOMPLETE_TIMEOUT_MS;
            m_cache.push_back(entry); found = m_cache.end() - 1;
        } else if (now >= found->expiresAt) {
            found->complete = false; memset(found->mac, 0, 6);
            found->expiresAt = now + ARP_INCOMPLETE_TIMEOUT_MS;
        }
        // [assignment6] 재요청 때문에 Incomplete의 3분 수명이 무한히 연장되지 않게 한다.
        found->lastRequestAt = now;
    }
    const unsigned char broadcast[6] = {255,255,255,255,255,255}, unknown[6] = {};
    unsigned char local[6]; GetEffectiveMac(local);
    const BOOL sent = Transmit(ARP_OPERATION_REQUEST, m_localIp, local,
        targetIp, unknown, broadcast);
    NotifyChanged();
    if (!sent) Report(_T("ARP Request 송신 실패"));
    return sent;
}

BOOL CARPLayer::SendGratuitous(const unsigned char* advertisedMac)
{
    if (!m_localIp || !IsUnicastMac(advertisedMac)) return FALSE;
    unsigned char current[6]; GetEffectiveMac(current);
    if (memcmp(current,advertisedMac,6) || !IsUsable()) {
        // [assignment7] 이전에는 광고만 바뀌었지만 이제 변경 MAC을 검증 후 실제 앱 송신/필터에도 적용한다.
        return BeginAddressCheck(advertisedMac);
    }
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        // [assignment7] Npcap이 방금 송신한 실습용 GARP를 다시 캡처해도 자신의 IP 충돌로 오인하지 않는다.
        memcpy(m_lastGarpMac, advertisedMac, 6); m_lastGarpAt = GetTickCount64();
        m_addressState = ADDRESS_STATE::Announcing; m_announcementsSent = 1;
        m_nextActionAt = GetTickCount64() + ARP_ANNOUNCE_INTERVAL_MS;
    }
    // [assignment7] GARP는 별도 opcode가 아니다. Sender IP == Target IP인 ARP Request를 광고한다.
    const BOOL sent = SendAnnouncement(advertisedMac);
    if (!sent) { std::lock_guard<std::mutex> lock(m_mutex); m_addressState = ADDRESS_STATE::Conflict; }
    NotifyChanged(); Report(sent ? _T("GARP 광고 1/2 송신 (응답 대기 없음)") : _T("GARP 송신 실패: 주소 사용 중단"));
    return sent;
}

bool CARPLayer::BeginAddressCheck(const unsigned char* candidateMac)
{
    if (!m_localIp || !IsUnicastMac(candidateMac)) return false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        const ULONGLONG now = GetTickCount64();
        if (m_addressState == ADDRESS_STATE::Probing ||
            (m_conflicts >= ARP_MAX_CONFLICTS && m_lastAttemptAt && now - m_lastAttemptAt < ARP_RATE_LIMIT_MS)) return false;
        memcpy(m_candidateMac,candidateMac,6); m_addressState = ADDRESS_STATE::Probing;
        m_probesSent = m_announcementsSent = 0; m_lastAttemptAt = now;
        m_nextActionAt = now + std::uniform_int_distribution<unsigned int>(0,static_cast<unsigned int>(ARP_PROBE_WAIT_MS))(m_random);
    }
    // [assignment7] 광고 전에는 유효 MAC을 바꾸지 않되 Probe에 대한 unicast Reply를 새 MAC으로 수신한다.
    if (GetUnderLayer()) static_cast<CEthernetLayer*>(GetUnderLayer())->SetProbeAddress(candidateMac);
    NotifyChanged(); Report(_T("주소 충돌 검사 시작: Probe 3회 동안 IP 송수신/중계를 보류합니다.")); return true;
}
bool CARPLayer::IsUsable() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_addressState == ADDRESS_STATE::Ready || m_addressState == ADDRESS_STATE::Announcing;
}
CARPLayer::ADDRESS_STATE CARPLayer::GetAddressState() const
{
    std::lock_guard<std::mutex> lock(m_mutex); return m_addressState;
}
void CARPLayer::GetEffectiveMac(unsigned char* mac) const
{
    std::lock_guard<std::mutex> lock(m_mutex); memcpy(mac,m_localMac,6);
}
// [assignment7] Sender IP와 Target IP를 내 IP로 설정한 broadcast GARP를 전송한다.
BOOL CARPLayer::SendAnnouncement(const unsigned char* mac)
{
    const unsigned char broadcast[6] = {255,255,255,255,255,255}, unknown[6] = {};
    return Transmit(ARP_OPERATION_REQUEST,m_localIp,mac,m_localIp,unknown,broadcast);
}

BOOL CARPLayer::Receive(unsigned char* payload, int length, const unsigned char* source)
{
    if (!payload || !source || length < ARP_PACKET_SIZE || !m_localIp) return FALSE;
    // [assignment6] 캡처 버퍼를 구조체로 직접 참조하지 않고 28바이트를 복사해 정렬/길이 문제를 피한다.
    ARP_PACKET packet;
    memcpy(&packet, payload, sizeof(packet));
    const unsigned short operation = ntohs(packet.operation);
    if (ntohs(packet.hardwareType) != ARP_HARDWARE_ETHERNET ||
        ntohs(packet.protocolType) != ETHERNET_TYPE_IPV4 || packet.hardwareLength != 6 ||
        packet.protocolLength != 4 || (operation != ARP_OPERATION_REQUEST && operation != ARP_OPERATION_REPLY) ||
        !IsUnicastMac(packet.senderMac) || memcmp(source, packet.senderMac, 6)) return FALSE;

    const uint32_t senderIp = ReadIp(packet.senderIp), targetIp = ReadIp(packet.targetIp);
    if (senderIp && !IsUnicastIp(senderIp)) return FALSE;
    // [assignment7] Probe에 대한 Reply는 요청자의 SPA가 0이었으므로 Target IP도 0.0.0.0이다.
    if ((targetIp && !IsUnicastIp(targetIp)) || (!targetIp && operation != ARP_OPERATION_REPLY)) return FALSE;
    bool changed = false, reply = false, conflict = false, proxyReply = false, defend = false, stopped = false;
    unsigned char local[6] = {};
    const ULONGLONG now = GetTickCount64();
    bool registeredProxy = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        registeredProxy = std::any_of(m_proxies.begin(),m_proxies.end(),
            [targetIp](const ARP_PROXY_ENTRY& entry) { return entry.ip == targetIp; });
    }
    // [assignment7] ARP lock 밖에서 경로를 확인한다. route lock -> ARP lookup과의 교착을 막는다.
    // [assignment7] 중계 PC 앱이 NIC 1의 IP로 NIC 2 쪽에 송신했을 때 돌아오는 응답을 위해
    // [assignment7] 다른 NIC에 속한 자신의 IP도 대리 해석한다. 충돌로 사용 중단된 주소에는 응답하지 않는다.
    const bool proxyAvailable = m_router &&
        (registeredProxy || (targetIp != m_localIp && m_router->IsLocalAddress(targetIp))) &&
        m_router->CanProxyReply(m_interfaceIndex,targetIp);
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (senderIp == m_localIp && targetIp == m_localIp && operation == ARP_OPERATION_REQUEST &&
            m_lastGarpAt && now - m_lastGarpAt < ARP_OWN_GARP_WINDOW_MS &&
            !memcmp(packet.senderMac, m_lastGarpMac, 6)) return FALSE;
        const bool ownMac = !memcmp(packet.senderMac,m_localMac,6) || !memcmp(packet.senderMac,m_hardwareMac,6) ||
            (m_addressState == ADDRESS_STATE::Probing && !memcmp(packet.senderMac,m_candidateMac,6));
        if (ownMac) return FALSE;
        conflict = senderIp == m_localIp || (m_addressState == ADDRESS_STATE::Probing &&
            operation == ARP_OPERATION_REQUEST && !senderIp && targetIp == m_localIp);
        memcpy(local,m_localMac,6);
        // [assignment7] GARP/ACD 확장: 주소 충돌 시 방어 광고를 보내거나 주소 사용을 중단한다.
        if (conflict && m_addressState != ADDRESS_STATE::Conflict) {
            ++m_conflicts;
            if (m_addressState == ADDRESS_STATE::Probing || (m_lastDefendAt && now - m_lastDefendAt < ARP_DEFEND_INTERVAL_MS)) {
                m_addressState = ADDRESS_STATE::Conflict; stopped = true;
            } else { m_lastDefendAt = now; defend = true; }
        }
        if (!conflict) {
            bool proxy = proxyAvailable;
            const bool forUs = targetIp == m_localIp || proxy;
            const bool usable = m_addressState == ADDRESS_STATE::Ready || m_addressState == ADDRESS_STATE::Announcing;
            auto entry = std::find_if(m_cache.begin(), m_cache.end(),
                [senderIp](const ARP_CACHE_ENTRY& value) { return value.ip == senderIp; });
            // [assignment6] RFC 826: 기존 Sender 매핑은 갱신하고, 새 항목은 자신/Proxy 대상 요청에서 학습한다.
            // [assignment7] GARP도 기존 매핑을 갱신한다. Probe의 Sender IP 0.0.0.0은 캐시에 넣지 않는다.
            // [assignment7] 유효한 unsolicited GARP는 새 항목도 학습하여 처음 보는 IP/MAC 광고도 반영한다.
            const bool announcement = senderIp && senderIp == targetIp;
            if (senderIp && senderIp != m_localIp && (entry != m_cache.end() || forUs || announcement)) {
                if (entry == m_cache.end() && m_cache.size() < ARP_CACHE_MAX_ENTRIES) {
                    ARP_CACHE_ENTRY value; value.ip = senderIp;
                    m_cache.push_back(value); entry = m_cache.end() - 1;
                }
                if (entry != m_cache.end()) {
                    memcpy(entry->mac, packet.senderMac, 6); entry->complete = true;
                    entry->expiresAt = now + ARP_COMPLETE_TIMEOUT_MS; changed = true;
                }
            }
            reply = usable && operation == ARP_OPERATION_REQUEST && forUs && !announcement;
            proxyReply = reply && proxy;
        }
    }
    if (conflict) {
        if (defend) {
            if (!SendAnnouncement(local)) { std::lock_guard<std::mutex> lock(m_mutex); m_addressState = ADDRESS_STATE::Conflict; stopped = true; }
            else Report(_T("IP 충돌 감지: GARP 1회로 주소를 방어했습니다. 10초 이내 재충돌 시 사용 중단합니다."));
        }
        if (stopped) Report(_T("IP 충돌: 주소 사용/ARP Reply/IP 송수신 및 중계를 중단했습니다. 연결 해제 후 다른 IP로 설정하세요."));
        if (stopped && GetUnderLayer()) static_cast<CEthernetLayer*>(GetUnderLayer())->SetProbeAddress(nullptr);
        NotifyChanged(); return FALSE;
    }
    if (changed) {
        NotifyChanged();
        if (operation == ARP_OPERATION_REPLY) Report(_T("ARP Reply 수신: 캐시를 갱신했습니다."));
    }
    if (reply) {
        // [assignment7] PARP는 Target IP를 자신의 MAC에 대응시켜 대리 응답한다.
        // [assignment7] 대리 응답한 IP 데이터는 연결된 CIPRouter가 다른 NIC의 next hop으로 전달한다.
        const BOOL sent = Transmit(ARP_OPERATION_REPLY, targetIp, local,
            senderIp, packet.senderMac, packet.senderMac);
        if (!sent) Report(_T("ARP Reply 송신 실패"));
        else if (proxyReply) Report(_T("Proxy ARP Reply 송신 완료"));
    }
    // [assignment6] 잠금을 해제한 상태에서 IP에 알려 ARP 대기 중이던 채팅을 실제 송신한다.
    if (changed && m_ipLayer) m_ipLayer->OnArpResolved(senderIp);
    if (changed && m_router) m_router->OnArpResolved(m_interfaceIndex,senderIp);
    return TRUE;
}

void CARPLayer::Tick(ULONGLONG now)
{
    bool changed;
    int action = 0; unsigned char mac[6] = {};
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        const size_t before = m_cache.size();
        m_cache.erase(std::remove_if(m_cache.begin(), m_cache.end(),
            [now](const ARP_CACHE_ENTRY& entry) { return now >= entry.expiresAt; }), m_cache.end());
        changed = before != m_cache.size();
        // [assignment7] GARP/ACD의 Probe·Announcement 타이머 상태를 진행한다. 위 캐시 만료는 과제 6이다.
        if (m_addressState == ADDRESS_STATE::Probing && now >= m_nextActionAt) {
            memcpy(mac,m_candidateMac,6);
            if (m_probesSent < ARP_PROBE_COUNT) {
                action = 1; ++m_probesSent;
                m_nextActionAt = now + (m_probesSent == ARP_PROBE_COUNT ? ARP_ANNOUNCE_WAIT_MS :
                    std::uniform_int_distribution<unsigned int>(static_cast<unsigned int>(ARP_PROBE_MIN_MS),
                        static_cast<unsigned int>(ARP_PROBE_MAX_MS))(m_random));
            } else {
                // [assignment7] 검사를 통과한 MAC을 실제 Ethernet 송신 및 수신 필터와 일치시킨다.
                memcpy(m_localMac,m_candidateMac,6);
                m_announcementsSent = 1; m_nextActionAt = now + ARP_ANNOUNCE_INTERVAL_MS;
                m_lastDefendAt = 0; action = 2;
            }
        } else if (m_addressState == ADDRESS_STATE::Announcing && now >= m_nextActionAt) {
            memcpy(mac,m_localMac,6); action = 2;
            ++m_announcementsSent;
        }
    }
    if (changed) NotifyChanged();
    if (action) {
        BOOL sent = FALSE;
        if (action == 1) {
            const unsigned char broadcast[6] = {255,255,255,255,255,255}, unknown[6] = {};
            sent = Transmit(ARP_OPERATION_REQUEST,0,mac,m_localIp,unknown,broadcast);
        } else {
            if (GetUnderLayer()) static_cast<CEthernetLayer*>(GetUnderLayer())->SetLogicalSourceAddress(mac);
            sent = SendAnnouncement(mac);
            if (sent) {
                std::lock_guard<std::mutex> lock(m_mutex);
                // [assignment7] 첫 Announcement가 송신된 뒤에만 주소를 usable로 전환한다.
                // [assignment7] 그 사이 수신 worker가 감지한 Conflict 상태는 다시 Ready로 덮어쓰지 않는다.
                if (m_addressState != ADDRESS_STATE::Conflict) {
                    m_addressState = m_announcementsSent >= ARP_ANNOUNCE_COUNT ? ADDRESS_STATE::Ready : ADDRESS_STATE::Announcing;
                    Report(_T("주소 검사 완료: GARP Announcement 송신 · IP 송수신 사용 가능"));
                }
            }
        }
        if (!sent) {
            std::lock_guard<std::mutex> lock(m_mutex); m_addressState = ADDRESS_STATE::Conflict;
            if (GetUnderLayer()) static_cast<CEthernetLayer*>(GetUnderLayer())->SetProbeAddress(nullptr);
            Report(_T("주소 검사/광고 송신 실패: 주소 사용 중단"));
        }
        NotifyChanged();
    }
}

void CARPLayer::DeleteEntry(uint32_t ip)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_cache.erase(std::remove_if(m_cache.begin(), m_cache.end(),
        [ip](const ARP_CACHE_ENTRY& entry) { return entry.ip == ip; }), m_cache.end());
    NotifyChanged();
}
void CARPLayer::ClearCache()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_cache.clear(); NotifyChanged();
}
bool CARPLayer::AddProxy(uint32_t ip, const CString& device, int outgoing, uint32_t nextHop)
{
    if (!m_localIp || !IsUnicastIp(ip) || ip == m_localIp) return false;
    // [assignment7] 실제 다른 NIC의 경로가 없는 PARP 등록은 거부해 트래픽 black hole을 만들지 않는다.
    if (!m_router || outgoing < 0 || !m_router->InstallProxyRoute(m_interfaceIndex,ip,outgoing,nextHop)) return false;
    std::lock_guard<std::mutex> lock(m_mutex);
    ARP_PROXY_ENTRY entry; entry.ip = ip; entry.device = device; entry.outgoing = outgoing; entry.nextHop = nextHop;
    m_proxies.push_back(entry); NotifyChanged(); return true;
}
void CARPLayer::DeleteProxy(uint32_t ip)
{
    if (m_router) m_router->RemoveProxyRoute(m_interfaceIndex,ip);
    std::lock_guard<std::mutex> lock(m_mutex);
    m_proxies.erase(std::remove_if(m_proxies.begin(), m_proxies.end(),
        [ip](const ARP_PROXY_ENTRY& entry) { return entry.ip == ip; }), m_proxies.end());
    NotifyChanged();
}
void CARPLayer::GetSnapshot(std::vector<ARP_CACHE_ENTRY>& cache, std::vector<ARP_PROXY_ENTRY>& proxies)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    cache = m_cache; proxies = m_proxies; // [assignment6] UI는 복사본만 읽어 잠금 중 컨트롤을 조작하지 않는다.
}
void CARPLayer::NotifyChanged() const
{
    if (m_window) ::PostMessage(m_window, WM_ARP_CHANGED, 0, 0);
}
void CARPLayer::Report(const CString& message) const
{
    if (!m_window) return;
    CString* text = new CString;
    text->Format(_T("[NIC %d] %s"),m_interfaceIndex+1,static_cast<LPCTSTR>(message));
    if (!::PostMessage(m_window, WM_NETWORK_EVENT, 0, reinterpret_cast<LPARAM>(text))) delete text;
}
