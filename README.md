# computer_network
# computer_network


## Assignment 4 추가 구현

기준은 `main`의 `6e14f209c826ffe8513e30daf913399284bc35b8`이다. 기존 코드와 주석을 유지하고 과제 4에 필요한 기능만 추가했다. 새 소스 파일은 `NILayer.h/.cpp`, `FileAppLayer.h/.cpp` 네 개다. 프로토콜 상수는 기존 `stdafx.h`에 추가했다.

### 연결 구조와 주요 변경

- 채팅: Dialog → ChatApp → Ethernet → NI. 수신은 역순이다.
- 파일: Dialog → FileApp → Ethernet → NI. 수신 파일은 실행 파일 옆 `ReceivedFiles`에 저장한다.
- `LayerManager::ConnectLayers()`로 연결한다. Dialog의 단일 Under 포인터는 ChatApp에 유지하고 FileApp의 Upper만 Dialog에 연결한다.
- 채팅 EtherType은 `0x2080`, 파일은 `0x2090`이다. 채팅은 1496바이트, 파일 데이터는 1488바이트 단위로 분할한다.
- 파일 정보/데이터/종료를 구분하고 수신한 실제 길이와 순번을 검증한다. 검증 완료 전에는 `.part`로 저장한다.
- NI의 수신 스레드와 FileApp의 송신 스레드를 분리해 파일 전송 중에도 채팅을 처리한다. UI 출력은 `PostMessage`를 통해 UI 스레드에서 처리한다.
- Packet32의 `OID_802_3_CURRENT_ADDRESS`로 어댑터의 실제 MAC을 조회한다.
- 기존 FileLayer, IPC 등록 메시지, ACK와 타이머 코드는 보존했다. `USE_NPCAP_STACK=1`에서는 네트워크 경로를 실행하므로 IPC ACK 타이머가 시작되지 않는다. `0`이면 기존 IPC 경로를 선택한다.
- 원본에 있던 잘린 `DoDataExchange` 선언과 잘못된 `_T` 호출, x64 타이머 매개변수 형식은 빌드에 필요한 범위에서 수정했다. 레이어 이름의 문자열 리터럴은 `const char*`로 받는다.

### 빌드와 실행

1. Windows에서 C++ MFC 개발 도구와 해당 프로젝트의 플랫폼 도구 집합을 설치한다.
2. 과제 안내에 따라 Npcap을 WinPcap API 호환 모드로 설치하고 Npcap SDK를 준비한다.
3. `NPCAP_SDK_DIR`을 SDK의 `Include`와 `Lib` 폴더가 있는 경로로 설정한다. Visual Studio를 다시 실행해야 새 환경 변수를 읽는다.
4. `ipc2019/ipc2019.vcxproj`를 열고 빌드한다. Win32는 `Lib`, x64는 `Lib/x64`를 참조한다.
5. 각 PC의 유선 어댑터를 선택하고 상대 MAC을 Destination에 입력한 뒤 설정을 누른다. 어댑터 선택 시 Source MAC을 바로 확인할 수 있다.
6. 파일을 선택하고 전송한다. Wireshark 표시 필터는 `eth.type == 0x2080 || eth.type == 0x2090`이다.

### 검증 범위와 제한

기존 주석 353개 보존, 프로젝트 XML 및 리소스 ID 대조를 수행했다. Linux의 임시 Windows/MFC 대체 코드에서 실제 Base/LayerManager/Chat/Ethernet/FileApp 소스를 사용해 채팅 크기 10종, 파일 크기 10종(빈 파일 포함)의 왕복과 파일 바이트 일치, 주소/타입 필터, 잘린 프레임, 누락/잘못된 순서 및 레이어 연결을 검사했다. AddressSanitizer/UndefinedBehaviorSanitizer 검사도 통과했다. 환경상 LeakSanitizer는 실행하지 못했다. 이 검증은 실제 Windows/MFC 빌드나 Npcap 장치 통신 성공을 의미하지 않는다.

