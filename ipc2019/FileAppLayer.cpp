// FileAppLayer.cpp: threaded file fragmentation and reassembly.

#include "pch.h"
#include "FileAppLayer.h"

#include <atlconv.h>

#ifdef _DEBUG
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#define new DEBUG_NEW
#endif

CFileAppLayer::CFileAppLayer(const char* pName)
    : CBaseLayer(pName),
      m_hNotifyWnd(NULL),
      m_pSendThread(NULL),
      m_lSending(FALSE),
      m_lCancelRequested(FALSE),
      m_bReceivingFile(FALSE),
      m_dwReceiveTotalLength(0),
      m_dwExpectedSequence(0),
      m_ullReceivedLength(0),
      m_nLastReceiveProgress(-1)
{
    memset(m_ReceiveSource, 0, sizeof(m_ReceiveSource));
}

CFileAppLayer::~CFileAppLayer()
{
    StopTransfer();

    CSingleLock lock(&m_ReceiveLock, TRUE);
    ResetReceiveState(TRUE);
}

void CFileAppLayer::SetNotifyWindow(HWND hWnd)
{
    m_hNotifyWnd = hWnd;
}

BOOL CFileAppLayer::IsSending() const
{
    return InterlockedCompareExchange(
               const_cast<volatile LONG*>(&m_lSending),
               FALSE,
               FALSE) != FALSE;
}

BOOL CFileAppLayer::StartSendFile(const CString& filePath)
{
    CleanupFinishedThread();

    if (filePath.IsEmpty() || mp_UnderLayer == NULL)
        return FALSE;

    if (InterlockedCompareExchange(
            &m_lSending,
            TRUE,
            FALSE) != FALSE)
    {
        NotifyStatus(
            _T("이미 파일을 전송하고 있습니다."),
            0,
            FALSE,
            TRUE);
        return FALSE;
    }

    InterlockedExchange(&m_lCancelRequested, FALSE);

    FILE_SEND_CONTEXT* context = new FILE_SEND_CONTEXT;
    context->layer = this;
    context->filePath = filePath;

    CWinThread* thread = AfxBeginThread(
        FileTransferThread,
        context,
        THREAD_PRIORITY_NORMAL,
        0,
        CREATE_SUSPENDED);

    if (thread == NULL)
    {
        delete context;
        InterlockedExchange(&m_lSending, FALSE);
        NotifyStatus(
            _T("파일 전송 스레드를 생성하지 못했습니다."),
            0,
            TRUE,
            TRUE);
        return FALSE;
    }

    thread->m_bAutoDelete = FALSE;

    {
        CSingleLock lock(&m_ThreadLock, TRUE);
        m_pSendThread = thread;
    }

    thread->ResumeThread();
    return TRUE;
}

void CFileAppLayer::CleanupFinishedThread()
{
    CSingleLock lock(&m_ThreadLock, TRUE);

    if (m_pSendThread != NULL &&
        WaitForSingleObject(m_pSendThread->m_hThread, 0) == WAIT_OBJECT_0)
    {
        delete m_pSendThread;
        m_pSendThread = NULL;
    }
}

void CFileAppLayer::StopTransfer()
{
    InterlockedExchange(&m_lCancelRequested, TRUE);

    CSingleLock lock(&m_ThreadLock, TRUE);
    if (m_pSendThread != NULL)
    {
        WaitForSingleObject(m_pSendThread->m_hThread, INFINITE);
        delete m_pSendThread;
        m_pSendThread = NULL;
    }

    InterlockedExchange(&m_lSending, FALSE);
}

UINT __cdecl CFileAppLayer::FileTransferThread(LPVOID pParam)
{
    FILE_SEND_CONTEXT* context =
        reinterpret_cast<FILE_SEND_CONTEXT*>(pParam);

    if (context == NULL || context->layer == NULL)
    {
        delete context;
        return 1;
    }

    CFileAppLayer* layer = context->layer;
    const CString filePath = context->filePath;
    delete context;

    const UINT result = layer->SendFileWorker(filePath);
    InterlockedExchange(&layer->m_lSending, FALSE);
    return result;
}

