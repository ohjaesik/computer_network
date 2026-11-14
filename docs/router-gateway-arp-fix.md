# 게이트웨이 ARP 상호운용성과 송신 실패 처리

## 기준과 수정

Cisco IOS 내부 소스가 아니라 공개된 Cisco 동작 설명과 RFC를 기준으로 비교했다.

- [Cisco: Next Hop IP Address and Exit Interface](https://www.cisco.com/c/en/us/support/docs/dial-access/floating-static-route/118263-technote-nexthop-00.html): Ethernet에서 next hop을 지정한 경로는 next hop의 MAC을 해석한다. 기존 UG 경로 선택은 이 방식이므로 유지했다.
- [RFC 9568, 8.1.2](https://www.rfc-editor.org/rfc/rfc9568.html#section-8.1.2): VRRP ARP 응답은 Ethernet source에 물리 MAC, ARP sender hardware address(SHA)에 가상 MAC을 넣는다. 두 주소의 일치를 강제하던 검사를 제거하고 SHA를 학습한다. 두 MAC의 유효한 unicast 여부와 기존 ARP 형식 검사는 유지한다. 일반 Proxy ARP가 언제나 이 불일치를 만든다는 의미는 아니다.
- [RFC 1812, 3.3.2](https://www.rfc-editor.org/rfc/rfc1812.html#section-3.3.2): ARP 해석 중 패킷을 잠시 보관하는 동작을 참고했다. 첫 로컬 ARP 송신 실패에도 패킷을 기존 제한 큐에 남겨 재시도한다. 기존 요청 간격 1초와 패킷 대기 기한 3초를 유지하며, 재시도로 기한을 늘리지 않는다. 로컬 주입 실패 재시도 자체가 RFC의 의무라는 주장은 아니다.
- [Npcap 송신 API](https://npcap.com/guide/wpcap/pcap_inject.html): 송신 실패 시 pcap_geterr 정보를 장치 ID와 프레임 길이와 함께 보존한다. 수신 worker가 종료되면 UI가 이를 감지해 연결을 정리하고 재연결 안내를 표시한다.

Host A의 ARP 요청을 Port 2로 전달하는 구조가 아니다. Host A는 Port 1의 MAC을 찾고, 라우터는 수신한 IP 패킷의 경로를 조회한 뒤 Port 2에서 별도의 게이트웨이 ARP 요청을 생성한다.

## 검증

`python tests/run_protocol_tests.py`와 `python tests/run_router_ui_syntax.py` 통과.

추가 회귀 테스트:

- Port 1 = 192.168.50.1/24, Port 2 = 10.10.1.20/24, 172.20.1.0/24 via 10.10.1.254 구성에서 ARP 대상이 .254인지 확인.
- 독립적으로 구성한 Ethernet source / ARP SHA 불일치 응답을 수신하고, SHA로 next hop을 학습해 양방향 ICMP를 전달하는지 확인.
- 잘못된 SHA(0, multicast, broadcast)는 계속 거부.
- 전달 패킷과 자체 생성 패킷 모두 첫 ARP 송신 실패 후 복구하는지 확인. 지속 실패 시 원래 기한에 폐기하고, 늦은 ARP 응답으로 만료된 패킷을 보내지 않는지 확인.
- 기존 라우팅, TTL/checksum, ICMP, PARP, GARP, 파일 전달 회귀 테스트 유지.

Linux portable 테스트이므로 Windows/MFC 전체 빌드와 실제 Npcap/Cisco 연결을 검증한 것은 아니다. 이 변경은 VRRP 자체를 구현하지 않으며, 기존 static-router 모드의 라우터 자체 IP에 대한 Echo Reply 미지원도 변경하지 않는다.

## 실제 장비 확인

새 브랜치를 Windows에서 빌드한 뒤 기존 표로 실행한다. Port 2에서 송신 ARP의 SPA=10.10.1.20, TPA=10.10.1.254를 확인하고 응답의 Ethernet source와 SHA를 비교한다. ARP 캐시에 .254가 학습되고 IP 프레임의 Ethernet 목적지가 해당 SHA가 되는지 확인한다.

기존 `ARP Request 송신 실패`는 응답 timeout이나 MAC 불일치 수신 거부가 아니라 로컬 송신 경로의 실패를 뜻한다. 따라서 MAC 검사 수정만으로 보고된 현상의 원인이 확정되거나 해결됐다고 단정할 수 없다. 같은 메시지가 발생하면 새로 표시되는 `최근 NIC 오류`의 pcap 오류와 장치 ID를 확인해야 한다.
