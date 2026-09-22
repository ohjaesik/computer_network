#pragma once

#include "BaseLayer.h"

#pragma pack(push, 1)
typedef struct _CHAT_APP_HEADER
{
    uint16_t capp_totlen;
    unsigned char capp_type;
    unsigned char capp_unused;
    unsigned char capp_data[CHAT_APP_DATA_SIZE];
} CHAT_APP_HEADER, *PCHAT_APP_HEADER;

// Kept so the Assignment 3 application-address format remains documented in
// the extended project even though Assignment 4 uses Ethernet MAC addresses.
typedef struct _LEGACY_CHAT_APP_HEADER
{
    unsigned int app_dstaddr;
    unsigned int app_srcaddr;
    unsigned short app_length;
    unsigned char app_type;
    unsigned char app_data[APP_DATA_SIZE];
} LEGACY_CHAT_APP_HEADER, *PLEGACY_CHAT_APP_HEADER;
#pragma pack(pop)

static_assert(sizeof(CHAT_APP_HEADER) == ETHER_MAX_DATA_SIZE,
              "Chat header plus data must equal the Ethernet MTU.");

class CChatAppLayer : public CBaseLayer
{
public:
    explicit CChatAppLayer(const char* pName);
    virtual ~CChatAppLayer();

    virtual BOOL Send(unsigned char* ppayload, int nlength);
    virtual BOOL Receive(unsigned char* ppayload);
    virtual BOOL Receive(
        unsigned char* ppayload,
        int nlength,
        const unsigned char* sourceAddress,
        const unsigned char* destinationAddress);

    // Assignment 3 accessors are retained for source compatibility.
    unsigned int GetDestinAddress() const;
    unsigned int GetSourceAddress() const;
    void SetDestinAddress(unsigned int dstAddress);
    void SetSourceAddress(unsigned int srcAddress);

private:
    unsigned int m_unLegacySourceAddress;
    unsigned int m_unLegacyDestinationAddress;

    std::vector<unsigned char> m_ReceiveBuffer;
    uint16_t m_nExpectedLength;
    BOOL m_bReceivingFragments;
    unsigned char m_ReceiveSource[ETHERNET_ADDRESS_SIZE];
    unsigned char m_ReceiveDestination[ETHERNET_ADDRESS_SIZE];
    CCriticalSection m_ReceiveLock;

    void ResetReceiveState();
    BOOL DeliverCompletedMessage();
};