과제에서 제시한 헤더 크기를 그대로 사용하므로 채팅 전체 길이는 65,535 UTF-8 바이트, 파일 전체 크기는 4 GiB 미만이다. 이를 넘어서는 크기까지 지원하려면 헤더 또는 별도 길이 협약을 확장해야 한다. 단편화는 MTU에 따른 한 프레임 크기 제한을 해소한다.

ACK·재전송은 구현하지 않았다. 송신 완료는 로컬 프레임 송신 완료이며, 상대 파일 저장 완료는 수신 측 결과와 원본/수신 파일 해시로 확인해야 한다. 파일 전송은 수신 측에서 한 번에 한 송신자를 처리한다. 채팅 헤더에는 조각 순번이 없어 같은 길이의 중간 조각 순서 변경을 검출하지 못한다. 두 PC의 실제 송수신, 긴 채팅 중 파일 전송, 한글 표시와 Wireshark 캡처는 Windows 실습 환경에서 최종 확인해야 한다.

### 컴파일 오류 수정 (2026-09-23)

- `stdafx.h`의 마지막 `#endif` 뒤에 남아 있던 단독 `P` 문자를 수정했다. 이 문자는 C++ 선언이 아니므로 공통 헤더를 포함하는 모든 소스의 컴파일을 막는다.
- `afxwin.h`를 `WinSock2.h`보다 먼저 포함하도록 바꾸었다. MFC보다 먼저 Windows 헤더가 포함되는 문제를 막고, `WIN32_LEAN_AND_MEAN`으로 구버전 Winsock.h의 자동 포함도 차단한다. 프로토콜 상수와 기존 주석은 유지했다.
- 수정 후에는 해당 브랜치의 최신 소스를 받아 Visual Studio에서 **빌드 → 솔루션 다시 빌드**를 실행한다.

기존 Linux 프로토콜 검사는 공통 헤더에서 상수만 가져와 마지막 `P`를 발견하지 못했다. 이번에는 외부 MFC 헤더만 임시로 대체하고 `stdafx.h` 전체를 검사하여 수정 전 구문 오류와 수정 후 통과를 확인했다. 이 검사는 실제 MFC 헤더·Npcap SDK를 사용한 Windows 빌드 검증이 아니다.

### 과제 PDF 예시를 반영한 UI 배치

`ipc2019.rc`의 Dialog를 PDF 38쪽처럼 왼쪽 채팅/파일 전송, 오른쪽 어댑터/MAC 설정의 두 열로 배치했다. 버튼과 입력창의 높이·여백을 맞추고, 네트워크 상태 표시를 파일 전송 상태에서 분리했다. 어댑터 목록을 펼치면 컨트롤 폰트로 측정한 설명 길이에 맞춰 폭이 늘어난다. 기존 컨트롤 ID와 프로토콜 동작은 유지한다.

UI 스레드는 MFC의 기본 애플리케이션 스레드다. 별도로 구현한 작업 스레드는 `NILayer::ReadingThread`와 `FileAppLayer::FileTransferThread` 두 개이며, 작업 결과는 `PostMessage`로 UI에 전달한다. UI 스레드를 추가 생성하는 변경은 하지 않았다.

리소스의 컨트롤 좌표·ID·상태 표시 연결과 배치 미리보기를 확인했다. Linux 환경에서는 실제 Windows/MFC 빌드와 DPI별 실행 화면을 검증하지 못했다.

## 채팅 줄바꿈 및 파일 전송 상태 표시