UINT CFileAppLayer::SendFileWorker(const CString& filePath)
{
    CFile sourceFile;
    CFileException openError;

    if (!sourceFile.Open(
            filePath,
            CFile::modeRead | CFile::shareDenyWrite,
            &openError))
    {
        NotifyStatus(
            _T("전송할 파일을 열지 못했습니다."),
            0,
            TRUE,
            TRUE);
        return 1;
    }

    try
    {
        const ULONGLONG fileLength64 = sourceFile.GetLength();
        if (fileLength64 > 0xffffffffULL)
        {
            sourceFile.Close();
            NotifyStatus(
                _T("현재 32비트 파일 길이 필드로는 4 GiB 이상의 파일을 전송할 수 없습니다."),
                0,
                TRUE,
                TRUE);
            return 1;
        }

        const uint32_t fileLength =
            static_cast<uint32_t>(fileLength64);
        const CString fileName = ExtractFileName(filePath);
        CStringA fileNameUtf8(CW2A(fileName, CP_UTF8));

        if (fileNameUtf8.IsEmpty() ||
            fileNameUtf8.GetLength() + 1 > FILE_APP_DATA_SIZE)
        {
            sourceFile.Close();
            NotifyStatus(
                _T("파일명이 File App 데이터 영역보다 깁니다."),
                0,
                TRUE,
                TRUE);
            return 1;
        }

        if (!SendFilePacket(
                FILE_MESSAGE_INFO,
                fileLength,
                0,
                reinterpret_cast<const unsigned char*>(
                    static_cast<LPCSTR>(fileNameUtf8)),
                fileNameUtf8.GetLength() + 1))
        {
            sourceFile.Close();
            NotifyStatus(
                _T("파일 정보 frame 전송에 실패했습니다."),
                0,
                TRUE,
                TRUE);
            return 1;
        }

        CString startMessage;
        startMessage.Format(
            _T("파일 전송 시작: %s (%u bytes)"),
            static_cast<LPCTSTR>(fileName),
            fileLength);
        NotifyStatus(startMessage, 0, FALSE, FALSE);

        unsigned char buffer[FILE_APP_DATA_SIZE];
        uint32_t sequence = 1;
        ULONGLONG sentLength = 0;
        int lastProgress = 0;

        for (;;)
        {
            if (InterlockedCompareExchange(
                    &m_lCancelRequested,
                    FALSE,
                    FALSE) != FALSE)
            {
                sourceFile.Close();
                NotifyStatus(
                    _T("파일 전송이 취소되었습니다."),
                    static_cast<int>(
                        fileLength == 0
                            ? 0
                            : sentLength * 100 / fileLength),
                    TRUE,
                    TRUE);
                return 1;
            }

            const UINT readLength =
                sourceFile.Read(buffer, FILE_APP_DATA_SIZE);
            if (readLength == 0)
                break;

            if (!SendFilePacket(
                    FILE_MESSAGE_DATA,
                    fileLength,
                    sequence,
                    buffer,
                    static_cast<int>(readLength)))
            {
                sourceFile.Close();
                NotifyStatus(
                    _T("파일 데이터 frame 전송에 실패했습니다."),
                    static_cast<int>(
                        fileLength == 0
                            ? 0
                            : sentLength * 100 / fileLength),
                    TRUE,
                    TRUE);
                return 1;
            }

            sentLength += readLength;
            ++sequence;

            const int progress =
                fileLength == 0
                    ? 100
                    : static_cast<int>(
                          sentLength * 100 / fileLength);
            if (progress != lastProgress)
            {
                NotifyStatus(
                    _T("파일 전송 중..."),
                    progress,
                    FALSE,
                    FALSE);
                lastProgress = progress;
            }
            Sleep(FILE_SEND_THROTTLE_MS);
        }

        sourceFile.Close();

        if (!SendFilePacket(
                FILE_MESSAGE_END,
                fileLength,
                sequence,
                NULL,
                0))
        {
            NotifyStatus(
                _T("파일 종료 frame 전송에 실패했습니다."),
                100,
                TRUE,
                TRUE);
            return 1;
        }

        NotifyStatus(
            _T("파일 전송 완료"),
            100,
            TRUE,
            FALSE);
        return 0;
    }
    catch (CFileException* exception)
    {
        exception->Delete();
        if (sourceFile.m_hFile != CFile::hFileNull)
            sourceFile.Close();

        NotifyStatus(
            _T("파일을 읽는 중 오류가 발생했습니다."),
            0,
            TRUE,
            TRUE);
        return 1;
    }
}

