#include "pch.h"
#include "FileAppLayer.h"

CFileAppLayer::CFileAppLayer(const char* name)
	: CBaseLayer(name), m_window(NULL), m_thread(NULL), m_sending(FALSE),
	m_cancel(FALSE), m_receiving(FALSE), m_total(0), m_nextSequence(0),
	m_received(0), m_lastPercent(-1)
{
	memset(m_sender, 0, sizeof(m_sender));
}

CFileAppLayer::~CFileAppLayer()
{
	StopTransfer();
	ResetReceive();
}

BOOL CFileAppLayer::IsSending() const
{
	return InterlockedCompareExchange(const_cast<volatile LONG*>(&m_sending), 0, 0) != 0;
}

BOOL CFileAppLayer::StartSendFile(const CString& path)
{
	if (IsSending() || path.IsEmpty() || !mp_UnderLayer) return FALSE;
	// 완료된 CWinThread는 자동 삭제하지 않으므로 새 작업 전에 직접 정리한다.
	StopTransfer();
	m_sendPath = path;
	InterlockedExchange(&m_cancel, FALSE);
	InterlockedExchange(&m_sending, TRUE);
	m_thread = AfxBeginThread(FileTransferThread, this, THREAD_PRIORITY_NORMAL, 0, CREATE_SUSPENDED);
	if (!m_thread) {
		InterlockedExchange(&m_sending, FALSE);
		return FALSE;
	}
	m_thread->m_bAutoDelete = FALSE;
	m_thread->ResumeThread();
	return TRUE;
}

void CFileAppLayer::StopTransfer()
{
	InterlockedExchange(&m_cancel, TRUE);
	if (m_thread) {
		WaitForSingleObject(m_thread->m_hThread, INFINITE);
		delete m_thread;
		m_thread = NULL;
	}
	InterlockedExchange(&m_sending, FALSE);
}

// 파일 읽기/단편 전송을 UI와 분리하여 전송 도중에도 채팅 버튼을 처리할 수 있다.
// 완료 알림보다 먼저 sending을 해제하므로 UI에서 재전송할 때 상태가 일치한다.
UINT __cdecl CFileAppLayer::FileTransferThread(LPVOID parameter)
{
	CFileAppLayer* layer = static_cast<CFileAppLayer*>(parameter);
	CString result;
	BOOL ok = layer->SendFile(result);
	InterlockedExchange(&layer->m_sending, FALSE);
	layer->Notify(result, ok ? 100 : 0, TRUE, TRUE);
	return ok ? 0 : 1;
}

BOOL CFileAppLayer::SendFile(CString& result)
{
	CFile file;
	CFileException error;
	if (!file.Open(m_sendPath, CFile::modeRead | CFile::shareDenyWrite, &error)) {
		result = _T("전송 파일을 열지 못했습니다."); return FALSE;
	}
	try {
		ULONGLONG size = file.GetLength();
		// unsigned long이 4바이트인 과제 헤더에 맞춘다. 넘치는 크기를 강제 형변환해
		// 다른 크기로 전송하지 않는다. 파일 전체를 메모리에 올리지 않고 순차로 읽는다.
		if (size > 0xffffffffULL) {
			result = _T("과제의 32비트 길이 필드로는 4 GiB 이상을 표현할 수 없습니다.");
			return FALSE;
		}
		uint32_t total = static_cast<uint32_t>(size);
		int slash = (std::max)(m_sendPath.ReverseFind(_T('\\')), m_sendPath.ReverseFind(_T('/')));
		CString name = m_sendPath.Mid(slash + 1);
		CStringA utf8(CT2A(name, CP_UTF8));
		if (utf8.IsEmpty() || utf8.GetLength() + 1 > FILE_APP_DATA_SIZE) {
			result = _T("전송 파일명이 너무 깁니다."); return FALSE;
		}
		// INFO: 순번 0, 전체 크기, 데이터 종류, UTF-8 파일명(널 종료)을 보낸다.
		if (!SendPacket(FILE_MESSAGE_INFO, total, 0,
			reinterpret_cast<const unsigned char*>(static_cast<LPCSTR>(utf8)), utf8.GetLength() + 1)) {
			result = _T("파일 정보 프레임 송신 실패"); return FALSE;
		}
		Notify(_T("파일 송신 시작: ") + name, 0, TRUE, FALSE);
		unsigned char buffer[FILE_APP_DATA_SIZE];
		uint32_t sequence = 1;
		uint64_t sent = 0;
		int previous = -1;
		for (;;) {
			if (InterlockedCompareExchange(&m_cancel, 0, 0)) {
				result = _T("파일 송신이 중단되었습니다."); return FALSE;
			}
			UINT count = file.Read(buffer, sizeof(buffer));
			if (!count) break;
			if (!SendPacket(FILE_MESSAGE_DATA, total, sequence++, buffer, count)) {
				result = _T("파일 데이터 프레임 송신 실패"); return FALSE;
			}
			sent += count;
			int percent = total ? static_cast<int>(sent * 100 / total) : 100;
			// 패킷마다 알림을 쌓지 않고 표시할 정수 진행률이 바뀔 때만 알린다.
			if (percent != previous) { Notify(_T("파일 송신 중"), percent, TRUE, FALSE); previous = percent; }
			Sleep(1); // 수신/채팅에도 실행 기회를 주고 연속 주입 속도를 낮춘다. ACK 대기는 아니다.
		}
		file.Close();
		if (sent != total || !SendPacket(FILE_MESSAGE_END, total, sequence, NULL, 0)) {
			result = _T("파일 종료 프레임 송신 실패"); return FALSE;
		}
		// pcap_sendpacket 성공은 상대의 파일 저장 확인을 뜻하지 않는다.
		// 이 과제에는 ACK/재전송을 추가하지 않고 수신 측 END 검증 결과를 확인한다.
		result = _T("파일 프레임 송신 완료 (상대 수신 결과 확인 필요)");
		return TRUE;
	} catch (CFileException* exception) {
		exception->Delete();
		file.Abort();
		result = _T("파일을 읽는 중 오류가 발생했습니다.");
		return FALSE;
	}
}

