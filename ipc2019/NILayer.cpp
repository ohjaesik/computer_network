#include "pch.h"
#include "NILayer.h"
// [assignment4] 선택한 어댑터의 현재 MAC 주소를 Packet32 OID 요청으로 조회하기 위한 API를 포함한다.
#include <Packet32.h>
#include <ntddndis.h>

// [assignment4] 캡처 핸들과 수신 스레드를 미생성 상태로 두고 종료 플래그와 MAC 저장 공간을 초기화한다.
CNILayer::CNILayer(const char* name)
	: CBaseLayer(name), m_handle(NULL), m_thread(NULL), m_stop(TRUE)
{
	memset(m_mac, 0, sizeof(m_mac));
}

// [assignment4] 객체가 사라지기 전에 수신 스레드와 캡처 핸들을 정리한다.
CNILayer::~CNILayer()
{
	CloseAdapter();
}

// [assignment4] 어댑터 목록의 name은 pcap_open_live에 사용할 장치 식별자이고,
// [assignment4] description은 사용자가 ComboBox에서 구분하기 위한 표시 문자열이다.
// [assignment4] pcap_findalldevs로 장치 목록을 가져와 이름과 설명을 복사한 뒤 Npcap 목록 메모리를 해제한다.
BOOL CNILayer::LoadAdapters()
{
	m_adapters.clear();
	pcap_if_t* devices = NULL;
	char error[PCAP_ERRBUF_SIZE] = {};
	if (pcap_findalldevs(&devices, error) < 0) {
		m_error = CA2T(error);
		return FALSE;
	}
	for (pcap_if_t* p = devices; p; p = p->next) {
		if (!p->name) continue;
		ADAPTER adapter;
		adapter.name = p->name;
		adapter.description = CA2T(p->description ? p->description : p->name);
		m_adapters.push_back(adapter); // [assignment4] pcap 목록 해제 전에 문자열을 복사한다.
	}
	pcap_freealldevs(devices);
	if (m_adapters.empty()) m_error = _T("사용 가능한 Npcap 어댑터가 없습니다.");
	return !m_adapters.empty();
}

// [assignment4] 선택 인덱스가 유효할 때 ComboBox에 표시할 어댑터 설명을 반환한다.
CString CNILayer::GetAdapterName(int index) const
{
	return index >= 0 && index < GetAdapterCount() ? m_adapters[index].description : CString();
}

// [assignment4] 선택한 장치를 pcap_open_live로 열고 Ethernet 링크 형식인지 확인한다.
// [assignment4] 채팅/파일 EtherType 필터와 nonblocking 캡처를 설정한 뒤 QueryMac으로 출발지 MAC을 확보한다.
BOOL CNILayer::OpenAdapter(int index)
{
	CloseAdapter();
	if (index < 0 || index >= GetAdapterCount()) {
		m_error = _T("네트워크 어댑터를 선택하십시오.");
		return FALSE;
	}
	char error[PCAP_ERRBUF_SIZE] = {};
	m_handle = pcap_open_live(m_adapters[index].name, 65536, 1, 100, error);
	if (!m_handle) { m_error = CA2T(error); return FALSE; }
	if (pcap_datalink(m_handle) != DLT_EN10MB) {
		m_error = _T("Ethernet 형식을 제공하는 어댑터를 선택하십시오.");
		CloseAdapter();
		return FALSE;
	}

	// [assignment4] 다른 프로토콜의 패킷을 계속 처리하지 않도록 커널 캡처 필터를 설정한다.
	// [assignment4] 목적지와 자기 출발지 주소 검사는 과제 요구대로 Ethernet Layer에서 수행한다.
	bpf_program filter;
	CStringA filterText;
	filterText.Format("ether proto 0x%04x or ether proto 0x%04x", ETHERNET_TYPE_CHAT, ETHERNET_TYPE_FILE);
	if (pcap_compile(m_handle, &filter, filterText,
		1, PCAP_NETMASK_UNKNOWN) < 0) {
		m_error = _T("Npcap 필터 컴파일 실패");
		CloseAdapter(); return FALSE;
	}
	int result = pcap_setfilter(m_handle, &filter);
	pcap_freecode(&filter);
	if (result < 0 || pcap_setnonblock(m_handle, 1, error) < 0) {
		m_error = _T("Npcap 필터 또는 nonblocking 설정 실패");
		CloseAdapter(); return FALSE;
	}
	if (!QueryMac(m_adapters[index].name)) { CloseAdapter(); return FALSE; }
	return TRUE;
}

