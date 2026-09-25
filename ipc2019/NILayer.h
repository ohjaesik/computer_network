#pragma once
#include "BaseLayer.h"
#include <pcap.h>

// [assignment4] 실제 NIC와 Ethernet Layer 사이에서 raw frame을 전달하는 계층.
// [assignment4] PCAP 핸들은 이 객체만 소유하며, Dialog가 닫히기 전에 수신 스레드를 종료한다.
class CNILayer : public CBaseLayer
{
public:
	CNILayer(const char* name);
	virtual ~CNILayer();
	// [assignment4] 어댑터 목록 조회, 선택 장치 열기, MAC 조회 결과 제공을 담당하는 인터페이스다.
	BOOL LoadAdapters();
	int GetAdapterCount() const { return static_cast<int>(m_adapters.size()); }
	CString GetAdapterName(int index) const;
	BOOL OpenAdapter(int index);
	// [assignment4] 패킷 수신 전용 스레드를 시작하고 장치 종료 시 해당 스레드가 끝날 때까지 기다린다.
	BOOL StartReceive();
	void CloseAdapter();
	void GetMacAddress(unsigned char* address) const;
	CString GetError() const { return m_error; }
	BOOL Send(unsigned char* payload, int length);
	static UINT __cdecl ReadingThread(LPVOID parameter);

private:
	struct ADAPTER { CStringA name; CString description; };
	std::vector<ADAPTER> m_adapters;
	// [assignment4] Npcap 핸들과 수신 스레드를 소유하고 m_pcapLock으로 동일 핸들의 송수신 접근을 직렬화한다.
	pcap_t* m_handle;
	CWinThread* m_thread;
	volatile LONG m_stop;
	CCriticalSection m_pcapLock;
	unsigned char m_mac[ETHERNET_ADDRESS_SIZE];
	CString m_error;
	BOOL QueryMac(const CStringA& name);
};
