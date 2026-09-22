// BaseLayer.cpp: implementation of the CBaseLayer class.
//
//////////////////////////////////////////////////////////////////////

#include "pch.h"  // /Yu 빌드에서 공통 선언을 먼저 불러온다.
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

	// 등록 가능한 개수를 넘으면 배열 밖에 포인터를 기록하지 않는다.
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

const char* CBaseLayer::GetLayerName()
{
	return m_pLayerName;
}

