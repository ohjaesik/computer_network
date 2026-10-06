#pragma once
// [assignment6] Linux에서 실제 Base/LayerManager/Ethernet/IP/ARP/Chat 소스를 검사하기 위한 최소 플랫폼 대체물.
// [assignment6] GUI, Npcap 드라이버, Windows 파일 I/O의 빌드/실행을 검증하는 도구는 아니다.
#define PCH_H
#define AFX_STDAFX_H__119ECB1B_6E70_4662_A2A9_A20B5201CA81__INCLUDED_
#define __AFXWIN_H__
#include "ProtocolConstants.h" // run_protocol_tests.py가 실제 stdafx.h의 상수 영역에서 생성한다.
#include <arpa/inet.h>
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>
#include <mutex>

using BOOL = int;
using UINT = unsigned int;
using ULONGLONG = uint64_t;
using HWND = void*;
using WPARAM = uintptr_t;
using LPARAM = intptr_t;
using TCHAR = char;
using LPCTSTR = const char*;
#define TRUE 1
#define FALSE 0
#define WM_APP 0x8000
#define _T(x) x
#define DECLARE_MESSAGE_MAP()
#define ASSERT(x) assert(x)
class CObject {};
class CWinApp {};
class CString {
    std::string value;
public:
    CString() = default;
    CString(const char* text) : value(text ? text : "") {}
    void Format(const char* format, ...) {
        char buffer[4096]; va_list args; va_start(args, format);
        std::vsnprintf(buffer, sizeof(buffer), format, args); va_end(args); value = buffer;
    }
    char* GetBuffer(int) { return value.data(); }
    operator const char*() const { return value.c_str(); }
};
inline ULONGLONG testNow = 10000;
inline ULONGLONG GetTickCount64() { return testNow; }
inline BOOL PostMessage(HWND, UINT message, WPARAM, LPARAM data) {
    if (message == WM_NETWORK_EVENT) delete reinterpret_cast<CString*>(data);
    return TRUE;
}
inline int strcpy_s(char* dst, size_t capacity, const char* src) {
    assert(std::strlen(src) < capacity); std::strcpy(dst, src); return 0;
}
template<size_t N> int strcpy_s(char (&dst)[N], const char* src) { return strcpy_s(dst, N, src); }
inline char* strtok_s(char* text, const char* delimiters, char** context) { return strtok_r(text, delimiters, context); }
