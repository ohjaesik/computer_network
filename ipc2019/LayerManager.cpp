#pragma once
// LayerManager.cpp: implementation of the CLayerManager class.
//
//////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "pch.h"
#include "LayerManager.h"

#ifdef _DEBUG
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#define new DEBUG_NEW
#endif

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

CLayerManager::CLayerManager()
	: m_nLayerCount(0),
	mp_sListHead(NULL),
	mp_sListTail(NULL),
	m_nTop(-1)
{

}

CLayerManager::~CLayerManager()
{

}

void CLayerManager::AddLayer(CBaseLayer* pLayer, BOOL owned)
{
	if (!pLayer || m_nLayerCount >= MAX_LAYER_NUMBER) return;
	m_owned[m_nLayerCount] = owned;
	mp_aLayers[m_nLayerCount++] = pLayer;
}

CBaseLayer* CLayerManager::GetLayer(int nindex)
{
	return mp_aLayers[nindex];
}

CBaseLayer* CLayerManager::GetLayer(const char* pName)
{
	for (int i = 0; i < m_nLayerCount; i++)
	{
		if (!strcmp(pName, mp_aLayers[i]->GetLayerName()))
			return mp_aLayers[i];
	}

	return NULL;
}

void CLayerManager::ConnectLayers(const char* pcList)
{
	MakeList(pcList);
	LinkLayer(mp_sListHead);
	int arr;
	arr = 3;
	// 연결 문자열을 분석하며 만든 임시 노드만 해제한다. 실제 Layer는 유지한다.
	while (mp_sListHead) {
		PNODE next = mp_sListHead->next;
		delete mp_sListHead;
		mp_sListHead = next;
	}
	mp_sListTail = NULL;
}

void CLayerManager::MakeList(const char* pcList)
{
	// strtok_s modifies its buffer, but pcList is a string literal.
	size_t nSize = strlen(pcList) + 1;
	char* pcCopy = new char[nSize];
	strcpy_s(pcCopy, nSize, pcList);

	char* pcNext = NULL;
	for (char* pcToken = strtok_s(pcCopy, " ", &pcNext);
		pcToken;
		pcToken = strtok_s(NULL, " ", &pcNext))
	{
		AddNode(AllocNode(pcToken));
	}

	delete[] pcCopy;
}

CLayerManager::PNODE CLayerManager::AllocNode(char* pcName)
{
	PNODE node = new NODE;
	ASSERT(node);

	strcpy_s(node->token, pcName);
	node->next = NULL;

	return node;
}

void CLayerManager::AddNode(PNODE pNode)
{
	if (!mp_sListHead)
	{
		mp_sListHead = mp_sListTail = pNode;
	}
	else
	{
		mp_sListTail->next = pNode;
		mp_sListTail = pNode;
	}
}


void CLayerManager::Push(CBaseLayer* pLayer)
{
	if (m_nTop + 1 >= MAX_LAYER_NUMBER)
	{
#ifdef _DEBUG
		TRACE("The Stack is full.. so cannot run the push operation.. \n");
#endif
		return;
	}

	mp_Stack[++m_nTop] = pLayer;
}

CBaseLayer* CLayerManager::Pop()
{
	if (m_nTop < 0)
	{
#ifdef _DEBUG
		TRACE("The Stack is empty.. so cannot run the pop operation.. \n");
#endif
		return NULL;
	}

	CBaseLayer* pLayer = mp_Stack[m_nTop];
	mp_Stack[m_nTop] = NULL;
	m_nTop--;

	return pLayer;
}

CBaseLayer* CLayerManager::Top()
{
	if (m_nTop < 0)
	{
#ifdef _DEBUG
		TRACE("The Stack is empty.. so cannot run the top operation.. \n");
#endif
		return NULL;
	}

	return mp_Stack[m_nTop];
}

void CLayerManager::LinkLayer(PNODE pNode)
{
	CBaseLayer* pLayer = NULL;

	while (pNode)
	{
		if (!pLayer)
			pLayer = GetLayer(pNode->token);
		else
		{
			if (*pNode->token == '(')
				Push(pLayer);
			else if (*pNode->token == ')')
				Pop();
			else
			{
				char cMode = *pNode->token;
				char* pcName = pNode->token + 1;

				pLayer = GetLayer(pcName);

				switch (cMode)
				{
				case '*': Top()->SetUpperUnderLayer(pLayer); break;
				case '+': Top()->SetUpperLayer(pLayer); break;
				case '-': Top()->SetUnderLayer(pLayer); break;
				}
			}
		}

		pNode = pNode->next;
	}
}

void CLayerManager::DeAllocLayer()
{
	// OnDestroy에서 먼저 작업 스레드를 끝낸 후, 소유 중인 Layer만 정리한다.
	for (int i = 0; i < this->m_nLayerCount; i++)
		if (m_owned[i]) delete this->mp_aLayers[i];
	m_nLayerCount = 0; // 반복 종료 호출에 의한 이중 해제 방지
}
