#include "pch.h"
#include "NatTable.h"
#include <algorithm>
#include <utility>

bool CNatTable::Allocate(uint16_t insideId, uint16_t& outsideId)
{
    if (!m_nextId) m_nextId = NAT_ICMP_FIRST_ID;
    // [assignment8][nat-i] ID는 16비트다. 최댓값 이후 시작점으로 돌아가도 다른 활성 매핑과 겹치지 않는다.
    for (unsigned int i = NAT_ICMP_FIRST_ID; i <= 0xffffu; ++i) {
        const uint16_t candidate = m_nextId;
        m_nextId = candidate == 0xffffu ? NAT_ICMP_FIRST_ID : static_cast<uint16_t>(candidate + 1);
        if (candidate != insideId && std::none_of(m_mappings.begin(),m_mappings.end(),
            [candidate](const MAPPING& row) { return row.entry.outsideId == candidate; })) {
            outsideId = candidate;
            return true;
        }
    }
    return false;
}

bool CNatTable::RefreshPeer(MAPPING& mapping, uint32_t remoteIp, uint64_t now, bool add)
{
    const uint64_t expires = now + NAT_ICMP_IDLE_MS;
    for (auto& peer : mapping.peers) if (peer.ip == remoteIp && now < peer.expiresAt) {
        peer.expiresAt = mapping.entry.expiresAt = expires;
        return true;
    }
    if (!add) return false;
    mapping.peers.erase(std::remove_if(mapping.peers.begin(),mapping.peers.end(),
        [now](const PEER& peer) { return now >= peer.expiresAt; }),mapping.peers.end());
    if (mapping.peers.size() >= NAT_ICMP_MAX_PEERS) return false;
    // [assignment8][nat-i] 같은 로컬 IP/ID는 목적지가 달라도 외부 ID를 재사용한다.
    // [assignment8][nat-i] 역방향은 실제 요청을 받은 외부 IP의 Echo Reply만 허용한다.
    mapping.peers.push_back({remoteIp,expires});
    mapping.entry.expiresAt = expires;
    return true;
}

bool CNatTable::TrackRequest(uint32_t insideIp, uint16_t insideId, uint32_t outsideIp, uint32_t remoteIp,
    uint64_t now, uint16_t& outsideId)
{
    Expire(now);
    for (auto& row : m_mappings) if (row.entry.insideIp == insideIp && row.entry.insideId == insideId &&
        row.entry.outsideIp == outsideIp) {
        if (!RefreshPeer(row,remoteIp,now,true)) return false;
        outsideId = row.entry.outsideId;
        return true;
    }
    if (m_mappings.size() + m_flows.size() >= NAT_TABLE_MAX_ENTRIES || !Allocate(insideId,outsideId)) return false;
    MAPPING mapping;
    mapping.entry.insideIp = insideIp; mapping.entry.insideId = insideId;
    mapping.entry.outsideIp = outsideIp; mapping.entry.outsideId = outsideId;
    if (!RefreshPeer(mapping,remoteIp,now,true)) return false;
    m_mappings.push_back(std::move(mapping));
    return true;
}

bool CNatTable::FindReply(uint32_t outsideIp, uint16_t outsideId, uint32_t remoteIp, uint64_t now, ENTRY& entry) const
{
    for (const auto& row : m_mappings) if (row.entry.outsideId == outsideId &&
        row.entry.outsideIp == outsideIp && now < row.entry.expiresAt) {
        for (const auto& peer : row.peers) if (peer.ip == remoteIp && now < peer.expiresAt) {
            entry = row.entry;
            return true;
        }
        break;
    }
    return false;
}

void CNatTable::RefreshReply(uint16_t outsideId, uint32_t remoteIp, uint64_t now)
{
    for (auto& row : m_mappings) if (row.entry.outsideId == outsideId && now < row.entry.expiresAt) {
        RefreshPeer(row,remoteIp,now,false);
        return;
    }
}

void CNatTable::Expire(uint64_t now)
{
    m_mappings.erase(std::remove_if(m_mappings.begin(),m_mappings.end(),
        [now](const MAPPING& row) { return now >= row.entry.expiresAt; }),m_mappings.end());
    m_flows.erase(std::remove_if(m_flows.begin(),m_flows.end(),
        [now](const FLOW& row) { return now >= row.entry.expiresAt; }),m_flows.end());
}

void CNatTable::GetEntries(std::vector<ENTRY>& entries) const
{
    entries.clear(); entries.reserve(m_mappings.size() + m_flows.size());
    for (const auto& row : m_mappings) entries.push_back(row.entry);
    for (const auto& row : m_flows) entries.push_back(row.entry);
}

void CNatTable::Clear()
{
    m_mappings.clear(); m_nextId = NAT_ICMP_FIRST_ID;
    m_flows.clear(); m_nextTcpPort = m_nextUdpPort = NAT_TRANSPORT_FIRST_PORT;
}