- 채팅 기록은 읽기 전용 다중 행 편집창으로 자동 줄바꿈한다. MAC 주소와 본문을 두 줄로 구분하며 긴 메시지는 세로 스크롤로 읽거나 복사할 수 있다.
- 파일 송신과 수신에 별도 진행률/상태창을 사용한다. 실제 처리 바이트, 전체 크기, 최근 약 1초 속도, 경과 시간, 현재 속도 기준 예상 남은 시간을 표시한다. 용량 단위는 1024 기준 KiB/MiB/GiB이다.
- 작업 스레드는 기존 정수 진행률 변화 외에도 250ms마다 누적 계수를 UI 메시지로 전달한다. UI 타이머가 5초 이상 바이트 증가가 없는 상황을 표시하며, 이를 자동 실패나 ACK 타임아웃으로 판정하지 않는다.
- 수신량은 파일의 미리 할당된 크기가 아니라 Write 성공 바이트로 계산한다. 데이터 100%와 END 검증/저장 완료를 구분하고, 송신 완료는 상대 저장 확인을 뜻하지 않는다. 양쪽 PC의 진행률을 각각 확인한다.
- 수신 완료 경로/오류는 채팅 기록에도 남고, 수신 폴더 열기 버튼은 실행 파일 옆 ReceivedFiles를 연다.
- Debug/Win32의 문자 집합을 다른 구성과 같은 Unicode로 맞췄다. 기존 CT2A/CA2T의 CP_UTF8 변환이 모든 구성에서 적용되도록 양쪽 PC를 다시 빌드해야 한다.
- 패킷 형식, ACK/재전송 정책, 기존 Sleep(1) 송신 간격은 유지한다. 이번 변경은 속도를 측정하여 표시하며 전송 속도 자체를 높이는 변경은 아니다.

검증: 실제 수정한 함수에 임시 Linux용 MFC 대체 코드를 적용해 긴 채팅 추가, 송수신 분리, 속도/ETA, 정체 표시, 종료 확인 대기, 완료 뒤 시간 고정, 오류 후 처리량 유지, 재시작, 빈 파일, UI/ACK 타이머 분리를 검사했다. 실제 프로토콜 소스의 채팅 10종/파일 10종 왕복과 파일 바이트 일치 및 진행 상태 스냅샷을 확인했고 AddressSanitizer/UndefinedBehaviorSanitizer 검사도 통과했다(LeakSanitizer 제외). 리소스 ID/배치/기존 주석 보존을 확인했다. 실제 Windows/MFC 빌드, DPI별 화면, 두 PC의 Npcap 통신 속도는 이 환경에서 검증하지 못했다.

## Assignment 6: IP / ARP / PARP / GARP

기존 과제 3/4 경로와 주석은 보존하고 `USE_IP_STACK=1`에서 IP 계층을 사용한다. 아래는 이 브랜치의 현재 동작이며, 위 과제 4 절의 EtherType 및 MTU 설명은 `USE_IP_STACK=0` 기준이다. 추가/변경 기능에는 `[assignment6]` 주석을 달았다. 프로토콜 상수는 `stdafx.h`에 모았다.

강의자료 23·29쪽의 기본 ARP 실습은 어댑터 선택, 내 IP 설정, Request/Reply와 캐시 생성·갱신·만료를 중심으로 한다. MAC은 `NILayer::QueryMac()`이 Packet32의 `OID_802_3_CURRENT_ADDRESS`로 조회하므로 상대 IP를 입력하기 전에도 표시된다. 프로그램의 내 IP는 직접 입력하며 Windows IP를 자동으로 가져오거나 변경하지 않는다. ARP 표는 자신을 대상으로 하는 Request/Reply나 유효한 GARP 수신으로도 학습되므로 요청 입력칸이 비어 있어도 행이 생길 수 있다.

강의자료 30쪽에는 Basic ARP/PARP/GARP 데모가 명시되어 있지만, 아래 두 NIC의 실제 IP 중계, Next hop, TTL·ICMP 처리와 RFC 5227 주소 충돌 검사는 기본 ARP 실습보다 확장한 부분이다. 기본 ARP 확인에는 단일 NIC를 사용한다. GARP 광고와 충돌 검사는 별개 기능으로 설명해야 한다.

### 계층과 식별값

