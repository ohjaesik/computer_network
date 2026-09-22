// FileLayer.cpp: legacy Assignment 3 file-backed IPC transport.

#include "pch.h"
#include "FileLayer.h"

#ifdef _DEBUG
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#define new DEBUG_NEW
#endif

CFileLayer::CFileLayer(const char* pName)
    : CBaseLayer(pName)
{
}

CFileLayer::~CFileLayer()
{
    TRY
    {
        CFile::Remove(_T("IpcBuff.txt"));
    }
    CATCH(CFileException, exception)
    {
        UNREFERENCED_PARAMETER(exception);
    }
    END_CATCH
}

BOOL CFileLayer::Send(unsigned char* ppayload, int nlength)
{
    if (ppayload == NULL || nlength <= 0)
        return FALSE;

    TRY
    {
        CFile destination(
            _T("IpcBuff.txt"),
            CFile::modeCreate | CFile::modeWrite);
        destination.Write(ppayload, nlength);
        destination.Close();
    }
    CATCH(CFileException, exception)
    {
        UNREFERENCED_PARAMETER(exception);
        return FALSE;
    }
    END_CATCH

    return TRUE;
}

BOOL CFileLayer::Receive()
{
    CBaseLayer* upper = GetUpperLayer(0);
    if (upper == NULL)
        return FALSE;

    TRY
    {
        CFile source(_T("IpcBuff.txt"), CFile::modeRead);
        const ULONGLONG fileLength64 = source.GetLength();

        if (fileLength64 == 0 || fileLength64 > INT_MAX)
        {
            source.Close();
            return FALSE;
        }

        const int fileLength = static_cast<int>(fileLength64);
        std::vector<unsigned char> payload(fileLength + 1, 0);
        const UINT readLength = source.Read(payload.data(), fileLength);
        source.Close();

        if (readLength != static_cast<UINT>(fileLength))
            return FALSE;

        return upper->Receive(payload.data(), fileLength);
    }
    CATCH(CFileException, exception)
    {
        UNREFERENCED_PARAMETER(exception);
        return FALSE;
    }
    END_CATCH
}

