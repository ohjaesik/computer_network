#pragma once

#include "BaseLayer.h"

#pragma pack(push, 1)
typedef struct _ETHERNET_HEADER
{
    unsigned char enet_dstaddr[ETHERNET_ADDRESS_SIZE];
    unsigned char enet_srcaddr[ETHERNET_ADDRESS_SIZE];
    uint16_t enet_type;
    unsigned char enet_data[ETHER_MAX_DATA_SIZE];
} ETHERNET_HEADER, *PETHERNET_HEADER;
#pragma pack(pop)

static_assert(sizeof(ETHERNET_HEADER) == ETHER_MAX_SIZE,
              "Ethernet header packing must produce a 1514-byte frame buffer.");

class CEthernetLayer : public CBaseLayer
{
public:
    explicit CEthernetLayer(const char* pName);
    virtual ~CEthernetLayer();

    virtual BOOL Send(unsigned char* ppayload, int nlength);
    virtual BOOL Send(
        unsigned char* ppayload,
        int nlength,
        uint16_t protocol);

    virtual BOOL Receive(unsigned char* ppayload);
    virtual BOOL Receive(unsigned char* ppayload, int nlength);

    void SetDestinAddress(const unsigned char* pAddress);
    void SetSourceAddress(const unsigned char* pAddress);
    unsigned char* GetDestinAddress();
    unsigned char* GetSourceAddress();
    void GetDestinAddress(unsigned char* pAddress);
    void GetSourceAddress(unsigned char* pAddress);

private:
    ETHERNET_HEADER m_sHeader;
    CCriticalSection m_AddressLock;

    void ResetHeader();
    CBaseLayer* FindUpperLayer(const char* layerName) const;
    static BOOL IsSameAddress(
        const unsigned char* lhs,
        const unsigned char* rhs);
    static BOOL IsBroadcastAddress(const unsigned char* address);
};

