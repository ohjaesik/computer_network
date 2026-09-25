#pragma once
// BaseLayer.h: interface for the CBaseLayer class.
//
//////////////////////////////////////////////////////////////////////

#include "pch.h"
#include"stdafx.h"

#if !defined(AFX_BASELAYER_H__041C5A07_23A9_4CBC_970B_8743460A7DA9__INCLUDED_)
#define AFX_BASELAYER_H__041C5A07_23A9_4CBC_970B_8743460A7DA9__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

// [assignment4] 모든 프로토콜 계층이 상속하는 공통 인터페이스로 상위·하위 객체 포인터를 관리한다.
class CBaseLayer
{
public:
	const char* GetLayerName();

	CBaseLayer* GetUnderLayer();
	CBaseLayer* GetUpperLayer(int nindex);
	void			SetUnderUpperLayer(CBaseLayer* pUULayer = NULL);
	void			SetUpperUnderLayer(CBaseLayer* pUULayer = NULL);
	void			SetUnderLayer(CBaseLayer* pUnderLayer = NULL);
	void			SetUpperLayer(CBaseLayer* pUpperLayer = NULL);

	CBaseLayer(const char* pName = NULL);
	virtual ~CBaseLayer();

	// param : unsigned char*	- the data of the upperlayer
	//         int				- the length of data
	virtual	BOOL	Send(unsigned char*, int) { return FALSE; }
	// param : unsigned char*	- the data of the underlayer
	virtual	BOOL	Receive(unsigned char* ppayload) { return FALSE; }
	virtual	BOOL	Receive() { return FALSE; }

	// [assignment4] 기존 Receive(pointer)는 유지한다. 실제 캡처 길이를 함께 받아야
	// [assignment4] 짧거나 잘린 프레임을 검사할 수 있다. source는 조각의 송신자 확인에 쓴다.
	virtual BOOL Receive(unsigned char* payload, int length, const unsigned char* source = NULL)
	{
		return Receive(payload);
	}

	// [assignment4] 채팅/파일이 동시에 Ethernet을 사용하므로 EtherType을 공유 멤버에 쓰지
	// [assignment4] 않고 전송 호출마다 전달한다. 기존 두 인자 Send는 그대로 사용 가능하다.
	virtual BOOL Send(unsigned char* payload, int length, unsigned short type)
	{
		return Send(payload, length);
	}

protected:
	const char* m_pLayerName;
	CBaseLayer* mp_UnderLayer;							// UnderLayer pointer
	CBaseLayer* mp_aUpperLayer[MAX_LAYER_NUMBER];		// UpperLayer pointer
	int				m_nUpperLayerCount;						// UpperLayer Count
};

#endif // !defined(AFX_BASELAYER_H__041C5A07_23A9_4CBC_970B_8743460A7DA9__INCLUDED_)
