
// ipc2019Dlg.h: 헤더 파일
//

#pragma once

#include "LayerManager.h"	// Added by ClassView
#include "ChatAppLayer.h"	// Added by ClassView
#include "EthernetLayer.h"	// Added by ClassView
#include "NILayer.h"
#include "FileAppLayer.h"
#include "FileLayer.h"	// Added by ClassView
// Cipc2019Dlg 대화 상자
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
	CListBox m_ListChat;
	afx_msg void OnBnClickedCheckToall();

	// [과제 4 추가] NI에서 받은 채팅은 PostMessage로 UI 스레드에 복사 전달한다.
	BOOL Receive(unsigned char* payload, int length, const unsigned char* source = NULL);
	afx_msg void OnDestroy();
	afx_msg void OnAdapterChanged();
	afx_msg void OnFileBrowse();
	afx_msg void OnFileSend();
	afx_msg LRESULT OnChatReceived(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnFileStatus(WPARAM wParam, LPARAM lParam);

private:
	CNILayer* m_NI = NULL;
	CEthernetLayer* m_Ethernet = NULL;
	CFileAppLayer* m_FileApp = NULL;
	CComboBox m_AdapterCombo;
	CProgressCtrl m_FileProgress;
	CString m_sourceMac, m_destinationMac, m_filePath;
	void SetNetworkAddress();
	void SetNetworkDlgState(int state);
	void SendNetworkChat();
	static BOOL ParseMac(const CString& text, unsigned char* address);
	static CString FormatMac(const unsigned char* address);
};
