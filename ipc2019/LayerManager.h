#pragma once

#include "BaseLayer.h"

class CLayerManager
{
private:
    struct NODE
    {
        char token[50];
        NODE* next;
    };
    typedef NODE* PNODE;

public:
    CLayerManager();
    virtual ~CLayerManager();

    void AddLayer(CBaseLayer* pLayer, BOOL bOwned = TRUE);
    CBaseLayer* GetLayer(const char* pName) const;
    CBaseLayer* GetLayer(int nindex) const;
    void ConnectLayers(const char* pcList);
    void DeAllocLayer();

private:
    CBaseLayer* mp_aLayers[MAX_LAYER_NUMBER];
    BOOL m_abOwned[MAX_LAYER_NUMBER];
    int m_nLayerCount;

    CBaseLayer* mp_Stack[MAX_LAYER_NUMBER];
    int m_nTop;

    PNODE mp_sListHead;
    PNODE mp_sListTail;

    CBaseLayer* Top() const;
    CBaseLayer* Pop();
    void Push(CBaseLayer* pLayer);

    PNODE AllocNode(const char* pcName);
    void AddNode(PNODE pNode);
    void MakeList(const char* pcList);
    void LinkLayer(PNODE pNode);
    void ClearNodes();
};

