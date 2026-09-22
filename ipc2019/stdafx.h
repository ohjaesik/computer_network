// stdafx.h : frequently used system/project declarations.
#if !defined(AFX_STDAFX_H__119ECB1B_6E70_4662_A2A9_A20B5201CA81__INCLUDED_)
#define AFX_STDAFX_H__119ECB1B_6E70_4662_A2A9_A20B5201CA81__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif

#define VC_EXTRALEAN
#define NOMINMAX

#include <WinSock2.h>
#include <afxwin.h>
#include <afxext.h>
#include <afxdisp.h>
#include <afxdtctl.h>
#include <afxmt.h>
#ifndef _AFX_NO_AFXCMN_SUPPORT
#include <afxcmn.h>
#endif

#include <algorithm>
#include <stdint.h>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Common layer limits
// ---------------------------------------------------------------------------
#define MAX_LAYER_NUMBER             0xff

// Assignment 3 constants are intentionally retained for the legacy IPC path.
#define ETHER_MAX_SIZE               1514
#define ETHER_HEADER_SIZE            14
#define ETHER_MAX_DATA_SIZE          (ETHER_MAX_SIZE - ETHER_HEADER_SIZE)

#define TCP_HEADER_SIZE              20
#define IP_HEADER_SIZE               20

#define APP_HEADER_SIZE              (sizeof(unsigned int) * 2 + \
                                      sizeof(unsigned short) + \
                                      sizeof(unsigned char))
#define APP_DATA_SIZE                (ETHER_MAX_DATA_SIZE - (APP_HEADER_SIZE + \
                                      TCP_HEADER_SIZE + IP_HEADER_SIZE))

// ---------------------------------------------------------------------------
// Assignment 4 protocol constants
// ---------------------------------------------------------------------------
#define ETHERNET_ADDRESS_SIZE        6
#define ETHERNET_TYPE_CHAT           0x2080
#define ETHERNET_TYPE_FILE           0x2090

#define CHAT_APP_HEADER_SIZE         4
#define CHAT_APP_DATA_SIZE           (ETHER_MAX_DATA_SIZE - CHAT_APP_HEADER_SIZE)
#define CHAT_MAX_MESSAGE_SIZE        0xffff

#define CHAT_FRAGMENT_FIRST          0x00
#define CHAT_FRAGMENT_MIDDLE         0x01
#define CHAT_FRAGMENT_LAST           0x02

#define FILE_APP_HEADER_SIZE         12
#define FILE_APP_DATA_SIZE           (ETHER_MAX_DATA_SIZE - FILE_APP_HEADER_SIZE)
#define FILE_TYPE_BINARY             0x0000

#define FILE_MESSAGE_INFO            0x00
#define FILE_MESSAGE_DATA            0x01
#define FILE_MESSAGE_END             0x02

#define PCAP_READ_TIMEOUT_MS         500
#define FILE_SEND_THROTTLE_MS        1

// Assignment 3 registered-message/ACK functions remain in the source, but the
// Assignment 4 protocol does not define or start that legacy ACK exchange.
#define ENABLE_LEGACY_IPC_ACK        0

#define WM_APP_CHAT_RECEIVED         (WM_APP + 101)
#define WM_APP_FILE_STATUS           (WM_APP + 102)

// Avoid a Winsock/Windows header-order dependency while still applying the
// network-byte-order rule required by the protocol.
inline uint16_t HostToNetwork16(uint16_t value)
{
    return static_cast<uint16_t>((value << 8) | (value >> 8));
}

inline uint16_t NetworkToHost16(uint16_t value)
{
    return HostToNetwork16(value);
}

inline uint32_t HostToNetwork32(uint32_t value)
{
    return ((value & 0x000000ffUL) << 24) |
           ((value & 0x0000ff00UL) << 8) |
           ((value & 0x00ff0000UL) >> 8) |
           ((value & 0xff000000UL) >> 24);
}

inline uint32_t NetworkToHost32(uint32_t value)
{
    return HostToNetwork32(value);
}

#endif