- 채팅/파일: Dialog → ChatApp/FileApp → IP → Ethernet → NI. IP Protocol은 각각 253/254(실험용)이며 TCP/UDP 헤더를 가장하지 않는다.
- Ethernet은 IPv4 `0x0800`과 ARP `0x0806`만 분기한다. NI는 중계에 필요한 일반 TCP/UDP/ICMP도 놓치지 않도록 모든 IPv4와 ARP를 캡처한다.
- IP 헤더 20바이트를 포함해 Ethernet payload 1500바이트 이하로 만든다. 채팅 본문 1476바이트, 파일 본문 1468바이트이며, 기존 앱 재조립 규약은 유지한다.
- ARP Request/Reply, 캐시 생성/갱신/삭제, Complete 20분·Incomplete 3분 만료를 구현했다. IP 송신 대기의 3초 제한과 ARP 캐시의 3분 수명은 다르다.

### PARP와 실제 전달

`IPRouter.h/.cpp`가 두 NIC 사이의 정적 IPv4 forwarding을 담당한다. PARP 항목은 응답할 IP와 실제 출력 NIC, 선택적인 next-hop IP를 함께 등록한다. 응답은 요청이 들어온 NIC의 MAC으로 보내고, 받은 IP 데이터는 다른 NIC의 다음 홉으로 전달한다. 반대 방향의 Proxy 항목도 등록해야 양방향 통신이 된다.

- 목적지 /32 Proxy 경로를 connected subnet보다 먼저 선택한다. 출력 NIC와 입력 NIC가 같거나, 경로/사용 가능한 주소가 없으면 대리 Reply하지 않는다.
- 직접 연결된 대상은 next hop을 빈칸으로 두며, 간접 경로는 출력 NIC의 subnet에 있는 실제 gateway IP를 지정한다. 다음 홉이 자신의 IP이거나 network/broadcast인 설정은 거부한다.
- 중계 시 원래 IP source/destination, Protocol, ID 및 단편 필드를 보존한다. TTL을 1 줄이고 헤더 체크섬을 다시 계산하며 Ethernet의 source/destination만 출력 링크에 맞게 교체한다.
- 중계 PC 자체의 채팅/파일도 목적지에 맞는 출력 NIC를 사용한다. 자체 생성 패킷의 TTL은 감소시키지 않는다. 반대 NIC로 들어온 자신의 IP 대상 응답은 공유 ChatApp/FileApp으로 로컬 전달하며, 필요한 경우 다른 NIC의 자기 IP도 대리 ARP 응답한다.
- 출력 MAC이 없으면 최대 2 MiB의 유한 큐에서 ARP 해석을 기다린다. 큐는 3초 후 만료되며 늦은 Reply로 만료 데이터를 보내지 않는다.
- TTL 만료 시 ICMP Time Exceeded, 경로 없음/ARP 실패 시 Destination Unreachable을 반환한다. ICMP 오류에 재차 오류를 보내지 않으며 non-first fragment·broadcast 등에 오류를 생성하지 않는다. ICMP 오류는 초당 10개로 제한한다.
- UI에 중계 송신/대기/폐기 수를 표시한다. 송신 수는 Npcap 주입 성공 수이며 종단 수신 ACK가 아니다.

PARP 데모 예시(서로 분리된 두 Ethernet LAN, 중계 PC에는 NIC 두 개 필요):

| 장치/인터페이스 | 프로그램 IP | Mask |
| --- | --- | --- |
| Host A | 192.168.10.1 | 255.255.255.0 |
| 중계 NIC 1 | 192.168.10.254 | 255.255.255.0 |
| 중계 NIC 2 | 192.168.20.254 | 255.255.255.0 |
| Host B | 192.168.20.2 | 255.255.255.0 |

