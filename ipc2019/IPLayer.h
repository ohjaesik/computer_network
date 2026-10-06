#pragma once
#include "BaseLayer.h"
#include "NetworkPackets.h"
#include <mutex>
#include <deque>

class CARPLayer;

// [assignment6] ChatApp/FileApp의 공통 하위 계층. IP Protocol 253/254로 앱을 구분한다.
// [assignment6] 이 실습은 단일 LAN의 IPv4 송수신이며 라우팅/IPv4 단편 재조립은 구현 범위가 아니다.
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
    BOOL Transmit(std::vector<unsigned char>& packet, const unsigned char* destination);
    void FlushLocked(uint32_t ip, const unsigned char* mac);
    void Report(const CString& message) const;
};
