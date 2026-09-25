// ChatAppLayer.cpp: implementation of the CChatAppLayer class.
//
//////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "pch.h"
#include "ChatAppLayer.h"

#ifdef _DEBUG
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#define new DEBUG_NEW
#endif

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

CChatAppLayer::CChatAppLayer(const char* pName)
	: CBaseLayer(pName),
	mp_Dlg(NULL)
{
	ResetHeader();
}

CChatAppLayer::~CChatAppLayer()
{

}

void CChatAppLayer::SetSourceAddress(unsigned int src_addr)
{
	m_sHeader.app_srcaddr = src_addr;
}

void CChatAppLayer::SetDestinAddress(unsigned int dst_addr)
{
	m_sHeader.app_dstaddr = dst_addr;
}

void CChatAppLayer::ResetHeader()
{
	m_sHeader.app_srcaddr = 0x00000000;
	m_sHeader.app_dstaddr = 0x00000000;
	m_sHeader.app_length = 0x0000;
	m_sHeader.app_type = 0x00;
	memset(m_sHeader.app_data, 0, APP_DATA_SIZE);
}

unsigned int CChatAppLayer::GetSourceAddress()
{
	return m_sHeader.app_srcaddr;
}

unsigned int CChatAppLayer::GetDestinAddress()
{
	return m_sHeader.app_dstaddr;
}

BOOL CChatAppLayer::Send(unsigned char* ppayload, int nlength)
{
#if USE_NPCAP_STACK
	// [assignment4] 네트워크 모드에서는 SendNetwork로 UTF-8 메시지를 단편화하여 Ethernet에 전달한다.
	return SendNetwork(ppayload, nlength);
#endif
	m_sHeader.app_length = (unsigned short)nlength;

	BOOL bSuccess = FALSE;
	//////////////////////// fill the blank ///////////////////////////////
		// 메모리 복사로 데이터를 header에 저장
		// ChatApp 레이어의 헤더에 데이터와 그 길이를 저장한다.
	memcpy(m_sHeader.app_data, ppayload, nlength > APP_DATA_SIZE ? APP_DATA_SIZE : nlength);

	// 기존 IPC 송신: ChatApp 헤더와 데이터를 하위 Ethernet 계층에 전달한다.
	// ChatApp 구조체의 시작 주소와 헤더를 포함한 전송 길이를
	// 다음 계층의 data로 넘겨준다.
	bSuccess = mp_UnderLayer->Send((unsigned char*)&m_sHeader, nlength + APP_HEADER_SIZE);
	///////////////////////////////////////////////////////////////////////
	return bSuccess;
}

BOOL CChatAppLayer::Receive(unsigned char* ppayload)
{
	// 전달받은 바이트 배열을 기존 IPC ChatApp 헤더 구조체로 해석한다.
	PCHAT_APP_HEADER app_hdr = (PCHAT_APP_HEADER)ppayload;

	// 목적지가 자신의 주소이거나 다른 프로세스가 보낸 브로드캐스트인 경우 수신한다.
	if (app_hdr->app_dstaddr == m_sHeader.app_srcaddr ||
		(app_hdr->app_srcaddr != m_sHeader.app_srcaddr &&
			app_hdr->app_dstaddr == (unsigned int)0xff))
	{
		//////////////////////// fill the blank ///////////////////////////////
				// 밑 계층에서 넘겨받은 ppayload를 분석하여 ChatDlg 계층으로 넘겨준다.
		unsigned char GetBuff[APP_DATA_SIZE]; // APP_DATA_SIZE바이트의 메시지 복사 버퍼를 확보한다.
		memset(GetBuff, '\0', APP_DATA_SIZE);  // GetBuff를 초기화해준다.

		// 받은 데이터인 App Header를 분석하여, GetBuff에 data 길이와 APP_DATA_SIZE 길이와 비교하여 정한 길이만큼
		// data를 저장한다.
		memcpy(GetBuff, app_hdr->app_data, app_hdr->app_length > APP_DATA_SIZE ? APP_DATA_SIZE : app_hdr->app_length);

		CString Msg;
		// 송수신 주소와 메시지를 조합하여 Dialog에 전달할 출력 문자열을 만든다.
		// 보내는 쪽 또는 받는 쪽과 GetBuff에 저장된 메시지 내용을 합친다.
		if (app_hdr->app_dstaddr == (unsigned int)0xff)
			Msg.Format(_T("[%d:BROADCAST] %s"), app_hdr->app_srcaddr, (char*)GetBuff);
		else
			Msg.Format(_T("[%d:%d] %s"), app_hdr->app_srcaddr, app_hdr->app_dstaddr, (char*)GetBuff);

		// 위에서 만들어진 메시지 포맷을 ChatDlg로 넘겨준다.
		mp_aUpperLayer[0]->Receive((unsigned char*)Msg.GetBuffer(0));
		///////////////////////////////////////////////////////////////////////
		return TRUE;
	}
	else
		return FALSE;
}




