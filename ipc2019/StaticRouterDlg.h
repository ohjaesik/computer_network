#pragma once
#include <afxdialogex.h>
#include "LayerManager.h"
#include "NILayer.h"
#include "EthernetLayer.h"
#include "IPLayer.h"
#include "ARPLayer.h"
#include "IPRouter.h"
#include "RouterDlg.h"
#include "resource.h"

// [assignment7][static-router] 자료의 Device/IP/MAC 입력창이다. 선택한 NIC의 실제 응답 MAC을 읽기 전용으로 보여 준다.
class CRouterProxyEntryDlg : public CDialogEx {
public:
    CRouterProxyEntryDlg(CARPLayer* first, CARPLayer* second, CWnd* parent);
protected:
    BOOL OnInitDialog() override;
    void OnOK() override;
    afx_msg void OnDeviceChanged();
    DECLARE_MESSAGE_MAP()
private:
    CARPLayer* m_arps[ROUTER_MAX_INTERFACES];
};

// [assignment7][static-router] 라우터 PC 전용 시작 화면이다. 두 NIC와 세 표를 한 창에 두고 Host용 채팅/파일 UI는 만들지 않는다.
// [assignment7][static-router] 수동 라우팅을 기본으로 사용하며 기존 프로토콜 소스/과제 주석을 삭제하지 않는다.
class CStaticRouterDlg : public CDialogEx {
public:
    explicit CStaticRouterDlg(CWnd* parent = nullptr);
    ~CStaticRouterDlg() override;
protected:
    BOOL OnInitDialog() override;
    void DoDataExchange(CDataExchange* dx) override;
    void OnOK() override {} // [assignment7][static-router] Enter로 라우터 창이 닫히지 않게 한다.
    afx_msg void OnStart();
    afx_msg void OnEnd();
    afx_msg void OnInternalChanged();
    afx_msg void OnExternalChanged();
    afx_msg void OnRouteAdd();
    afx_msg void OnRouteDelete();
    afx_msg void OnArpDelete();
    afx_msg void OnProxyAdd();
    afx_msg void OnProxyDelete();
    afx_msg void OnTimer(UINT_PTR id);
    afx_msg void OnDestroy();
    afx_msg void OnSize(UINT type, int cx, int cy);
    afx_msg void OnGetMinMaxInfo(MINMAXINFO* info);
    afx_msg LRESULT OnArpChanged(WPARAM, LPARAM);
    afx_msg LRESULT OnNetworkEvent(WPARAM, LPARAM message);
    DECLARE_MESSAGE_MAP()
private:
    struct PORT {
        CNILayer* ni = nullptr;
        CEthernetLayer* ethernet = nullptr;
        CIPLayer* ip = nullptr;
        CARPLayer* arp = nullptr;
        CComboBox devices;
    };
    struct ROW_KEY { int port = -1; uint32_t ip = 0; };
    struct CHILD_RECT { HWND window; CRect rect; bool combo; };
    CLayerManager m_layers;
    CIPRouter m_router;
    PORT m_ports[ROUTER_MAX_INTERFACES];
    CListCtrl m_routes, m_cache, m_proxies;
    std::vector<ROW_KEY> m_cacheRows, m_proxyRows;
    std::vector<uint64_t> m_routeState, m_cacheState, m_proxyState;
    std::vector<CHILD_RECT> m_layout;
    CSize m_designSize;
    CString m_status, m_displayedStatus;
    bool m_connected = false, m_ready = false, m_closing = false, m_timerActive = false;
    void PreviewAdapter(int port);
    void StopNetwork();
    void RefreshControls();
    void RefreshTables();
    void SetStatus(const CString& text);
    void RefreshStatus();
    void InitColumns();
    void SizeColumns();
    void RememberLayout();
    void FitToWorkArea();
    void LayoutControls(int cx, int cy);
};
