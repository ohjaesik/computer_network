#pragma once
#include <cstdint>
#include <vector>

// [assignment8][nat-i] 두 수신 스레드와 UI가 CIPRouter의 한 mutex 아래 이 표를 사용한다.
// [assignment8][nat-i] IP 주소/ICMP ID는 계산하기 쉬운 host order, 시각은 GetTickCount64() 기준이다.
class CNatTable {
public:
    // [assignment8][nat-bonus] ICMP ID와 TCP/UDP Port를 같은 16비트 필드로 표시하되 Protocol로 구별한다.
    // TCP 상태는 NAT 수명 관리용이며 TCP endpoint의 sequence/window 검증을 대체하지 않는다.
    enum class TCP_STATE { Opening, Established, HalfClosed, Closing };
    struct ENTRY {
        uint32_t insideIp = 0, outsideIp = 0;
        uint16_t insideId = 0, outsideId = 0;
        uint64_t expiresAt = 0;
        uint8_t protocol = 1;
        uint32_t remoteIp = 0;
        uint16_t remotePort = 0;
        bool portForward = false;
        TCP_STATE tcpState = TCP_STATE::Opening;
    };
    struct PORT_FORWARD {
        uint32_t id = 0, insideIp = 0;
        uint16_t outsidePort = 0, insidePort = 0;
        uint8_t protocol = 0;
    };

    bool TrackRequest(uint32_t insideIp, uint16_t insideId, uint32_t outsideIp, uint32_t remoteIp,
        uint64_t now, uint16_t& outsideId);
    bool FindReply(uint32_t outsideIp, uint16_t outsideId, uint32_t remoteIp, uint64_t now, ENTRY& entry) const;
    void RefreshReply(uint16_t outsideId, uint32_t remoteIp, uint64_t now);
    void Expire(uint64_t now);
    void GetEntries(std::vector<ENTRY>& entries) const;
    void Clear();

    // [assignment8][nat-bonus] 외부 IP는 설정한 Port 2를 사용한다. 고정 규칙은 End/Start 후에도 남고,
    // 동적 세션은 Clear()로 지운다. 같은 TCP/UDP 외부 포트는 동적 allocator가 사용하지 못한다.
    bool AddPortForward(PORT_FORWARD rule);
    void DeletePortForward(uint32_t id);
    void GetPortForwards(std::vector<PORT_FORWARD>& rules) const;
    bool TrackTransportOutbound(uint8_t protocol, uint32_t insideIp, uint16_t insidePort,
        uint32_t outsideIp, uint32_t remoteIp, uint16_t remotePort, uint8_t tcpFlags,
        uint64_t now, ENTRY& entry);
    bool FindTransportInbound(uint8_t protocol, uint32_t outsideIp, uint16_t outsidePort,
        uint32_t remoteIp, uint16_t remotePort, uint8_t tcpFlags, uint64_t now, ENTRY& entry) const;
    bool TrackTransportInbound(const ENTRY& entry, uint8_t tcpFlags, uint64_t now);

private:
    struct PEER { uint32_t ip; uint64_t expiresAt; };
    struct MAPPING {
        ENTRY entry;
        std::vector<PEER> peers;
    };
    std::vector<MAPPING> m_mappings;
    uint16_t m_nextId = 0;
    struct FLOW {
        ENTRY entry;
        bool insideFin = false, outsideFin = false;
    };
    std::vector<FLOW> m_flows;
    std::vector<PORT_FORWARD> m_forwards;
    uint32_t m_nextRuleId = 1;
    uint16_t m_nextTcpPort = 0, m_nextUdpPort = 0;

    bool Allocate(uint16_t insideId, uint16_t& outsideId);
    static bool RefreshPeer(MAPPING& mapping, uint32_t remoteIp, uint64_t now, bool add);
    bool AllocatePort(uint8_t protocol, uint32_t outsideIp, uint16_t insidePort, uint16_t& outsidePort);
    static bool SameFlow(const ENTRY& left, const ENTRY& right);
    static bool CanOpen(uint8_t protocol, uint8_t tcpFlags);
    static void RefreshFlow(FLOW& flow, bool fromInside, uint8_t tcpFlags, uint64_t now);
};
