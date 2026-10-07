#pragma once
#include "BaseLayer.h"
#include "NetworkPackets.h"
#include <mutex>
#include <deque>

class CARPLayer;
class CIPRouter;

// [assignment6] ChatApp/FileApp의 공통 하위 계층. IP Protocol 253/254로 앱을 구분한다.
// [assignment6] 로컬 앱 송수신은 이 계층, [assignment7] PARP로 들어온 다른 목적지의 IP 중계는 CIPRouter가 맡는다.
class CIPLayer : public CBaseLayer {
public:
    explicit CIPLayer(const char* name);
    void Configure(uint32_t source, CARPLayer* arp);
    bool SetDestination(uint32_t destination);
    uint32_t GetDestination();
    bool IsDestinationResolved();
    BOOL RequestDestination();
    bool CanAcceptChat(int bytes);
    void SetNotifyWindow(HWND window) { m_window = window; }
    void SetRouter(CIPRouter* router, int interfaceIndex) { m_router = router; m_interfaceIndex = interfaceIndex; }
    BOOL Send(unsigned char* payload, int length, unsigned short protocol) override;
    BOOL Receive(unsigned char* payload, int length, const unsigned char* source = NULL) override;
    void OnArpResolved(uint32_t ip);
    void Tick(ULONGLONG now);
    void Reset();

private:
    struct PENDING_PACKET {
        std::vector<unsigned char> bytes;
        uint32_t destination;
        ULONGLONG expiresAt;
    };
    std::mutex m_mutex;
    std::deque<PENDING_PACKET> m_pending;
    size_t m_pendingBytes = 0;
    uint32_t m_sourceIp = 0;
    uint32_t m_destinationIp = 0;
    uint16_t m_identification = 0;
    CARPLayer* m_arp = nullptr;
    HWND m_window = NULL;
    CIPRouter* m_router = nullptr;
    int m_interfaceIndex = 0;
    BOOL Transmit(std::vector<unsigned char>& packet, const unsigned char* destination);
    void FlushLocked(uint32_t ip, const unsigned char* mac);
    void Report(const CString& message) const;
};