// [assignment4] Packet32의 OID 요청으로 현재 어댑터 MAC을 읽는다. 수동 입력한 가상 주소를
// [assignment4] 쓰지 않으며, 조회한 6바이트를 Ethernet source와 Dialog에 동일하게 전달한다.
// [assignment4] PacketOpenAdapter로 조회 핸들을 열고 OID_802_3_CURRENT_ADDRESS로 현재 MAC 6바이트를 요청한다.
// [assignment4] PacketRequest의 FALSE는 조회 요청이며, 성공 여부와 반환 길이를 확인한 뒤 핸들을 닫는다.
BOOL CNILayer::QueryMac(const CStringA& name)
{
	LPADAPTER adapter = PacketOpenAdapter(const_cast<char*>(static_cast<LPCSTR>(name)));
	// [assignment4] PacketOpenAdapter는 열기에 실패하면 NULL을 반환하고 내부 자원을 정리한다.
	// [assignment4] ADAPTER의 hFile은 SDK 내부 전용 멤버이므로 직접 검사하지 않는다(C4996).
	// [assignment4] 열기 성공 뒤 MAC 조회의 성공 여부는 아래 PacketRequest 반환값으로 확인한다.
	if (adapter == NULL) {
		m_error = _T("Packet32 어댑터 열기 실패");
		return FALSE;
	}
	// [assignment4] PACKET_OID_DATA 끝의 가변 데이터 영역에 MAC 6바이트를 받을 공간을 추가한다.
	alignas(PACKET_OID_DATA) unsigned char buffer[sizeof(PACKET_OID_DATA) + ETHERNET_ADDRESS_SIZE] = {};
	PPACKET_OID_DATA oid = reinterpret_cast<PPACKET_OID_DATA>(buffer);
	oid->Oid = OID_802_3_CURRENT_ADDRESS;
	oid->Length = ETHERNET_ADDRESS_SIZE;
	BOOL ok = PacketRequest(adapter, FALSE, oid) && oid->Length == ETHERNET_ADDRESS_SIZE;
	if (ok) memcpy(m_mac, oid->Data, sizeof(m_mac));
	PacketCloseAdapter(adapter);
	if (!ok) m_error = _T("Packet32 MAC 주소 조회 실패");
	return ok;
}

// [assignment4] 조회해 둔 MAC 주소를 호출자 버퍼에 복사하여 Source 표시와 Ethernet 출발지 설정에 사용한다.
void CNILayer::GetMacAddress(unsigned char* address) const
{
	if (address) memcpy(address, m_mac, sizeof(m_mac));
}

// [assignment4] NI 계층의 ReadingThread를 별도 작업 스레드로 시작하여 UI가 패킷 수신을 기다리지 않게 한다.
BOOL CNILayer::StartReceive()
{
	if (!m_handle) return FALSE;
	if (m_thread) return TRUE;
	InterlockedExchange(&m_stop, FALSE);
	// [assignment4] suspended 상태에서 m_bAutoDelete를 끈 뒤 시작해야 빠르게 종료되는 스레드가
	// [assignment4] CWinThread 객체를 먼저 해제하는 경쟁을 막고 종료 시 직접 join할 수 있다.
	m_thread = AfxBeginThread(ReadingThread, this, THREAD_PRIORITY_NORMAL, 0, CREATE_SUSPENDED);
	if (!m_thread) {
		InterlockedExchange(&m_stop, TRUE);
		m_error = _T("수신 스레드 생성 실패");
		return FALSE;
	}
	m_thread->m_bAutoDelete = FALSE;
	m_thread->ResumeThread();
	return TRUE;
}

// [assignment4] 종료 플래그를 설정하고 수신 스레드 종료를 기다린 뒤 pcap 핸들을 닫아 사용 중 해제를 막는다.
void CNILayer::CloseAdapter()
{
	InterlockedExchange(&m_stop, TRUE);
	if (m_thread) {
		WaitForSingleObject(m_thread->m_hThread, INFINITE);
		delete m_thread;
		m_thread = NULL;
	}
	if (m_handle) { pcap_close(m_handle); m_handle = NULL; }
}

// [assignment4] Ethernet 프레임 길이를 검사하고 pcap_sendpacket으로 실제 장치에 송신한다.
// [assignment4] 반환값은 로컬 송신 API의 성공 여부이며 상대방의 수신 확인(ACK)은 아니다.
BOOL CNILayer::Send(unsigned char* payload, int length)
{
	if (!m_handle || !payload || length < ETHER_HEADER_SIZE || length > ETHER_MAX_SIZE)
		return FALSE;
	// [assignment4] 채팅 송신, 파일 송신, 수신 스레드가 같은 pcap 핸들을 동시에 호출하지 않게 한다.
	CSingleLock lock(&m_pcapLock, TRUE);
	return pcap_sendpacket(m_handle, payload, length) == 0;
}

// [assignment4] pcap_next_ex로 수신 프레임을 반복 읽고 잘린 프레임을 제외하여 Ethernet 계층에 전달한다.
// [assignment4] 수신 데이터가 없으면 잠시 양보하고, 프레임을 복사한 뒤 잠금을 풀어 상위 계층을 호출한다.
UINT __cdecl CNILayer::ReadingThread(LPVOID parameter)
{
	CNILayer* layer = static_cast<CNILayer*>(parameter);
	while (!InterlockedCompareExchange(&layer->m_stop, 0, 0)) {
		std::vector<unsigned char> frame;
		int result;
		{
			CSingleLock lock(&layer->m_pcapLock, TRUE);
			pcap_pkthdr* header = NULL;
			const unsigned char* data = NULL;
			result = pcap_next_ex(layer->m_handle, &header, &data);
			// [assignment4] pcap 버퍼의 수명은 다음 캡처 호출까지다. lock 안에서 복사한 뒤
			// [assignment4] lock 밖에서 상위 계층을 호출해야 파일 기록 중에도 송신할 수 있다.
			if (result == 1 && header && data && header->caplen == header->len &&
				header->caplen >= ETHER_HEADER_SIZE && header->caplen <= ETHER_MAX_SIZE)
				frame.assign(data, data + header->caplen);
		}
		if (result < 0) break;
		if (result == 0) { Sleep(1); continue; }
		CBaseLayer* upper = layer->GetUpperLayer(0);
		if (upper && !frame.empty()) upper->Receive(frame.data(), static_cast<int>(frame.size()));
	}
	return 0;
}
