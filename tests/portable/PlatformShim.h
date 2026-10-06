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
#include <filesystem>
#include <fstream>
#include <unistd.h>

// [assignment6] Windows COM 헤더의 interface 매크로도 재현해 동명 변수로 인한 MSVC 구문 오류를 막는다.
#define interface struct

using BOOL = int;
using UINT = unsigned int;
using ULONGLONG = uint64_t;
using HWND = void*;
using WPARAM = uintptr_t;
using LPARAM = intptr_t;
using TCHAR = char;
using LPCTSTR = const char*;
using LPCSTR = const char*;
using DWORD = uint32_t;
using LONG = int32_t;
using LPVOID = void*;
#define TRUE 1
#define FALSE 0
#define WM_APP 0x8000
#define _T(x) x
#define DECLARE_MESSAGE_MAP()
#define ASSERT(x) assert(x)
#define __cdecl
#define CP_UTF8 65001
#define MAX_PATH 260
#define ERROR_ALREADY_EXISTS 183
#define MOVEFILE_REPLACE_EXISTING 1
#define THREAD_PRIORITY_NORMAL 0
#define CREATE_SUSPENDED 0
#define INFINITE 0xffffffff
class CObject {};
class CWinApp {};
class CString {
    std::string value;
public:
    CString() = default;
    CString(const char* text) : value(text ? text : "") {}
    CString(const char* text, int length) : value(text,length) {}
    bool IsEmpty() const { return value.empty(); }
    void Empty() { value.clear(); }
    int GetLength() const { return int(value.size()); }
    char operator[](int index) const { return value[index]; }
    int ReverseFind(char ch) const { const auto p=value.rfind(ch); return p==value.npos ? -1 : int(p); }
    CString Mid(int index) const { return value.substr(index).c_str(); }
    CString Left(int length) const { return value.substr(0,length).c_str(); }
    CString Right(int length) const { return value.substr(value.size()-(std::min)(value.size(),size_t(length))).c_str(); }
    int FindOneOf(const char* chars) const { const auto p=value.find_first_of(chars); return p==value.npos ? -1 : int(p); }
    friend CString operator+(const CString& left,const CString& right) { return (left.value+right.value).c_str(); }
    friend bool operator==(const CString& left,const char* right) { return left.value==right; }
    void Format(const char* format, ...) {
        char buffer[4096]; va_list args; va_start(args, format);
        std::vsnprintf(buffer, sizeof(buffer), format, args); va_end(args); value = buffer;
    }
    char* GetBuffer(int) { return value.data(); }
    operator const char*() const { return value.c_str(); }
};
using CStringA = CString;
#define CA2T(x,...) (static_cast<const char*>(x))
#define CT2A(x,...) (static_cast<const char*>(x))
inline ULONGLONG testNow = 10000;
inline ULONGLONG GetTickCount64() { return testNow; }
inline void Sleep(int milliseconds) { testNow += milliseconds; }
inline LONG InterlockedCompareExchange(volatile LONG* value,LONG replacement,LONG comparand) { return __sync_val_compare_and_swap(value,comparand,replacement); }
inline LONG InterlockedExchange(volatile LONG* value,LONG replacement) { return __sync_lock_test_and_set(value,replacement); }
// [assignment6] 파일 테스트 worker는 동기 실행한다. 실제 Windows thread scheduling 검증은 아니다.
class CWinThread {
public:
    BOOL m_bAutoDelete = FALSE;
    void* m_hThread = nullptr;
    UINT (*function)(void*) = nullptr;
    void* parameter = nullptr;
    void ResumeThread() { function(parameter); }
};
inline CWinThread* AfxBeginThread(UINT (*function)(void*),void* parameter,int,int,int) {
    auto* thread = new CWinThread; thread->function=function; thread->parameter=parameter; return thread;
}
inline int WaitForSingleObject(void*,unsigned) { return 0; }
inline std::vector<LPARAM> fileStatuses;
inline BOOL PostMessage(HWND, UINT message, WPARAM, LPARAM data) {
    if (message == WM_NETWORK_EVENT) delete reinterpret_cast<CString*>(data);
    if (message == WM_FILE_STATUS) fileStatuses.push_back(data);
    return TRUE;
}
inline std::string TestPath(const char* path) { std::string text(path); std::replace(text.begin(),text.end(),'\\','/'); return text; }
inline DWORD GetLastError() { return ERROR_ALREADY_EXISTS; }
inline BOOL CreateDirectory(const char* path,void*) { return std::filesystem::create_directories(TestPath(path)); }
inline BOOL DeleteFile(const char* path) { return std::filesystem::remove(TestPath(path)); }
inline BOOL MoveFileEx(const char* from,const char* to,int) {
    std::error_code error; std::filesystem::rename(TestPath(from),TestPath(to),error); return !error;
}
inline DWORD GetModuleFileName(void*,char* buffer,int capacity) {
    const std::string path=std::filesystem::current_path().string()+"/run\\app.exe";
    std::snprintf(buffer,capacity,"%s",path.c_str()); return DWORD(path.size());
}
class CFileException { public: void Delete() { delete this; } };
class CFile {
public:
    enum { modeRead=1,modeReadWrite=2,modeWrite=4,modeCreate=8,shareDenyWrite=16,shareExclusive=32 };
    static constexpr FILE* hFileNull = nullptr;
    FILE* m_hFile = nullptr;
    ~CFile() { Abort(); }
    BOOL Open(const char* path,int flags,CFileException*) {
        const std::string converted=TestPath(path); m_hFile=std::fopen(converted.c_str(),flags&modeCreate ? "wb+" : "rb"); return m_hFile!=nullptr;
    }
    ULONGLONG GetLength() {
        const auto current=ftello(m_hFile); fseeko(m_hFile,0,SEEK_END); const auto length=ftello(m_hFile); fseeko(m_hFile,current,SEEK_SET); return length;
    }
    void SetLength(ULONGLONG length) { if(ftruncate(fileno(m_hFile),length)) throw new CFileException; }
    void SeekToBegin() { std::fseek(m_hFile,0,SEEK_SET); }
    UINT Read(void* buffer,UINT count) { return UINT(std::fread(buffer,1,count,m_hFile)); }
    void Write(const void* buffer,UINT count) { if(std::fwrite(buffer,1,count,m_hFile)!=count) throw new CFileException; }
    void Close() { if(m_hFile) std::fclose(m_hFile); m_hFile=nullptr; }
    void Abort() { Close(); }
};
inline int strcpy_s(char* dst, size_t capacity, const char* src) {
    assert(std::strlen(src) < capacity); std::strcpy(dst, src); return 0;
}
template<size_t N> int strcpy_s(char (&dst)[N], const char* src) { return strcpy_s(dst, N, src); }
inline char* strtok_s(char* text, const char* delimiters, char** context) { return strtok_r(text, delimiters, context); }
