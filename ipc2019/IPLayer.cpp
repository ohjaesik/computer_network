#include "pch.h"
#include "IPLayer.h"
#include "ARPLayer.h"
#include "EthernetLayer.h"

using namespace NetworkPackets;

CIPLayer::CIPLayer(const char* name) : CBaseLayer(name) {}

void CIPLayer::Configure(uint32_t source, CARPLayer* arp)
{
    Reset();
    m_sourceIp = source; m_arp = arp;
}
void CIPLayer::Reset()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_pending.clear(); m_pendingBytes = 0;
    m_sourceIp = m_destinationIp = 0; m_arp = nullptr; m_identification = 0;
}
bool CIPLayer::SetDestination(uint32_t destination)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_sourceIp || (destination != IPV4_BROADCAST && !IsUnicastIp(destination)) || destination == m_sourceIp)
        return false;
    // [assignment6] ARP 대기 중 상대 IP를 바꾸면 이전 메시지가 다른 상대에게 갈 수 있으므로 막는다.
    if (destination != m_destinationIp && !m_pending.empty()) return false;
    m_destinationIp = destination; return true;
}
uint32_t CIPLayer::GetDestination()
{
    std::lock_guard<std::mutex> lock(m_mutex); return m_destinationIp;
}
bool CIPLayer::IsDestinationResolved()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    unsigned char mac[6];
    return m_destinationIp == IPV4_BROADCAST ||
        (m_arp && m_destinationIp && m_arp->Lookup(m_destinationIp, mac));
}
BOOL CIPLayer::RequestDestination()
{
    uint32_t destination = GetDestination();
    return destination == IPV4_BROADCAST || (m_arp && m_arp->SendRequest(destination));
}

bool CIPLayer::CanAcceptChat(int bytes)
{
    if (bytes <= 0 || bytes > CHAT_MAX_MESSAGE_SIZE) return false;
    std::lock_guard<std::mutex> lock(m_mutex);
    // [assignment6] 한 메시지를 부분적으로만 큐에 넣지 않도록 UI 송신 전에 전체 공간을 확인한다.
    const size_t fragments = (bytes + CHAT_APP_DATA_SIZE - 1) / CHAT_APP_DATA_SIZE;
    return m_pendingBytes + bytes + fragments * (IP_HEADER_SIZE + CHAT_APP_HEADER_SIZE) <= IP_PENDING_MAX_BYTES;
}

BOOL CIPLayer::Transmit(std::vector<unsigned char>& packet, const unsigned char* destination)
{
    return GetUnderLayer() && static_cast<CEthernetLayer*>(GetUnderLayer())->SendTo(
        packet.data(), static_cast<int>(packet.size()), ETHERNET_TYPE_IPV4, destination);
}

BOOL CIPLayer::Send(unsigned char* payload, int length, unsigned short protocol)
{
    if (!payload || length <= 0 || length > IP_MAX_DATA_SIZE ||
        (protocol != IP_PROTOCOL_CHAT && protocol != IP_PROTOCOL_FILE)) return FALSE;
    uint32_t requestIp = 0;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_sourceIp || !m_destinationIp || !m_arp) return FALSE;
        // [assignment6] IPv4 기본 헤더 20바이트. 앱에서 1480바이트 이하로 나눴으므로 DF=1, offset=0이다.
        std::vector<unsigned char> packet(IP_HEADER_SIZE + length, 0);
        IPV4_HEADER header = {};
        header.versionIhl = 0x45;
        header.totalLength = htons(static_cast<uint16_t>(packet.size()));
        header.identification = htons(m_identification++);
        header.flagsOffset = htons(0x4000);
        header.ttl = IP_DEFAULT_TTL;
        header.protocol = static_cast<unsigned char>(protocol);
        WriteIp(header.source, m_sourceIp); WriteIp(header.destination, m_destinationIp);
        header.checksum = htons(Checksum(reinterpret_cast<const unsigned char*>(&header), sizeof(header)));
        memcpy(packet.data(), &header, sizeof(header));
        memcpy(packet.data() + IP_HEADER_SIZE, payload, length);

        unsigned char mac[6];
        if (m_destinationIp == IPV4_BROADCAST) {
            memset(mac, 0xff, 6); return Transmit(packet, mac);
        }
        if (m_arp->Lookup(m_destinationIp, mac)) {
            FlushLocked(m_destinationIp, mac); // [assignment6] 먼저 대기하던 조각이 새 조각보다 먼저 나가게 한다.
            return Transmit(packet, mac);
        }
        // [assignment6] 파일은 UI에서 ARP 완료 후 worker를 시작한다. 도중 해석이 사라지면 실패를 반환한다.
        // [assignment6] 파일 전체를 무제한 큐에 넣거나 실제 송신 전에 진행률을 올리지 않는다.
        if (protocol == IP_PROTOCOL_FILE) return FALSE;
        if (m_pendingBytes + packet.size() > IP_PENDING_MAX_BYTES) {
            m_pending.clear(); m_pendingBytes = 0;
            Report(_T("ARP 대기 버퍼 부족: 대기 중인 채팅을 취소했습니다.")); return FALSE;
        }
        PENDING_PACKET pending;
        pending.bytes.swap(packet); pending.destination = m_destinationIp;
        // [assignment6] 한 메시지의 조각은 같은 만료 시점을 사용하여 절반만 뒤늦게 보내지 않는다.
        pending.expiresAt = m_pending.empty() ? GetTickCount64() + IP_RESOLVE_TIMEOUT_MS : m_pending.front().expiresAt;
        m_pendingBytes += pending.bytes.size(); m_pending.push_back(std::move(pending));
        requestIp = m_destinationIp;
    }
    // [assignment6] TRUE는 채팅의 송신 요청 접수까지 포함한다. UI는 ACK/전달 완료로 표현하지 않는다.
    if (!m_arp->SendRequest(requestIp)) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_pending.clear(); m_pendingBytes = 0; return FALSE;
    }
    // [assignment6] 큐 등록 직전/직후에 Reply가 도착해 알림을 놓친 경우도 다시 조회해 처리한다.
    OnArpResolved(requestIp);
    return TRUE;
}