BOOL CFileAppLayer::SendFilePacket(
    unsigned char messageType,
    uint32_t totalLength,
    uint32_t sequence,
    const unsigned char* data,
    int dataLength)
{
    if (dataLength < 0 ||
        dataLength > FILE_APP_DATA_SIZE ||
        (dataLength > 0 && data == NULL) ||
        mp_UnderLayer == NULL)
        return FALSE;

    FILE_APP_HEADER packet;
    memset(&packet, 0, sizeof(packet));

    packet.fapp_totlen = HostToNetwork32(totalLength);
    packet.fapp_type = HostToNetwork16(FILE_TYPE_BINARY);
    packet.fapp_msg_type = messageType;
    packet.fapp_seq_num = HostToNetwork32(sequence);

    if (dataLength > 0)
        memcpy(packet.fapp_data, data, dataLength);

    return mp_UnderLayer->Send(
        reinterpret_cast<unsigned char*>(&packet),
        FILE_APP_HEADER_SIZE + dataLength,
        ETHERNET_TYPE_FILE);
}

BOOL CFileAppLayer::Receive(
    unsigned char* ppayload,
    int nlength,
    const unsigned char* sourceAddress,
    const unsigned char* destinationAddress)
{
    UNREFERENCED_PARAMETER(destinationAddress);

    if (ppayload == NULL ||
        sourceAddress == NULL ||
        nlength < FILE_APP_HEADER_SIZE)
        return FALSE;

    PFILE_APP_HEADER header =
        reinterpret_cast<PFILE_APP_HEADER>(ppayload);
    const int dataLength =
        std::min(nlength - FILE_APP_HEADER_SIZE, FILE_APP_DATA_SIZE);

    CSingleLock lock(&m_ReceiveLock, TRUE);

    if (NetworkToHost16(header->fapp_type) != FILE_TYPE_BINARY)
        return FALSE;

    if (header->fapp_msg_type != FILE_MESSAGE_INFO &&
        m_bReceivingFile &&
        memcmp(
            m_ReceiveSource,
            sourceAddress,
            ETHERNET_ADDRESS_SIZE) != 0)
        return FALSE;

    switch (header->fapp_msg_type)
    {
    case FILE_MESSAGE_INFO:
        return ReceiveFileInfo(
            header,
            dataLength,
            sourceAddress);
    case FILE_MESSAGE_DATA:
        return ReceiveFileData(header, dataLength);
    case FILE_MESSAGE_END:
        return ReceiveFileEnd(header);
    default:
        return FALSE;
    }
}

