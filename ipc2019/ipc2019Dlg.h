#pragma once

#include "LayerManager.h"
#include "ChatAppLayer.h"
#include "FileAppLayer.h"
#include "EthernetLayer.h"
#include "NILayer.h"
#include "FileLayer.h"

class Cipc2019Dlg : public CDialogEx, public CBaseLayer
{
public:
    explicit Cipc2019Dlg(CWnd* pParent = nullptr);
    virtual ~Cipc2019Dlg();

#ifdef AFX_DESIGN_TIME
    enum { IDD = IDD_IPC2019_DIALOG };
#endif

    virtual BOOL PreTranslateMessage(MSG* pMsg);

    // Legacy Assignment 3 upper-layer entry point.
    virtual BOOL Receive(unsigned char* ppayload);

    // Assignment 4 chat data is delivered with the frame MAC addresses.
    virtual BOOL Receive(
        unsigned char* ppayload,
        int nlength,
        const unsigned char* sourceAddress,
        const unsigned char* destinationAddress);

protected:
    virtual void DoDataExchange(CDataExchange* pDX);
    virtual BOOL OnInitDialog();

    HICON m_hIcon;

    afx_msg void OnSysCommand(UINT nID, LPARAM lParam);
    afx_msg void OnPaint();
    afx_msg HCURSOR OnQueryDragIcon();
    afx_msg void OnDestroy();
    afx_msg void OnTimer(UINT nIDEvent);

    afx_msg void OnBnClickedButtonAddr();
    afx_msg void OnBnClickedButtonSend();
    afx_msg void OnBnClickedCheckToall();
    afx_msg void OnBnClickedButtonFileBrowse();
    afx_msg void OnBnClickedButtonFileSend();

    afx_msg LRESULT OnChatReceived(WPARAM wParam, LPARAM lParam);
    afx_msg LRESULT OnFileStatus(WPARAM wParam, LPARAM lParam);
    afx_msg LRESULT OnRegSendMsg(WPARAM wParam, LPARAM lParam);
    afx_msg LRESULT OnRegAckMsg(WPARAM wParam, LPARAM lParam);

    DECLARE_MESSAGE_MAP()

private:
    CLayerManager m_LayerMgr;

    CChatAppLayer* m_ChatApp;
    CFileAppLayer* m_FileApp;
    CEthernetLayer* m_Ethernet;
    CNILayer* m_NILayer;

    BOOL m_bSendReady;
    int m_nAckReady;

    CString m_stMessage;
    CString m_strSourceMac;
    CString m_strDestinationMac;
    CString m_strFilePath;

    CListBox m_ListChat;
    CComboBox m_AdapterCombo;
    CProgressCtrl m_FileProgress;

    // Assignment 3 variables/functions are retained in the extended project.
    UINT m_unSrcAddr;
    UINT m_unDstAddr;
    UINT m_wParam;
    DWORD m_lParam;

    enum
    {
        IPC_INITIALIZING,
        IPC_READYTOSEND,
        IPC_WAITFORACK,
        IPC_ERROR,
        IPC_BROADCASTMODE,
        IPC_UNICASTMODE,
        IPC_ADDR_SET,
        IPC_ADDR_RESET
    };

    void SendData();
    void SetDlgState(int state);
    void EndofProcess();
    void SetRegstryMessage();

    static BOOL ParseMacAddress(
        const CString& text,
        unsigned char address[ETHERNET_ADDRESS_SIZE]);
    static CString FormatMacAddress(const unsigned char* address);
    static CString DecodeUtf8(
        const unsigned char* data,
        int length);
    static CStringA EncodeUtf8(const CString& text);
};

