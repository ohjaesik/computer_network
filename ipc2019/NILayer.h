#pragma once

#include "BaseLayer.h"
#include <pcap.h>

class CNILayer : public CBaseLayer
{
public:
    explicit CNILayer(const char* pName);
    virtual ~CNILayer();

    BOOL RefreshAdapterList();
    int GetAdapterCount() const;
    CString GetAdapterDescription(int index) const;

    BOOL OpenAdapter(int index);
    void CloseAdapter();
    BOOL StartReceive();
    void StopReceive();

    BOOL GetMacAddress(unsigned char address[ETHERNET_ADDRESS_SIZE]) const;
    CString GetLastErrorMessage() const;

    virtual BOOL Send(unsigned char* ppayload, int nlength);

    // Required receive-only worker for Ethernet frames.
    static UINT __cdecl ReadingThread(LPVOID pParam);

private:
    struct ADAPTER_ENTRY
    {
        CStringA name;
        CString description;
    };

    std::vector<ADAPTER_ENTRY> m_Adapters;
    pcap_t* m_pAdapterHandle;
    CWinThread* m_pReadThread;
    volatile LONG m_lStopRequested;
    unsigned char m_MacAddress[ETHERNET_ADDRESS_SIZE];
    int m_nSelectedAdapter;
    CString m_strLastError;
    CCriticalSection m_SendLock;

    BOOL QueryMacAddress(const CStringA& adapterName);
    void ReadPackets();
    void SetLastErrorMessage(LPCTSTR message);
};

