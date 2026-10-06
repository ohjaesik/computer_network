#pragma once
#include <cstdint>
#include <cstring>

// [assignment6] 패킷의 바이트 배치와 체크섬 계산만 담는다. 프로토콜 상수는 stdafx.h에 둔다.
// [assignment6] C++ bit-field는 컴파일러에 따라 배치가 달라지므로 version/IHL도 한 바이트로 표현한다.
#pragma pack(push, 1)
struct IPV4_HEADER {
    uint8_t versionIhl;
    uint8_t tos;
    uint16_t totalLength;
    uint16_t identification;
    uint16_t flagsOffset;
    uint8_t ttl;
    uint8_t protocol;
    uint16_t checksum;
    uint8_t source[4];
    uint8_t destination[4];
};
struct ARP_PACKET {
    uint16_t hardwareType;
    uint16_t protocolType;
    uint8_t hardwareLength;
    uint8_t protocolLength;
    uint16_t operation;
    uint8_t senderMac[6];
    uint8_t senderIp[4];
    uint8_t targetMac[6];
    uint8_t targetIp[4];
};
#pragma pack(pop)
static_assert(sizeof(IPV4_HEADER) == 20, "IPv4 base header must be 20 bytes");
static_assert(sizeof(ARP_PACKET) == 28, "Ethernet/IPv4 ARP must be 28 bytes");

namespace NetworkPackets {
    // [assignment6] 바이트 배열을 big-endian 수치로 읽는다. 주소 키는 항상 host-order 수치로 관리한다.
    inline uint32_t ReadIp(const uint8_t* p) {
        return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
    }
    inline void WriteIp(uint8_t* p, uint32_t ip) {
        p[0] = uint8_t(ip >> 24); p[1] = uint8_t(ip >> 16); p[2] = uint8_t(ip >> 8); p[3] = uint8_t(ip);
    }
    // [assignment6] 체크섬 필드를 0으로 놓고 계산한 결과를 htons로 저장한다.
    // [assignment6] 수신 헤더 전체(기존 체크섬 포함)의 계산 결과가 0이면 체크섬이 유효하다.
    inline uint16_t Checksum(const uint8_t* data, size_t length) {
        uint32_t sum = 0;
        while (length >= 2) { sum += (uint16_t(data[0]) << 8) | data[1]; data += 2; length -= 2; }
        if (length) sum += uint16_t(data[0]) << 8;
        while (sum >> 16) sum = (sum & 0xffff) + (sum >> 16);
        return uint16_t(~sum);
    }
    inline bool IsUnicastIp(uint32_t ip) {
        const uint8_t first = uint8_t(ip >> 24);
        return first != 0 && first != 127 && first < 224;
    }
    inline bool IsUnicastMac(const uint8_t* mac) {
        const uint8_t zero[6] = {};
        return mac && !(mac[0] & 1) && std::memcmp(mac, zero, 6) != 0;
    }
    // [assignment6] /1~ /30의 연속된 subnet mask만 지원한다. /31 point-to-point는 실습 범위 밖이다.
    inline bool IsSubnetMask(uint32_t mask) {
        const uint32_t inverse = ~mask;
        return mask && inverse >= 3 && (inverse & (inverse + 1)) == 0;
    }
    inline bool IsHostOnSubnet(uint32_t ip, uint32_t localIp, uint32_t mask) {
        return IsUnicastIp(ip) && IsSubnetMask(mask) && (ip & mask) == (localIp & mask) &&
            (ip & ~mask) != 0 && (ip & ~mask) != ~mask;
    }
    inline void FixIpChecksum(uint8_t* packet, size_t headerLength) {
        packet[10] = packet[11] = 0;
        const uint16_t checksum = Checksum(packet, headerLength);
        packet[10] = uint8_t(checksum >> 8); packet[11] = uint8_t(checksum);
    }
}
