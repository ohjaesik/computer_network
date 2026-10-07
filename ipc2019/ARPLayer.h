#pragma once
#include "BaseLayer.h"
#include "NetworkPackets.h"
#include <mutex>
#include <random>

class CIPLayer;
class CIPRouter;

// [assignment6] ARP 캐시와 [assignment7] Proxy 등록표는 서로 다른 자료다.
// [assignment6] 캐시는 수신으로 학습하고 만료되지만, [assignment7] Proxy 표는 사용자가 등록/삭제한다.
struct ARP_CACHE_ENTRY {
    uint32_t ip = 0;
    unsigned char mac[6] = {};
    bool complete = false;
    ULONGLONG expiresAt = 0;
    ULONGLONG lastRequestAt = 0;
};
// [assignment7] PARP 등록 항목: 대리 응답 IP와 실제 출력 NIC/next hop을 저장한다.
struct ARP_PROXY_ENTRY {
    uint32_t ip = 0;
    CString device;
    int outgoing = 0;
    uint32_t nextHop = 0;
};

class CARPLayer : public CBaseLayer {
public:
    enum class ADDRESS_STATE { Disabled, Probing, Announcing, Ready, Conflict };
    explicit CARPLayer(const char* name);
    void Configure(uint32_t localIp, const unsigned char* localMac, CIPLayer* ipLayer);
    void Reset(); // [assignment6] NI 수신 종료 후 호출하여 어댑터 변경 전의 캐시와 [assignment7] Proxy 설정을 비운다.
    void SetNotifyWindow(HWND window) { m_window = window; }
    bool Lookup(uint32_t ip, unsigned char* mac);
    BOOL SendRequest(uint32_t targetIp, bool force = false);
    // [assignment7] GARP 광고 또는 변경 MAC의 Probe 검사를 시작한다.
    BOOL SendGratuitous(const unsigned char* advertisedMac);
    // [assignment7] 주소 사용 전 Probe와 GARP Announcement는 서로 다른 단계로 처리한다.
    bool BeginAddressCheck(const unsigned char* candidateMac);
    bool IsUsable() const;
    ADDRESS_STATE GetAddressState() const;
    void GetEffectiveMac(unsigned char* mac) const;
    void SetRouter(CIPRouter* router, int interfaceIndex) { m_router = router; m_interfaceIndex = interfaceIndex; }
    BOOL Receive(unsigned char* payload, int length, const unsigned char* source = NULL) override;
    void Tick(ULONGLONG now);
    void DeleteEntry(uint32_t ip);
    void ClearCache();
    // [assignment7] PARP 항목과 출력 경로를 함께 등록/삭제한다. 일반 ARP 캐시 삭제와 구분한다.
    bool AddProxy(uint32_t ip, const CString& device, int outgoing = -1, uint32_t nextHop = 0);
    void DeleteProxy(uint32_t ip);
    void GetSnapshot(std::vector<ARP_CACHE_ENTRY>& cache, std::vector<ARP_PROXY_ENTRY>& proxies);
    void Report(const CString& message) const;

private:
    // [assignment6] UI의 삭제/조회와 NI 스레드의 학습이 겹치므로 표에 대한 접근을 보호한다.
    // [assignment6] IP 완료 콜백이나 Ethernet 송신은 이 잠금을 놓은 뒤 호출하여 잠금 역전을 막는다.
    mutable std::mutex m_mutex;
    std::vector<ARP_CACHE_ENTRY> m_cache;
    std::vector<ARP_PROXY_ENTRY> m_proxies;
    uint32_t m_localIp = 0;
    unsigned char m_localMac[6] = {};
    unsigned char m_hardwareMac[6] = {}, m_candidateMac[6] = {};
    ADDRESS_STATE m_addressState = ADDRESS_STATE::Disabled;
    int m_probesSent = 0, m_announcementsSent = 0, m_conflicts = 0;
    ULONGLONG m_nextActionAt = 0, m_lastDefendAt = 0, m_lastAttemptAt = 0;
    std::mt19937 m_random{std::random_device{}()};
    unsigned char m_lastGarpMac[6] = {};
    ULONGLONG m_lastGarpAt = 0;
    CIPLayer* m_ipLayer = nullptr;
    HWND m_window = NULL;
    CIPRouter* m_router = nullptr;
    int m_interfaceIndex = 0;
    BOOL Transmit(unsigned short operation, uint32_t senderIp, const unsigned char* senderMac,
        uint32_t targetIp, const unsigned char* targetMac, const unsigned char* ethernetDestination);
    void NotifyChanged() const;
    BOOL SendAnnouncement(const unsigned char* mac);
};