1. 중계 PC에서 `두 NIC를 사용하는 PARP 중계`를 체크하고 서로 다른 실제 NIC/IP/Mask를 지정한다. Host A/B는 단일 NIC로 연결한다. 실습 IP는 다른 장치와 중복되지 않는 주소를 사용한다.
2. 주소 충돌 검사가 끝나면 ARP 화면에서 조회/요청 NIC를 `NIC 1`로 선택하고 Proxy IP `192.168.20.2`, 출력 `NIC 2`, next hop 빈칸으로 Add한다.
3. 조회/요청 NIC를 `NIC 2`로 바꾸고 Proxy IP `192.168.10.1`, 출력 `NIC 1`, next hop 빈칸으로 Add한다.
4. Host A의 상대 IP를 `192.168.20.2`로 지정해 채팅/파일을 보내고, Host B에서도 `192.168.10.1`로 역방향 채팅을 보낸다. 양쪽 링크의 ARP Reply MAC과 IP TTL/주소/Protocol을 비교한다.
5. 수신 파일은 실행 파일 옆 `ReceivedFiles`에서 확인한다. 양 끝에서 원본과 수신 파일 해시를 비교하면 실제 저장 완료를 검증할 수 있다.

### GARP와 주소 충돌 감지(ACD)

GARP는 별도 opcode가 아니라 Sender IP와 Target IP가 같은 broadcast ARP Announcement다. 주소 중복 검사는 GARP 그 자체가 아니라 별도의 ARP Probe 단계로 구현했다(RFC 5227).

- 연결 시 0~1초 무작위 대기 후 SPA=0인 Probe 3개를 1~2초 간격으로 보낸다. 마지막 Probe 뒤 2초간 충돌이 없으면 Announcement 2개를 2초 간격으로 보낸다. UI 타이머의 250ms 해상도만큼 송신 시각이 늦어질 수 있다.
- 첫 Announcement 송신 후 IP 사용을 허용한다. Probe 중 충돌하면 ARP Reply·IP 송수신·중계를 중단하고 UI에 오류를 알린다. Probe 응답의 Target IP가 `0.0.0.0`인 경우도 정상 수신한다.
- 사용 중 다른 MAC이 자신의 IP를 주장하면 GARP 1개로 방어한다. 10초 이내에 다시 충돌하면 주소 사용을 중단한다. 10회 이상의 충돌 후 같은 연결에서 재시도는 60초 간격으로 제한한다.
- 같은 MAC의 수동 GARP는 재광고하며, 다른 MAC은 Probe를 통과한 뒤 raw 앱의 실제 송신 MAC/수신 필터와 광고를 함께 바꾼다. 다른 호스트의 유효한 GARP는 기존 캐시 갱신 및 신규 학습에 반영한다.
- 이 변경은 프로그램의 논리 MAC에만 적용한다. Windows 어댑터 MAC/IP/라우팅 테이블을 바꾸지 않는다. 임의 MAC 수신/송신 허용은 NIC·드라이버·스위치 정책에 달려 있으므로 유선 NIC에서 확인해야 한다. OS가 같은 IP를 사용하면 OS 자체 ARP/ICMP 응답과 raw 앱 동작이 함께 나타날 수 있다.

### 빌드·검증·한계

Npcap SDK 경로는 `NPCAP_SDK_DIR`을 우선 사용하고, 없으면 `C:\NpcapSDK`를 쓴다. 솔루션을 다시 빌드한다. 새 파일은 프로젝트/filters에 등록되어 있다. Wireshark 표시 필터는 `arp || ip.proto == 253 || ip.proto == 254 || icmp`이며, 일반 IP 중계까지 확인하려면 `arp || ip`를 쓴다.

