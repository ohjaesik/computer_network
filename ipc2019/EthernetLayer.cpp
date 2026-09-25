// EthernetLayer.cpp: implementation of the CEthernetLayer class.
//
//////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "pch.h"
#include "EthernetLayer.h"

#ifdef _DEBUG
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#define new DEBUG_NEW
#endif

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

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
	memset(m_sHeader.enet_dstaddr, 0, 6);
	memset(m_sHeader.enet_srcaddr, 0, 6);
	memset(m_sHeader.enet_data, 0, ETHER_MAX_DATA_SIZE);
	m_sHeader.enet_type = 0;
}

unsigned char* CEthernetLayer::GetSourceAddress()
{
	return m_sHeader.enet_srcaddr;
}

unsigned char* CEthernetLayer::GetDestinAddress()
{
	//////////////////////// fill the blank ///////////////////////////////
	// Ethernet 헤더에 저장된 목적지 MAC 주소 배열을 반환한다.
	return m_sHeader.enet_dstaddr;
	///////////////////////////////////////////////////////////////////////
}

void CEthernetLayer::SetSourceAddress(unsigned char* pAddress)
{
	//////////////////////// fill the blank ///////////////////////////////
		// 전달받은 MAC 주소 6바이트를 Ethernet 헤더의 출발지 주소에 복사한다.
	memcpy(m_sHeader.enet_srcaddr, pAddress, 6);
	///////////////////////////////////////////////////////////////////////
}

void CEthernetLayer::SetDestinAddress(unsigned char* pAddress)
{
	memcpy(m_sHeader.enet_dstaddr, pAddress, 6);
}

BOOL CEthernetLayer::Send(unsigned char* ppayload, int nlength)
{
#if USE_NPCAP_STACK
	// [assignment4] 기존 두 인자 Send 호출도 채팅용 EtherType을 지정한 네트워크 송신으로 연결한다.
	return Send(ppayload, nlength, ETHERNET_TYPE_CHAT);
#endif
	// 기존 IPC 송신: 상위 ChatApp의 헤더와 데이터를 Ethernet 데이터 영역에 복사한다.
	memcpy(m_sHeader.enet_data, ppayload, nlength);

	BOOL bSuccess = FALSE;
	//////////////////////// fill the blank ///////////////////////////////

		// Ethernet 헤더와 데이터를 합친 프레임을 하위 File 계층에 전달한다.
		// 전달 길이는 Ethernet 헤더 크기와 상위 계층 데이터 길이의 합이다.
	bSuccess = mp_UnderLayer->Send((unsigned char*)&m_sHeader, nlength + ETHER_HEADER_SIZE);
	///////////////////////////////////////////////////////////////////////
	return bSuccess;
}

BOOL CEthernetLayer::Receive(unsigned char* ppayload)
{
	PETHERNET_HEADER pFrame = (PETHERNET_HEADER)ppayload;

	BOOL bSuccess = FALSE;
	//////////////////////// fill the blank ///////////////////////////////
		// 기존 IPC 수신: Ethernet 헤더를 제외한 데이터 영역을 상위 ChatApp에 전달한다.
	bSuccess = mp_aUpperLayer[0]->Receive((unsigned char*)pFrame->enet_data);
	///////////////////////////////////////////////////////////////////////

	return bSuccess;
}


// [assignment4] 파일 스레드와 UI의 채팅 전송이 동시에 이 함수에 들어올 수 있다.
// [assignment4] 공통 m_sHeader의 data/type을 덮어쓰지 않고 각 호출의 지역 프레임을 만들어 보낸다.
// [assignment4] 주소는 수신/송신을 멈춘 설정 단계에서만 바뀌므로 전송 도중 변경되지 않는다.
BOOL CEthernetLayer::Send(unsigned char* payload, int length, unsigned short type)
{
	if (!payload || length < 0 || length > ETHER_MAX_DATA_SIZE || !mp_UnderLayer ||
		(type != ETHERNET_TYPE_CHAT && type != ETHERNET_TYPE_FILE)) return FALSE;
	ETHERNET_HEADER frame = {};
	memcpy(frame.enet_dstaddr, m_sHeader.enet_dstaddr, ETHERNET_ADDRESS_SIZE);
	memcpy(frame.enet_srcaddr, m_sHeader.enet_srcaddr, ETHERNET_ADDRESS_SIZE);
	frame.enet_type = htons(type); // [assignment4] 예: 0x2080을 실제 전송 바이트 20 80으로 저장
	memcpy(frame.enet_data, payload, length);
	// [assignment4] FCS를 제외한 Ethernet 최소 크기는 60바이트다. 나머지는 초기화된 0으로 채운다.
	return mp_UnderLayer->Send(reinterpret_cast<unsigned char*>(&frame),
		(std::max)(60, ETHER_HEADER_SIZE + length));
}

// [assignment4] 프레임 길이와 MAC 주소를 검사한 뒤 EtherType에 따라 ChatApp 또는 FileApp으로 역캡슐화한다.
BOOL CEthernetLayer::Receive(unsigned char* payload, int length, const unsigned char* source)
{
	if (!payload || length < ETHER_HEADER_SIZE || length > ETHER_MAX_SIZE) return FALSE;
	ETHERNET_HEADER* frame = reinterpret_cast<ETHERNET_HEADER*>(payload);
	const unsigned char broadcast[ETHERNET_ADDRESS_SIZE] = {255,255,255,255,255,255};

	// [assignment4] 자기 주소 또는 broadcast 목적지만 수신한다. Npcap은 자신이 보낸 프레임도
	// [assignment4] 캡처할 수 있으므로 source == 자기 MAC인 경우에는 상위로 되돌려 보내지 않는다.
	if (memcmp(frame->enet_dstaddr, m_sHeader.enet_srcaddr, ETHERNET_ADDRESS_SIZE) &&
		memcmp(frame->enet_dstaddr, broadcast, ETHERNET_ADDRESS_SIZE)) return FALSE;
	if (!memcmp(frame->enet_srcaddr, m_sHeader.enet_srcaddr, ETHERNET_ADDRESS_SIZE)) return FALSE;

	// [assignment4] 16비트 EtherType을 호스트 바이트 순서로 복원하여 채팅 0x2080과 파일 0x2090을 구분한다.
	unsigned short type = ntohs(frame->enet_type);
	const char* name = type == ETHERNET_TYPE_CHAT ? "ChatApp" :
		(type == ETHERNET_TYPE_FILE ? "FileApp" : NULL);
	if (!name) return FALSE;
	// [assignment4] LayerManager에 연결된 상위 레이어 중 이름으로 찾으므로 등록 순서에 의존하지 않는다.
	for (int i = 0; i < m_nUpperLayerCount; ++i) {
		CBaseLayer* upper = GetUpperLayer(i);
		if (upper && !strcmp(upper->GetLayerName(), name))
			return upper->Receive(frame->enet_data, length - ETHER_HEADER_SIZE, frame->enet_srcaddr);
	}
	return FALSE;
}
