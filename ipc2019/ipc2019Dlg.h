
// ipc2019Dlg.h: 헤더 파일
//

#pragma once

#include "LayerManager.h"	// Added by ClassView
#include "ChatAppLayer.h"	// Added by ClassView
#include "EthernetLayer.h"	// Added by ClassView
#include "NILayer.h"
#include "FileAppLayer.h"
#include "FileLayer.h"	// Added by ClassView
#include "IPLayer.h"
#include "ARPLayer.h"
#include "ARPDlg.h"
#include "IPRouter.h"
// Cipc2019Dlg 대화 상자
// [assignment4] MFC Dialog와 CBaseLayer를 함께 상속하여 화면 객체도 프로토콜 스택의 상위 계층으로 연결한다.
class Cipc2019Dlg : public CDialogEx, public CBaseLayer
{
// 생성입니다.
public:
	Cipc2019Dlg(CWnd* pParent = nullptr);	// 표준 생성자입니다.



// 대화 상자 데이터입니다.
#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_IPC2019_DIALOG };
#endif

	public:
	virtual BOOL PreTranslateMessage(MSG* pMsg);


	protected:
	virtual void DoDataExchange(CDataExchange* pDX);	// DDX/DDV 지원입니다.


// 구현입니다.
protected:
	HICON m_hIcon;

	// 생성된 메시지 맵 함수
	virtual BOOL OnInitDialog();
	afx_msg void OnSysCommand(UINT nID, LPARAM lParam);
	afx_msg void OnPaint();
	afx_msg HCURSOR OnQueryDragIcon();
	DECLARE_MESSAGE_MAP()
public:
//	UINT m_unDstAddr;
//	UINT unSrcAddr;
//	CString m_stMessage;
//	CListBox m_ListChat;
	
	afx_msg void OnTimer(UINT_PTR nIDEvent);


public:
	BOOL			Receive(unsigned char* ppayload);
	inline void		SendData();

private:
	CLayerManager	m_LayerMgr;
	int				m_nAckReady;

	enum {
		IPC_INITIALIZING,
		IPC_READYTOSEND,
		IPC_WAITFORACK,
		IPC_ERROR,
		IPC_BROADCASTMODE,
		IPC_UNICASTMODE,
		IPC_ADDR_SET,
		IPC_ADDR_RESET
	};

	void			SetDlgState(int state);
	inline void		EndofProcess();
	inline void		SetRegstryMessage();
	LRESULT			OnRegSendMsg(WPARAM wParam, LPARAM lParam);
	LRESULT			OnRegAckMsg(WPARAM wParam, LPARAM lParam);

	BOOL			m_bSendReady;

	// Object App
	CChatAppLayer* m_ChatApp;

	// Implementation
	UINT			m_wParam;
	DWORD			m_lParam;
public:
	afx_msg void OnBnClickedButtonAddr();
	afx_msg void OnBnClickedButtonSend();
	UINT m_unSrcAddr;
	UINT m_unDstAddr;
	CString m_stMessage;
    // [assignment4] 기존 컨트롤 ID/변수명은 유지하며 자동 줄바꿈이 되는 읽기 전용 편집창을 사용한다.
    CEdit m_ListChat;
	afx_msg void OnBnClickedCheckToall();

