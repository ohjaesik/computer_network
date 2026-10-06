#pragma once
#include <afxdialogex.h>
#include "ARPLayer.h"
#include "resource.h"

// [assignment6] 강의자료의 ARP Cache / Proxy ARP / Gratuitous ARP 배치를 가진 자식 화면이다.
// [assignment6] 프로토콜이나 수신 스레드를 소유하지 않으므로 화면을 숨겨도 네트워크 동작은 유지된다.
class CARPDlg : public CDialogEx {
public:
    explicit CARPDlg(CWnd* parent = nullptr) : CDialogEx(IDD_ARP_DIALOG, parent) {}
    void Attach(CARPLayer* layer, CARPLayer* second = nullptr) { m_layers[0] = layer; m_layers[1] = second; m_layer = m_layers[m_selected]; }
    void SetConnection(bool connected, const CString& device, const CString& mac);
    void SetConnection(int index, bool connected, const CString& device, const CString& mac);
    void SetGarpAllowed(bool allowed) { m_garpAllowed = allowed; }
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
    afx_msg void OnInterfaceChanged();
    DECLARE_MESSAGE_MAP()
private:
    CARPLayer* m_layer = nullptr;
    CARPLayer* m_layers[2] = {};
    bool m_connections[2] = {};
    CString m_devices[2], m_macs[2];
    int m_selected = 0;
    bool m_garpAllowed = true;
    CComboBox m_interfaceCombo, m_outgoingCombo;
    CListCtrl m_cache, m_proxy;
    CString m_device, m_mac;
    bool m_connected = false;
    bool ReadTarget(int id, uint32_t& ip);
    void RefreshControls();
};
