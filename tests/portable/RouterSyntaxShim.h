#pragma once
#include "PlatformShim.h"

// [assignment7][static-router] 새 RouterDlg의 선언/호출/타입 검사용 최소 MFC 대체물이다. Windows ABI/실제 UI 실행 검사는 아니다.
#define afx_msg
#define BEGIN_MESSAGE_MAP(...)
#define END_MESSAGE_MAP(...)
#define ON_BN_CLICKED(...)
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
using DWORD_PTR = uintptr_t;
using LRESULT = intptr_t;
struct CDataExchange {};
struct CRect { int Width() const { return 800; } };
class CWnd {
public:
    HWND GetSafeHwnd() const { return nullptr; }
    CWnd* GetDlgItem(int) { return nullptr; }
    CWnd* GetParent() { return nullptr; }
    void EnableWindow(BOOL) {}
    LRESULT SendMessage(UINT,WPARAM = 0,LPARAM = 0) { return 0; }
    void GetClientRect(CRect*) {}
    void Invalidate() {}
};
class CDialogEx : public CWnd {
public:
    CDialogEx(UINT,CWnd*) {}
    virtual BOOL OnInitDialog() { return TRUE; }
    virtual void OnOK() {}
    virtual void OnCancel() {}
    virtual void DoDataExchange(CDataExchange*) {}
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
    int InsertItem(int,const char*) { return 0; }
    BOOL SetItemText(int,int,const char*) { return TRUE; }
    BOOL SetItemData(int,DWORD_PTR) { return TRUE; }
    BOOL SetItemState(int,UINT,UINT) { return TRUE; }
};
inline int GetSystemMetrics(int) { return 16; }
inline int AfxMessageBox(const char*) { return 0; }
template<class T> void DDX_Control(CDataExchange*,int,T&) {}
