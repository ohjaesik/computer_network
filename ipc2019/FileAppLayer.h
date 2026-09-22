#pragma once

#include "BaseLayer.h"

#pragma pack(push, 1)
typedef struct _FILE_APP_HEADER
{
    uint32_t fapp_totlen;
    uint16_t fapp_type;
    unsigned char fapp_msg_type;
    unsigned char fapp_unused;
    uint32_t fapp_seq_num;
    unsigned char fapp_data[FILE_APP_DATA_SIZE];
} FILE_APP_HEADER, *PFILE_APP_HEADER;
#pragma pack(pop)

static_assert(sizeof(FILE_APP_HEADER) == ETHER_MAX_DATA_SIZE,
              "File header plus data must equal the Ethernet MTU.");

struct FILE_STATUS_EVENT
{
    CString message;
    int progress;
    BOOL finished;
    BOOL error;
};

class CFileAppLayer : public CBaseLayer
{
public:
    explicit CFileAppLayer(const char* pName);
    virtual ~CFileAppLayer();

    BOOL StartSendFile(const CString& filePath);
    void StopTransfer();
    BOOL IsSending() const;
    void SetNotifyWindow(HWND hWnd);

    virtual BOOL Receive(
        unsigned char* ppayload,
        int nlength,
        const unsigned char* sourceAddress,
        const unsigned char* destinationAddress);

    // Required worker that allows chat transmission during file transfer.
    static UINT __cdecl FileTransferThread(LPVOID pParam);

private:
    struct FILE_SEND_CONTEXT
    {
        CFileAppLayer* layer;
        CString filePath;
    };

    HWND m_hNotifyWnd;
    CWinThread* m_pSendThread;
    volatile LONG m_lSending;
    volatile LONG m_lCancelRequested;
    CCriticalSection m_ThreadLock;
    CCriticalSection m_ReceiveLock;

    CFile m_ReceiveFile;
    BOOL m_bReceivingFile;
    uint32_t m_dwReceiveTotalLength;
    uint32_t m_dwExpectedSequence;
    uint64_t m_ullReceivedLength;
    int m_nLastReceiveProgress;
    unsigned char m_ReceiveSource[ETHERNET_ADDRESS_SIZE];
    CString m_strReceiveFinalPath;
    CString m_strReceivePartialPath;

    UINT SendFileWorker(const CString& filePath);
    BOOL SendFilePacket(
        unsigned char messageType,
        uint32_t totalLength,
        uint32_t sequence,
        const unsigned char* data,
        int dataLength);

    BOOL ReceiveFileInfo(
        PFILE_APP_HEADER header,
        int dataLength,
        const unsigned char* sourceAddress);
    BOOL ReceiveFileData(PFILE_APP_HEADER header, int dataLength);
    BOOL ReceiveFileEnd(PFILE_APP_HEADER header);
    void ResetReceiveState(BOOL deletePartialFile);
    void NotifyStatus(
        const CString& message,
        int progress,
        BOOL finished,
        BOOL error) const;
    void CleanupFinishedThread();

    static CString ExtractFileName(const CString& path);
    static CString SanitizeFileName(const CString& fileName);
    static CString GetReceiveDirectory();
};
