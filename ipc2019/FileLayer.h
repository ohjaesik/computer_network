#pragma once

#include "BaseLayer.h"

// Assignment 3 file-backed IPC layer. It is intentionally kept in the project
// for comparison, while Assignment 4 uses CNILayer as the active lower layer.
class CFileLayer : public CBaseLayer
{
public:
    explicit CFileLayer(const char* pName);
    virtual ~CFileLayer();

    virtual BOOL Receive();
    virtual BOOL Send(unsigned char* ppayload, int nlength);
};

