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
	// Ethernet ������ �ּ� return
	return m_sHeader.enet_dstaddr;
	///////////////////////////////////////////////////////////////////////
}

void CEthernetLayer::SetSourceAddress(unsigned char* pAddress)
{
	//////////////////////// fill the blank ///////////////////////////////
		// �Ѱܹ��� source �ּҸ� Ethernet source�ּҷ� ����
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
	return Send(ppayload, nlength, ETHERNET_TYPE_CHAT);
#endif
	// ChatApp �������� ���� App ������ Frame ���̸�ŭ�� Ethernet������ data�� �ִ´�.
	memcpy(m_sHeader.enet_data, ppayload, nlength);

	BOOL bSuccess = FALSE;
	//////////////////////// fill the blank ///////////////////////////////

		// Ethernet Data + Ethernet Header�� ����� ���� ũ�⸸ŭ�� Ethernet Frame��
		// File �������� ������.
	bSuccess = mp_UnderLayer->Send((unsigned char*)&m_sHeader, nlength + ETHER_HEADER_SIZE);
	///////////////////////////////////////////////////////////////////////
	return bSuccess;
}

BOOL CEthernetLayer::Receive(unsigned char* ppayload)
{
	PETHERNET_HEADER pFrame = (PETHERNET_HEADER)ppayload;

	BOOL bSuccess = FALSE;
	//////////////////////// fill the blank ///////////////////////////////
		// ChatApp �������� Ethernet Frame�� data�� �Ѱ��ش�.
	bSuccess = mp_aUpperLayer[0]->Receive((unsigned char*)pFrame->enet_data);
	///////////////////////////////////////////////////////////////////////

	return bSuccess;
}


// [과제 4 추가] 파일 스레드와 UI의 채팅 전송이 동시에 이 함수에 들어올 수 있다.
// 공통 m_sHeader의 data/type을 덮어쓰지 않고 각 호출의 지역 프레임을 만들어 보낸다.
// 주소는 수신/송신을 멈춘 설정 단계에서만 바뀌므로 전송 도중 변경되지 않는다.
BOOL CEthernetLayer::Send(unsigned char* payload, int length, unsigned short type)
{
	if (!payload || length < 0 || length > ETHER_MAX_DATA_SIZE || !mp_UnderLayer ||
		(type != ETHERNET_TYPE_CHAT && type != ETHERNET_TYPE_FILE)) return FALSE;
	ETHERNET_HEADER frame = {};
	memcpy(frame.enet_dstaddr, m_sHeader.enet_dstaddr, ETHERNET_ADDRESS_SIZE);
	memcpy(frame.enet_srcaddr, m_sHeader.enet_srcaddr, ETHERNET_ADDRESS_SIZE);
	frame.enet_type = htons(type); // 예: 0x2080을 실제 전송 바이트 20 80으로 저장
	memcpy(frame.enet_data, payload, length);
	// FCS를 제외한 Ethernet 최소 크기는 60바이트다. 나머지는 초기화된 0으로 채운다.
	return mp_UnderLayer->Send(reinterpret_cast<unsigned char*>(&frame),
		(std::max)(60, ETHER_HEADER_SIZE + length));
}

BOOL CEthernetLayer::Receive(unsigned char* payload, int length, const unsigned char* source)
{
	if (!payload || length < ETHER_HEADER_SIZE || length > ETHER_MAX_SIZE) return FALSE;
	ETHERNET_HEADER* frame = reinterpret_cast<ETHERNET_HEADER*>(payload);
	const unsigned char broadcast[ETHERNET_ADDRESS_SIZE] = {255,255,255,255,255,255};

	// 자기 주소 또는 broadcast 목적지만 수신한다. Npcap은 자신이 보낸 프레임도
	// 캡처할 수 있으므로 source == 자기 MAC인 경우에는 상위로 되돌려 보내지 않는다.
	if (memcmp(frame->enet_dstaddr, m_sHeader.enet_srcaddr, ETHERNET_ADDRESS_SIZE) &&
		memcmp(frame->enet_dstaddr, broadcast, ETHERNET_ADDRESS_SIZE)) return FALSE;
	if (!memcmp(frame->enet_srcaddr, m_sHeader.enet_srcaddr, ETHERNET_ADDRESS_SIZE)) return FALSE;

	unsigned short type = ntohs(frame->enet_type);
	const char* name = type == ETHERNET_TYPE_CHAT ? "ChatApp" :
		(type == ETHERNET_TYPE_FILE ? "FileApp" : NULL);
	if (!name) return FALSE;
	// LayerManager에 연결된 상위 레이어 중 이름으로 찾으므로 등록 순서에 의존하지 않는다.
	for (int i = 0; i < m_nUpperLayerCount; ++i) {
		CBaseLayer* upper = GetUpperLayer(i);
		if (upper && !strcmp(upper->GetLayerName(), name))
			return upper->Receive(frame->enet_data, length - ETHER_HEADER_SIZE, frame->enet_srcaddr);
	}
	return FALSE;
}
