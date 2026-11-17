# Assignment 08 · NAT I

기존 Router II의 두 유선 NIC, 수동 `U`/`UG` 경로, 최장 접두사 선택, 포트 IP에 대한 ARP 응답을 사용한다. Cisco IOS의 NAT overload와 같이 여러 내부 호스트가 하나의 외부 주소를 공유하고, 이 실습에서는 TCP/UDP 포트 대신 ICMP Echo의 16비트 Identifier를 바꾼다. Cisco는 데모에서 **일반 IPv4 라우팅만** 맡으며 NAPT는 이 MFC 프로그램이 수행한다.

| 코드 | 역할 |
| --- | --- |
| `CIPLayer::Receive()` | IPv4 길이/체크섬을 검증하고, Outside IP를 목적지로 하는 Echo Reply도 로컬 폐기 전에 `CIPRouter`에 전달한다. |
| `CIPRouter::Forward()` | Port 1 → Port 2 요청은 기존 경로를 찾은 뒤 출발지 IP와 ID를 변환한다. Port 2의 응답은 목적지 IP와 ID를 복원한 뒤 그 내부 IP로 경로를 조회한다. ICMP 전체 체크섬과 IPv4 헤더 체크섬을 다시 계산하며 TTL은 전달당 한 번만 감소시킨다. |
| `CNatTable` | `(Inside IP, Inside ID)`에 충돌하지 않는 외부 ID를 배정하고 외부 IP/ID로 역조회한다. 같은 내부 값의 매핑을 재사용하며 응답 출발지 IP는 요청 목적지였어야 한다. 유효한 요청/응답이 매핑을 사용하면 유휴 시간을 60초로 갱신한다. |
| `CStaticRouterDlg::RefreshNatTable()` | `Protocol`, `Inside IP`, `Port / ID`, `Outside IP`, `Port / ID`, `Remote IP:Port`, `State / Mode`, `Timer (s)`를 표시한다. ICMP 행에서 Port / ID는 Identifier다. 타이머가 만료된 엔트리는 제거하며 `End`에서도 매핑을 비운다. 수동 라우팅 행은 `End` 후 남는다. |

`UG`에서 ARP 대상은 정적 경로의 Gateway다. 가령 Host B가 Cisco 너머에 있으면 Port 2에서 **Cisco의 MAC**을 학습하고, IP 패킷의 목적지는 그대로 Host B다. Ethernet 출발지 MAC은 출력 포트의 MAC이다. 정상 응답은 입력 포트와 외부 ID가 맞아야 하며, 등록되지 않았거나 만료된 응답은 내부로 전달하지 않는다. 직결 링크 점검을 위한 자체 IP Echo Reply는 NAT 엔트리를 만들지 않는다.

## 빌드와 배선

1. Windows의 Visual Studio에서 `ipc2019/ipc2019.vcxproj`를 열어 `Release | x64`로 빌드한다. 프로젝트는 VS 2026의 `v145` 도구 집합과 C++ MFC·Windows SDK를 사용한다. Npcap 드라이버와 SDK가 필요하다. SDK를 `C:\NpcapSDK`에 두거나 `NPCAP_SDK_DIR`에 SDK 루트를 지정한다.
2. 빌드한 `ipc2019.exe`를 NAT PC에서 실행한다. Host A와 Host B에서는 운영체제의 `ping`만 실행한다. NAT PC에는 서로 다른 실습용 유선 NIC 두 개를 연결한다. 과제의 기존 설정대로 **NAT PC의 실습용 NIC 두 개에서만** Windows TCP/IPv4 바인딩을 끄고, 프로그램의 IP/Mask 입력란에 주소를 넣는다. Host A/B의 TCP/IPv4는 켜 둔다.
3. 화면에서 `Inside Port 1`은 A 쪽 NIC, `Outside Port 2`는 B 또는 Cisco 쪽 NIC를 선택한다. NIC는 MAC 주소/연결 상태를 대조한다. `Static Routing Table → Add`에서 표를 입력하고 `NAPT (ICMP/TCP/UDP)`를 체크한 채 `Start`를 누른다. 양쪽 주소 검사가 끝난 후 시험한다. Proxy ARP 입력은 필요 없다.

