// EthernetLayer.cpp: Ethernet II encapsulation and demultiplexing.

#include "pch.h"
#include "EthernetLayer.h"

#ifdef _DEBUG
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#define new DEBUG_NEW
#endif

namespace
{
    const int ETHERNET_MIN_FRAME_WITHOUT_FCS = 60;
}

CEthernetLayer::CEthernetLayer(const char* pName)
    : CBaseLayer(pName)
{
    ResetHeader();
}

CEthernetLayer::~CEthernetLayer()
{
}

void CEthernetLayer::ResetHeader()
{
    memset(&m_sHeader, 0, sizeof(m_sHeader));
}

unsigned char* CEthernetLayer::GetSourceAddress()
{
    return m_sHeader.enet_srcaddr;
}

unsigned char* CEthernetLayer::GetDestinAddress()
{
    return m_sHeader.enet_dstaddr;
}

void CEthernetLayer::GetSourceAddress(unsigned char* pAddress)
{
    if (pAddress == NULL)
        return;

    CSingleLock lock(&m_AddressLock, TRUE);
    memcpy(pAddress, m_sHeader.enet_srcaddr, ETHERNET_ADDRESS_SIZE);
}

void CEthernetLayer::GetDestinAddress(unsigned char* pAddress)
{
    if (pAddress == NULL)
        return;

    CSingleLock lock(&m_AddressLock, TRUE);
    memcpy(pAddress, m_sHeader.enet_dstaddr, ETHERNET_ADDRESS_SIZE);
}

void CEthernetLayer::SetSourceAddress(const unsigned char* pAddress)
{
    if (pAddress == NULL)
        return;

    CSingleLock lock(&m_AddressLock, TRUE);
    memcpy(m_sHeader.enet_srcaddr, pAddress, ETHERNET_ADDRESS_SIZE);
}

void CEthernetLayer::SetDestinAddress(const unsigned char* pAddress)
{
    if (pAddress == NULL)
        return;

    CSingleLock lock(&m_AddressLock, TRUE);
    memcpy(m_sHeader.enet_dstaddr, pAddress, ETHERNET_ADDRESS_SIZE);
}

BOOL CEthernetLayer::Send(unsigned char* ppayload, int nlength)
{
    // Compatibility entry point for the old single-application stack.
    return Send(ppayload, nlength, ETHERNET_TYPE_CHAT);
}

BOOL CEthernetLayer::Send(
    unsigned char* ppayload,
    int nlength,
    uint16_t protocol)
{
    if (ppayload == NULL ||
        nlength < 0 ||
        nlength > ETHER_MAX_DATA_SIZE ||
        (protocol != ETHERNET_TYPE_CHAT &&
         protocol != ETHERNET_TYPE_FILE) ||
        mp_UnderLayer == NULL)
        return FALSE;

    unsigned char frame[ETHER_MAX_SIZE] = { 0 };
    PETHERNET_HEADER header =
        reinterpret_cast<PETHERNET_HEADER>(frame);

    {
        CSingleLock lock(&m_AddressLock, TRUE);
        memcpy(
            header->enet_dstaddr,
            m_sHeader.enet_dstaddr,
            ETHERNET_ADDRESS_SIZE);
        memcpy(
            header->enet_srcaddr,
            m_sHeader.enet_srcaddr,
            ETHERNET_ADDRESS_SIZE);
    }

    header->enet_type = HostToNetwork16(protocol);
    memcpy(header->enet_data, ppayload, nlength);

    const int unpaddedLength = ETHER_HEADER_SIZE + nlength;
    const int frameLength =
        std::max(unpaddedLength, ETHERNET_MIN_FRAME_WITHOUT_FCS);

    return mp_UnderLayer->Send(frame, frameLength);
}

BOOL CEthernetLayer::Receive(unsigned char* ppayload)
{
    // Legacy signature had no captured length. Its file-backed frame was fixed.
    return Receive(ppayload, ETHER_MAX_SIZE);
}

BOOL CEthernetLayer::Receive(unsigned char* ppayload, int nlength)
{
    if (ppayload == NULL ||
        nlength < ETHER_HEADER_SIZE ||
        nlength > ETHER_MAX_SIZE)
        return FALSE;

    PETHERNET_HEADER frame =
        reinterpret_cast<PETHERNET_HEADER>(ppayload);

    unsigned char localAddress[ETHERNET_ADDRESS_SIZE] = { 0 };
    GetSourceAddress(localAddress);

    const BOOL destinationAccepted =
        IsSameAddress(frame->enet_dstaddr, localAddress) ||
        IsBroadcastAddress(frame->enet_dstaddr);

    if (!destinationAccepted)
        return FALSE;

    // Npcap can capture frames sent by this same adapter. Do not loop them back.
    if (IsSameAddress(frame->enet_srcaddr, localAddress))
        return FALSE;

    const uint16_t protocol = NetworkToHost16(frame->enet_type);
    CBaseLayer* upper = NULL;

    if (protocol == ETHERNET_TYPE_CHAT)
        upper = FindUpperLayer("ChatApp");
    else if (protocol == ETHERNET_TYPE_FILE)
        upper = FindUpperLayer("FileApp");
    else
        return FALSE;

    if (upper == NULL)
        return FALSE;

    const int payloadLength =
        std::min(nlength - ETHER_HEADER_SIZE, ETHER_MAX_DATA_SIZE);

    return upper->Receive(
        frame->enet_data,
        payloadLength,
        frame->enet_srcaddr,
        frame->enet_dstaddr);
}

CBaseLayer* CEthernetLayer::FindUpperLayer(const char* layerName) const
{
    for (int i = 0; i < GetUpperLayerCount(); ++i)
    {
        CBaseLayer* layer = GetUpperLayer(i);
        if (layer != NULL &&
            strcmp(layer->GetLayerName(), layerName) == 0)
            return layer;
    }

    return NULL;
}

BOOL CEthernetLayer::IsSameAddress(
    const unsigned char* lhs,
    const unsigned char* rhs)
{
    return lhs != NULL &&
           rhs != NULL &&
           memcmp(lhs, rhs, ETHERNET_ADDRESS_SIZE) == 0;
}

BOOL CEthernetLayer::IsBroadcastAddress(const unsigned char* address)
{
    if (address == NULL)
        return FALSE;

    for (int i = 0; i < ETHERNET_ADDRESS_SIZE; ++i)
    {
        if (address[i] != 0xff)
            return FALSE;
    }

    return TRUE;
}

