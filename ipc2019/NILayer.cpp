#include "pch.h"
#include "NILayer.h"
#include <Packet32.h>
#include <ntddndis.h>

CNILayer::CNILayer(const char* name)
	: CBaseLayer(name), m_handle(NULL), m_thread(NULL), m_stop(TRUE)
{
	memset(m_mac, 0, sizeof(m_mac));
}

CNILayer::~CNILayer()
{
	CloseAdapter();
}

// 어댑터 목록의 name은 pcap_open_live에 사용할 장치 식별자이고,
// description은 사용자가 ComboBox에서 구분하기 위한 표시 문자열이다.
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
		m_adapters.push_back(adapter); // pcap 목록 해제 전에 문자열을 복사한다.
	}
	pcap_freealldevs(devices);
	if (m_adapters.empty()) m_error = _T("사용 가능한 Npcap 어댑터가 없습니다.");
	return !m_adapters.empty();
}

CString CNILayer::GetAdapterName(int index) const
{
	return index >= 0 && index < GetAdapterCount() ? m_adapters[index].description : CString();
}

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

	// 다른 프로토콜의 패킷을 계속 처리하지 않도록 커널 캡처 필터를 설정한다.
	// 목적지와 자기 출발지 주소 검사는 과제 요구대로 Ethernet Layer에서 수행한다.
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

// Packet32의 OID 요청으로 현재 어댑터 MAC을 읽는다. 수동 입력한 가상 주소를
// 쓰지 않으며, 조회한 6바이트를 Ethernet source와 Dialog에 동일하게 전달한다.
BOOL CNILayer::QueryMac(const CStringA& name)
{
	LPADAPTER adapter = PacketOpenAdapter(const_cast<char*>(static_cast<LPCSTR>(name)));
	if (!adapter || adapter->hFile == INVALID_HANDLE_VALUE) {
		if (adapter) PacketCloseAdapter(adapter);
		m_error = _T("Packet32 어댑터 열기 실패");
		return FALSE;
	}
	// PACKET_OID_DATA 끝의 가변 데이터 영역에 MAC 6바이트를 받을 공간을 추가한다.
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

void CNILayer::GetMacAddress(unsigned char* address) const
{
	if (address) memcpy(address, m_mac, sizeof(m_mac));
}

BOOL CNILayer::StartReceive()
{
	if (!m_handle) return FALSE;
	if (m_thread) return TRUE;
	InterlockedExchange(&m_stop, FALSE);
	// suspended 상태에서 m_bAutoDelete를 끈 뒤 시작해야 빠르게 종료되는 스레드가
	// CWinThread 객체를 먼저 해제하는 경쟁을 막고 종료 시 직접 join할 수 있다.
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

BOOL CNILayer::Send(unsigned char* payload, int length)
{
	if (!m_handle || !payload || length < ETHER_HEADER_SIZE || length > ETHER_MAX_SIZE)
		return FALSE;
	// 채팅 송신, 파일 송신, 수신 스레드가 같은 pcap 핸들을 동시에 호출하지 않게 한다.
	CSingleLock lock(&m_pcapLock, TRUE);
	return pcap_sendpacket(m_handle, payload, length) == 0;
}

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
			// pcap 버퍼의 수명은 다음 캡처 호출까지다. lock 안에서 복사한 뒤
			// lock 밖에서 상위 계층을 호출해야 파일 기록 중에도 송신할 수 있다.
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
