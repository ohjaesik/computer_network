// LayerManager.h: interface for the CLayerManager class.
//
//////////////////////////////////////////////////////////////////////

#if !defined(AFX_LAYERMANAGER_H__D9F8CF34_8A6D_425A_BDB9_47A4874FF902__INCLUDED_)
#define AFX_LAYERMANAGER_H__D9F8CF34_8A6D_425A_BDB9_47A4874FF902__INCLUDED_

#include "pch.h"
#include "BaseLayer.h"

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

class CLayerManager
{
private:
	typedef struct _NODE {

		char			token[50];
		struct _NODE* next;

	} NODE, * PNODE;

public:
	void			DeAllocLayer();

	void			ConnectLayers(const char* pcList);
	CBaseLayer* GetLayer(const char* pName);
	CBaseLayer* GetLayer(int nindex);
	void			AddLayer(CBaseLayer* pLayer, BOOL owned = TRUE);

	CLayerManager();
	virtual ~CLayerManager();

private:
	// about stack...
	int				m_nTop;
	CBaseLayer* mp_Stack[MAX_LAYER_NUMBER];

	CBaseLayer* Top();
	CBaseLayer* Pop();
	void			Push(CBaseLayer* pLayer);

	PNODE			mp_sListHead;
	PNODE			mp_sListTail;

	// about Link Layer...
	void			LinkLayer(PNODE pNode);

	inline void		AddNode(PNODE pNode);
	inline PNODE	AllocNode(char* pcName);
	void			MakeList(const char* pcList);

	int				m_nLayerCount;
	CBaseLayer* mp_aLayers[MAX_LAYER_NUMBER];
	BOOL m_owned[MAX_LAYER_NUMBER]; // Dialog(this)는 스택 객체이므로 delete하지 않는다.

};

#endif // !defined(AFX_LAYERMANAGER_H__D9F8CF34_8A6D_425A_BDB9_47A4874FF902__INCLUDED_)