BOOL CFileAppLayer::ReceiveFileInfo(
    PFILE_APP_HEADER header,
    int dataLength,
    const unsigned char* sourceAddress)
{
    const uint32_t sequence =
        NetworkToHost32(header->fapp_seq_num);

    if (sequence != 0 || dataLength <= 0)
        return FALSE;

    const unsigned char* terminator =
        reinterpret_cast<const unsigned char*>(
            memchr(header->fapp_data, '\0', dataLength));

    if (terminator == NULL || terminator == header->fapp_data)
        return FALSE;

    const int nameLength = static_cast<int>(
        terminator - header->fapp_data);
    CStringA utf8Name(
        reinterpret_cast<const char*>(header->fapp_data),
        nameLength);
    CString fileName(CA2W(
        static_cast<LPCSTR>(utf8Name),
        CP_UTF8));
    fileName = SanitizeFileName(fileName);

    if (fileName.IsEmpty())
        return FALSE;

    ResetReceiveState(TRUE);

    const CString receiveDirectory = GetReceiveDirectory();
    if (!CreateDirectory(receiveDirectory, NULL) &&
        GetLastError() != ERROR_ALREADY_EXISTS)
    {
        NotifyStatus(
            _T("수신 파일 폴더를 만들지 못했습니다."),
            0,
            TRUE,
            TRUE);
        return FALSE;
    }

    m_strReceiveFinalPath =
        receiveDirectory + _T("\\") + fileName;
    m_strReceivePartialPath =
        m_strReceiveFinalPath + _T(".part");

    DeleteFile(m_strReceivePartialPath);

    CFileException fileError;
    if (!m_ReceiveFile.Open(
            m_strReceivePartialPath,
            CFile::modeCreate |
                CFile::modeReadWrite |
                CFile::shareExclusive,
            &fileError))
    {
        NotifyStatus(
            _T("수신 파일을 생성하지 못했습니다."),
            0,
            TRUE,
            TRUE);
        ResetReceiveState(TRUE);
        return FALSE;
    }

    m_dwReceiveTotalLength =
        NetworkToHost32(header->fapp_totlen);
    try
    {
        // Reserve the announced size before accepting data fragments.
        m_ReceiveFile.SetLength(m_dwReceiveTotalLength);
        m_ReceiveFile.SeekToBegin();
    }
    catch (CFileException* exception)
    {
        exception->Delete();
        NotifyStatus(
            _T("수신 파일 공간을 확보하지 못했습니다."),
            0,
            TRUE,
            TRUE);
        ResetReceiveState(TRUE);
        return FALSE;
    }

    m_dwExpectedSequence = 1;
    m_ullReceivedLength = 0;
    m_nLastReceiveProgress = 0;
    memcpy(
        m_ReceiveSource,
        sourceAddress,
        ETHERNET_ADDRESS_SIZE);
    m_bReceivingFile = TRUE;

    CString message;
    message.Format(
        _T("파일 수신 시작: %s (%u bytes)"),
        static_cast<LPCTSTR>(fileName),
        m_dwReceiveTotalLength);
    NotifyStatus(message, 0, FALSE, FALSE);
    return TRUE;
}

BOOL CFileAppLayer::ReceiveFileData(
    PFILE_APP_HEADER header,
    int dataLength)
{
    if (!m_bReceivingFile || dataLength <= 0)
        return FALSE;

    const uint32_t totalLength =
        NetworkToHost32(header->fapp_totlen);
    const uint32_t sequence =
        NetworkToHost32(header->fapp_seq_num);

    if (totalLength != m_dwReceiveTotalLength ||
        sequence != m_dwExpectedSequence ||
        m_ullReceivedLength >= m_dwReceiveTotalLength)
    {
        NotifyStatus(
            _T("파일 조각의 길이 또는 순서가 올바르지 않습니다."),
            0,
            TRUE,
            TRUE);
        ResetReceiveState(TRUE);
        return FALSE;
    }

    const uint64_t remaining =
        static_cast<uint64_t>(m_dwReceiveTotalLength) -
        m_ullReceivedLength;
    const UINT writeLength = static_cast<UINT>(
        std::min<uint64_t>(remaining, dataLength));

    if (writeLength == 0)
    {
        ResetReceiveState(TRUE);
        return FALSE;
    }

    try
    {
        m_ReceiveFile.Write(header->fapp_data, writeLength);
    }
    catch (CFileException* exception)
    {
        exception->Delete();
        NotifyStatus(
            _T("수신 파일에 데이터를 기록하지 못했습니다."),
            0,
            TRUE,
            TRUE);
        ResetReceiveState(TRUE);
        return FALSE;
    }

    m_ullReceivedLength += writeLength;
    ++m_dwExpectedSequence;

    const int progress =
        m_dwReceiveTotalLength == 0
            ? 100
            : static_cast<int>(
                  m_ullReceivedLength * 100 /
                  m_dwReceiveTotalLength);
    if (progress != m_nLastReceiveProgress)
    {
        NotifyStatus(
            _T("파일 수신 중..."),
            progress,
            FALSE,
            FALSE);
        m_nLastReceiveProgress = progress;
    }
    return TRUE;
}

