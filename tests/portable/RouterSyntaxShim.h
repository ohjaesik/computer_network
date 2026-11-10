#pragma once
#include "PlatformShim.h"

// [assignment7][static-router] 새 RouterDlg의 선언/호출/타입 검사용 최소 MFC 대체물이다. Windows ABI/실제 UI 실행 검사는 아니다.
#define afx_msg
#define BEGIN_MESSAGE_MAP(...)
#define END_MESSAGE_MAP(...)
#define ON_BN_CLICKED(...)
#define ON_CBN_SELCHANGE(...)
#define ON_MESSAGE(...)
#define ON_WM_TIMER()
#define ON_WM_DESTROY()
#define ON_WM_SIZE()
#define ON_WM_GETMINMAXINFO()
#define BST_CHECKED 1
#define BST_UNCHECKED 0
#define IDOK 1
#define LVS_EX_FULLROWSELECT 1
#define LVS_EX_GRIDLINES 2
#define LVS_EX_DOUBLEBUFFER 4
#define LVCFMT_LEFT 0
#define SM_CXVSCROLL 2
#define LVNI_SELECTED 1
#define LVIS_SELECTED 1
#define MONITOR_DEFAULTTONEAREST 2
#define SPI_GETWORKAREA 48
#define GW_CHILD 5
#define SWP_NOZORDER 4
#define SWP_NOACTIVATE 16
#define RDW_INVALIDATE 1
#define RDW_ERASE 4
#define RDW_ALLCHILDREN 128
#define SIZE_MINIMIZED 1
#define PM_REMOVE 1
using DWORD_PTR = uintptr_t;
using LRESULT = intptr_t;
using UINT_PTR = uintptr_t;
struct POINT { long x = 0, y = 0; };
struct RECT { int left = 0, top = 0, right = 800, bottom = 600; };
struct CSize { int cx = 0, cy = 0; };
struct CRect : RECT {
    CRect() = default;
    explicit CRect(const RECT& r) : RECT(r) {}
    CRect(int x, int y, int r, int b) { left=x; top=y; right=r; bottom=b; }
    int Width() const { return right-left; }
    int Height() const { return bottom-top; }
    CSize Size() const { return {Width(),Height()}; }
};
struct MONITORINFO { DWORD cbSize; RECT rcWork; };
struct MINMAXINFO { POINT ptMinTrackSize; };
struct MSG { LPARAM lParam; };
class CFont {};
class CCriticalSection {};
inline bool operator==(const CString& a, const CString& b) { return std::strcmp(a,b) == 0; }
inline bool operator!=(const CString& a, const CString& b) { return !(a == b); }
inline void* MonitorFromWindow(HWND,DWORD) { return nullptr; }
inline BOOL GetMonitorInfo(void*,MONITORINFO*) { return TRUE; }
inline BOOL SystemParametersInfo(UINT,UINT,RECT*,UINT) { return TRUE; }
inline BOOL IsWindow(HWND) { return TRUE; }
inline BOOL MoveWindow(HWND,int,int,int,int,BOOL) { return TRUE; }
inline BOOL AdjustWindowRectEx(RECT*,DWORD,BOOL,DWORD) { return TRUE; }
inline BOOL PeekMessage(MSG*,HWND,UINT,UINT,UINT) { return FALSE; }
struct CDataExchange {};
class CWnd {
public:
    HWND m_hWnd = nullptr;
    virtual ~CWnd() = default;
    HWND GetSafeHwnd() const { return nullptr; }
    CWnd* GetDlgItem(int) { return nullptr; }
    CWnd* GetParent() { return nullptr; }
    void EnableWindow(BOOL) {}
    LRESULT SendMessage(UINT,WPARAM = 0,LPARAM = 0) { return 0; }
    void GetClientRect(CRect*) {}
    void GetWindowRect(CRect*) {}
    void ScreenToClient(CRect*) {}
    void MapDialogRect(CRect*) {}
    CWnd* GetWindow(UINT) { return nullptr; }
    CWnd* GetNextWindow() { return nullptr; }
    int GetDlgCtrlID() { return 0; }
    CFont* GetFont() { return nullptr; }
    DWORD GetStyle() { return 0; }
    DWORD GetExStyle() { return 0; }
    void SetWindowPos(CWnd*,int,int,int,int,UINT) {}
    void RedrawWindow(void*,void*,UINT) {}
    void Invalidate() {}
};
class CDialogEx : public CWnd {
public:
    CDialogEx(UINT,CWnd*) {}
    virtual BOOL OnInitDialog() { return TRUE; }
    virtual void OnOK() {}
    virtual void OnCancel() {}
    virtual void DoDataExchange(CDataExchange*) {}
    void OnTimer(UINT_PTR) {}
    void OnDestroy() {}
    void OnSize(UINT,int,int) {}
    void OnGetMinMaxInfo(MINMAXINFO*) {}
    UINT_PTR SetTimer(UINT_PTR id,UINT,void*) { return id; }
    void KillTimer(UINT_PTR) {}
    int DoModal() { return IDOK; }
    void CheckDlgButton(int,UINT) {}
    UINT IsDlgButtonChecked(int) { return 0; }
    void SetDlgItemText(int,const char*) {}
    void GetDlgItemText(int,CString&) {}
};
class CEdit : public CWnd { public: void SetLimitText(UINT) {} };
class CComboBox : public CWnd {
public:
    int AddString(const char*) { return 0; }
    int SetCurSel(int) { return 0; }
    int GetCurSel() { return 0; }
    void SetDroppedWidth(int) {}
};
class CClientDC {
public:
    explicit CClientDC(CWnd*) {}
    CFont* SelectObject(CFont*) { return nullptr; }
    CSize GetTextExtent(const char*) { return {100,16}; }
};
class CListCtrl : public CWnd {
public:
    void SetExtendedStyle(DWORD) {}
    int InsertColumn(int,const char*,int,int) { return 0; }
    int GetNextItem(int,int) { return 0; }
    DWORD_PTR GetItemData(int) { return 0; }
    void SetRedraw(BOOL) {}
    BOOL DeleteAllItems() { return TRUE; }
    int GetItemCount() { return 0; }
    int GetTopIndex() { return 0; }
    BOOL EnsureVisible(int,BOOL) { return TRUE; }
    BOOL SetColumnWidth(int,int) { return TRUE; }
    int InsertItem(int,const char*) { return 0; }
    BOOL SetItemText(int,int,const char*) { return TRUE; }
    BOOL SetItemData(int,DWORD_PTR) { return TRUE; }
    BOOL SetItemState(int,UINT,UINT) { return TRUE; }
};
inline int GetSystemMetrics(int) { return 16; }
inline int AfxMessageBox(const char*) { return 0; }
template<class T> void DDX_Control(CDataExchange*,int,T&) {}