void CIPLayer::FlushLocked(uint32_t ip, const unsigned char* mac)
{
    while (!m_pending.empty() && m_pending.front().destination == ip) {
        if (GetTickCount64() >= m_pending.front().expiresAt) {
            m_pending.clear(); m_pendingBytes = 0;
            Report(_T("ARP 응답 지연으로 대기 중인 채팅 송신을 취소했습니다.")); return;
        }
        const BOOL sent = Transmit(m_pending.front().bytes, mac);
        m_pendingBytes -= m_pending.front().bytes.size(); m_pending.pop_front();
        if (!sent) {
            m_pending.clear(); m_pendingBytes = 0;
            Report(_T("ARP 해석 후 채팅 패킷 송신에 실패했습니다.")); return;
        }
    }
}
void CIPLayer::OnArpResolved(uint32_t ip)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    unsigned char mac[6];
    if (m_arp && m_arp->Lookup(ip, mac)) FlushLocked(ip, mac);
}
void CIPLayer::Tick(ULONGLONG now)
{
    uint32_t retry = 0;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_pending.empty()) return;
        if (now >= m_pending.front().expiresAt) {
            m_pending.clear(); m_pendingBytes = 0;
            Report(_T("ARP 응답 없음: 대기 중인 채팅을 보내지 못했습니다."));
        } else retry = m_pending.front().destination;
    }
    if (retry && m_arp) { m_arp->SendRequest(retry); OnArpResolved(retry); }
}

BOOL CIPLayer::Receive(unsigned char* payload, int length, const unsigned char* source)
{
    if (!payload || length < IP_HEADER_SIZE || length > ETHER_MAX_DATA_SIZE || !m_sourceIp) return FALSE;
    IPV4_HEADER header;
    memcpy(&header, payload, sizeof(header));
    const int headerLength = (header.versionIhl & 0x0f) * 4;
    const int totalLength = ntohs(header.totalLength);
    // [assignment6] 현재 송수신 규약은 IPv4 옵션 없는 IHL=5다. 단편화 패킷은 재조립하지 않고 거절한다.
    if ((header.versionIhl >> 4) != 4 || headerLength != IP_HEADER_SIZE ||
        totalLength < headerLength || totalLength > length || header.ttl == 0 ||
        (ntohs(header.flagsOffset) & 0xbfff) != 0 || Checksum(payload, headerLength) != 0) return FALSE;
    const uint32_t destination = ReadIp(header.destination), sender = ReadIp(header.source);
    if ((destination != m_sourceIp && destination != IPV4_BROADCAST) || !IsUnicastIp(sender) || sender == m_sourceIp)
        return FALSE;
    const char* name = header.protocol == IP_PROTOCOL_CHAT ? "ChatApp" :
        (header.protocol == IP_PROTOCOL_FILE ? "FileApp" : nullptr);
    if (!name) return FALSE;
    // [assignment6] 앱의 기존 6바이트 송신자 인자 형식은 유지하되 IP 4바이트 + 0 두 바이트를 전달한다.
    // [assignment6] 앱에서 조각을 구분할 때 Ethernet 중계자의 MAC 대신 원래 송신 IP를 사용한다.
    unsigned char senderKey[ETHERNET_ADDRESS_SIZE] = {};
    memcpy(senderKey, header.source, 4);
    for (int i = 0; i < m_nUpperLayerCount; ++i) {
        CBaseLayer* upper = GetUpperLayer(i);
        if (upper && !strcmp(upper->GetLayerName(), name))
            // [assignment6] IP Total Length까지만 전달해 Ethernet 최소 길이 padding을 제거한다.
            return upper->Receive(payload + headerLength, totalLength - headerLength, senderKey);
    }
    return FALSE;
}
void CIPLayer::Report(const CString& message) const
{
    if (!m_window) return;
    CString* text = new CString(message);
    if (!::PostMessage(m_window, WM_NETWORK_EVENT, 0, reinterpret_cast<LPARAM>(text))) delete text;
}