`python tests/run_protocol_tests.py`는 실제 LayerManager/Base/Ethernet/IP/ARP/Router/ChatApp/FileApp 구현을 최소 POSIX 대체 플랫폼에서 컴파일한다. 두 LAN의 PARP 왕복, 일반 Protocol 보존, TTL/체크섬/ICMP, ARP 대기 만료, Probe/Announcement/충돌 방어/MAC 전환, 캐시 만료 및 9개 파일 크기(빈 파일~1 MiB)의 디스크 바이트 일치를 검사한다. AddressSanitizer/UndefinedBehaviorSanitizer를 켠다. FileApp worker는 이 테스트에서 동기 대체되므로 실제 MFC thread scheduling 검증은 아니다. 실제 Windows/MFC 컴파일·두 NIC Npcap 전달·DPI별 UI는 Windows 실습 환경에서 별도 확인해야 한다.

`python tests/check_project_integration.py`는 프로젝트/filters의 파일 등록, 리소스 ID/화면 범위/컨트롤 연결, 기존 `[assignment4]` 주석 보존을 검사한다. 주석 대조에는 git 이력이 필요하다. 새 MAC에 대한 Probe의 unicast Reply 수신, 중계 PC 자체의 양방향 채팅, 간접 next-hop 전달/Proxy 경로 삭제도 프로토콜 테스트에 포함된다.

이 구현은 두 NIC/정적 경로/1500바이트 Ethernet을 위한 실습용 raw IPv4 forwarding이다. 상용 RFC 1812 라우터 전체 기능, NAT, 동적 경로 학습, IPv4 옵션 처리, 로컬 IPv4 단편 재조립은 구현하지 않았다. 중계 IPv4 조각은 각각 전달할 수 있다. 기존 채팅/파일에 ACK나 재전송을 추가한 것은 아니며, 한 번에 처리하는 앱 수신 스트림과 헤더의 제약은 과제 4와 동일하다.

### 작은 화면에서 창 스크롤

- 첫 실행 시 작업 표시줄을 제외한 현재 모니터 영역 안으로 창 크기/위치를 맞춘다. 창 테두리로 크기를 조절하거나 최대화할 수 있다.
- 원래 채팅/파일/ARP/NIC 컨트롤의 배치를 가상 화면으로 유지한다. 창이 작으면 오른쪽 세로 바와 아래 가로 바로 모든 설정까지 이동하며, 창이 충분히 커지면 불필요한 바를 숨긴다.
- 일반 영역의 마우스 휠은 전체 화면을 세로로, Shift+휠은 가로로 이동한다. 채팅/상태 편집창, ARP 표, 어댑터 드롭다운 위에서는 해당 컨트롤의 휠 처리를 유지한다.
- Tab/Shift+Tab으로 화면 밖의 입력칸을 선택하면 자동으로 보이게 한다. ARP 자식 Dialog의 포커스 변경도 기존 UI 타이머가 확인하며, 포커스가 그대로인 동안 사용자가 보는 스크롤 위치를 되돌리지 않는다.
- `ipc2019DlgScroll.cpp`가 창/컨트롤을 이동하고 `DialogScrollLayout.h`가 두 축의 스크롤 범위를 계산한다. 새 구현에는 `[assignment6]` 주석을 달았으며 기존 과제 4 주석/ID를 유지했다.
- 스크롤 시 이전 화면 픽셀을 복사하지 않고, 부모 배경과 자식창/테두리를 즉시 다시 그린다. main의 group box 내부 배경도 지울 수 있도록 `WS_CLIPCHILDREN`을 사용하지 않는다. 이 수정은 화면 잔상에 대한 것으로 프로토콜 동작은 바꾸지 않는다.

`python tests/run_dialog_scroll_tests.py`로 216개의 창 크기/100~250% 가상 배율 조건, 끝까지 접근 가능한 범위, 확대 후 offset 복원, 32비트 thumb, 포커스 표시를 검사한다. 실제 scroll cpp의 선언/구문도 최소 Win32/MFC 선언 대체물에서 검사하여 LONG/int 및 Windows min/max 매크로 충돌을 확인한다. 이 검사는 실제 Windows/MFC 빌드, 배율별 렌더링이나 마우스/키보드 동작을 실행한 검증이 아니므로 Visual Studio에서 다시 빌드하고 작은 화면에서 최종 확인해야 한다.
