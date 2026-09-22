// BaseLayer.cpp: common layer-link implementation.

#include "pch.h"
#include "BaseLayer.h"

#ifdef _DEBUG
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#define new DEBUG_NEW
#endif

CBaseLayer::CBaseLayer(const char* pName)
    : m_pLayerName(pName),
      mp_UnderLayer(NULL),
      m_nUpperLayerCount(0)
{
    memset(mp_aUpperLayer, 0, sizeof(mp_aUpperLayer));
}

CBaseLayer::~CBaseLayer()
{
}

void CBaseLayer::SetUnderUpperLayer(CBaseLayer* pUULayer)
{
    if (pUULayer == NULL)
        return;

    mp_UnderLayer = pUULayer;
    pUULayer->SetUpperLayer(this);
}

void CBaseLayer::SetUpperUnderLayer(CBaseLayer* pUULayer)
{
    if (pUULayer == NULL)
        return;

    SetUpperLayer(pUULayer);
    pUULayer->SetUnderLayer(this);
}

void CBaseLayer::SetUpperLayer(CBaseLayer* pUpperLayer)
{
    if (pUpperLayer == NULL || m_nUpperLayerCount >= MAX_LAYER_NUMBER)
        return;

    mp_aUpperLayer[m_nUpperLayerCount++] = pUpperLayer;
}

void CBaseLayer::SetUnderLayer(CBaseLayer* pUnderLayer)
{
    if (pUnderLayer != NULL)
        mp_UnderLayer = pUnderLayer;
}

CBaseLayer* CBaseLayer::GetUpperLayer(int nindex) const
{
    if (nindex < 0 || nindex >= m_nUpperLayerCount)
        return NULL;

    return mp_aUpperLayer[nindex];
}

int CBaseLayer::GetUpperLayerCount() const
{
    return m_nUpperLayerCount;
}

CBaseLayer* CBaseLayer::GetUnderLayer() const
{
    return mp_UnderLayer;
}

const char* CBaseLayer::GetLayerName() const
{
    return m_pLayerName;
}

