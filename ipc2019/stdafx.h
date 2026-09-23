// stdafx.h : include file for standard system include files,
//  or project specific include files that are used frequently, but
//      are changed infrequently
//

#if !defined(AFX_STDAFX_H__119ECB1B_6E70_4662_A2A9_A20B5201CA81__INCLUDED_)
#define AFX_STDAFX_H__119ECB1B_6E70_4662_A2A9_A20B5201CA81__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#define VC_EXTRALEAN		// Exclude rarely-used stuff from Windows headers

#ifndef NOMINMAX
#define NOMINMAX              // std::min/max와 Windows 매크로의 이름 충돌 방지
#endif
#include <WinSock2.h>         // pcap보다 먼저 포함하여 Winsock 선언 충돌 방지
#include <afxwin.h>         // MFC core and standard components
#include <afxext.h>         // MFC extensions
#include <afxdisp.h>        // MFC Automation classes
#include <afxdtctl.h>		// MFC support for Internet Explorer 4 Common Controls
#ifndef _AFX_NO_AFXCMN_SUPPORT
#include <afxcmn.h>			// MFC support for Windows Common Controls
#endif // _AFX_NO_AFXCMN_SUPPORT


#include <afxmt.h>           // 송수신 스레드의 임계 구역
#include <stdint.h>          // 패킷 필드의 크기를 16/32비트로 고정
#include <algorithm>
#include <vector>
#include <atlconv.h>         // CString과 전송용 UTF-8 문자열 변환

//{{AFX_INSERT_LOCATION}}

#define MAX_LAYER_NUMBER		0xff

#define ETHER_MAX_SIZE			1514
#define ETHER_HEADER_SIZE		14
#define ETHER_MAX_DATA_SIZE		( ETHER_MAX_SIZE - ETHER_HEADER_SIZE )

#define TCP_HEADER_SIZE			20
#define IP_HEADER_SIZE			20

#define APP_HEADER_SIZE			( sizeof(unsigned int) * 2 +				\
								  sizeof(unsigned short) +					\
								  sizeof(unsigned char)	)
#define APP_DATA_SIZE			( ETHER_MAX_DATA_SIZE - ( APP_HEADER_SIZE +		\
												          TCP_HEADER_SIZE +		\
												          IP_HEADER_SIZE ) )

// [Assignment 4 추가] 기존 IPC 상수는 위에 유지하고 Ethernet 통신용 상수만 추가한다.
// 1: 과제 4의 NI 경로, 0: 기존 과제 3의 파일 기반 IPC 경로.
#define USE_NPCAP_STACK             1
#define ETHERNET_ADDRESS_SIZE       6
#define ETHERNET_TYPE_CHAT          0x2080
#define ETHERNET_TYPE_FILE          0x2090
#define CHAT_APP_HEADER_SIZE        4
#define CHAT_APP_DATA_SIZE          (ETHER_MAX_DATA_SIZE - CHAT_APP_HEADER_SIZE)
#define CHAT_MAX_MESSAGE_SIZE       0xffff  // 과제의 2바이트 전체 길이 필드 범위
#define CHAT_FRAGMENT_FIRST         0x00
#define CHAT_FRAGMENT_MIDDLE        0x01
#define CHAT_FRAGMENT_LAST          0x02
#define FILE_APP_HEADER_SIZE        12
#define FILE_APP_DATA_SIZE          (ETHER_MAX_DATA_SIZE - FILE_APP_HEADER_SIZE)
#define FILE_TYPE_BINARY            0x0000
#define FILE_MESSAGE_INFO           0x00
#define FILE_MESSAGE_DATA           0x01
#define FILE_MESSAGE_END            0x02
#define WM_CHAT_RECEIVED             (WM_APP + 101)
#define WM_FILE_STATUS              (WM_APP + 102)

// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_STDAFX_H__119ECB1B_6E70_4662_A2A9_A20B5201CA81__INCLUDED_)
P