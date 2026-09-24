#pragma once
#include "BaseLayer.h"

// 과제의 4 + 2 + 1 + 1 + 4 = 12바이트 헤더를 그대로 사용한다.
// fapp_type은 데이터 종류, fapp_msg_type은 INFO/DATA/END를 구분하는 필드다.
#pragma pack(push, 1)
struct FILE_APP_HEADER {
	uint32_t fapp_totlen;
	uint16_t fapp_type;
	unsigned char fapp_msg_type;
	unsigned char unused;
	uint32_t fapp_seq_num;
	unsigned char fapp_data[FILE_APP_DATA_SIZE];
};
#pragma pack(pop)
static_assert(sizeof(FILE_APP_HEADER) == ETHER_MAX_DATA_SIZE, "File MTU");

// 각 작업 스레드가 자신의 방향에 대해서만 갱신하는 누적 계수다.
// UI는 이 객체를 직접 읽지 않고 FILE_STATUS에 복사된 값만 사용한다.
struct FILE_PROGRESS {
    CString fileName;
    uint64_t totalBytes = 0;
    uint64_t completedBytes = 0;
    ULONGLONG startedAtMs = 0;
    ULONGLONG lastProgressAtMs = 0;
    ULONGLONG lastReportAtMs = 0;
};

// 스레드가 UI를 직접 수정하지 않고 PostMessage로 소유권을 넘기는 상태 정보.
// 수신/송신을 구분해야 파일 수신 완료가 아직 송신 중인 버튼을 활성화하지 않는다.
struct FILE_STATUS {
	CString message;
	int percent;
	BOOL sending;
	BOOL finished;
    FILE_PROGRESS progress;  // 송신 성공 바이트 / 실제 파일 기록 바이트
    ULONGLONG reportedAtMs;  // 완료 뒤 경과 시간을 고정할 때도 사용한다.
};

class CFileAppLayer : public CBaseLayer
{
public:
	CFileAppLayer(const char* name);
	virtual ~CFileAppLayer();
	void SetNotifyWindow(HWND window) { m_window = window; }
	BOOL StartSendFile(const CString& path);
	BOOL IsSending() const;
	static CString GetReceiveDirectory();
	void StopTransfer();
	void ResetReceive();
	BOOL Receive(unsigned char* payload, int length, const unsigned char* source = NULL);
	static UINT __cdecl FileTransferThread(LPVOID parameter);

private:
	HWND m_window;
	CWinThread* m_thread;
	volatile LONG m_sending;
	volatile LONG m_cancel;
	CString m_sendPath;
    FILE_PROGRESS m_sendProgress;
    FILE_PROGRESS m_receiveProgress;

	CFile m_receiveFile;
	BOOL m_receiving;
	uint32_t m_total;
	uint32_t m_nextSequence;
	uint64_t m_received;
	int m_lastPercent;
	unsigned char m_sender[ETHERNET_ADDRESS_SIZE];
	CString m_partialPath, m_finalPath;

	BOOL SendFile(CString& result);
	BOOL SendPacket(unsigned char messageType, uint32_t total, uint32_t sequence,
		const unsigned char* data, int length);
	void Notify(const CString& message, int percent, BOOL sending, BOOL finished);
	BOOL ReceiveInfo(FILE_APP_HEADER* packet, int length, const unsigned char* source);
	BOOL ReceiveData(FILE_APP_HEADER* packet, int length);
	BOOL ReceiveEnd(FILE_APP_HEADER* packet);
};
