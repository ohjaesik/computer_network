#pragma once
#include "NetworkPackets.h"

// [assignment6] UI 문자열과 계층에서 사용하는 주소의 변환을 한곳에서 처리한다.
namespace NetworkAddress {
    inline bool ParseIp(CString text, uint32_t& ip) {
        text.Trim(); ip = 0;
        int count = 0, value = 0, digits = 0;
        for (int i = 0; i <= text.GetLength(); ++i) {
            const TCHAR ch = i == text.GetLength() ? _T('.') : text[i];
            if (ch == _T('.')) {
                if (!digits || ++count > 4) return false;
                ip = (ip << 8) | static_cast<uint32_t>(value); value = digits = 0;
            } else {
                if (ch < _T('0') || ch > _T('9') || ++digits > 3) return false;
                value = value * 10 + ch - _T('0'); if (value > 255) return false;
            }
        }
        return count == 4;
    }
    inline CString FormatIp(uint32_t ip) {
        CString text; text.Format(_T("%u.%u.%u.%u"), (ip >> 24) & 255, (ip >> 16) & 255, (ip >> 8) & 255, ip & 255);
        return text;
    }
    inline bool ParseMac(CString text, unsigned char* address) {
        text.Trim(); text.Replace(_T('-'), _T(':')); text.MakeUpper();
        if (text.GetLength() != 17) return false;
        const CString digits = _T("0123456789ABCDEF");
        for (int i = 0; i < 6; ++i) {
            int high = digits.Find(text[i * 3]), low = digits.Find(text[i * 3 + 1]);
            if (high < 0 || low < 0 || (i < 5 && text[i * 3 + 2] != _T(':'))) return false;
            address[i] = static_cast<unsigned char>(high * 16 + low);
        }
        return true;
    }
    inline CString FormatMac(const unsigned char* address) {
        CString text; text.Format(_T("%02X-%02X-%02X-%02X-%02X-%02X"),
            address[0], address[1], address[2], address[3], address[4], address[5]);
        return text;
    }
}
