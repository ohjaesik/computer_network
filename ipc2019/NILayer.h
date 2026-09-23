#pragma once
#include "BaseLayer.h"
#include <pcap.h>

// [과제 4 추가] 실제 NIC와 Ethernet Layer 사이에서 raw frame을 전달하는 계층.
// PCAP 핸들은 이 객체만 소유하며, Dialog가 닫히기 전에 수신 스레드를 종료한다.
class CNILayer : public CBaseLayer
{
public:
	CNILayer(const char* name);
	virtual ~CNILayer();
	BOOL LoadAdapters();
	int GetAdapterCount() const { return static_cast<int>(m_adapters.size()); }
	CString GetAdapterName(int index) const;
	BOOL OpenAdapter(int index);
	BOOL StartReceive();
	void CloseAdapter();
	void GetMacAddress(unsigned char* address) const;
	CString GetError() const { return m_error; }
	BOOL Send(unsigned char* payload, int length);
	static UINT __cdecl ReadingThread(LPVOID parameter);

private:
	struct ADAPTER { CStringA name; CString description; };
	std::vector<ADAPTER> m_adapters;
	pcap_t* m_handle;
	CWinThread* m_thread;
	volatile LONG m_stop;
	CCriticalSection m_pcapLock;
	unsigned char m_mac[ETHERNET_ADDRESS_SIZE];
	CString m_error;
	BOOL QueryMac(const CStringA& name);
};
