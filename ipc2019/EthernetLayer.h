// EthernetLayer.h: interface for the CEthernetLayer class.
//
//////////////////////////////////////////////////////////////////////

#if !defined(AFX_ETHERNETLAYER_H__7857C9C2_B459_4DC8_B9B3_4E6C8B587B29__INCLUDED_)
#define AFX_ETHERNETLAYER_H__7857C9C2_B459_4DC8_B9B3_4E6C8B587B29__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#include "BaseLayer.h"
#include "pch.h"
#include <mutex>

class CEthernetLayer
	: public CBaseLayer
{
private:
	inline void		ResetHeader();

public:
	BOOL			Receive(unsigned char* ppayload);
	BOOL			Send(unsigned char* ppayload, int nlength);
	void			SetDestinAddress(unsigned char* pAddress);
	void			SetSourceAddress(unsigned char* pAddress);
	unsigned char* GetDestinAddress();
	unsigned char* GetSourceAddress();

	CEthernetLayer(const char* pName);
	virtual ~CEthernetLayer();

	typedef struct _ETHERNET_HEADER {

		unsigned char	enet_dstaddr[6];		// destination address of ethernet layer
		unsigned char	enet_srcaddr[6];		// source address of ethernet layer
		unsigned short	enet_type;		// type of ethernet layer
		unsigned char	enet_data[ETHER_MAX_DATA_SIZE]; // frame data

	} ETHERNET_HEADER, * PETHERNET_HEADER;

public:
	// [assignment4] 기존 두 인자 Send/한 인자 Receive는 IPC 호환용으로 유지한다.
	// [assignment4] 새 호출은 타입과 캡처 길이를 명시해 ChatApp/FileApp을 구분한다.
	BOOL Send(unsigned char* payload, int length, unsigned short type);
	// [assignment6] 각 패킷의 목적지를 지정한다. [assignment7] GARP 실습은 sourceOverride도 패킷 단위로 사용한다.
	BOOL SendTo(unsigned char* payload, int length, unsigned short type,
		const unsigned char* destination, const unsigned char* sourceOverride = NULL);
	BOOL Receive(unsigned char* payload, int length, const unsigned char* source = NULL);
	// [assignment7] 물리 NIC는 유지하고 앱의 논리 MAC만 바꾼다. 송신·필터가 같은 주소를 사용한다.
	void SetLogicalSourceAddress(const unsigned char* address);
	// [assignment7] 새 MAC을 Probe 중일 때 그 MAC으로 돌아오는 unicast ARP Reply만 임시로 받는다.
	void SetProbeAddress(const unsigned char* address);
	void SetProtocolLayers(CBaseLayer* ip, CBaseLayer* arp) { m_ipLayer = ip; m_arpLayer = arp; }

protected:
	ETHERNET_HEADER	m_sHeader;
	std::mutex m_addressMutex;
	unsigned char m_physicalMac[6] = {};
	unsigned char m_probeMac[6] = {};
	bool m_acceptProbe = false;
	CBaseLayer* m_ipLayer = nullptr;
	CBaseLayer* m_arpLayer = nullptr;
};

#endif // !defined(AFX_ETHERNETLAYER_H__7857C9C2_B459_4DC8_B9B3_4E6C8B587B29__INCLUDED_)