BOOL CFileAppLayer::ReceiveFileEnd(PFILE_APP_HEADER header)
{
    if (!m_bReceivingFile)
        return FALSE;

    const uint32_t totalLength =
        NetworkToHost32(header->fapp_totlen);
    const uint32_t sequence =
        NetworkToHost32(header->fapp_seq_num);

    if (totalLength != m_dwReceiveTotalLength ||
        sequence != m_dwExpectedSequence ||
        m_ullReceivedLength != m_dwReceiveTotalLength)
    {
        NotifyStatus(
            _T("파일 종료 검증에 실패했습니다. 일부 조각이 누락되었습니다."),
            0,
            TRUE,
            TRUE);
        ResetReceiveState(TRUE);
        return FALSE;
    }

    if (m_ReceiveFile.m_hFile != CFile::hFileNull)
        m_ReceiveFile.Close();

    DeleteFile(m_strReceiveFinalPath);
    if (!MoveFileEx(
            m_strReceivePartialPath,
            m_strReceiveFinalPath,
            MOVEFILE_REPLACE_EXISTING))
    {
        NotifyStatus(
            _T("완료된 수신 파일의 이름을 변경하지 못했습니다."),
            100,
            TRUE,
            TRUE);
        ResetReceiveState(TRUE);
        return FALSE;
    }

    CString completedPath = m_strReceiveFinalPath;
    m_bReceivingFile = FALSE;
    m_dwReceiveTotalLength = 0;
    m_dwExpectedSequence = 0;
    m_ullReceivedLength = 0;
    m_nLastReceiveProgress = -1;
    memset(m_ReceiveSource, 0, sizeof(m_ReceiveSource));
    m_strReceiveFinalPath.Empty();
    m_strReceivePartialPath.Empty();

    CString message;
    message.Format(
        _T("파일 수신 완료: %s"),
        static_cast<LPCTSTR>(completedPath));
    NotifyStatus(message, 100, TRUE, FALSE);
    return TRUE;
}

void CFileAppLayer::ResetReceiveState(BOOL deletePartialFile)
{
    if (m_ReceiveFile.m_hFile != CFile::hFileNull)
        m_ReceiveFile.Close();

    if (deletePartialFile && !m_strReceivePartialPath.IsEmpty())
        DeleteFile(m_strReceivePartialPath);

    m_bReceivingFile = FALSE;
    m_dwReceiveTotalLength = 0;
    m_dwExpectedSequence = 0;
    m_ullReceivedLength = 0;
    m_nLastReceiveProgress = -1;
    memset(m_ReceiveSource, 0, sizeof(m_ReceiveSource));
    m_strReceiveFinalPath.Empty();
    m_strReceivePartialPath.Empty();
}

void CFileAppLayer::NotifyStatus(
    const CString& message,
    int progress,
    BOOL finished,
    BOOL error) const
{
    if (m_hNotifyWnd == NULL || !IsWindow(m_hNotifyWnd))
        return;

    FILE_STATUS_EVENT* eventData = new FILE_STATUS_EVENT;
    eventData->message = message;
    eventData->progress = std::max(0, std::min(progress, 100));
    eventData->finished = finished;
    eventData->error = error;

    if (!PostMessage(
            m_hNotifyWnd,
            WM_APP_FILE_STATUS,
            0,
            reinterpret_cast<LPARAM>(eventData)))
        delete eventData;
}

CString CFileAppLayer::ExtractFileName(const CString& path)
{
    const int slash = std::max(
        path.ReverseFind(_T('\\')),
        path.ReverseFind(_T('/')));
    return slash < 0 ? path : path.Mid(slash + 1);
}

CString CFileAppLayer::SanitizeFileName(const CString& input)
{
    CString fileName = ExtractFileName(input);
    const CString invalidCharacters = _T("<>:\"/\\|?*");

    for (int i = 0; i < fileName.GetLength(); ++i)
    {
        if (fileName[i] < 32 ||
            invalidCharacters.Find(fileName[i]) >= 0)
            fileName.SetAt(i, _T('_'));
    }

    fileName.Trim();
    if (fileName == _T(".") ||
        fileName == _T("..") ||
        fileName.IsEmpty())
        fileName = _T("received_file.bin");

    return fileName;
}

CString CFileAppLayer::GetReceiveDirectory()
{
    TCHAR modulePath[MAX_PATH] = { 0 };
    GetModuleFileName(NULL, modulePath, MAX_PATH);

    CString directory(modulePath);
    const int slash = directory.ReverseFind(_T('\\'));
    if (slash >= 0)
        directory = directory.Left(slash);

    return directory + _T("\\ReceivedFiles");
}
