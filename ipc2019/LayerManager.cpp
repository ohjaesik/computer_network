// LayerManager.cpp: creates links from a compact layer expression.

#include "pch.h"
#include "LayerManager.h"

#ifdef _DEBUG
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#define new DEBUG_NEW
#endif

CLayerManager::CLayerManager()
    : m_nLayerCount(0),
      m_nTop(-1),
      mp_sListHead(NULL),
      mp_sListTail(NULL)
{
    memset(mp_aLayers, 0, sizeof(mp_aLayers));
    memset(m_abOwned, 0, sizeof(m_abOwned));
    memset(mp_Stack, 0, sizeof(mp_Stack));
}

CLayerManager::~CLayerManager()
{
    ClearNodes();
    DeAllocLayer();
}

void CLayerManager::AddLayer(CBaseLayer* pLayer, BOOL bOwned)
{
    if (pLayer == NULL || m_nLayerCount >= MAX_LAYER_NUMBER)
        return;

    mp_aLayers[m_nLayerCount] = pLayer;
    m_abOwned[m_nLayerCount] = bOwned;
    ++m_nLayerCount;
}

CBaseLayer* CLayerManager::GetLayer(int nindex) const
{
    if (nindex < 0 || nindex >= m_nLayerCount)
        return NULL;
    return mp_aLayers[nindex];
}

CBaseLayer* CLayerManager::GetLayer(const char* pName) const
{
    if (pName == NULL)
        return NULL;

    for (int i = 0; i < m_nLayerCount; ++i)
    {
        if (mp_aLayers[i] != NULL &&
            strcmp(pName, mp_aLayers[i]->GetLayerName()) == 0)
            return mp_aLayers[i];
    }

    return NULL;
}

void CLayerManager::ConnectLayers(const char* pcList)
{
    MakeList(pcList);
    LinkLayer(mp_sListHead);
    ClearNodes();
}

void CLayerManager::MakeList(const char* pcList)
{
    ClearNodes();
    if (pcList == NULL)
        return;

    const size_t size = strlen(pcList) + 1;
    char* copy = new char[size];
    strcpy_s(copy, size, pcList);

    char* context = NULL;
    for (char* token = strtok_s(copy, " ", &context);
         token != NULL;
         token = strtok_s(NULL, " ", &context))
    {
        AddNode(AllocNode(token));
    }

    delete[] copy;
}

CLayerManager::PNODE CLayerManager::AllocNode(const char* pcName)
{
    PNODE node = new NODE;
    strcpy_s(node->token, sizeof(node->token), pcName);
    node->next = NULL;
    return node;
}

void CLayerManager::AddNode(PNODE pNode)
{
    if (pNode == NULL)
        return;

    if (mp_sListHead == NULL)
        mp_sListHead = mp_sListTail = pNode;
    else
    {
        mp_sListTail->next = pNode;
        mp_sListTail = pNode;
    }
}

void CLayerManager::ClearNodes()
{
    while (mp_sListHead != NULL)
    {
        PNODE next = mp_sListHead->next;
        delete mp_sListHead;
        mp_sListHead = next;
    }
    mp_sListTail = NULL;
}

void CLayerManager::Push(CBaseLayer* pLayer)
{
    if (pLayer == NULL || m_nTop + 1 >= MAX_LAYER_NUMBER)
        return;

    mp_Stack[++m_nTop] = pLayer;
}

CBaseLayer* CLayerManager::Pop()
{
    if (m_nTop < 0)
        return NULL;

    CBaseLayer* layer = mp_Stack[m_nTop];
    mp_Stack[m_nTop--] = NULL;
    return layer;
}

CBaseLayer* CLayerManager::Top() const
{
    return (m_nTop < 0) ? NULL : mp_Stack[m_nTop];
}

void CLayerManager::LinkLayer(PNODE pNode)
{
    CBaseLayer* current = NULL;

    while (pNode != NULL)
    {
        if (current == NULL)
        {
            current = GetLayer(pNode->token);
        }
        else if (pNode->token[0] == '(')
        {
            Push(current);
        }
        else if (pNode->token[0] == ')')
        {
            Pop();
        }
        else
        {
            const char mode = pNode->token[0];
            CBaseLayer* next = GetLayer(pNode->token + 1);
            CBaseLayer* top = Top();

            if (next != NULL && top != NULL)
            {
                current = next;
                switch (mode)
                {
                case '*': top->SetUpperUnderLayer(next); break;
                case '+': top->SetUpperLayer(next); break;
                case '-': top->SetUnderLayer(next); break;
                default: break;
                }
            }
        }

        pNode = pNode->next;
    }
}

void CLayerManager::DeAllocLayer()
{
    // Destroy upper/application layers before their lower dependencies.
    for (int i = m_nLayerCount - 1; i >= 0; --i)
    {
        if (m_abOwned[i] && mp_aLayers[i] != NULL)
            delete mp_aLayers[i];

        mp_aLayers[i] = NULL;
        m_abOwned[i] = FALSE;
    }
    m_nLayerCount = 0;
}
