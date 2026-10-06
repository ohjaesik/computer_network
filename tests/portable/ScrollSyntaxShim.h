#pragma once
// [assignment6] 실제 scroll cpp의 C++ 구문/타입만 검사한다. Windows/MFC 실행 또는 렌더링 대체물이 아니다.
#include <algorithm>
#include <cstdint>
#include <vector>
using LONG = long; using UINT = unsigned int; using DWORD = unsigned long; using BOOL = int;
using HWND = void*; using HDWP = void*; using HMONITOR = void*; using LONG_PTR = std::intptr_t;
using WPARAM = std::uintptr_t; using LPARAM = std::intptr_t; using TCHAR = char;
struct RECT { LONG left, top, right, bottom; };
struct POINT { LONG x, y; };
struct SIZE { LONG cx, cy; };
struct CPoint : POINT { CPoint(LONG a = 0, LONG b = 0) : POINT{a,b} {} };
struct CSize : SIZE { CSize(LONG a = 0, LONG b = 0) : SIZE{a,b} {} };
struct CRect : RECT {
    CRect(LONG a = 0, LONG b = 0, LONG c = 0, LONG d = 0) : RECT{a,b,c,d} {}
    CRect(const RECT& value) : RECT(value) {}
    int Width() const { return int(right - left); } int Height() const { return int(bottom - top); }
    CSize Size() const { return CSize(Width(), Height()); } void OffsetRect(POINT);
};
struct MONITORINFO { DWORD cbSize; RECT rcMonitor, rcWork; DWORD dwFlags; };
struct MINMAXINFO { POINT ptReserved, ptMaxSize, ptMaxPosition, ptMinTrackSize, ptMaxTrackSize; };
struct SCROLLINFO { UINT cbSize, fMask; int nMin, nMax; UINT nPage; int nPos, nTrackPos; };
struct MSG { HWND hwnd; UINT message; WPARAM wParam; LPARAM lParam; };
class CScrollBar {};
class CWnd {
public:
    HWND m_hWnd = nullptr;
    void ShowScrollBar(UINT, BOOL); void GetClientRect(RECT*) const; void GetWindowRect(RECT*) const;
    CWnd* GetWindow(UINT) const; CWnd* GetNextWindow() const; HWND GetSafeHwnd() const;
    void ScreenToClient(RECT*) const; void MapDialogRect(RECT*) const; BOOL IsIconic() const;
    BOOL SetWindowPos(CWnd*, int, int, int, int, UINT); DWORD GetStyle() const; DWORD GetExStyle() const;
    int SetScrollInfo(int, SCROLLINFO*, BOOL); int SetScrollPos(int, int, BOOL); BOOL GetScrollInfo(int, SCROLLINFO*) const;
    BOOL RedrawWindow(const RECT*, void*, UINT);
    void OnSize(UINT,int,int); void OnGetMinMaxInfo(MINMAXINFO*);
    void OnVScroll(UINT,UINT,CScrollBar*); void OnHScroll(UINT,UINT,CScrollBar*); BOOL OnMouseWheel(UINT,short,CPoint);
};
class CDialogEx : public CWnd {};
HMONITOR MonitorFromWindow(HWND,DWORD); BOOL GetMonitorInfo(HMONITOR,MONITORINFO*);
BOOL SystemParametersInfo(UINT,UINT,void*,UINT); int GetSystemMetrics(int); BOOL AdjustWindowRectEx(RECT*,DWORD,BOOL,DWORD);
HDWP BeginDeferWindowPos(int); HDWP DeferWindowPos(HDWP,HWND,HWND,int,int,int,int,UINT); BOOL EndDeferWindowPos(HDWP);
BOOL SetWindowPos(HWND,HWND,int,int,int,int,UINT); BOOL IsWindow(HWND); HWND GetParent(HWND);
BOOL IsChild(HWND,HWND); HWND WindowFromPoint(POINT); int GetClassName(HWND,TCHAR*,int); LONG_PTR GetWindowLongPtr(HWND,int);
HWND GetFocus(); BOOL IsWindowVisible(HWND); BOOL GetWindowRect(HWND,RECT*); int _tcsicmp(const TCHAR*,const TCHAR*);
#define afx_msg
#define _T(x) x
#define _countof(x) (sizeof(x)/sizeof((x)[0]))
#define LOWORD(x) (static_cast<unsigned short>(x))
#define HIWORD(x) (static_cast<unsigned short>(static_cast<std::uintptr_t>(x) >> 16))
enum { FALSE = 0, TRUE = 1, SB_HORZ = 0, SB_VERT = 1, SB_BOTH = 3,
    SB_LINEUP = 0, SB_LINEDOWN = 1, SB_PAGEUP = 2, SB_PAGEDOWN = 3, SB_THUMBPOSITION = 4, SB_THUMBTRACK = 5, SB_TOP = 6, SB_BOTTOM = 7,
    SIF_RANGE = 1, SIF_PAGE = 2, SIF_POS = 4, SIF_TRACKPOS = 16, WHEEL_DELTA = 120,
    GW_CHILD = 5, SIZE_MINIMIZED = 1, GWL_STYLE = -16, ES_MULTILINE = 4, MK_SHIFT = 4, MK_CONTROL = 8,
    MONITOR_DEFAULTTONEAREST = 2, SPI_GETWORKAREA = 48, SPI_GETWHEELSCROLLLINES = 104,
    SM_CXVSCROLL = 2, SM_CYHSCROLL = 3, SM_CXSCREEN = 0, SM_CYSCREEN = 1,
    SWP_NOSIZE = 1, SWP_NOZORDER = 4, SWP_NOREDRAW = 8, SWP_NOACTIVATE = 16,
    RDW_INVALIDATE = 1, RDW_ERASE = 4, RDW_ALLCHILDREN = 128, WM_MOUSEWHEEL = 0x20a };
constexpr DWORD WS_VSCROLL = 0x00200000, WS_HSCROLL = 0x00100000;
constexpr UINT WHEEL_PAGESCROLL = 0xffffffff;
// [assignment6] Windows의 min/max 매크로 및 LONG/int 타입 차이도 실제 구현에서 검사한다.
#define min(a,b) win32_min_macro(a,b)
#define max(a,b) win32_max_macro(a,b)
