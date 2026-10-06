#pragma once
#include <afxdialogex.h>
#include "ARPLayer.h"
#include "resource.h"

// [assignment6] 강의자료의 ARP Cache / Proxy ARP / Gratuitous ARP 배치를 가진 자식 화면이다.
// [assignment6] 프로토콜이나 수신 스레드를 소유하지 않으므로 화면을 숨겨도 네트워크 동작은 유지된다.
class CARPDlg : public CDialogEx {
public:
    explicit CARPDlg(CWnd* parent = nullptr) : CDialogEx(IDD_ARP_DIALOG, parent) {}
    void Attach(CARPLayer* layer) { m_layer = layer; }
    void SetConnection(bool connected, const CString& device, const CString& mac);
    void RefreshTables();
    void SetStatus(const CString& message);
protected:
    BOOL OnInitDialog() override;
    void DoDataExchange(CDataExchange* dx) override;
    void OnOK() override {} // [assignment6] Enter/Escape로 자식 화면이 종료되지 않게 한다.
    void OnCancel() override {}
    afx_msg void OnRequest();
    afx_msg void OnDelete();
    afx_msg void OnClear();
    afx_msg void OnProxyAdd();
    afx_msg void OnProxyDelete();
    afx_msg void OnGarp();
    DECLARE_MESSAGE_MAP()
private:
    CARPLayer* m_layer = nullptr;
    CListCtrl m_cache, m_proxy;
    CString m_device, m_mac;
    bool m_connected = false;
    bool ReadTarget(int id, uint32_t& ip);
};
