#pragma once
#include "NetworkPackets.h"
#include <mutex>
#include <deque>
#include <vector>

class CARPLayer;
class CEthernetLayer;

// [assignment6] PARP는 MAC 대리 응답이고, 응답을 믿고 도착한 IP 데이터의 전달은 이 객체가 맡는다.
// [assignment6] 두 물리 NIC의 connected subnet 및 사용자가 등록한 /32 next-hop 경로를 공유한다.
// [assignment6] NAT/동적 라우팅은 하지 않는다. 앱 식별값과 무관하게 원본 IPv4 Protocol을 보존한다.
class CIPRouter {
public:
    struct STATS { uint64_t forwarded = 0, dropped = 0; size_t waiting = 0; };
    void BindInterface(int index, uint32_t ip, uint32_t mask, CARPLayer* arp, CEthernetLayer* ethernet);
    void Reset(); // [assignment6] 양쪽 NI worker를 먼저 종료한 뒤 호출한다.
    void Suspend(); // [assignment6] NIC를 닫기 전에 교차 NIC 송신을 먼저 정지한다.
    void SetNotifyWindow(HWND window) { m_window = window; }
    bool InstallProxyRoute(int incoming, uint32_t target, int outgoing, uint32_t nextHop);
    void RemoveProxyRoute(int incoming, uint32_t target);
    bool CanProxyReply(int incoming, uint32_t target);
    bool IsLocalAddress(uint32_t address);
    bool UsesDifferentInterface(int sourceInterface, uint32_t destination);
    bool IsDestinationResolved(int sourceInterface, uint32_t destination);
    BOOL RequestDestination(int sourceInterface, uint32_t destination);
    BOOL SendOriginated(int sourceInterface, const unsigned char* packet, int length, bool allowQueue);
    BOOL Forward(int incoming, const unsigned char* packet, int length, const unsigned char* previousMac);
    void OnArpResolved(int index, uint32_t ip);
    void Tick(ULONGLONG now);
    STATS GetStats();
private:
    struct ROUTER_PORT {
        uint32_t ip = 0, mask = 0;
        CARPLayer* arp = nullptr;
        CEthernetLayer* ethernet = nullptr;
    };
    struct ROUTE { int owner, outgoing; uint32_t target, nextHop; };
    struct PENDING {
        std::vector<unsigned char> packet, original;
        unsigned char previousMac[6] = {};
        int incoming = 0, outgoing = 0;
        uint32_t nextHop = 0;
        ULONGLONG expiresAt = 0;
        bool originated = false;
    };
    std::mutex m_mutex;
    ROUTER_PORT m_interfaces[ROUTER_MAX_INTERFACES];
    std::vector<ROUTE> m_routes;
    std::deque<PENDING> m_pending;
    size_t m_pendingBytes = 0;
    STATS m_stats;
    HWND m_window = NULL;
    ULONGLONG m_errorWindowAt = 0;
    unsigned int m_errorsInWindow = 0;
    uint16_t m_identification = 0;
    bool m_running = false;
    bool ValidInterface(int index) const;
    bool FindRoute(uint32_t destination, int& outgoing, uint32_t& nextHop) const;
    bool IsDirectedBroadcast(uint32_t ip) const;
    void FlushLocked(int outgoing, uint32_t nextHop, const unsigned char* mac, ULONGLONG now);
    void ErrorLocked(const PENDING& packet, unsigned char type, unsigned char code);
    void Report(const CString& text) const;
};
