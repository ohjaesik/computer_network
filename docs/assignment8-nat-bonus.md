# Assignment 08 NAT: TCP/UDP NAPT와 포트포워딩

NAT I 자료 18쪽의 가산점 두 항목을 기존 ICMP NAPT에 추가했다. 과거 ARP·라우팅·ICMP 소스와 주석은 유지하고 새 기능에는 `[assignment8][nat-bonus]` 주석을 붙였다. 주소와 포트포워딩 규칙은 UI 입력값이며 아래 IP/서비스 포트는 시연 예시다.

## 1. 참고한 상용 NAT의 동작과 구현 범위

Cisco IOS/IOS XE의 공개 NAT 문서에서 **overload(PAT), static TCP/UDP port mapping, 프로토콜별 translation timeout**을 참고했다. Cisco의 비공개 제품 소스 코드를 복사하거나 모든 상용 기능을 재현한 것은 아니다.

- [Cisco IOS XE: NAT for IP Address Conservation](https://www.cisco.com/c/en/us/td/docs/ios-xml/ios/ipaddr_nat/configuration/xe-16/nat-xe-16-book/iadnat-addr-consv.html): 하나의 외부 주소를 TCP/UDP 포트로 공유하는 overload, 내부 endpoint와 외부 endpoint의 정적 포트 매핑, 인터페이스 IP 이용.
- [Cisco: NAT translation timeout](https://www.cisco.com/c/en/us/td/docs/ios-xml/ios/ipaddr/command/ipaddr-cr-book/ipaddr-i4.html): ICMP/UDP/TCP SYN/TCP 연결/FINRST 유휴 시간 구분.
- [RFC 3022 §4.1–4.2](https://www.rfc-editor.org/rfc/rfc3022.html#section-4.1): TCP/UDP 포트 변경, IP 주소를 포함한 pseudo-header 체크섬 갱신, IPv4 UDP checksum=0 보존.
- [RFC 768](https://www.rfc-editor.org/rfc/rfc768.html): UDP 체크섬의 계산 결과가 0일 때 wire에는 0xffff로 전송.
- [Wireshark: checksum offloading](https://wiki.wireshark.org/CaptureSetup/Offloading): 송신 호스트 캡처의 체크섬 표시와 NIC offload의 관계.

외부 포트 `50001..65535`의 순차 배정은 강의 예시에 맞춘 구현 선택이다. TCP와 UDP의 번호 공간은 분리되며 정적 포트포워딩에 예약한 번호는 동적 매핑에 배정하지 않는다. 같은 `(Protocol, Inside IP, Inside Port)`가 다른 외부 서버에 접속하면 외부 포트를 재사용하지만, 원격 IP/Port별 세션과 타이머는 따로 관리한다. 동적 응답은 해당 원격 endpoint에서 온 경우만 내부로 복원한다. 포트포워딩 규칙은 외부에서 시작하는 새 TCP SYN 또는 UDP 요청을 명시적으로 허용한다.

## 2. 수정한 코드

| 파일 / 함수 | 역할 |
| --- | --- |
| `stdafx.h` | TCP/UDP 프로토콜 번호, TCP flags, 포트 배정 범위, 프로토콜별 타이머와 최대 규칙 수를 추가한다. |
| `NatTable.h/.cpp`의 `TrackTransportOutbound()` | 내부 endpoint를 조회하고 동적 외부 포트를 배정한다. 정적 내부 서버의 응답은 등록된 외부 포트를 사용한다. |
| `FindTransportInbound()` / `TrackTransportInbound()` | 외부 IP/목적지 Port와 원격 IP/Port로 역조회한다. 매핑이나 정적 규칙이 없으면 새 내부 연결을 만들지 않는다. 경로와 큐 용량 검사가 끝난 후 세션을 확정한다. |
| `RefreshFlow()` / `Expire()` | TCP SYN, SYN+ACK, 한쪽 FIN, 양쪽 FIN, RST에 따라 수명을 관리한다. UDP와 ICMP는 각 유휴 시간을 사용한다. |
| `AddPortForward()` / `DeletePortForward()` | TCP/UDP별 외부 포트를 내부 서버 IP/Port에 고정한다. 중복 외부 포트 및 동일 내부 endpoint의 모호한 별칭을 거부한다. |
| `NetworkPackets.h`의 `TransportChecksum()` | IPv4 source/destination, Protocol, TCP/UDP 길이의 pseudo header와 전송 헤더/본문을 함께 계산한다. |
| `IPRouter.cpp`의 `ReadTransport()` / `FixTransportChecksum()` | TCP Data Offset/UDP Length, 단편 여부와 수신 체크섬을 검증한다. 변환 후 체크섬을 다시 계산하며 UDP의 0/0xffff 예외를 처리한다. |
| `CIPRouter::Forward()` | Outbound는 경로 선택 후 SNAT, Inbound는 DNAT 후 복원된 목적지로 경로를 선택한다. TTL은 전달당 한 번 감소하고 Ethernet MAC은 다음 홉에 맞춘다. |
| `CIPRouter::ValidatePortForwards()` | Start 시 내부 서버가 Port 1 subnet의 유효한 다른 호스트인지, Port 1으로 가는 활성 경로가 있는지 검사한다. |
| `StaticRouterDlg.h/.cpp`, `ipc2019.rc`, `resource.h` | 메인 NAT 표를 ICMP/TCP/UDP 공용으로 확장하고, `Port Forwarding...` 설정창을 추가한다. |
| `tools/nat_demo.py` | Host A/B에서 실행할 TCP echo, UDP echo, HTTP 서버와 TCP/UDP 검증 클라이언트다. NAT를 대신 수행하지 않는다. |

IP 주소와 TCP/UDP 포트를 바꿔도 TCP Sequence/Acknowledgment, flags/options, 애플리케이션 데이터는 바꾸지 않는다. ARP 미완료 패킷은 변환 완료 상태로 대기하므로 나중에 ARP가 완성되어도 포트 변환이나 TTL 감소를 두 번 수행하지 않는다. `UG` 경로에서는 최종 Host B가 아닌 Cisco의 같은 링크 Gateway에 ARP한다.

| 항목 | 설정 시간 | 갱신/제거 |
| --- | --- | --- |
| ICMP Echo | 60초 | 유효 요청/응답에 갱신 |
| UDP | 300초 | 해당 원격 세션의 송수신에 갱신 |
| TCP SYN 대기 | 60초 | SYN 재전송에도 동일 매핑 사용 |
| TCP SYN+ACK 관측 이후 | 86,400초 | 유효 매핑을 사용하는 송수신에 갱신 |
| TCP 한쪽 FIN | 86,400초 | 반대 방향 송수신을 유지하는 half-close |
| TCP 양쪽 FIN 또는 RST | 60초 | 최종 ACK/재전송을 위한 종료 유예; 늦은 패킷으로 연장하지 않음 |
| 포트포워딩 설정 규칙 | 만료 없음 | 사용자 삭제 또는 프로그램 종료까지 유지 |

TCP 상태 추적은 NAT 타이머용이다. TCP endpoint처럼 Sequence/Window 및 모든 TCP state-machine 규칙을 검증하는 방화벽은 아니다. SYN+ACK를 관측하면 표에 `Established`로 표시한다. 일반 통신용 설정값을 사용하므로 TCP가 연결되자마자 60초 뒤 지워질 것이라고 기대하면 안 된다.

## 3. UI 사용 순서

1. Windows Visual Studio 2026에서 `ipc2019/ipc2019.vcxproj`를 `Release | x64`로 빌드한다. 기존 MFC, v145, Npcap 드라이버, `C:\NpcapSDK` 또는 `NPCAP_SDK_DIR` 설정을 사용한다. 기존 실행 파일이 켜져 있으면 먼저 종료한다.
2. NIC, IP, Netmask와 Routing Table은 [기본 NAT 시연 안내](assignment8-nat-i.md)대로 입력한다. `Port 1 = Inside`, `Port 2 = Outside`다.
3. `NAPT (ICMP/TCP/UDP)`를 체크한다. Outbound TCP/UDP만 시험할 때는 포트포워딩 규칙이 필요 없다.
4. 외부에서 내부 서버로 접속하려면 **Start 전에** `Port Forwarding...`을 누른다. `Protocol`, `Outside Port`, `Inside IP`, `Inside Port`를 입력하고 `Add → Close`를 누른다.
5. `Start` 후 주소 충돌 검사가 끝나면 호스트에서 시험한다. 규칙 수정/삭제는 `End` 후 한다. 고정 규칙과 수동 라우팅 행은 End/Start 사이에 남고, 동적 NAT 세션과 ARP 캐시는 초기화된다. **규칙의 디스크 저장은 구현하지 않았으므로 프로그램을 완전히 종료하면 다시 입력한다.**
6. NAT Table의 `Port / ID`는 TCP/UDP에서 Port, ICMP에서는 Identifier다. `Remote IP:Port`로 외부 세션을 구별하고 `State / Mode`의 `PF`는 포트포워딩 세션을 뜻한다. 설정만 등록하고 패킷이 아직 오지 않았다면 NAT Table에 세션 행은 없다. 고정 규칙은 설정창에서 확인한다.

## 4. Cisco 경유 가산점 데모 입력 예

`Host A — NAT — Cisco — Host B` 배선과 경로는 기본 ICMP 데모와 동일하다. Cisco에는 **Host A 사설망으로 가는 반환 경로를 추가하지 않는다**. Cisco에서 NAT를 켤 필요도 없다. 아래 값은 현장에서 지정한 주소에 맞춰 바꾼다.

| 위치 | IP / Netmask | Default Gateway |
| --- | --- | --- |
| A | `192.168.1.2 / 255.255.255.0` | `192.168.1.1` |
| NAT Port 1 | `192.168.1.1 / 255.255.255.0` | 프로그램 경로표 |
| NAT Port 2 | `10.0.0.2 / 255.255.255.0` | 프로그램 경로표 |
| Cisco NAT 쪽 | `10.0.0.1 / 255.255.255.0` | 직접 연결 |
| Cisco B 쪽 | `172.16.0.1 / 255.255.255.0` | 직접 연결 |
| B | `172.16.0.2 / 255.255.255.0` | `172.16.0.1` |

| NAT Destination | Netmask | Gateway | Flag | Interface | Metric |
| --- | --- | --- | --- | --- | --- |
| `192.168.1.0` | `255.255.255.0` | 비움 | U | Port 1 | 1 |
| `10.0.0.0` | `255.255.255.0` | 비움 | U | Port 2 | 1 |
| `172.16.0.0` | `255.255.255.0` | `10.0.0.1` | UG | Port 2 | 1 |

NAT PC는 기존 과제처럼 raw NIC 두 개를 사용한다. Host A/B의 OS TCP/IPv4는 켜 두고 위 주소와 기본 게이트웨이를 입력한다. 서버를 실행하는 호스트의 Windows 방화벽에서는 **실습 프로필·상대 주소·해당 TCP/UDP 수신 포트**를 허용한다. NAT PC에는 테스트 서버를 띄우지 않는다.

### 4-1. Outbound TCP NAPT

B에서 프로젝트 루트의 터미널로 서버를 실행한다.

```powershell
py tools\nat_demo.py server --protocol tcp --bind 172.16.0.2 --port 8000
```

A에서 실행한다.

```powershell
py tools\nat_demo.py client --protocol tcp --host 172.16.0.2 --port 8000 --size 8193 --count 3
```

클라이언트에 `PASS`가 나오고 B의 `peer=`가 `10.0.0.2:배정포트`인지 확인한다. TCP의 여러 세그먼트에 걸쳐 8,193바이트가 왕복하므로 SYN만 전달되는 상태와 구분할 수 있다. 동적 NAT 표에는 `TCP`, A의 IP/원래 포트, Port 2 IP/변환 포트가 나타난다.

### 4-2. Outbound UDP NAPT

B의 별도 터미널에서:

```powershell
py tools\nat_demo.py server --protocol udp --bind 172.16.0.2 --port 9001
```

A에서:

```powershell
py tools\nat_demo.py client --protocol udp --host 172.16.0.2 --port 9001 --size 257 --count 3
```

UDP는 TCP 연결 수립 없이 데이터그램을 보내고 응답을 확인한다. UDP 서비스가 열려 있지 않으면 응답이 없으므로 반드시 서버부터 실행한다. TCP 성공만으로 UDP 가산점까지 검증됐다고 판단하지 않는다.

### 4-3. 외부 B → 내부 A TCP 포트포워딩

NAT에서 `End → Port Forwarding...`으로 아래 두 규칙을 추가한다. UDP 규칙은 다음 시험에 사용한다.

| Protocol | Outside Port | Inside IP | Inside Port |
| --- | --- | --- | --- |
| TCP | 8080 | 192.168.1.2 | 8000 |
| UDP | 9000 | 192.168.1.2 | 9001 |

`Close → Start` 후 **A**에서 HTTP 서버를 실행한다. 외부 TCP 서비스를 테스트하기 위한 고정 응답 서버이며 로컬 파일을 공개하지 않는다.

```powershell
py tools\nat_demo.py server --protocol http --bind 192.168.1.2 --port 8000
```

**B**에서는 내부 IP가 아닌 **NAT의 Outside IP와 외부 포트**에 접속한다.

```powershell
curl.exe --noproxy "*" http://10.0.0.2:8080/
```

`NAT HTTP demo OK`가 보이면 TCP 접속과 HTTP 응답이 왕복한 것이다. A에서 미리 외부로 요청을 보내 NAT 행을 만들어 놓을 필요가 없다. HTTP 응답의 `peer=`는 실제 외부 B의 IP/포트다. 외부에서 들어오는 요청의 출발지는 보존한다.

| 관측 구간 | 출발지 | 목적지 |
| --- | --- | --- |
| B → Cisco → NAT | `172.16.0.2:클라이언트포트` | `10.0.0.2:8080` |
| NAT → A | `172.16.0.2:클라이언트포트` | `192.168.1.2:8000` |
| A → NAT | `192.168.1.2:8000` | `172.16.0.2:클라이언트포트` |
| NAT → Cisco → B | `10.0.0.2:8080` | `172.16.0.2:클라이언트포트` |

### 4-4. 외부 B → 내부 A UDP 포트포워딩

A에서:

```powershell
py tools\nat_demo.py server --protocol udp --bind 192.168.1.2 --port 9001
```

B에서:

```powershell
py tools\nat_demo.py client --protocol udp --host 10.0.0.2 --port 9000 --size 257 --count 3
```

B의 데이터그램은 `10.0.0.2:9000 → 192.168.1.2:9001`로 목적지가 변환된다. A의 응답은 `192.168.1.2:9001 → 10.0.0.2:9000`로 출발지가 복원된다. B에서 `PASS`와 NAT 표의 `UDP / PF` 행을 확인한다. 미등록 포트로 접속했을 때는 내부로 전달되지 않는다.

HTTP 대신 같은 Python TCP echo로 시험하려면 A에서 `server --protocol tcp --port 8000`을 실행하고 B에서 `client --protocol tcp --host 10.0.0.2 --port 8080`을 사용한다. 같은 주소/포트에 HTTP와 TCP echo 서버 두 개를 동시에 띄우지 않는다.

## 5. 캡처와 제출 증거

- 두 NIC를 동시에 캡처할 수 있으면 NAT PC의 Port 1/2에서 `arp || icmp || tcp || udp`로 확인한다. TCP 시험에는 `tcp.port == 8000 || tcp.port == 8080`, UDP 포트포워딩에는 `udp.port == 9000 || udp.port == 9001`을 사용할 수 있다.
- Outbound는 A의 **source IP/port가 변환**되고 목적지 B는 유지된다. Port forwarding은 Inbound의 **destination IP/port가 변환**되고 출발지 B는 유지된다. Cisco를 한 번 더 지나면 TTL은 라우터마다 한 번씩 감소한다.
- TCP SYN/SYN+ACK/ACK와 데이터/응답을 함께 캡처한다. NAT 표, 서버 `peer=` 출력, 클라이언트 `PASS` 또는 HTTP 응답을 함께 남긴다. Wireshark의 IPv4/TCP/UDP checksum 검증 옵션으로 변환 후 값을 검사할 수 있다. 송신 PC에서 NIC checksum offload 이전에 잡힌 패킷은 표시가 다를 수 있으므로 수신 측 캡처와 대조한다.
- IP/포트 외에 TCP Sequence/Acknowledgment, flags/options, payload가 보존되는지 확인한다. UDP는 원래 checksum=0이면 그대로 0이며, 사용 중인 체크섬의 계산 결과가 0일 때는 0xffff다.

## 6. 검증 결과와 한계

실제 C++ 프로토콜 코드를 Linux의 이식용 플랫폼 대체물과 가상 Ethernet 링크로 실행했다. `python tests/run_protocol_tests.py`에서 직결 TCP/UDP, Cisco 경유 동적 NAPT 및 포트포워딩, Port 1 ARP 대기 후 복원된 목적지의 경로 검증, 원격 IP/포트 필터, 동일 내부 포트 충돌, TCP options/본문 보존, pseudo 체크섬, UDP 0/0xffff, SYN/FIN/RST 및 만료, 규칙 예약/중복, 테이블 한계를 검증한다. 기존 ICMP NAT와 Router/ARP/채팅/파일 회귀 검사도 포함한다.

`python tests/run_nat_demo_smoke.py`는 실제 loopback TCP/UDP/HTTP 소켓으로 시연 도구 자체만 확인한다. 이 결과는 NAT를 통한 실제 OS TCP 연결 검증이 아니다. `python tests/run_router_ui_syntax.py`와 `python tests/check_project_integration.py`는 MFC 선언 대체물을 이용한 구문 및 프로젝트/리소스 연결 검증이다. `check_project_integration.py`의 기존 주석 보존 검사는 Git 이력이 있는 작업본에서 실행한다. **Windows MSBuild, 실제 MFC 화면, Npcap 드라이버 및 Cisco 장비를 통한 실연은 이 환경에서 수행하지 않았다.**

지원 대상은 Ethernet MTU 1500, IPv4 기본 헤더, 단편화되지 않은 ICMP Echo/TCP/UDP다. IPv4 fragment 재조립, NAT를 통과하는 ICMP 오류 메시지 내부 인용 헤더 재작성, hairpin NAT, FTP/SIP ALG, IPv6, NAT 규칙의 디스크 저장은 포함하지 않는다. 테스트 도구는 UDP payload를 1400바이트 이하로 제한하며 큰 TCP 데이터는 호스트 TCP 스택이 세그먼트로 나누어 보낸다. TCP/UDP/ICMP 세션의 합계는 256개, 포트포워딩 규칙은 32개다. 강의자료 NAT II에서 새 필수 조건이 제시되면 그 자료와 추가 대조해야 한다.
