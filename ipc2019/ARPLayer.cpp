#include "pch.h"
#include "ARPLayer.h"
#include "IPLayer.h"
#include "EthernetLayer.h"

using namespace NetworkPackets;

CARPLayer::CARPLayer(const char* name) : CBaseLayer(name) {}

void CARPLayer::Configure(uint32_t localIp, const unsigned char* localMac, CIPLayer* ipLayer)
{
    // [assignment6] 설정은 NI를 시작하기 전에 완료한다. 실행 중 주소 변경은 연결 해제 후에만 허용한다.
    Reset();
    m_localIp = localIp;
    memcpy(m_localMac, localMac, sizeof(m_localMac));
    m_ipLayer = ipLayer;
}

void CARPLayer::Reset()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_cache.clear(); m_proxies.clear();
    m_localIp = 0; m_ipLayer = nullptr;
    memset(m_localMac, 0, sizeof(m_localMac));
    memset(m_lastGarpMac, 0, sizeof(m_lastGarpMac)); m_lastGarpAt = 0;
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
    if (!m_localIp || !IsUnicastIp(targetIp) || targetIp == m_localIp) return FALSE;
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
    const BOOL sent = Transmit(ARP_OPERATION_REQUEST, m_localIp, m_localMac,
        targetIp, unknown, broadcast);
    NotifyChanged();
    if (!sent) Report(_T("ARP Request 송신 실패"));
    return sent;
}

BOOL CARPLayer::SendGratuitous(const unsigned char* advertisedMac)
{
    if (!m_localIp || !IsUnicastMac(advertisedMac)) return FALSE;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        // [assignment6] Npcap이 방금 송신한 실습용 GARP를 다시 캡처해도 자신의 IP 충돌로 오인하지 않는다.
        memcpy(m_lastGarpMac, advertisedMac, 6); m_lastGarpAt = GetTickCount64();
    }
    const unsigned char broadcast[6] = {255,255,255,255,255,255}, unknown[6] = {};
    // [assignment6] GARP는 별도 opcode가 아니다. Sender IP == Target IP인 ARP Request를 광고한다.
    const BOOL sent = Transmit(ARP_OPERATION_REQUEST, m_localIp, advertisedMac,
        m_localIp, unknown, broadcast);
    Report(sent ? _T("GARP 광고 송신 완료 (응답 대기 없음)") : _T("GARP 송신 실패"));
    return sent;
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
    if (!IsUnicastIp(targetIp)) return FALSE;
    bool changed = false, reply = false, conflict = false, proxyReply = false;
    const ULONGLONG now = GetTickCount64();
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (senderIp == m_localIp && targetIp == m_localIp && operation == ARP_OPERATION_REQUEST &&
            m_lastGarpAt && now - m_lastGarpAt < ARP_OWN_GARP_WINDOW_MS &&
            !memcmp(packet.senderMac, m_lastGarpMac, 6)) return FALSE;
        conflict = senderIp == m_localIp && memcmp(packet.senderMac, m_localMac, 6) != 0;
        if (!conflict) {
            bool proxy = std::any_of(m_proxies.begin(), m_proxies.end(),
                [targetIp](const ARP_PROXY_ENTRY& entry) { return entry.ip == targetIp; });
            const bool forUs = targetIp == m_localIp || proxy;
            auto entry = std::find_if(m_cache.begin(), m_cache.end(),
                [senderIp](const ARP_CACHE_ENTRY& value) { return value.ip == senderIp; });
            // [assignment6] RFC 826: 기존 Sender 매핑은 갱신하고, 새 항목은 자신/Proxy 대상 요청에서 학습한다.
            // [assignment6] GARP도 기존 매핑을 갱신한다. Probe의 Sender IP 0.0.0.0은 캐시에 넣지 않는다.
            if (senderIp && senderIp != m_localIp && (entry != m_cache.end() || forUs)) {
                if (entry == m_cache.end() && m_cache.size() < ARP_CACHE_MAX_ENTRIES) {
                    ARP_CACHE_ENTRY value; value.ip = senderIp;
                    m_cache.push_back(value); entry = m_cache.end() - 1;
                }
                if (entry != m_cache.end()) {
                    memcpy(entry->mac, packet.senderMac, 6); entry->complete = true;
                    entry->expiresAt = now + ARP_COMPLETE_TIMEOUT_MS; changed = true;
                }
            }
            reply = operation == ARP_OPERATION_REQUEST && forUs;
            proxyReply = reply && proxy;
        }
    }
    if (conflict) { Report(_T("IP 충돌 감지: 다른 MAC이 현재 내 IP를 사용하고 있습니다.")); return FALSE; }
    if (changed) {
        NotifyChanged();
        if (operation == ARP_OPERATION_REPLY) Report(_T("ARP Reply 수신: 캐시를 갱신했습니다."));
    }
    if (reply) {
        // [assignment6] PARP는 Target IP를 자신의 MAC에 대응시켜 대리 응답한다.
        // [assignment6] 실제 타 호스트로 IP 패킷을 전달하는 라우터 기능과는 별개의 ARP 응답 기능이다.
        const BOOL sent = Transmit(ARP_OPERATION_REPLY, targetIp, m_localMac,
            senderIp, packet.senderMac, packet.senderMac);
        if (!sent) Report(_T("ARP Reply 송신 실패"));
        else if (proxyReply) Report(_T("Proxy ARP Reply 송신 완료"));
    }
    // [assignment6] 잠금을 해제한 상태에서 IP에 알려 ARP 대기 중이던 채팅을 실제 송신한다.
    if (changed && m_ipLayer) m_ipLayer->OnArpResolved(senderIp);
    return TRUE;
}

void CARPLayer::Tick(ULONGLONG now)
{
    bool changed;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        const size_t before = m_cache.size();
        m_cache.erase(std::remove_if(m_cache.begin(), m_cache.end(),
            [now](const ARP_CACHE_ENTRY& entry) { return now >= entry.expiresAt; }), m_cache.end());
        changed = before != m_cache.size();
    }
    if (changed) NotifyChanged();
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
bool CARPLayer::AddProxy(uint32_t ip, const CString& device)
{
    if (!m_localIp || !IsUnicastIp(ip) || ip == m_localIp) return false;
    std::lock_guard<std::mutex> lock(m_mutex);
    for (const auto& entry : m_proxies) if (entry.ip == ip) return false;
    if (m_proxies.size() >= ARP_CACHE_MAX_ENTRIES) return false;
    ARP_PROXY_ENTRY entry; entry.ip = ip; entry.device = device;
    m_proxies.push_back(entry); NotifyChanged(); return true;
}
void CARPLayer::DeleteProxy(uint32_t ip)
{
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
    CString* text = new CString(message);
    if (!::PostMessage(m_window, WM_NETWORK_EVENT, 0, reinterpret_cast<LPARAM>(text))) delete text;
}