BOOL CFileAppLayer::SendPacket(unsigned char type, uint32_t total, uint32_t sequence,
	const unsigned char* data, int length)
{
	if (!mp_UnderLayer || length < 0 || length > FILE_APP_DATA_SIZE || (length && !data)) return FALSE;
	FILE_APP_HEADER packet = {};
	packet.fapp_totlen = htonl(total);
	packet.fapp_type = htons(FILE_TYPE_BINARY);
	packet.fapp_msg_type = type;
	packet.fapp_seq_num = htonl(sequence);
	if (length) memcpy(packet.fapp_data, data, length);
	return mp_UnderLayer->Send(reinterpret_cast<unsigned char*>(&packet),
		FILE_APP_HEADER_SIZE + length, ETHERNET_TYPE_FILE);
}

BOOL CFileAppLayer::Receive(unsigned char* payload, int length, const unsigned char* source)
{
	if (!payload || !source || length < FILE_APP_HEADER_SIZE || length > ETHER_MAX_DATA_SIZE) return FALSE;
	FILE_APP_HEADER* packet = reinterpret_cast<FILE_APP_HEADER*>(payload);
	if (ntohs(packet->fapp_type) != FILE_TYPE_BINARY) return FALSE;
	int dataLength = length - FILE_APP_HEADER_SIZE;
	if (packet->fapp_msg_type == FILE_MESSAGE_INFO)
		return ReceiveInfo(packet, dataLength, source);
	if (!m_receiving || memcmp(m_sender, source, sizeof(m_sender))) return FALSE;
	if (packet->fapp_msg_type == FILE_MESSAGE_DATA) return ReceiveData(packet, dataLength);
	if (packet->fapp_msg_type == FILE_MESSAGE_END) return ReceiveEnd(packet);
	return FALSE;
}

