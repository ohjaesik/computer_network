// NILayer.cpp: Npcap based network-interface layer.

#include "pch.h"
#include "NILayer.h"

#include <Packet32.h>
#include <ntddndis.h>
#include <atlconv.h>

#ifdef _DEBUG
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#define new DEBUG_NEW
#endif

CNILayer::CNILayer(const char* pName)
    : CBaseLayer(pName),
      m_pAdapterHandle(NULL),
      m_pReadThread(NULL),
      m_lStopRequested(TRUE),
      m_nSelectedAdapter(-1)
{
    memset(m_MacAddress, 0, sizeof(m_MacAddress));
}

CNILayer::~CNILayer()
{
    CloseAdapter();
}

void CNILayer::SetLastErrorMessage(LPCTSTR message)
{
    m_strLastError = (message == NULL) ? _T("") : message;
}

CString CNILayer::GetLastErrorMessage() const
{
    return m_strLastError;
}

BOOL CNILayer::RefreshAdapterList()
{
    m_Adapters.clear();

    pcap_if_t* allDevices = NULL;
    char errorBuffer[PCAP_ERRBUF_SIZE] = { 0 };

    if (pcap_findalldevs(&allDevices, errorBuffer) == -1)
    {
        SetLastErrorMessage(CString(CA2W(errorBuffer, CP_ACP)));
        return FALSE;
    }

    for (pcap_if_t* device = allDevices;
         device != NULL;
         device = device->next)
    {
        if (device->name == NULL)
            continue;

        ADAPTER_ENTRY entry;
        entry.name = device->name;

        if (device->description != NULL && device->description[0] != '\0')
            entry.description = CString(CA2W(device->description, CP_ACP));
        else
            entry.description = CString(CA2W(device->name, CP_ACP));

        m_Adapters.push_back(entry);
    }

    pcap_freealldevs(allDevices);

    if (m_Adapters.empty())
    {
        SetLastErrorMessage(_T("Npcap에서 사용 가능한 네트워크 어댑터를 찾지 못했습니다."));
        return FALSE;
    }

    SetLastErrorMessage(_T(""));
    return TRUE;
}

int CNILayer::GetAdapterCount() const
{
    return static_cast<int>(m_Adapters.size());
}

CString CNILayer::GetAdapterDescription(int index) const
{
    if (index < 0 || index >= GetAdapterCount())
        return _T("");

    return m_Adapters[index].description;
}

BOOL CNILayer::OpenAdapter(int index)
{
    CloseAdapter();

    if (index < 0 || index >= GetAdapterCount())
    {
        SetLastErrorMessage(_T("올바른 네트워크 어댑터를 선택하십시오."));
        return FALSE;
    }

    char errorBuffer[PCAP_ERRBUF_SIZE] = { 0 };
    m_pAdapterHandle = pcap_open_live(
        m_Adapters[index].name,
        65536,
        1,
        PCAP_READ_TIMEOUT_MS,
        errorBuffer);

    if (m_pAdapterHandle == NULL)
    {
        SetLastErrorMessage(CString(CA2W(errorBuffer, CP_ACP)));
        return FALSE;
    }

    if (pcap_datalink(m_pAdapterHandle) != DLT_EN10MB)
    {
        SetLastErrorMessage(
            _T("선택한 어댑터는 Ethernet frame 형식을 제공하지 않습니다."));
        pcap_close(m_pAdapterHandle);
        m_pAdapterHandle = NULL;
        return FALSE;
    }

    // Only the two user-defined Ethernet protocols are delivered to the stack.
    bpf_program filterProgram;
    const char* filter =
        "ether proto 0x2080 or ether proto 0x2090";

    if (pcap_compile(
            m_pAdapterHandle,
            &filterProgram,
            filter,
            1,
            PCAP_NETMASK_UNKNOWN) == -1)
    {
        SetLastErrorMessage(_T("Npcap 수신 필터를 컴파일하지 못했습니다."));
        pcap_close(m_pAdapterHandle);
        m_pAdapterHandle = NULL;
        return FALSE;
    }

    const int filterResult = pcap_setfilter(m_pAdapterHandle, &filterProgram);
    pcap_freecode(&filterProgram);

    if (filterResult == -1)
    {
        SetLastErrorMessage(_T("Npcap 수신 필터를 적용하지 못했습니다."));
        pcap_close(m_pAdapterHandle);
        m_pAdapterHandle = NULL;
        return FALSE;
    }

    // Non-blocking capture guarantees that StopReceive can join the worker
    // even when no frame arrives on an otherwise idle adapter.
    if (pcap_setnonblock(
            m_pAdapterHandle,
            1,
            errorBuffer) == -1)
    {
        SetLastErrorMessage(CString(CA2W(errorBuffer, CP_ACP)));
        pcap_close(m_pAdapterHandle);
        m_pAdapterHandle = NULL;
        return FALSE;
    }

    if (!QueryMacAddress(m_Adapters[index].name))
    {
        pcap_close(m_pAdapterHandle);
        m_pAdapterHandle = NULL;
        return FALSE;
    }

    m_nSelectedAdapter = index;
    SetLastErrorMessage(_T(""));
    return TRUE;
}