bool CNatTable::AddPortForward(PORT_FORWARD rule)
{
    // [assignment8][nat-bonus] 하나의 외부 endpoint를 두 서버에 배정하면 역방향을 결정할 수 없다.
    // 내부 endpoint의 별칭도 거부하여 서버 응답의 출발지 외부 포트가 항상 한 값으로 정해지게 한다.
    if ((rule.protocol != IP_PROTOCOL_TCP && rule.protocol != IP_PROTOCOL_UDP) ||
        !rule.insideIp || !rule.insidePort || !rule.outsidePort || !m_nextRuleId ||
        m_forwards.size() >= NAT_MAX_PORT_FORWARDS) return false;
    for (const auto& old : m_forwards) if (old.protocol == rule.protocol &&
        (old.outsidePort == rule.outsidePort || (old.insideIp == rule.insideIp && old.insidePort == rule.insidePort))) return false;
    for (const auto& flow : m_flows) if (flow.entry.protocol == rule.protocol &&
        (flow.entry.outsideId == rule.outsidePort ||
        (flow.entry.insideIp == rule.insideIp && flow.entry.insideId == rule.insidePort))) return false;
    rule.id = m_nextRuleId++;
    m_forwards.push_back(rule);
    return true;
}

void CNatTable::DeletePortForward(uint32_t id)
{
    for (const auto& rule : m_forwards) if (rule.id == id) {
        m_flows.erase(std::remove_if(m_flows.begin(),m_flows.end(),[&rule](const FLOW& flow) {
            return flow.entry.portForward && flow.entry.protocol == rule.protocol && flow.entry.outsideId == rule.outsidePort;
        }),m_flows.end());
        break;
    }
    m_forwards.erase(std::remove_if(m_forwards.begin(),m_forwards.end(),
        [id](const PORT_FORWARD& rule) { return rule.id == id; }),m_forwards.end());
}

void CNatTable::GetPortForwards(std::vector<PORT_FORWARD>& rules) const { rules = m_forwards; }

bool CNatTable::AllocatePort(uint8_t protocol, uint32_t outsideIp, uint16_t insidePort, uint16_t& outsidePort)
{
    uint16_t& next = protocol == IP_PROTOCOL_TCP ? m_nextTcpPort : m_nextUdpPort;
    if (!next) next = NAT_TRANSPORT_FIRST_PORT;
    for (unsigned int attempt = NAT_TRANSPORT_FIRST_PORT; attempt <= 0xffffu; ++attempt) {
        const uint16_t candidate = next;
        next = candidate == 0xffffu ? NAT_TRANSPORT_FIRST_PORT : static_cast<uint16_t>(candidate+1);
        if (candidate == insidePort) continue;
        const bool reserved = std::any_of(m_forwards.begin(),m_forwards.end(),[=](const PORT_FORWARD& rule) {
            return rule.protocol == protocol && rule.outsidePort == candidate;
        });
        const bool used = std::any_of(m_flows.begin(),m_flows.end(),[=](const FLOW& flow) {
            return flow.entry.protocol == protocol && flow.entry.outsideIp == outsideIp && flow.entry.outsideId == candidate;
        });
        if (!reserved && !used) { outsidePort = candidate; return true; }
    }
    return false;
}

bool CNatTable::SameFlow(const ENTRY& left, const ENTRY& right)
{
    return left.protocol == right.protocol && left.insideIp == right.insideIp && left.insideId == right.insideId &&
        left.outsideIp == right.outsideIp && left.outsideId == right.outsideId &&
        left.remoteIp == right.remoteIp && left.remotePort == right.remotePort;
}

bool CNatTable::CanOpen(uint8_t protocol, uint8_t flags)
{
    // [assignment8][nat-bonus] 세션 없는 TCP ACK/data는 새 매핑을 만들지 않는다. SYN 재전송은 기존 행을 재사용한다.
    return protocol == IP_PROTOCOL_UDP || (protocol == IP_PROTOCOL_TCP &&
        (flags & (TCP_FLAG_SYN | TCP_FLAG_ACK | TCP_FLAG_FIN | TCP_FLAG_RST)) == TCP_FLAG_SYN);
}

void CNatTable::RefreshFlow(FLOW& flow, bool fromInside, uint8_t flags, uint64_t now)
{
    auto& entry = flow.entry;
    if (entry.protocol == IP_PROTOCOL_UDP) { entry.expiresAt = now + NAT_UDP_IDLE_MS; return; }
    // [assignment8][nat-bonus] Cisco의 SYN/TCP/FINRST 구분을 참고한다. SYN+ACK 이후 긴 TCP 유휴 시간을 쓰고,
    // 한쪽 FIN만 온 half-close는 연결을 유지한다. 양쪽 FIN 또는 RST 뒤에는 최종 ACK/재전송용 유예를 둔다.
    // 종료 유예의 끝은 늦은 ACK가 와도 연장하지 않아 이미 닫힌 연결이 무기한 남지 않게 한다.
    if (entry.tcpState == TCP_STATE::Closing) return;
    if (flags & TCP_FLAG_FIN) { if (fromInside) flow.insideFin = true; else flow.outsideFin = true; }
    if ((flags & TCP_FLAG_RST) || (flow.insideFin && flow.outsideFin)) {
        entry.tcpState = TCP_STATE::Closing; entry.expiresAt = now + NAT_TCP_CLOSING_MS; return;
    }
    if (flow.insideFin || flow.outsideFin) entry.tcpState = TCP_STATE::HalfClosed;
    else if ((flags & (TCP_FLAG_SYN | TCP_FLAG_ACK)) == (TCP_FLAG_SYN | TCP_FLAG_ACK)) entry.tcpState = TCP_STATE::Established;
    entry.expiresAt = now + (entry.tcpState == TCP_STATE::Opening ? NAT_TCP_SYN_MS : NAT_TCP_IDLE_MS);
}