## 1. 먼저 직결 시험 (강의자료 예시)

여기서는 Host B가 Port 2와 같은 링크에 있으므로 **B의 기본 게이트웨이를 비워** 시험한다. `10.0.0.0/24`는 실습에서 외부망 역할을 하는 사설 대역이다. 아래 주소는 입력 예시이며, 코드에 고정되어 있지 않다.

| 장비 | IP/마스크 | 기본 게이트웨이 |
| --- | --- | --- |
| Host A | `192.168.1.2/24` | `192.168.1.1` |
| NAT Inside Port 1 | `192.168.1.1/24` | 프로그램 경로표 |
| NAT Outside Port 2 | `10.0.0.2/24` | 프로그램 경로표 |
| Host B | `10.0.0.1/24` | 비움 |

| Destination | Netmask | Gateway | Flag | Interface | Metric |
| --- | --- | --- | --- | --- | --- |
| `192.168.1.0` | `255.255.255.0` | 비움 | `U` | Port 1 | 1 |
| `10.0.0.0` | `255.255.255.0` | 비움 | `U` | Port 2 | 1 |

1. A에서 `ping 192.168.1.1`, B에서 `ping 10.0.0.2`를 실행해 각 직결 링크의 Port IP 응답을 확인한다.
2. A에서 `ping 10.0.0.1`을 실행한다. NAT Table의 Inside IP는 A, Outside IP는 `10.0.0.2`다. Inside ID는 실제 ping 값이고 Outside ID는 별도로 배정된 값이다.
3. B 캡처에서 Echo Request의 출발지가 `10.0.0.2`인지 확인한다. B가 초기 TTL 128로 답하면 A의 Reply TTL은 `127`이다. B에서 `ping 192.168.1.2`는 실패하는 것이 정상이다.
4. ping을 끝낸 후 마지막 유효 패킷부터 60초 뒤 행이 사라지는지 NAT Table에서 확인한다.

## 2. Cisco 1921 경유 데모 (연습 주소)

`Host A — NAT Port 1 / Port 2 — Cisco Gi0/0 / Gi0/1 — Host B`

| 장비 | IP/마스크 | 기본 게이트웨이 |
| --- | --- | --- |
| Host A | `192.168.1.2/24` | `192.168.1.1` |
| NAT Inside Port 1 | `192.168.1.1/24` | 프로그램 경로표 |
| NAT Outside Port 2 | `10.0.0.2/24` | 프로그램 경로표 |
| Cisco Gi0/0 (NAT 쪽) | `10.0.0.1/24` | 두 망은 직접 연결 |
| Cisco Gi0/1 (B 쪽) | `172.16.0.1/24` | 두 망은 직접 연결 |
| Host B | `172.16.0.2/24` | **`172.16.0.1`** |

직결 시험과 달리 **Cisco 데모의 Host B에는 Cisco의 B 쪽 주소를 기본 게이트웨이로 넣는다**. B가 reply를 보내는 목적지 `10.0.0.2`가 B의 서브넷 밖에 있기 때문이다. Cisco 자신에게는 A 쪽 `192.168.1.0/24` 반환 경로를 추가하지 않는다. `show ip interface brief`로 인터페이스 주소와 up 상태를, `show ip route`로 두 직접 연결 경로와 A 쪽 반환 경로의 부재를 확인한다. 실제 데모에선 조교가 지정한 주소와 마스크로 모두 바꾼다.

| NAT의 Destination | Netmask | Gateway | Flag | Interface | Metric |
| --- | --- | --- | --- | --- | --- |
| `192.168.1.0` | `255.255.255.0` | 비움 | `U` | Port 1 | 1 |
| `10.0.0.0` | `255.255.255.0` | 비움 | `U` | Port 2 | 1 |
| `172.16.0.0` | `255.255.255.0` | **`10.0.0.1`** | **`UG`** | **Port 2** | 1 |