BOOL CNILayer::QueryMacAddress(const CStringA& adapterName)
{
    LPADAPTER packetAdapter =
        PacketOpenAdapter(const_cast<PCHAR>(static_cast<LPCSTR>(adapterName)));

    if (packetAdapter == NULL || packetAdapter->hFile == INVALID_HANDLE_VALUE)
    {
        if (packetAdapter != NULL)
            PacketCloseAdapter(packetAdapter);

        SetLastErrorMessage(_T("Packet32에서 어댑터를 열지 못했습니다."));
        return FALSE;
    }

    unsigned char oidBuffer[
        sizeof(PACKET_OID_DATA) + ETHERNET_ADDRESS_SIZE] = { 0 };

    PPACKET_OID_DATA oidData =
        reinterpret_cast<PPACKET_OID_DATA>(oidBuffer);
    oidData->Oid = OID_802_3_CURRENT_ADDRESS;
    oidData->Length = ETHERNET_ADDRESS_SIZE;

    const BOOLEAN succeeded = PacketRequest(packetAdapter, FALSE, oidData);
    if (succeeded)
        memcpy(m_MacAddress, oidData->Data, ETHERNET_ADDRESS_SIZE);

    PacketCloseAdapter(packetAdapter);

    if (!succeeded)
    {
        SetLastErrorMessage(_T("Packet32에서 MAC 주소를 조회하지 못했습니다."));
        return FALSE;
    }

    return TRUE;
}

BOOL CNILayer::StartReceive()
{
    if (m_pAdapterHandle == NULL)
    {
        SetLastErrorMessage(_T("먼저 네트워크 어댑터를 여십시오."));
        return FALSE;
    }

    if (m_pReadThread != NULL)
        return TRUE;

    InterlockedExchange(&m_lStopRequested, FALSE);

    m_pReadThread = AfxBeginThread(
        ReadingThread,
        this,
        THREAD_PRIORITY_NORMAL,
        0,
        CREATE_SUSPENDED);

    if (m_pReadThread == NULL)
    {
        InterlockedExchange(&m_lStopRequested, TRUE);
        SetLastErrorMessage(_T("Ethernet 수신 스레드를 생성하지 못했습니다."));
        return FALSE;
    }

    m_pReadThread->m_bAutoDelete = FALSE;
    m_pReadThread->ResumeThread();
    return TRUE;
}

void CNILayer::StopReceive()
{
    if (m_pReadThread == NULL)
        return;

    InterlockedExchange(&m_lStopRequested, TRUE);

    WaitForSingleObject(m_pReadThread->m_hThread, INFINITE);
    delete m_pReadThread;
    m_pReadThread = NULL;
}

void CNILayer::CloseAdapter()
{
    StopReceive();

    if (m_pAdapterHandle != NULL)
    {
        pcap_close(m_pAdapterHandle);
        m_pAdapterHandle = NULL;
    }

    m_nSelectedAdapter = -1;
}

BOOL CNILayer::GetMacAddress(
    unsigned char address[ETHERNET_ADDRESS_SIZE]) const
{
    if (m_nSelectedAdapter < 0 || address == NULL)
        return FALSE;

    memcpy(address, m_MacAddress, ETHERNET_ADDRESS_SIZE);
    return TRUE;
}

BOOL CNILayer::Send(unsigned char* ppayload, int nlength)
{
    if (m_pAdapterHandle == NULL ||
        ppayload == NULL ||
        nlength < ETHER_HEADER_SIZE ||
        nlength > ETHER_MAX_SIZE)
        return FALSE;

    CSingleLock lock(&m_SendLock, TRUE);
    const int result =
        pcap_sendpacket(m_pAdapterHandle, ppayload, nlength);

    if (result != 0)
        SetLastErrorMessage(_T("Npcap이 Ethernet frame을 전송하지 못했습니다."));

    return result == 0;
}

UINT __cdecl CNILayer::ReadingThread(LPVOID pParam)
{
    CNILayer* layer = reinterpret_cast<CNILayer*>(pParam);
    if (layer != NULL)
        layer->ReadPackets();

    return 0;
}

void CNILayer::ReadPackets()
{
    while (InterlockedCompareExchange(
               &m_lStopRequested, FALSE, FALSE) == FALSE)
    {
        pcap_pkthdr* packetHeader = NULL;
        const unsigned char* packetData = NULL;

        const int result = pcap_next_ex(
            m_pAdapterHandle,
            &packetHeader,
            &packetData);

        if (result == 0)
        {
            Sleep(1);
            continue;
        }

        if (result < 0)
            break;

        if (packetHeader == NULL ||
            packetData == NULL ||
            packetHeader->caplen < ETHER_HEADER_SIZE)
            continue;

        CBaseLayer* upper = GetUpperLayer(0);
        if (upper != NULL)
        {
            upper->Receive(
                const_cast<unsigned char*>(packetData),
                static_cast<int>(packetHeader->caplen));
        }
    }
}