	// [assignment4] NI에서 받은 채팅은 PostMessage로 UI 스레드에 복사 전달한다.
	BOOL Receive(unsigned char* payload, int length, const unsigned char* source = NULL);
	afx_msg void OnDestroy();
	afx_msg void OnAdapterChanged();
	afx_msg void OnFileBrowse();
	afx_msg void OnFileSend();
	afx_msg void OnOpenReceivedFolder();
	afx_msg LRESULT OnChatReceived(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnFileStatus(WPARAM wParam, LPARAM lParam);

private:
	// [assignment4] 장치 송수신·Ethernet 캡슐화·파일 송수신을 담당하는 계층 객체를 LayerManager에서 받아 보관한다.
	CNILayer* m_NI = NULL;
	CEthernetLayer* m_Ethernet = NULL;
	CFileAppLayer* m_FileApp = NULL;
	CComboBox m_AdapterCombo;
	CProgressCtrl m_FileProgress;
    CProgressCtrl m_FileReceiveProgress;
    // [assignment4] UI 스레드만 사용하는 표시 상태다. 두 방향의 속도 샘플을 따로 보관한다.
    struct FILE_VIEW {
        FILE_STATUS latest = {};
        BOOL hasStatus = FALSE;
        ULONGLONG sampleAtMs = 0;
        uint64_t sampleBytes = 0;
        double bytesPerSecond = 0;
    };
    FILE_VIEW m_sendView, m_receiveView;
    void AppendChatMessage(const CString& message);
    void RefreshFileView(FILE_VIEW& view, CProgressCtrl& progress, int statusId);
    static CString FormatFileSize(uint64_t bytes);
    static CString FormatDuration(ULONGLONG milliseconds);
	CString m_sourceMac, m_destinationMac, m_filePath;
	void SetNetworkAddress();
	void SetNetworkDlgState(int state);
	void SendNetworkChat();
	static BOOL ParseMac(const CString& text, unsigned char* address);
	static CString FormatMac(const unsigned char* address);

    // [assignment6] 모든 화면은 하나의 IP/ARP/NI 인스턴스를 공유한다. 화면 선택은 표시만 바꾼다.
    // [assignment7] 위 설명의 단일 NIC 기본 모드는 유지하며, PARP 중계 모드만 두 번째 스택을 추가로 공유한다.
    CIPLayer* m_IP = nullptr;
    CARPLayer* m_ARP = nullptr;
    // [assignment7] PARP는 양쪽 LAN이 필요하므로 두 번째 NI/Ethernet/IP/ARP 인스턴스를 소유한다.
    CNILayer* m_NI2 = nullptr;
    CEthernetLayer* m_Ethernet2 = nullptr;
    CIPLayer* m_IP2 = nullptr;
    CARPLayer* m_ARP2 = nullptr;
    CIPRouter m_router;
    CComboBox m_AdapterCombo2;
    bool m_secondConnected = false;
    CARPDlg m_arpDialog;
    std::vector<HWND> m_chatControls;
    CString m_sourceIp, m_pendingFile;
    ULONGLONG m_fileResolveStarted = 0;
    void InitIpUi();
    void ShowIpPage(bool arp);
    void SetIpNetworkAddress();
    void RefreshIpControls();
    bool PrepareIpDestination();
    void SendIpFile();
    void StartIpFile(const CString& path);
    void PollIpNetwork();
    afx_msg void OnShowChatPage();
    afx_msg void OnShowArpPage();
    afx_msg LRESULT OnArpChanged(WPARAM wParam, LPARAM lParam);
    afx_msg LRESULT OnNetworkEvent(WPARAM wParam, LPARAM lParam);
    afx_msg void OnSecondAdapterChanged();
    afx_msg void OnRoutingModeChanged();

    // [assignment6] 큰 원본 배치를 가상 화면으로 보관하고, 창에는 스크롤 위치만큼 이동해 표시한다.
    // [assignment6] ARP 자식창도 함께 이동하므로 화면 전환 및 기존 컨트롤 ID/상태는 유지된다.
    struct SCROLL_CHILD { HWND window; CRect original; };
    std::vector<SCROLL_CHILD> m_scrollChildren;
    CSize m_scrollContent = CSize(0, 0), m_scrollPage = CSize(0, 0), m_scrollMaximum = CSize(0, 0);
    CSize m_scrollLine = CSize(16, 24);
    CPoint m_scrollOffset = CPoint(0, 0);
    int m_wheelRemainder[2] = {};
    HWND m_lastScrollFocus = nullptr;
    bool m_scrollReady = false, m_updatingScroll = false;
    void InitDialogScroll();
    void UpdateDialogScroll();
    void PositionScrollChildren();
    void ScrollDialogTo(int x, int y);
    void HandleDialogScroll(int bar, UINT code);
    bool ScrollDialogWheel(UINT flags, short delta);
    bool PreTranslateDialogScroll(MSG* message);
    bool ControlOwnsWheel(CPoint screenPoint) const;
    void RevealFocusedControl();
    afx_msg void OnSize(UINT type, int cx, int cy);
    afx_msg void OnGetMinMaxInfo(MINMAXINFO* info);
    afx_msg void OnVScroll(UINT code, UINT position, CScrollBar* scrollBar);
    afx_msg void OnHScroll(UINT code, UINT position, CScrollBar* scrollBar);
    afx_msg BOOL OnMouseWheel(UINT flags, short delta, CPoint point);
};