1. B가 Cisco 너머에서 온 ICMP Echo Request를 받을 수 있게 Host B의 방화벽 규칙을 실습 프로필/출발지에 맞춘다. A에서 `ping 172.16.0.2`를 실행한다.
2. NAT PC의 Port 2 ARP Cache에 `10.0.0.1`이 `Complete`인지 확인하고, NAT Table에 A와 Port 2 IPv4의 매핑이 나타나는지 확인한다. Cisco는 같은 링크에 있는 `10.0.0.2`로 응답을 돌려준다.
3. NAT의 양쪽 NIC나 A/B에서 `arp || icmp`로 캡처한다. 같은 Sequence Number를 기준으로 TTL, 출발지·목적지 IP, ICMP ID를 대조한다. 예를 들어 **A와 B의 송신 초기 TTL이 128**이면 다음과 같다.

| 캡처 위치와 방향 | 출발지 → 목적지 IP | ICMP | ID | TTL |
| --- | --- | --- | --- | --- |
| A → NAT | `192.168.1.2` → `172.16.0.2` | Request | 원래 ID | 128 |
| NAT → Cisco | `10.0.0.2` → `172.16.0.2` | Request | 변환 ID | 127 |
| Cisco → B | `10.0.0.2` → `172.16.0.2` | Request | 변환 ID | 126 |
| B → Cisco | `172.16.0.2` → `10.0.0.2` | Reply | 변환 ID | 128 |
| Cisco → NAT | `172.16.0.2` → `10.0.0.2` | Reply | 변환 ID | 127 |
| NAT → A | `172.16.0.2` → `192.168.1.2` | Reply | 원래 ID | 126 |

4. NAT 효과를 비교하려면 프로그램에서 `End → NAPT 체크 해제 → Start`를 선택하고 같은 ping을 실행한다. Cisco에 사설망 반환 경로가 없다면 응답이 돌아오지 않는다. 다시 NAT를 켜면 새 엔트리가 생기며 ping 왕복을 확인할 수 있다. IP 헤더 체크섬을 Wireshark로 검증하려면 IPv4 프로토콜 설정에서 체크섬 검사를 켠다.

## 검증 범위

`python tests/run_protocol_tests.py`는 실제 Ethernet/IP/ARP/라우터/NAT C++ 소스를 가상 링크에서 구동한다. 직결 모드와 세 링크의 Cisco 경유 모드, Cisco의 사설망 반환 경로 부재(NAT OFF 실패 / ON 성공), `UG` 다음 홉 ARP와 포트 IP ARP 응답, 양쪽 IP/ICMP ID와 체크섬, 동일 내부 ID 충돌, 미등록/만료 응답, 60초 타이머를 검증한다. `python tests/run_router_ui_syntax.py`는 UI 구문을, Git 작업본에서 실행하는 `python tests/check_project_integration.py`는 프로젝트 등록과 UI 연결 및 기존 주석 보존을 검사한다. Windows MFC 빌드와 물리 Cisco/Npcap 연결 시연은 별도 확인이 필요하다.

강의자료의 NAT I 필수 범위인 **단편화되지 않은 IPv4 ICMP Echo**를 지원한다. 같은 프로젝트에 가산점인 TCP/UDP NAPT와 포트포워딩을 추가했다. 구체적인 설정과 검증은 [가산점 시연 안내](assignment8-nat-bonus.md)를 참고한다. NAT를 끄면 기존 Router II처럼 동작한다.

참고: [Cisco IOS NAT overloading](https://www.cisco.com/c/en/us/td/docs/ios-xml/ios/ipaddr_nat/configuration/xe-2/nat-xe-2-book/iadnat-addr-consv.html), [RFC 5508 §3: ICMP query ID mapping and timeout](https://datatracker.ietf.org/doc/html/rfc5508#section-3).