BOOL CFileAppLayer::ReceiveInfo(FILE_APP_HEADER* packet, int length, const unsigned char* source)
{
	if (ntohl(packet->fapp_seq_num) != 0 || length <= 0) return FALSE;
	const unsigned char* end = static_cast<const unsigned char*>(memchr(packet->fapp_data, 0, length));
	if (!end || end == packet->fapp_data) return FALSE;
	CStringA utf8(reinterpret_cast<const char*>(packet->fapp_data), static_cast<int>(end - packet->fapp_data));
	CString name(CA2T(utf8, CP_UTF8));
	// 패킷의 파일명은 파일명으로만 사용한다. 상대 경로/절대 경로로 저장 폴더를 벗어나지 않는다.
	if (name.FindOneOf(_T("\\/:*?\"<>|")) >= 0 || name == _T(".") || name == _T("..")) return FALSE;
	for (int i = 0; i < name.GetLength(); ++i) if (name[i] < 32) return FALSE;
	if (name.IsEmpty() || name.Right(1) == _T(".") || name.Right(1) == _T(" ")) return FALSE;
	ResetReceive();
	TCHAR module[MAX_PATH] = {};
	DWORD count = GetModuleFileName(NULL, module, MAX_PATH);
	if (!count || count >= MAX_PATH) return FALSE;
	CString directory(module);
	directory = directory.Left(directory.ReverseFind(_T('\\'))) + _T("\\ReceivedFiles");
	if (!CreateDirectory(directory, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) {
		Notify(_T("수신 폴더 생성 실패"), 0, FALSE, TRUE); return FALSE;
	}
	m_finalPath = directory + _T("\\") + name;
	m_partialPath = m_finalPath + _T(".part");
	CFileException error;
	if (!m_receiveFile.Open(m_partialPath, CFile::modeCreate | CFile::modeReadWrite | CFile::shareExclusive, &error)) {
		Notify(_T("수신 파일 생성 실패"), 0, FALSE, TRUE);
		m_partialPath.Empty(); // 열지 못한 다른 작업의 파일을 지우지 않는다.
		ResetReceive(); return FALSE;
	}
	try {
		m_total = ntohl(packet->fapp_totlen);
		m_receiveFile.SetLength(m_total); // 과제 요구: 첫 정보 수신 시 저장 공간 확보
		m_receiveFile.SeekToBegin();
	} catch (CFileException* exception) {
		exception->Delete(); ResetReceive();
		Notify(_T("수신 파일 공간 확보 실패"), 0, FALSE, TRUE); return FALSE;
	}
	m_received = 0;
	m_nextSequence = 1;
	m_lastPercent = -1;
	memcpy(m_sender, source, sizeof(m_sender));
	m_receiving = TRUE;
	Notify(_T("파일 수신 시작: ") + name, 0, FALSE, FALSE);
	return TRUE;
}

BOOL CFileAppLayer::ReceiveData(FILE_APP_HEADER* packet, int length)
{
	// DATA는 항상 최대 크기씩 나누고 마지막 DATA만 짧다. 남은 크기로 padding을 제외한다.
	uint64_t remaining = static_cast<uint64_t>(m_total) - m_received;
	UINT count = static_cast<UINT>((std::min)(remaining, static_cast<uint64_t>(FILE_APP_DATA_SIZE)));
	if (ntohl(packet->fapp_totlen) != m_total || ntohl(packet->fapp_seq_num) != m_nextSequence ||
		!count || length < static_cast<int>(count)) {
		Notify(_T("파일 크기/순서 오류: 미완성 파일 폐기"), 0, FALSE, TRUE);
		ResetReceive(); return FALSE;
	}
	try { m_receiveFile.Write(packet->fapp_data, count); }
	catch (CFileException* exception) {
		exception->Delete(); ResetReceive();
		Notify(_T("수신 파일 기록 실패"), 0, FALSE, TRUE); return FALSE;
	}
	m_received += count;
	++m_nextSequence;
	int percent = static_cast<int>(m_received * 100 / m_total);
	if (percent != m_lastPercent) {
		Notify(_T("파일 수신 중"), percent, FALSE, FALSE);
		m_lastPercent = percent;
	}
	return TRUE;
}

BOOL CFileAppLayer::ReceiveEnd(FILE_APP_HEADER* packet)
{
	// 미리 SetLength했으므로 파일 크기만으로 성공 여부를 판단하면 안 된다.
	// 실제 기록한 누적 길이, total 필드, 다음 순번을 함께 비교한다. 빈 파일도 처리한다.
	if (ntohl(packet->fapp_totlen) != m_total || ntohl(packet->fapp_seq_num) != m_nextSequence || m_received != m_total) {
		Notify(_T("파일 종료 검증 실패: 누락된 조각이 있습니다."), 0, FALSE, TRUE);
		ResetReceive(); return FALSE;
	}
	try { m_receiveFile.Close(); }
	catch (CFileException* exception) {
		exception->Delete(); ResetReceive();
		Notify(_T("수신 파일 닫기 실패"), 0, FALSE, TRUE); return FALSE;
	}
	if (!MoveFileEx(m_partialPath, m_finalPath, MOVEFILE_REPLACE_EXISTING)) {
		Notify(_T("수신 파일 이름 변경 실패"), 0, FALSE, TRUE);
		ResetReceive(); return FALSE;
	}
	CString result = _T("파일 수신 완료: ") + m_finalPath;
	m_partialPath.Empty(); // 완료된 파일은 ResetReceive에서 지우지 않는다.
	ResetReceive();
	Notify(result, 100, FALSE, TRUE);
	return TRUE;
}

void CFileAppLayer::ResetReceive()
{
	// NI 스레드 내부 또는 NI가 종료된 뒤 UI에서만 호출하여 수신 기록과 충돌하지 않는다.
	if (m_receiveFile.m_hFile != CFile::hFileNull) m_receiveFile.Abort();
	if (!m_partialPath.IsEmpty()) DeleteFile(m_partialPath);
	m_partialPath.Empty(); m_finalPath.Empty();
	m_receiving = FALSE; m_total = 0; m_received = 0; m_nextSequence = 0;
	m_lastPercent = -1;
}

void CFileAppLayer::Notify(const CString& message, int percent, BOOL sending, BOOL finished)
{
	if (!m_window) return;
	FILE_STATUS* status = new FILE_STATUS;
	status->message = message; status->percent = percent;
	status->sending = sending; status->finished = finished;
	// 성공 시 UI 핸들러가 delete한다. 전달 실패 시 여기서 해제해 누수를 방지한다.
	if (!::PostMessage(m_window, WM_FILE_STATUS, 0, reinterpret_cast<LPARAM>(status))) delete status;
}
