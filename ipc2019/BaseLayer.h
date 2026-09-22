#pragma once

#include "stdafx.h"

class CBaseLayer
{
public:
    explicit CBaseLayer(const char* pName = NULL);
    virtual ~CBaseLayer();

    const char* GetLayerName() const;

    CBaseLayer* GetUnderLayer() const;
    CBaseLayer* GetUpperLayer(int nindex) const;
    int GetUpperLayerCount() const;

    void SetUnderUpperLayer(CBaseLayer* pUULayer = NULL);
    void SetUpperUnderLayer(CBaseLayer* pUULayer = NULL);
    void SetUnderLayer(CBaseLayer* pUnderLayer = NULL);
    void SetUpperLayer(CBaseLayer* pUpperLayer = NULL);

    // Legacy Assignment 3 interface.
    virtual BOOL Send(unsigned char*, int) { return FALSE; }
    virtual BOOL Receive(unsigned char*) { return FALSE; }
    virtual BOOL Receive() { return FALSE; }

    // Assignment 4 interfaces. The protocol argument lets two application
    // layers share one Ethernet layer without racing on mutable type state.
    virtual BOOL Send(unsigned char* ppayload, int nlength, uint16_t protocol)
    {
        UNREFERENCED_PARAMETER(protocol);
        return Send(ppayload, nlength);
    }

    virtual BOOL Receive(unsigned char* ppayload, int nlength)
    {
        UNREFERENCED_PARAMETER(nlength);
        return Receive(ppayload);
    }

    virtual BOOL Receive(
        unsigned char* ppayload,
        int nlength,
        const unsigned char* sourceAddress,
        const unsigned char* destinationAddress)
    {
        UNREFERENCED_PARAMETER(sourceAddress);
        UNREFERENCED_PARAMETER(destinationAddress);
        return Receive(ppayload, nlength);
    }

protected:
    const char* m_pLayerName;
    CBaseLayer* mp_UnderLayer;
    CBaseLayer* mp_aUpperLayer[MAX_LAYER_NUMBER];
    int m_nUpperLayerCount;
};

