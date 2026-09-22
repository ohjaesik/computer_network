// ChatAppLayer.cpp: chat fragmentation and reassembly.

#include "pch.h"
#include "ChatAppLayer.h"

#ifdef _DEBUG
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#define new DEBUG_NEW
#endif

CChatAppLayer::CChatAppLayer(const char* pName)
    : CBaseLayer(pName),
      m_unLegacySourceAddress(0),
      m_unLegacyDestinationAddress(0),
      m_nExpectedLength(0),
      m_bReceivingFragments(FALSE)
{
    memset(m_ReceiveSource, 0, sizeof(m_ReceiveSource));
    memset(m_ReceiveDestination, 0, sizeof(m_ReceiveDestination));
}

CChatAppLayer::~CChatAppLayer()
{
}

void CChatAppLayer::SetSourceAddress(unsigned int srcAddress)
{
    m_unLegacySourceAddress = srcAddress;
}

void CChatAppLayer::SetDestinAddress(unsigned int dstAddress)
{
    m_unLegacyDestinationAddress = dstAddress;
}

unsigned int CChatAppLayer::GetSourceAddress() const
{
    return m_unLegacySourceAddress;
}

unsigned int CChatAppLayer::GetDestinAddress() const
{
    return m_unLegacyDestinationAddress;
}

BOOL CChatAppLayer::Send(unsigned char* ppayload, int nlength)
{
    if (ppayload == NULL ||
        nlength <= 0 ||
        nlength > CHAT_MAX_MESSAGE_SIZE ||
        mp_UnderLayer == NULL)
        return FALSE;

    int offset = 0;

    while (offset < nlength)
    {
        CHAT_APP_HEADER packet;
        memset(&packet, 0, sizeof(packet));

        const int fragmentLength =
            std::min(CHAT_APP_DATA_SIZE, nlength - offset);

        packet.capp_totlen =
            HostToNetwork16(static_cast<uint16_t>(nlength));

        if (offset == 0)
            packet.capp_type = CHAT_FRAGMENT_FIRST;
        else if (offset + fragmentLength >= nlength)
            packet.capp_type = CHAT_FRAGMENT_LAST;
        else
            packet.capp_type = CHAT_FRAGMENT_MIDDLE;

        memcpy(
            packet.capp_data,
            ppayload + offset,
            fragmentLength);

        if (!mp_UnderLayer->Send(
                reinterpret_cast<unsigned char*>(&packet),
                CHAT_APP_HEADER_SIZE + fragmentLength,
                ETHERNET_TYPE_CHAT))
            return FALSE;

        offset += fragmentLength;
    }

    return TRUE;
}

BOOL CChatAppLayer::Receive(unsigned char* ppayload)
{
    if (ppayload == NULL)
        return FALSE;

    PCHAT_APP_HEADER header =
        reinterpret_cast<PCHAT_APP_HEADER>(ppayload);
    const int payloadLength =
        std::min<int>(
            NetworkToHost16(header->capp_totlen),
            CHAT_APP_DATA_SIZE);

    unsigned char emptyAddress[ETHERNET_ADDRESS_SIZE] = { 0 };
    return Receive(
        ppayload,
        CHAT_APP_HEADER_SIZE + payloadLength,
        emptyAddress,
        emptyAddress);
}

BOOL CChatAppLayer::Receive(
    unsigned char* ppayload,
    int nlength,
    const unsigned char* sourceAddress,
    const unsigned char* destinationAddress)
{
    if (ppayload == NULL ||
        sourceAddress == NULL ||
        destinationAddress == NULL ||
        nlength < CHAT_APP_HEADER_SIZE)
        return FALSE;

    PCHAT_APP_HEADER header =
        reinterpret_cast<PCHAT_APP_HEADER>(ppayload);

    const uint16_t totalLength =
        NetworkToHost16(header->capp_totlen);
    const unsigned char fragmentType = header->capp_type;

    if (totalLength == 0 ||
        (fragmentType != CHAT_FRAGMENT_FIRST &&
         fragmentType != CHAT_FRAGMENT_MIDDLE &&
         fragmentType != CHAT_FRAGMENT_LAST))
        return FALSE;

    CSingleLock lock(&m_ReceiveLock, TRUE);

    if (fragmentType == CHAT_FRAGMENT_FIRST)
    {
        ResetReceiveState();
        m_nExpectedLength = totalLength;
        m_ReceiveBuffer.reserve(totalLength);
        memcpy(
            m_ReceiveSource,
            sourceAddress,
            ETHERNET_ADDRESS_SIZE);
        memcpy(
            m_ReceiveDestination,
            destinationAddress,
            ETHERNET_ADDRESS_SIZE);
        m_bReceivingFragments = TRUE;
    }
    else
    {
        if (!m_bReceivingFragments ||
            m_nExpectedLength != totalLength ||
            memcmp(
                m_ReceiveSource,
                sourceAddress,
                ETHERNET_ADDRESS_SIZE) != 0)
        {
            ResetReceiveState();
            return FALSE;
        }
    }

    const size_t remaining =
        static_cast<size_t>(m_nExpectedLength) -
        m_ReceiveBuffer.size();
    const int available = nlength - CHAT_APP_HEADER_SIZE;
    const size_t copyLength =
        std::min<size_t>(remaining, std::max(available, 0));

    if (copyLength > 0)
    {
        m_ReceiveBuffer.insert(
            m_ReceiveBuffer.end(),
            header->capp_data,
            header->capp_data + copyLength);
    }

    // A message fitting one frame uses FIRST and can be delivered immediately.
    if (m_ReceiveBuffer.size() == m_nExpectedLength)
    {
        if (fragmentType == CHAT_FRAGMENT_MIDDLE)
        {
            ResetReceiveState();
            return FALSE;
        }

        return DeliverCompletedMessage();
    }

    if (fragmentType == CHAT_FRAGMENT_LAST)
    {
        ResetReceiveState();
        return FALSE;
    }

    return TRUE;
}

BOOL CChatAppLayer::DeliverCompletedMessage()
{
    CBaseLayer* upper = GetUpperLayer(0);
    if (upper == NULL ||
        m_ReceiveBuffer.size() != m_nExpectedLength)
    {
        ResetReceiveState();
        return FALSE;
    }

    std::vector<unsigned char> completed = m_ReceiveBuffer;
    unsigned char source[ETHERNET_ADDRESS_SIZE];
    unsigned char destination[ETHERNET_ADDRESS_SIZE];
    memcpy(source, m_ReceiveSource, sizeof(source));
    memcpy(destination, m_ReceiveDestination, sizeof(destination));

    ResetReceiveState();

    return upper->Receive(
        completed.data(),
        static_cast<int>(completed.size()),
        source,
        destination);
}

void CChatAppLayer::ResetReceiveState()
{
    m_ReceiveBuffer.clear();
    m_nExpectedLength = 0;
    m_bReceivingFragments = FALSE;
    memset(m_ReceiveSource, 0, sizeof(m_ReceiveSource));
    memset(m_ReceiveDestination, 0, sizeof(m_ReceiveDestination));
}