// [assignment4] UTF-8 메시지를 최대 1496바이트씩 나누고 4바이트 채팅 헤더를 붙여 송신한다.
// [assignment4] 모든 조각의 totlen에는 원본 메시지의 전체 바이트 길이를 기록한다.
// [assignment4] 작은 메시지는 FIRST 하나로 끝내고, 큰 메시지는 FIRST-(MIDDLE...)-LAST로 보낸다.
// [assignment4] 2바이트 totlen에 담을 수 없는 크기는 잘라서 보내지 않고 명시적으로 실패한다.
BOOL CChatAppLayer::SendNetwork(unsigned char* payload, int length)
{
	static_assert(sizeof(NETWORK_CHAT_HEADER) == ETHER_MAX_DATA_SIZE, "Chat MTU");
	if (!payload || length <= 0 || length > CHAT_MAX_MESSAGE_SIZE || !mp_UnderLayer)
		return FALSE;

	for (int offset = 0; offset < length; offset += CHAT_APP_DATA_SIZE) {
		NETWORK_CHAT_HEADER packet = {};
		int count = (std::min)(CHAT_APP_DATA_SIZE, length - offset);
		packet.capp_totlen = htons(static_cast<uint16_t>(length));
		packet.capp_type = offset == 0 ? CHAT_FRAGMENT_FIRST :
			(offset + count == length ? CHAT_FRAGMENT_LAST : CHAT_FRAGMENT_MIDDLE);
		memcpy(packet.capp_data, payload + offset, count);
		if (!mp_UnderLayer->Send(reinterpret_cast<unsigned char*>(&packet),
			CHAT_APP_HEADER_SIZE + count, ETHERNET_TYPE_CHAT)) return FALSE;
	}
	return TRUE;
}

// [assignment4] 재조립 중이던 바이트, 전체 길이, 송신자 MAC을 초기화한다. 새 메시지·오류·주소 재설정에 사용한다.
void CChatAppLayer::ResetNetworkReceive()
{
	m_received.clear();
	m_totalLength = 0;
	memset(m_receiveSource, 0, sizeof(m_receiveSource));
}

// [assignment4] NI 수신 스레드에서만 호출된다. 최종 조각까지 확인하기 전에는 UI에 전달하지 않는다.
// [assignment4] Ethernet 최소 프레임의 padding은 totlen을 기준으로 제외하여 메시지에 섞이지 않는다.
BOOL CChatAppLayer::Receive(unsigned char* payload, int length, const unsigned char* source)
{
	if (!payload || !source || length < CHAT_APP_HEADER_SIZE || length > ETHER_MAX_DATA_SIZE)
		return FALSE;
	NETWORK_CHAT_HEADER* packet = reinterpret_cast<NETWORK_CHAT_HEADER*>(payload);
	// [assignment4] 네트워크 바이트 순서의 전체 길이를 복원하고 FIRST/MIDDLE/LAST 유형을 검사한다.
	unsigned int total = ntohs(packet->capp_totlen);
	unsigned char type = packet->capp_type;
	if (!total || type > CHAT_FRAGMENT_LAST) return FALSE;

	if (type == CHAT_FRAGMENT_FIRST) {
		ResetNetworkReceive();
		m_totalLength = total;
		m_received.reserve(total); // [assignment4] 첫 조각의 전체 길이로 재조립 버퍼 확보
		memcpy(m_receiveSource, source, sizeof(m_receiveSource));
	} else {
		// [assignment4] 다른 송신자의 조각이 진행 중인 메시지에 섞이지 않도록 확인한다.
		if (memcmp(m_receiveSource, source, sizeof(m_receiveSource)) != 0) return FALSE;
		if (!m_totalLength || total != m_totalLength) {
			ResetNetworkReceive();
			return FALSE;
		}
	}
	int remaining = static_cast<int>(m_totalLength - m_received.size());
	int count = (std::min)(CHAT_APP_DATA_SIZE, remaining);
	bool final = count == remaining;
	// [assignment4] 첫 조각 이후에는 남은 데이터가 1496바이트 이하면 LAST, 초과하면 MIDDLE이어야 한다.
	if (length - CHAT_APP_HEADER_SIZE < count ||
		(type != CHAT_FRAGMENT_FIRST && type != (final ? CHAT_FRAGMENT_LAST : CHAT_FRAGMENT_MIDDLE))) {
		ResetNetworkReceive();
		return FALSE;
	}
	// [assignment4] 현재 조각의 유효 바이트만 이어 붙이고 전체 메시지가 모이면 상위 Dialog로 한 번 전달한다.
	m_received.insert(m_received.end(), packet->capp_data, packet->capp_data + count);
	if (!final) return TRUE;

	// [assignment4] UI는 수신 버퍼의 포인터를 보관하지 않고 자신의 메시지 큐용 복사본을 만든다.
	CBaseLayer* upper = GetUpperLayer(0);
	BOOL result = upper && upper->Receive(m_received.data(),
		static_cast<int>(m_received.size()), m_receiveSource);
	ResetNetworkReceive();
	return result;
}
