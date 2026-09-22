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

CChatAppLayer::CChatAppLayer(char* pName)
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
	m_sHeader.app_length = (unsigned short)nlength;

	BOOL bSuccess = FALSE;
	//////////////////////// fill the blank ///////////////////////////////
		// 메모리 복사로 데이터를 header에 저장
		// ChatApp 레이어의 헤더에 데이터와 그 길이를 저장한다.
	memcpy(m_sHeader.app_data, ppayload, nlength > APP_DATA_SIZE ? APP_DATA_SIZE : nlength);

	// ChatApp 레이어의 밑에 레이어인 Ethertnet 레이어에 데이터를 넘겨준다.
	// 메로리 참조로 ChatApp의(헤더 + 데이터)와 (데이터 길이+헤더길이)를
	// 다음 계층의 data로 넘겨준다.
	bSuccess = mp_UnderLayer->Send((unsigned char*)&m_sHeader, nlength + APP_HEADER_SIZE);
	///////////////////////////////////////////////////////////////////////
	return bSuccess;
}

BOOL CChatAppLayer::Receive(unsigned char* ppayload)
{
	// ppayload를 ChatApp 헤더 구조체로 넣는다.
	PCHAT_APP_HEADER app_hdr = (PCHAT_APP_HEADER)ppayload;

	// 보내는 쪽 주소와 받는 쪽의 주소가 일치한 경우 메시지를 보낸다.
	if (app_hdr->app_dstaddr == m_sHeader.app_srcaddr ||
		(app_hdr->app_srcaddr != m_sHeader.app_srcaddr &&
			app_hdr->app_dstaddr == (unsigned int)0xff))
	{
		//////////////////////// fill the blank ///////////////////////////////
				// 밑 계층에서 넘겨받은 ppayload를 분석하여 ChatDlg 계층으로 넘겨준다.
		unsigned char GetBuff[APP_DATA_SIZE]; // 32비트 크기의 App Data Size만큼의 GetBuff를 선언한다.
		memset(GetBuff, '\0', APP_DATA_SIZE);  // GetBuff를 초기화해준다.

		// 받은 데이터인 App Header를 분석하여, GetBuff에 data 길이와 APP_DATA_SIZE 길이와 비교하여 정한 길이만큼
		// data를 저장한다.
		memcpy(GetBuff, app_hdr->app_data, app_hdr->app_length > APP_DATA_SIZE ? APP_DATA_SIZE : app_hdr->app_length);

		CString Msg;
		// App Header를 분석하여, 리스트 창에 뿌려줄 내용의 메시지를 구성한다.
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


