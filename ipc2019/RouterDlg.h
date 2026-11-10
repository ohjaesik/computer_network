#pragma once
#include <afxdialogex.h>
#include "IPRouter.h"
#include "ARPLayer.h"
#include "resource.h"

// [assignment7][static-router] 강의자료의 Routing entry 입력창이다. 주소를 코드에 고정하지 않고 사용자 입력을 반환한다.
class CRouteEntryDlg : public CDialogEx {
public:
    CRouteEntryDlg(CIPRouter* router, CWnd* parent) : CDialogEx(IDD_ROUTE_ENTRY_DIALOG,parent), m_router(router) {}
    CIPRouter::STATIC_ROUTE route;
protected:
    BOOL OnInitDialog() override;
    void OnOK() override;
private:
    CIPRouter* m_router;
};

// [assignment7][static-router] UI 스레드의 자식 화면이다. 패킷/스레드는 기존 계층이 처리하고 화면은 잠금된 snapshot만 읽는다.
class CRouterDlg : public CDialogEx {
public:
    explicit CRouterDlg(CWnd* parent = nullptr) : CDialogEx(IDD_ROUTER_DIALOG,parent) {}
    void Attach(CIPRouter* router, CARPLayer* first, CARPLayer* second);
    void SetConnection(bool connected, bool staticMode);
    void RefreshTables();
    void SetStatus(const CString& message);
protected:
    BOOL OnInitDialog() override;
    void DoDataExchange(CDataExchange* dx) override;
    void OnOK() override {}
    void OnCancel() override {}
    afx_msg void OnModeChanged();
    afx_msg void OnAdd();
    afx_msg void OnDelete();
    afx_msg void OnCacheDelete();
    DECLARE_MESSAGE_MAP()
private:
    CIPRouter* m_router = nullptr;
    CARPLayer* m_arps[ROUTER_MAX_INTERFACES] = {};
    CListCtrl m_routes, m_cache;
    bool m_connected = false;
    struct CACHE_ROW { int port; uint32_t ip; };
    std::vector<CACHE_ROW> m_cacheRows;
};
