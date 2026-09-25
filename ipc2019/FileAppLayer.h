#pragma once
#include "BaseLayer.h"

// [assignment4] 과제의 4 + 2 + 1 + 1 + 4 = 12바이트 헤더를 그대로 사용한다.
// [assignment4] fapp_type은 데이터 종류, fapp_msg_type은 INFO/DATA/END를 구분하는 필드다.
#pragma pack(push, 1)
struct FILE_APP_HEADER {
	uint32_t fapp_totlen; // [assignment4] 원본 파일 전체 크기(바이트), 32비트 network byte order
	uint16_t fapp_type; // [assignment4] 파일 데이터 종류(FILE_TYPE_BINARY), 16비트 network byte order
	unsigned char fapp_msg_type; // [assignment4] INFO=0, DATA=1, END=2로 파일 정보·본문·종료를 구분
	unsigned char unused; // [assignment4] 예약 필드이며 송신 구조체의 0 초기화로 항상 0을 전송
	uint32_t fapp_seq_num; // [assignment4] INFO는 0, DATA는 1부터 증가, END는 다음 순번을 사용
	unsigned char fapp_data[FILE_APP_DATA_SIZE]; // [assignment4] 최대 1488바이트의 파일명 또는 파일 데이터
};
#pragma pack(pop)
static_assert(sizeof(FILE_APP_HEADER) == ETHER_MAX_DATA_SIZE, "File MTU");

// [assignment4] 각 작업 스레드가 자신의 방향에 대해서만 갱신하는 누적 계수다.
// [assignment4] UI는 이 객체를 직접 읽지 않고 FILE_STATUS에 복사된 값만 사용한다.
struct FILE_PROGRESS {
    CString fileName;
    uint64_t totalBytes = 0;
    uint64_t completedBytes = 0;
    ULONGLONG startedAtMs = 0;
    ULONGLONG lastProgressAtMs = 0;
    ULONGLONG lastReportAtMs = 0;
};

// [assignment4] 스레드가 UI를 직접 수정하지 않고 PostMessage로 소유권을 넘기는 상태 정보.
// [assignment4] 수신/송신을 구분해야 파일 수신 완료가 아직 송신 중인 버튼을 활성화하지 않는다.
struct FILE_STATUS {
	CString message;
	int percent;
	BOOL sending;
	BOOL finished;
    FILE_PROGRESS progress;  // [assignment4] 송신 성공 바이트 / 실제 파일 기록 바이트
    ULONGLONG reportedAtMs;  // [assignment4] 완료 뒤 경과 시간을 고정할 때도 사용한다.
};

// [assignment4] 파일을 INFO/DATA/END 프레임으로 전송하고 수신 조각을 디스크 파일로 재조립하는 응용 계층이다.
class CFileAppLayer : public CBaseLayer
{
public:
	CFileAppLayer(const char* name);
	virtual ~CFileAppLayer();
	// [assignment4] UI 알림 대상과 파일 송신 작업 스레드의 시작·중단 인터페이스를 제공한다.
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
    // [assignment4] 송신 API 성공 바이트와 실제 수신 파일 기록 바이트를 방향별로 분리해 측정한다.
    FILE_PROGRESS m_sendProgress;
    FILE_PROGRESS m_receiveProgress;

	// [assignment4] 수신 중인 .part 파일과 기대 순번·누적 기록량·송신자 MAC을 보관한다.
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
