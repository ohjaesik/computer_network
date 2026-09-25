#pragma once
// ChatAppLayer.h: interface for the CChatAppLayer class.
//
//////////////////////////////////////////////////////////////////////

#if !defined(AFX_CHATAPPLAYER_H__E78615DE_0F23_41A9_B814_34E2B3697EF2__INCLUDED_)
#define AFX_CHATAPPLAYER_H__E78615DE_0F23_41A9_B814_34E2B3697EF2__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#include "BaseLayer.h"
#include "pch.h"
class CChatAppLayer
	: public CBaseLayer
{
private:
	inline void		ResetHeader();
	CObject* mp_Dlg;

public:
	BOOL			Receive(unsigned char* ppayload);
	BOOL			Send(unsigned char* ppayload, int nlength);
	unsigned int	GetDestinAddress();
	unsigned int	GetSourceAddress();
	void			SetDestinAddress(unsigned int dst_addr);
	void			SetSourceAddress(unsigned int src_addr);

	CChatAppLayer(const char* pName);
	virtual ~CChatAppLayer();

	typedef struct _CHAT_APP_HEADER {

		unsigned int	app_dstaddr; // destination address of application layer
		unsigned int	app_srcaddr; // source address of application layer
		unsigned short	app_length; // total length of the data
		unsigned char	app_type; // type of application data
		unsigned char	app_data[APP_DATA_SIZE]; // application data

	} CHAT_APP_HEADER, * PCHAT_APP_HEADER;

public:
	// [assignment4] 기존 IPC 헤더는 유지하고 네트워크용 4바이트 헤더를 별도로 둔다.
	// [assignment4] pack(1)은 컴파일러가 필드 사이에 정렬용 바이트를 넣지 못하게 한다.
#pragma pack(push, 1)
	struct NETWORK_CHAT_HEADER {
		uint16_t capp_totlen;       // [assignment4] 전체 UTF-8 데이터 길이, network byte order
		unsigned char capp_type;   // [assignment4] 첫 조각 0, 중간 조각 1, 마지막 조각 2
		unsigned char capp_unused; // [assignment4] 예약 필드. 송신 시 반드시 0으로 초기화
		unsigned char capp_data[CHAT_APP_DATA_SIZE];
	};
#pragma pack(pop)
	BOOL Receive(unsigned char* payload, int length, const unsigned char* source = NULL);
	void ResetNetworkReceive(); // [assignment4] 어댑터 재설정 시 이전의 미완성 메시지 제거

private:
	BOOL SendNetwork(unsigned char* payload, int length);
	// [assignment4] 한 메시지의 조각과 전체 길이, 송신자 MAC을 보관하여 다른 송신자의 조각이 섞이지 않게 한다.
	std::vector<unsigned char> m_received;
	unsigned int m_totalLength = 0;
	unsigned char m_receiveSource[ETHERNET_ADDRESS_SIZE] = {};

protected:
	CHAT_APP_HEADER		m_sHeader;

	enum {
		DATA_TYPE_CONT = 0x01,
		DATA_TYPE_END = 0x02
	};
};

#endif // !defined(AFX_CHATAPPLAYER_H__E78615DE_0F23_41A9_B814_34E2B3697EF2__INCLUDED_)










