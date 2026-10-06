#include "pch.h"
#include "ipc2019Dlg.h"
#include "DialogScrollLayout.h"

namespace {
    RECT MonitorWorkArea(HWND window) {
        MONITORINFO monitor = {}; monitor.cbSize = sizeof(monitor);
        if (::GetMonitorInfo(::MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor)) return monitor.rcWork;
        RECT work = {};
        if (!::SystemParametersInfo(SPI_GETWORKAREA, 0, &work, 0)) {
            work.right = GetSystemMetrics(SM_CXSCREEN); work.bottom = GetSystemMetrics(SM_CYSCREEN);
        }
        return work;
    }
}

// [assignment6] 원본 배치는 폰트/DPI에 따른 실제 픽셀로 한 번 저장한다. 그 뒤 창을 줄여도 내용은 잘리지 않는다.
void Cipc2019Dlg::InitDialogScroll()
{
    ShowScrollBar(SB_BOTH, FALSE);
    CRect client; GetClientRect(&client); m_scrollContent = client.Size();
    CRect padding(0, 0, 10, 10); MapDialogRect(&padding);
    for (CWnd* child = GetWindow(GW_CHILD); child; child = child->GetNextWindow()) {
        CRect rect; child->GetWindowRect(&rect); ScreenToClient(&rect);
        m_scrollChildren.push_back({child->GetSafeHwnd(), rect});
        // [assignment6] OS가 초기 창을 이미 줄였더라도 화면 밖 원본 child와 끝 여백까지 가상 크기에 포함한다.
        m_scrollContent.cx = (std::max)(static_cast<int>(m_scrollContent.cx), static_cast<int>(rect.right) + padding.Width());
        m_scrollContent.cy = (std::max)(static_cast<int>(m_scrollContent.cy), static_cast<int>(rect.bottom) + padding.Height());
    }
    CRect line(0, 0, 8, 12); MapDialogRect(&line); m_scrollLine = line.Size();
    m_scrollReady = true;

    // [assignment6] 작업 표시줄을 제외한 현재 모니터의 영역 안으로 초기 창 크기/위치를 제한한다.
    const CRect work(MonitorWorkArea(m_hWnd)); CRect window; GetWindowRect(&window);
    const int margin = (std::max)(0, (std::min)(8, (std::min)(work.Width(), work.Height()) / 4));
    const int width = (std::min)(window.Width(), (std::max)(1, work.Width() - 2 * margin));
    const int height = (std::min)(window.Height(), (std::max)(1, work.Height() - 2 * margin));
    const int x = (std::max)(work.left + margin, (std::min)(window.left, work.right - margin - width));
    const int y = (std::max)(work.top + margin, (std::min)(window.top, work.bottom - margin - height));
    SetWindowPos(nullptr, x, y, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
    UpdateDialogScroll();
}

// [assignment6] 두 표준 스크롤바가 차지하는 크기를 함께 계산한다. 확대 시 불필요한 바와 이전 offset도 제거한다.
void Cipc2019Dlg::UpdateDialogScroll()
{
    if (!m_scrollReady || m_updatingScroll || IsIconic()) return;
    m_updatingScroll = true;
    CRect client; GetClientRect(&client);
    const int barWidth = GetSystemMetrics(SM_CXVSCROLL), barHeight = GetSystemMetrics(SM_CYHSCROLL);
    const DWORD style = GetStyle();
    const auto layout = DialogScrollLayout::Calculate(m_scrollContent.cx, m_scrollContent.cy,
        client.Width() + ((style & WS_VSCROLL) ? barWidth : 0),
        client.Height() + ((style & WS_HSCROLL) ? barHeight : 0), barWidth, barHeight);
    ShowScrollBar(SB_HORZ, layout.horizontal); ShowScrollBar(SB_VERT, layout.vertical);
    GetClientRect(&client); m_scrollPage = client.Size();
    m_scrollMaximum = CSize((std::max)(0, static_cast<int>(m_scrollContent.cx) - client.Width()),
        (std::max)(0, static_cast<int>(m_scrollContent.cy) - client.Height()));
    m_scrollOffset.x = DialogScrollLayout::Clamp(m_scrollOffset.x, m_scrollMaximum.cx);
    m_scrollOffset.y = DialogScrollLayout::Clamp(m_scrollOffset.y, m_scrollMaximum.cy);
    for (int bar : {SB_HORZ, SB_VERT}) {
        const bool horizontal = bar == SB_HORZ;
        SCROLLINFO info = {}; info.cbSize = sizeof(info); info.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
        info.nMax = (horizontal ? m_scrollContent.cx : m_scrollContent.cy) - 1;
        info.nPage = static_cast<UINT>((std::max)(1, static_cast<int>(horizontal ? m_scrollPage.cx : m_scrollPage.cy)));
        info.nPos = horizontal ? m_scrollOffset.x : m_scrollOffset.y;
        SetScrollInfo(bar, &info, TRUE);
    }
    m_updatingScroll = false; PositionScrollChildren();
}

// [assignment6] 일부만 보이는 child도 원래 좌표에서 이동한다. ScrollWindowEx의 부분 child redraw 문제를 피한다.
// [assignment6] 숨겨진 채팅/ARP 화면도 이동하지만 Show/Hide 및 Z-order는 건드리지 않는다.
void Cipc2019Dlg::PositionScrollChildren()
{
    // [assignment6] 이동 전 화면을 복사하면 group box 안의 입력칸/글자가 이전 위치에 남을 수 있다.
    // [assignment6] SWP_NOCOPYBITS로 이전 픽셀을 버리고, 모든 child 이동이 끝난 후 한 번에 다시 그린다.
    const UINT flags = SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOREDRAW | SWP_NOCOPYBITS;
    HDWP batch = ::BeginDeferWindowPos(static_cast<int>(m_scrollChildren.size()));
    for (const auto& child : m_scrollChildren) {
        if (!::IsWindow(child.window) || ::GetParent(child.window) != m_hWnd) continue;
        if (!batch) break;
        batch = ::DeferWindowPos(batch, child.window, nullptr, child.original.left - m_scrollOffset.x,
            child.original.top - m_scrollOffset.y, 0, 0, flags);
    }
    if (!batch || !::EndDeferWindowPos(batch)) {
        // [assignment6] 일괄 위치 변경이 실패해도 누적 offset 대신 원본 좌표로 모든 child를 복원한다.
        for (const auto& child : m_scrollChildren) if (::IsWindow(child.window) && ::GetParent(child.window) == m_hWnd)
            ::SetWindowPos(child.window, nullptr, child.original.left - m_scrollOffset.x,
                child.original.top - m_scrollOffset.y, 0, 0, flags);
    }
    // [assignment6] client 배경뿐 아니라 Edit/List의 테두리와 ARP 자식 Dialog 내부까지 무효화한다.
    // [assignment6] RDW_UPDATENOW로 이번 스크롤의 WM_ERASEBKGND/WM_PAINT를 끝내 이전 화면 잔상을 지운다.
    RedrawWindow(nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN | RDW_UPDATENOW);
}

void Cipc2019Dlg::ScrollDialogTo(int x, int y)
{
    x = DialogScrollLayout::Clamp(x, m_scrollMaximum.cx); y = DialogScrollLayout::Clamp(y, m_scrollMaximum.cy);
    if (x == m_scrollOffset.x && y == m_scrollOffset.y) return;
    m_scrollOffset = CPoint(x, y);
    SetScrollPos(SB_HORZ, x, TRUE); SetScrollPos(SB_VERT, y, TRUE); PositionScrollChildren();
}

void Cipc2019Dlg::OnSize(UINT type, int cx, int cy)
{
    CDialogEx::OnSize(type, cx, cy);
    if (type != SIZE_MINIMIZED) UpdateDialogScroll();
}
void Cipc2019Dlg::OnGetMinMaxInfo(MINMAXINFO* info)
{
    CDialogEx::OnGetMinMaxInfo(info);
    if (!m_scrollReady) return;
    CRect minimum(0, 0, 260, 180); MapDialogRect(&minimum);
    ::AdjustWindowRectEx(&minimum, GetStyle() & ~(WS_HSCROLL | WS_VSCROLL), FALSE, GetExStyle());
    const CRect work(MonitorWorkArea(m_hWnd));
    info->ptMinTrackSize.x = (std::min)(minimum.Width(), work.Width());
    info->ptMinTrackSize.y = (std::min)(minimum.Height(), work.Height());
}

// [assignment6] 가로/세로 공통 handler다. thumb는 메시지의 16비트 nPos 대신 32비트 nTrackPos를 읽는다.
void Cipc2019Dlg::HandleDialogScroll(int bar, UINT code)
{
    const bool horizontal = bar == SB_HORZ;
    const int current = horizontal ? m_scrollOffset.x : m_scrollOffset.y;
    const int maximum = horizontal ? m_scrollMaximum.cx : m_scrollMaximum.cy;
    const int line = (std::max)(1, static_cast<int>(horizontal ? m_scrollLine.cx : m_scrollLine.cy));
    const int page = (std::max)(line, static_cast<int>(horizontal ? m_scrollPage.cx : m_scrollPage.cy) - line);
    using ACTION = DialogScrollLayout::ACTION;
    ACTION action = ACTION::None; int thumb = current;
    switch (code) {
    case SB_LINEUP: action = ACTION::LineBack; break;
    case SB_LINEDOWN: action = ACTION::LineForward; break;
    case SB_PAGEUP: action = ACTION::PageBack; break;
    case SB_PAGEDOWN: action = ACTION::PageForward; break;
    case SB_TOP: action = ACTION::First; break;
    case SB_BOTTOM: action = ACTION::Last; break;
    case SB_THUMBTRACK: case SB_THUMBPOSITION: {
        SCROLLINFO info = {}; info.cbSize = sizeof(info); info.fMask = SIF_TRACKPOS;
        if (!GetScrollInfo(bar, &info)) return;
        thumb = info.nTrackPos; action = ACTION::Thumb; break;
    }
    }
    const int next = DialogScrollLayout::Next(current, maximum, line, page, action, thumb);
    ScrollDialogTo(horizontal ? next : m_scrollOffset.x, horizontal ? m_scrollOffset.y : next);
}
void Cipc2019Dlg::OnVScroll(UINT code, UINT position, CScrollBar* scrollBar)
{
    if (!m_scrollReady || scrollBar) CDialogEx::OnVScroll(code, position, scrollBar);
    else HandleDialogScroll(SB_VERT, code);
}
void Cipc2019Dlg::OnHScroll(UINT code, UINT position, CScrollBar* scrollBar)
{
    if (!m_scrollReady || scrollBar) CDialogEx::OnHScroll(code, position, scrollBar);
    else HandleDialogScroll(SB_HORZ, code);
}

// [assignment6] 채팅/상태 편집창, ARP 표, 드롭다운의 휠은 컨트롤 자체에 맡긴다.
bool Cipc2019Dlg::ControlOwnsWheel(CPoint screenPoint) const
{
    for (HWND child = ::WindowFromPoint(screenPoint); child && child != m_hWnd; child = ::GetParent(child)) {
        if (!::IsChild(m_hWnd, child)) return false;
        TCHAR name[64] = {}; ::GetClassName(child, name, _countof(name));
        if ((!_tcsicmp(name, _T("Edit")) && (::GetWindowLongPtr(child, GWL_STYLE) & ES_MULTILINE)) ||
            !_tcsicmp(name, _T("SysListView32")) || !_tcsicmp(name, _T("ComboBox")) ||
            !_tcsicmp(name, _T("ComboLBox")) || !_tcsicmp(name, _T("ListBox"))) return true;
    }
    return false;
}

bool Cipc2019Dlg::ScrollDialogWheel(UINT flags, short delta)
{
    if (!m_scrollReady || (flags & MK_CONTROL)) return false;
    const bool horizontal = (flags & MK_SHIFT) || (!m_scrollMaximum.cy && m_scrollMaximum.cx);
    const int maximum = horizontal ? m_scrollMaximum.cx : m_scrollMaximum.cy;
    if (!maximum) return false;
    UINT lines = 3; ::SystemParametersInfo(SPI_GETWHEELSCROLLLINES, 0, &lines, 0);
    int& remainder = m_wheelRemainder[horizontal ? 0 : 1]; remainder += delta;
    const int notches = remainder / WHEEL_DELTA; remainder %= WHEEL_DELTA;
    const int line = (std::max)(1, static_cast<int>(horizontal ? m_scrollLine.cx : m_scrollLine.cy));
    const int page = (std::max)(line, static_cast<int>(horizontal ? m_scrollPage.cx : m_scrollPage.cy) - line);
    const long long distance = lines == WHEEL_PAGESCROLL ? page : static_cast<long long>(lines) * line;
    const int next = DialogScrollLayout::Clamp((horizontal ? m_scrollOffset.x : m_scrollOffset.y) - notches * distance, maximum);
    ScrollDialogTo(horizontal ? next : m_scrollOffset.x, horizontal ? m_scrollOffset.y : next); return true;
}
bool Cipc2019Dlg::PreTranslateDialogScroll(MSG* message)
{
    if (message->message != WM_MOUSEWHEEL || !m_scrollReady) return false;
    const CPoint point(static_cast<short>(LOWORD(message->lParam)), static_cast<short>(HIWORD(message->lParam)));
    const HWND hovered = ::WindowFromPoint(point);
    if (hovered != m_hWnd && !::IsChild(m_hWnd, hovered)) return false;
    if (ControlOwnsWheel(point)) {
        // [assignment6] 포커스가 다른 입력칸에 있어도 마우스 아래 채팅/표가 자신의 내용만 스크롤한다.
        message->hwnd = hovered; return false;
    }
    return ScrollDialogWheel(LOWORD(message->wParam), static_cast<short>(HIWORD(message->wParam)));
}
BOOL Cipc2019Dlg::OnMouseWheel(UINT flags, short delta, CPoint point)
{
    if (!ControlOwnsWheel(point) && ScrollDialogWheel(flags, delta)) return TRUE;
    return CDialogEx::OnMouseWheel(flags, delta, point);
}

// [assignment6] ARP 자식창 내부 컨트롤도 main 좌표로 변환해 키보드 접근 시 화면 안으로 가져온다.
void Cipc2019Dlg::RevealFocusedControl()
{
    const HWND focus = ::GetFocus();
    m_lastScrollFocus = focus;
    if (!m_scrollReady || !::IsChild(m_hWnd, focus) || !::IsWindowVisible(focus)) return;
    CRect rect; ::GetWindowRect(focus, &rect); ScreenToClient(&rect); rect.OffsetRect(m_scrollOffset);
    ScrollDialogTo(DialogScrollLayout::Reveal(m_scrollOffset.x, m_scrollMaximum.cx, m_scrollPage.cx,
        rect.left, rect.right, m_scrollLine.cx / 2),
        DialogScrollLayout::Reveal(m_scrollOffset.y, m_scrollMaximum.cy, m_scrollPage.cy,
            rect.top, rect.bottom, m_scrollLine.cy / 2));
}
