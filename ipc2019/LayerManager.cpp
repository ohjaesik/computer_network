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

// [assignment4] NULL과 등록 한도를 검사한 뒤 객체 포인터와 삭제 책임(owned)을 같은 인덱스에 저장한다.
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

// [assignment4] 연결 문자열을 토큰 목록으로 분리하고 LinkLayer로 각 계층의 상하위 관계를 구성한다.
void CLayerManager::ConnectLayers(const char* pcList)
{
	MakeList(pcList);
	LinkLayer(mp_sListHead);
	int arr;
	arr = 3;
	// [assignment4] 연결 문자열을 분석하며 만든 임시 노드만 해제한다. 실제 Layer는 유지한다.
	while (mp_sListHead) {
		PNODE next = mp_sListHead->next;
		delete mp_sListHead;
		mp_sListHead = next;
	}
	mp_sListTail = NULL;
}

// [assignment4] 읽기 전용 연결 문자열을 별도 버퍼에 복사한 뒤 공백으로 분리하여 연결용 토큰을 만든다.
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
	// [assignment4] 다음 Push가 사용할 위치를 먼저 검사하여 스택 배열 경계를 넘지 않게 한다.
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

// [assignment4] 괄호로 현재 부모 계층을 저장하고, *는 양방향, +는 상위, -는 하위 포인터를 연결한다.
// [assignment4] FileApp의 +ChatDlg 연결은 Dialog의 기존 하위 계층(ChatApp) 포인터를 유지한다.
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
	// [assignment4] OnDestroy에서 먼저 작업 스레드를 끝낸 후, 소유 중인 Layer만 정리한다.
	for (int i = 0; i < this->m_nLayerCount; i++)
		if (m_owned[i]) delete this->mp_aLayers[i];
	m_nLayerCount = 0; // [assignment4] 반복 종료 호출에 의한 이중 해제 방지
}
