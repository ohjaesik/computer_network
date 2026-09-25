// BaseLayer.cpp: implementation of the CBaseLayer class.
//
//////////////////////////////////////////////////////////////////////

#include "pch.h"  // [assignment4] /Yu 빌드에서 공통 선언을 먼저 불러온다.
#include "stdafx.h"
#include "ipc2019.h"
#include "BaseLayer.h"
#include "pch.h"


#ifdef _DEBUG
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#define new DEBUG_NEW
#endif

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

CBaseLayer::CBaseLayer(const char* pName)
	: m_nUpperLayerCount(0),
	mp_UnderLayer(NULL)
{
	m_pLayerName = pName;
	// [assignment4] 등록 전 상위 계층 배열을 NULL로 초기화하여 초기 포인터 값을 명확하게 한다.
	memset(mp_aUpperLayer, 0, sizeof(mp_aUpperLayer));
}

CBaseLayer::~CBaseLayer()
{

}

void CBaseLayer::SetUnderUpperLayer(CBaseLayer* pUULayer)
{
	if (!pUULayer) // if the pointer is null, 
	{
#ifdef _DEBUG
		TRACE("[CBaseLayer::SetUnderUpperLayer] The variable , 'pUULayer' is NULL");
#endif
		return;
	}

	//////////////////////// fill the blank ///////////////////////////////
		// 인자로 받은 계층은 현재 계층의 Under로 놓고
		// 현재 계층을 인자로 받은 계층의 Upper로 놓는다.
	this->mp_UnderLayer = pUULayer;
	pUULayer->SetUpperLayer(this);
	///////////////////////////////////////////////////////////////////////
}

void CBaseLayer::SetUpperUnderLayer(CBaseLayer* pUULayer)
{
	if (!pUULayer) // if the pointer is null, 
	{
#ifdef _DEBUG
		TRACE("[CBaseLayer::SetUpperUnderLayer] The variable , 'pUULayer' is NULL");
#endif
		return;
	}

	//////////////////////// fill the blank ///////////////////////////////
		// 인자로 받은 계층을 Upper에 놓고
		// 현재 계층은 Upper로 놓은 계층의 Under로 놓는다.
	SetUpperLayer(pUULayer);
	pUULayer->SetUnderLayer(this);
	///////////////////////////////////////////////////////////////////////
}

void CBaseLayer::SetUpperLayer(CBaseLayer* pUpperLayer)
{
	if (!pUpperLayer) // if the pointer is null, 
	{
#ifdef _DEBUG
		TRACE("[CBaseLayer::SetUpperLayer] The variable , 'pUpperLayer' is NULL");
#endif
		return;
	}

	// [assignment4] 등록 가능한 개수를 넘으면 배열 밖에 포인터를 기록하지 않는다.
	if (m_nUpperLayerCount >= MAX_LAYER_NUMBER) return;

	// UpperLayer is added..
	this->mp_aUpperLayer[m_nUpperLayerCount++] = pUpperLayer;
}

void CBaseLayer::SetUnderLayer(CBaseLayer* pUnderLayer)
{
	if (!pUnderLayer) // if the pointer is null, 
	{
#ifdef _DEBUG
		TRACE("[CBaseLayer::SetUnderLayer] The variable , 'pUnderLayer' is NULL\n");
#endif
		return;
	}

	// UnderLayer assignment..
	this->mp_UnderLayer = pUnderLayer;
}

// [assignment4] 상위 계층 인덱스를 등록 개수 미만으로 제한하여 배열의 다음 빈 칸을 반환하지 않는다.
CBaseLayer* CBaseLayer::GetUpperLayer(int nindex)
{
	if (nindex < 0 ||
		nindex >= m_nUpperLayerCount ||
		m_nUpperLayerCount < 0)
	{
#ifdef _DEBUG
		TRACE("[CBaseLayer::GetUpperLayer] There is no UpperLayer in Array..\n");
#endif 
		return NULL;
	}

	return mp_aUpperLayer[nindex];
}

CBaseLayer* CBaseLayer::GetUnderLayer()
{
	if (!mp_UnderLayer)
	{
#ifdef _DEBUG
		TRACE("[CBaseLayer::GetUnderLayer] There is not a UnerLayer..\n");
#endif 
		return NULL;
	}

	return mp_UnderLayer;
}

// [assignment4] 계층 검색에 쓰는 이름을 읽기 전용 포인터로 반환하여 문자열 리터럴을 수정하지 않는다.
const char* CBaseLayer::GetLayerName()
{
	return m_pLayerName;
}