bool CNatTable::TrackTransportOutbound(uint8_t protocol, uint32_t insideIp, uint16_t insidePort,
    uint32_t outsideIp, uint32_t remoteIp, uint16_t remotePort, uint8_t flags, uint64_t now, ENTRY& entry)
{
    if (protocol != IP_PROTOCOL_TCP && protocol != IP_PROTOCOL_UDP) return false;
    Expire(now);
    for (auto& flow : m_flows) {
        const auto& row = flow.entry;
        if (row.protocol == protocol && row.insideIp == insideIp && row.insideId == insidePort &&
            row.outsideIp == outsideIp && row.remoteIp == remoteIp && row.remotePort == remotePort) {
            RefreshFlow(flow,true,flags,now); entry = flow.entry; return true;
        }
    }
    if (!CanOpen(protocol,flags) || m_mappings.size() + m_flows.size() >= NAT_TABLE_MAX_ENTRIES) return false;
    FLOW flow;
    flow.entry.protocol = protocol; flow.entry.insideIp = insideIp; flow.entry.insideId = insidePort;
    flow.entry.outsideIp = outsideIp; flow.entry.remoteIp = remoteIp; flow.entry.remotePort = remotePort;
    for (const auto& rule : m_forwards) if (rule.protocol == protocol && rule.insideIp == insideIp && rule.insidePort == insidePort) {
        flow.entry.outsideId = rule.outsidePort; flow.entry.portForward = true; break;
    }
    // [assignment8][nat-bonus] 같은 (Protocol, 내부 IP, Port)는 목적지가 달라도 외부 Port를 재사용한다.
    // 세션/타이머는 원격 IP+Port별로 따로 보관하여 서로 다른 응답을 섞거나 제3자의 응답을 허용하지 않는다.
    if (!flow.entry.outsideId) for (const auto& old : m_flows) {
        const auto& row = old.entry;
        if (row.protocol == protocol && row.insideIp == insideIp && row.insideId == insidePort && row.outsideIp == outsideIp) {
            flow.entry.outsideId = row.outsideId; break;
        }
    }
    if (!flow.entry.outsideId && !AllocatePort(protocol,outsideIp,insidePort,flow.entry.outsideId)) return false;
    RefreshFlow(flow,true,flags,now); entry = flow.entry; m_flows.push_back(flow); return true;
}

bool CNatTable::FindTransportInbound(uint8_t protocol, uint32_t outsideIp, uint16_t outsidePort,
    uint32_t remoteIp, uint16_t remotePort, uint8_t flags, uint64_t now, ENTRY& entry) const
{
    for (const auto& flow : m_flows) {
        const auto& row = flow.entry;
        if (row.protocol == protocol && row.outsideIp == outsideIp && row.outsideId == outsidePort &&
            row.remoteIp == remoteIp && row.remotePort == remotePort && now < row.expiresAt) { entry = row; return true; }
    }
    // [assignment8][nat-bonus] 고정 규칙이 있으면 외부에서 시작한 연결도 허용한다. 새 TCP는 SYN이어야 한다.
    // 여기서는 조회만 하고 실제 행 생성은 경로/TTL/ARP 큐 용량 검사 후에 한다.
    if (!CanOpen(protocol,flags)) return false;
    for (const auto& rule : m_forwards) if (rule.protocol == protocol && rule.outsidePort == outsidePort) {
        entry = ENTRY(); entry.protocol = protocol;
        entry.insideIp = rule.insideIp; entry.insideId = rule.insidePort;
        entry.outsideIp = outsideIp; entry.outsideId = outsidePort;
        entry.remoteIp = remoteIp; entry.remotePort = remotePort; entry.portForward = true;
        return true;
    }
    return false;
}

bool CNatTable::TrackTransportInbound(const ENTRY& entry, uint8_t flags, uint64_t now)
{
    Expire(now);
    for (auto& flow : m_flows) if (SameFlow(flow.entry,entry)) { RefreshFlow(flow,false,flags,now); return true; }
    if (!entry.portForward || !CanOpen(entry.protocol,flags) ||
        m_mappings.size() + m_flows.size() >= NAT_TABLE_MAX_ENTRIES) return false;
    const bool configured = std::any_of(m_forwards.begin(),m_forwards.end(),[&entry](const PORT_FORWARD& rule) {
        return rule.protocol == entry.protocol && rule.outsidePort == entry.outsideId &&
            rule.insideIp == entry.insideIp && rule.insidePort == entry.insideId;
    });
    if (!configured) return false;
    FLOW flow; flow.entry = entry; RefreshFlow(flow,false,flags,now); m_flows.push_back(flow); return true;
}
