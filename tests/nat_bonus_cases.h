#pragma once

// [assignment8][nat-bonus] Production Ethernet/IP/ARP/NAT code runs on simulated links.
// TCP packets model endpoint exchanges; these are not Windows sockets or a physical Cisco test.
namespace NatBonusTests {
using namespace NatTests;
constexpr size_t transportOffset = ETHER_HEADER_SIZE + IP_HEADER_SIZE;

inline uint16_t Word(const unsigned char* p) { return uint16_t((uint16_t(p[0]) << 8) | p[1]); }
inline void Put(unsigned char* p, uint16_t value) { p[0] = uint8_t(value >> 8); p[1] = uint8_t(value); }

inline uint16_t ReferenceChecksum(const std::vector<unsigned char>& frame)
{
    // Build the pseudo header independently of production TransportChecksum's streaming sum.
    const auto* ip = frame.data()+ipOffset;
    const auto* data = frame.data()+transportOffset;
    const size_t count = ip[9] == IP_PROTOCOL_UDP ? Word(data+4) : Word(ip+2)-IP_HEADER_SIZE;
    std::vector<unsigned char> pseudo(ip+12,ip+20);
    pseudo.push_back(0); pseudo.push_back(ip[9]); pseudo.push_back(uint8_t(count >> 8)); pseudo.push_back(uint8_t(count));
    pseudo.insert(pseudo.end(),data,data+count);
    return Checksum(pseudo.data(),pseudo.size());
}
inline void FixTransport(std::vector<unsigned char>& frame, bool noChecksum = false)
{
    const bool udp = frame[ipOffset+9] == IP_PROTOCOL_UDP;
    auto* field = frame.data()+transportOffset+(udp ? 6 : 16); Put(field,0);
    if (udp && noChecksum) return;
    const auto value = ReferenceChecksum(frame);
    Put(field,udp && !value ? 0xffff : value);
}
inline std::vector<unsigned char> Segment(const Host& host, uint32_t destination, const unsigned char* nextMac,
    uint8_t protocol, uint16_t source, uint16_t target, uint8_t flags = 0,
    const std::string& payload = "odd-payload", bool noChecksum = false)
{
    const bool tcp = protocol == IP_PROTOCOL_TCP;
    const size_t header = tcp ? 24 : UDP_HEADER_SIZE; // include TCP MSS option
    const size_t total = IP_HEADER_SIZE+header+payload.size();
    assert(total <= ETHER_MAX_DATA_SIZE);
    std::vector<unsigned char> frame((std::max)(size_t(60),ETHER_HEADER_SIZE+total),0);
    memcpy(frame.data(),nextMac,6); memcpy(frame.data()+6,host.mac,6); frame[12] = 8;
    auto* ip = frame.data()+ipOffset;
    ip[0] = 0x45; Put(ip+2,uint16_t(total)); ip[6] = 0x40; ip[8] = 128; ip[9] = protocol;
    WriteIp(ip+12,host.address); WriteIp(ip+16,destination);
    auto* data = frame.data()+transportOffset;
    Put(data,source); Put(data+2,target);
    if (tcp) {
        data[7] = 100; data[11] = 200; data[12] = 0x60; data[13] = flags; Put(data+14,8192);
        data[20] = 2; data[21] = 4; Put(data+22,1460);
    } else Put(data+4,uint16_t(header+payload.size()));
    memcpy(data+header,payload.data(),payload.size());
    FixTransport(frame,noChecksum); FixIpChecksum(frame); return frame;
}
inline void CheckSegment(const std::vector<unsigned char>& frame, const std::vector<unsigned char>& original,
    uint32_t sourceIp, uint32_t destinationIp, uint16_t sourcePort, uint16_t destinationPort, uint8_t ttl,
    const unsigned char* srcMac, const unsigned char* dstMac)
{
    const auto* ip = frame.data()+ipOffset; const auto* data = frame.data()+transportOffset;
    assert(ReadIp(ip+12) == sourceIp && ReadIp(ip+16) == destinationIp && ip[8] == ttl);
    assert(!memcmp(frame.data(),dstMac,6) && !memcmp(frame.data()+6,srcMac,6));
    assert(Word(data) == sourcePort && Word(data+2) == destinationPort && Checksum(ip,20) == 0);
    const bool udp = ip[9] == IP_PROTOCOL_UDP;
    assert(udp && Word(data+6) == 0 ? true : ReferenceChecksum(frame) == 0);
    auto expected = original;
    memcpy(expected.data(),dstMac,6); memcpy(expected.data()+6,srcMac,6);
    WriteIp(expected.data()+ipOffset+12,sourceIp); WriteIp(expected.data()+ipOffset+16,destinationIp);
    expected[ipOffset+8] = ttl;
    Put(expected.data()+transportOffset,sourcePort); Put(expected.data()+transportOffset+2,destinationPort);
    FixTransport(expected,udp && Word(original.data()+transportOffset+6) == 0); FixIpChecksum(expected);
    assert(frame == expected); // includes Sequence/Ack, MSS options, flags, data and Ethernet padding
}

inline void TestTransportDirect()
{
    for (uint8_t protocol : {IP_PROTOCOL_TCP,IP_PROTOCOL_UDP}) {
        Host a(41,0xc0a80b02), inside(42,0xc0a80b01), outside(43,0x0a280002), b(44,0x0a280001), a2(45,0xc0a80b03);
        CIPRouter router; Configure(router,inside,outside,true);
        const bool tcp = protocol == IP_PROTOCOL_TCP;
        const uint16_t sourcePort = 41000, servicePort = 8000;
        auto request = Segment(a,b.address,inside.mac,protocol,sourcePort,servicePort,tcp ? TCP_FLAG_SYN : 0,"abc");
        Deliver(request,inside); assert(router.GetStats().waiting == 1);
        Pump(outside,b); const auto sent = LastIpv4(outside.ni);
        const uint16_t translated = Word(sent.data()+transportOffset);
        assert(translated >= NAT_TRANSPORT_FIRST_PORT && translated != sourcePort);
        CheckSegment(sent,request,outside.address,b.address,translated,servicePort,127,outside.mac,b.mac);
        auto reply = Segment(b,outside.address,outside.mac,protocol,servicePort,translated,
            tcp ? TCP_FLAG_SYN | TCP_FLAG_ACK : 0,"xyz");
        Deliver(reply,outside); assert(router.GetStats().waiting == 1);
        Pump(a,inside);
        CheckSegment(LastIpv4(inside.ni),reply,b.address,a.address,servicePort,sourcePort,127,inside.mac,a.mac);
        std::vector<CNatTable::ENTRY> rows; router.GetNatEntries(rows,testNow);
        assert(rows.size() == 1 && rows[0].protocol == protocol);
        assert(rows[0].expiresAt == testNow+(tcp ? NAT_TCP_IDLE_MS : NAT_UDP_IDLE_MS));

        // Data, ACK and retransmissions reuse the same mapping and preserve their payload exactly.
        request = Segment(a,b.address,inside.mac,protocol,sourcePort,servicePort,TCP_FLAG_ACK,std::string(1301,'x'));
        Deliver(request,inside); Pump(outside,b);
        CheckSegment(LastIpv4(outside.ni),request,outside.address,b.address,translated,servicePort,127,outside.mac,b.mac);
        reply = Segment(b,outside.address,outside.mac,protocol,servicePort,translated,TCP_FLAG_ACK,std::string(1399,'y'));
        Deliver(reply,outside); Pump(a,inside);
        CheckSegment(LastIpv4(inside.ni),reply,b.address,a.address,servicePort,sourcePort,127,inside.mac,a.mac);

        // Remote IP/port are part of the reply filter. Unsolicited connections cannot use a dynamic binding.
        const auto forwarded = router.GetStats().forwarded;
        auto wrongPort = Segment(b,outside.address,outside.mac,protocol,servicePort+1,translated,TCP_FLAG_ACK);
        Deliver(wrongPort,outside);
        Host stranger(46,b.address+8);
        Deliver(Segment(stranger,outside.address,outside.mac,protocol,servicePort,translated,TCP_FLAG_ACK),outside);
        Deliver(Segment(b,outside.address,outside.mac,protocol,servicePort,49000,TCP_FLAG_SYN),outside);
        Deliver(Segment(b,a.address,outside.mac,protocol,servicePort,sourcePort,TCP_FLAG_SYN),outside);
        assert(router.GetStats().forwarded == forwarded && router.GetStats().waiting == 0);

        // Identical source ports on two private hosts must be distinguishable on the outside link.
        Deliver(Segment(a2,b.address,inside.mac,protocol,sourcePort,servicePort,TCP_FLAG_SYN),inside); Pump(outside,b);
        const auto secondPort = Word(LastIpv4(outside.ni).data()+transportOffset); assert(secondPort != translated);
        auto secondReply = Segment(b,outside.address,outside.mac,protocol,servicePort,secondPort,TCP_FLAG_SYN | TCP_FLAG_ACK);
        Deliver(secondReply,outside); Pump(a2,inside);
        CheckSegment(LastIpv4(inside.ni),secondReply,b.address,a2.address,servicePort,sourcePort,127,inside.mac,a2.mac);

        // Bad checksum, invalid TCP/UDP length, and fragments must not be repaired and forwarded as valid traffic.
        auto broken = request; broken[transportOffset+5] ^= 1; Deliver(broken,inside);
        broken = request;
        if (tcp) broken[transportOffset+12] = 0x10; else Put(broken.data()+transportOffset+4,7);
        Deliver(broken,inside);
        broken = request;
        if (tcp) { broken.resize(60); Put(broken.data()+ipOffset+2,40); broken[transportOffset+12] = 0xf0; }
        else Put(broken.data()+transportOffset+4,1600);
        FixIpChecksum(broken); Deliver(broken,inside);
        broken = request; broken[ipOffset+6] |= 0x20; FixIpChecksum(broken); Deliver(broken,inside);
        broken = request; broken[ipOffset+7] = 1; FixIpChecksum(broken); Deliver(broken,inside);
        assert(router.GetStats().forwarded == forwarded+2);
        if (tcp) {
            Deliver(Segment(a,b.address,inside.mac,protocol,42000,servicePort,TCP_FLAG_ACK),inside);
            assert(router.GetStats().forwarded == forwarded+2); // no midstream new state
        } else {
            auto zero = Segment(a,b.address,inside.mac,protocol,42000,servicePort,0,"zero",true);
            Deliver(zero,inside); Pump(outside,b); auto external = LastIpv4(outside.ni);
            const auto zeroPort = Word(external.data()+transportOffset); assert(Word(external.data()+transportOffset+6) == 0);
            auto response = Segment(b,outside.address,outside.mac,protocol,servicePort,zeroPort,0,"zero",true);
            Deliver(response,outside); Pump(a,inside);
            CheckSegment(LastIpv4(inside.ni),response,b.address,a.address,servicePort,42000,127,inside.mac,a.mac);

            // Force the translated UDP checksum's mathematical result to zero; wire value must be ffff.
            auto special = Segment(a,b.address,inside.mac,protocol,42000,servicePort,0,std::string(2,'\0'));
            WriteIp(special.data()+ipOffset+12,outside.address); Put(special.data()+transportOffset,zeroPort);
            Put(special.data()+transportOffset+6,0);
            const auto adjust = ReferenceChecksum(special);
            Put(special.data()+transportOffset+8,adjust);
            assert(ReferenceChecksum(special) == 0);
            WriteIp(special.data()+ipOffset+12,a.address); Put(special.data()+transportOffset,42000);
            FixTransport(special); FixIpChecksum(special);
            Deliver(special,inside); Pump(outside,b);
            assert(Word(LastIpv4(outside.ni).data()+transportOffset+6) == 0xffff);
        }
    }
}

inline CNatTable::PORT_FORWARD Rule(uint8_t protocol, uint32_t ip, uint16_t external, uint16_t internal)
{
    CNatTable::PORT_FORWARD result;
    result.protocol = protocol; result.insideIp = ip; result.outsidePort = external; result.insidePort = internal; return result;
}

inline void TestPortForwardCisco()
{
    for (uint8_t protocol : {IP_PROTOCOL_TCP,IP_PROTOCOL_UDP}) {
        Host a(51,0xc0a81602), inside(52,0xc0a81601), outside(53,0x0a360002);
        Host near(54,0x0a360001), far(55,0xac1a0401), b(56,0xac1a0402);
        CIPRouter nat, cisco; CString error;
        const bool tcp = protocol == IP_PROTOCOL_TCP;
        const uint16_t publicPort = tcp ? 8080 : 9000, serverPort = tcp ? 8000 : 9001, clientPort = 43210;
        assert(nat.AddPortForward(Rule(protocol,a.address,publicPort,serverPort),error));
        Configure(nat,inside,outside,true); Configure(cisco,near,far,false);
        assert(nat.AddStaticRoute(StaticRoute(b.address&mask,mask,near.address,1,UG),error));
        assert(nat.ValidatePortForwards(error));
        assert(!nat.AddPortForward(Rule(protocol,a.address,publicPort+1,serverPort+1),error)); // edits require End
        auto request = Segment(b,outside.address,far.mac,protocol,clientPort,publicPort,tcp ? TCP_FLAG_SYN : 0,"outside-start");
        Deliver(request,far); Pump(near,outside);
        assert(nat.GetStats().waiting == 1 && ReadIp(inside.ni.frames.front().data()+38) == a.address);
        assert(nat.AddStaticRoute(StaticRoute(0xac1f0000,0xffff0000,near.address,1,UG),error));
        assert(nat.GetStats().waiting == 1); // queue is validated using restored A, not original Outside IP
        Pump(a,inside);
        CheckSegment(LastIpv4(inside.ni),request,b.address,a.address,clientPort,serverPort,126,inside.mac,a.mac);

        // A replies to B's original IP through its default gateway; the NAT restores the advertised public port.
        auto reply = Segment(a,b.address,inside.mac,protocol,serverPort,clientPort,tcp ? TCP_FLAG_SYN | TCP_FLAG_ACK : 0,"server-reply");
        Deliver(reply,inside); Pump(outside,near); Pump(far,b);
        CheckSegment(LastIpv4(far.ni),reply,outside.address,b.address,publicPort,clientPort,126,far.mac,b.mac);
        std::vector<CNatTable::ENTRY> entries; nat.GetNatEntries(entries,testNow);
        assert(entries.size() == 1 && entries[0].portForward && entries[0].remoteIp == b.address);
        // A has not originated any request: external connection creation is exclusively from the configured rule.
        auto data = Segment(b,outside.address,far.mac,protocol,clientPort,publicPort,TCP_FLAG_ACK,std::string(1313,'q'));
        Deliver(data,far); Pump(near,outside); Pump(a,inside);
        CheckSegment(LastIpv4(inside.ni),data,b.address,a.address,clientPort,serverPort,126,inside.mac,a.mac);

        // A second, outbound-initiated session uses dynamic PAT on the same Cisco path, with no private return route.
        auto outbound = Segment(a,b.address,inside.mac,protocol,45000,8081,tcp ? TCP_FLAG_SYN : 0,"dynamic-cisco");
        Deliver(outbound,inside); Pump(outside,near); Pump(far,b);
        const uint16_t allocated = Word(LastIpv4(far.ni).data()+transportOffset);
        assert(allocated >= NAT_TRANSPORT_FIRST_PORT && allocated != publicPort);
        CheckSegment(LastIpv4(far.ni),outbound,outside.address,b.address,allocated,8081,126,far.mac,b.mac);
        auto inbound = Segment(b,outside.address,far.mac,protocol,8081,allocated,tcp ? TCP_FLAG_SYN | TCP_FLAG_ACK : 0,"dynamic-reply");
        Deliver(inbound,far); Pump(near,outside); Pump(a,inside);
        CheckSegment(LastIpv4(inside.ni),inbound,b.address,a.address,8081,45000,126,inside.mac,a.mac);
        const auto forwarded = nat.GetStats().forwarded;
        Deliver(Segment(b,outside.address,far.mac,protocol,clientPort,publicPort+1,TCP_FLAG_SYN),far);
        Pump(near,outside); assert(nat.GetStats().forwarded == forwarded);

        // Changing/removing the Port 1 route discards an unresolved translated packet instead of sending it later.
        inside.arp.DeleteEntry(a.address);
        Deliver(data,far); Pump(near,outside); assert(nat.GetStats().waiting == 1);
        std::vector<CIPRouter::STATIC_ROUTE> routes; nat.GetStaticRoutes(routes);
        for (const auto& row : routes) if (row.outgoing == NAT_INSIDE_PORT) nat.DeleteStaticRoute(row.id);
        assert(nat.GetStats().waiting == 0 && !nat.ValidatePortForwards(error));
        nat.Suspend(); nat.Reset();
        std::vector<CNatTable::PORT_FORWARD> rules; nat.GetPortForwards(rules); assert(rules.size() == 1);
        nat.GetNatEntries(entries,testNow); assert(entries.empty());
        assert(nat.DeletePortForward(rules[0].id,error)); nat.GetPortForwards(rules); assert(rules.empty());
    }
}

inline void TestTransportTable()
{
    CNatTable table; CNatTable::ENTRY first, reply, other;
    const uint32_t inside = 0xc0a80102, outside = 0x0a000002, remote = 0xac100002;
    const uint64_t now = 90000;
    assert(table.AddPortForward(Rule(IP_PROTOCOL_TCP,inside,50001,8000)));
    assert(!table.AddPortForward(Rule(IP_PROTOCOL_TCP,inside+1,50001,8000)));
    assert(!table.AddPortForward(Rule(IP_PROTOCOL_TCP,inside,50002,8000)));
    assert(table.AddPortForward(Rule(IP_PROTOCOL_UDP,inside,50001,8000))); // protocol namespaces are independent
    assert(!table.AddPortForward(Rule(IP_PROTOCOL_ICMP,inside,50003,8001)));
    assert(!table.AddPortForward(Rule(IP_PROTOCOL_UDP,inside,0,8001)));
    assert(!table.TrackTransportOutbound(IP_PROTOCOL_TCP,inside,43000,outside,remote,80,TCP_FLAG_ACK,now,first));
    assert(table.TrackTransportOutbound(IP_PROTOCOL_TCP,inside,43000,outside,remote,80,TCP_FLAG_SYN,now,first));
    assert(first.outsideId == 50002 && first.expiresAt == now+NAT_TCP_SYN_MS);
    assert(table.TrackTransportOutbound(IP_PROTOCOL_TCP,inside,43000,outside,remote,80,TCP_FLAG_SYN,now+1,reply));
    assert(reply.outsideId == first.outsideId); // SYN retransmit
    assert(table.TrackTransportOutbound(IP_PROTOCOL_TCP,inside,43000,outside,remote+1,443,TCP_FLAG_SYN,now,other));
    assert(first.outsideId == other.outsideId); // endpoint-independent mapping with per-remote session expiry
    assert(table.FindTransportInbound(IP_PROTOCOL_TCP,outside,first.outsideId,remote,80,TCP_FLAG_SYN | TCP_FLAG_ACK,now+10,reply));
    assert(table.TrackTransportInbound(reply,TCP_FLAG_SYN | TCP_FLAG_ACK,now+10));
    assert(table.TrackTransportOutbound(IP_PROTOCOL_TCP,inside,43000,outside,remote,80,TCP_FLAG_ACK,now+20,reply));
    assert(reply.tcpState == CNatTable::TCP_STATE::Established && reply.expiresAt == now+20+NAT_TCP_IDLE_MS);
    assert(table.TrackTransportOutbound(IP_PROTOCOL_TCP,inside,43000,outside,remote,80,TCP_FLAG_FIN | TCP_FLAG_ACK,now+30,reply));
    assert(reply.tcpState == CNatTable::TCP_STATE::HalfClosed && reply.expiresAt == now+30+NAT_TCP_IDLE_MS);
    assert(table.TrackTransportInbound(reply,TCP_FLAG_FIN | TCP_FLAG_ACK,now+40));
    assert(table.TrackTransportOutbound(IP_PROTOCOL_TCP,inside,43000,outside,remote,80,TCP_FLAG_ACK,now+50,reply));
    assert(reply.tcpState == CNatTable::TCP_STATE::Closing && reply.expiresAt == now+40+NAT_TCP_CLOSING_MS);
    assert(!table.FindTransportInbound(IP_PROTOCOL_TCP,outside,first.outsideId,remote,80,TCP_FLAG_ACK,reply.expiresAt,other));
    table.Expire(reply.expiresAt); std::vector<CNatTable::ENTRY> rows; table.GetEntries(rows); assert(rows.empty());

    assert(table.TrackTransportOutbound(IP_PROTOCOL_TCP,inside,43000,outside,remote,80,TCP_FLAG_SYN,now,first));
    assert(table.TrackTransportInbound(first,TCP_FLAG_RST,now+1));
    assert(table.FindTransportInbound(IP_PROTOCOL_TCP,outside,first.outsideId,remote,80,TCP_FLAG_ACK,now+2,reply));
    assert(reply.tcpState == CNatTable::TCP_STATE::Closing && reply.expiresAt == now+1+NAT_TCP_CLOSING_MS);
    table.Clear();
    assert(table.FindTransportInbound(IP_PROTOCOL_TCP,outside,50001,remote,44444,TCP_FLAG_SYN,now,reply));
    assert(!table.FindTransportInbound(IP_PROTOCOL_TCP,outside,50001,remote,44444,TCP_FLAG_ACK,now,other));
    assert(table.TrackTransportInbound(reply,TCP_FLAG_SYN,now));
    assert(table.TrackTransportOutbound(IP_PROTOCOL_TCP,inside,8000,outside,remote,44444,TCP_FLAG_SYN | TCP_FLAG_ACK,now,reply));
    assert(reply.outsideId == 50001 && reply.portForward);
    table.Clear();

    assert(table.TrackTransportOutbound(IP_PROTOCOL_UDP,inside,45000,outside,remote,53,0,now,first));
    assert(first.outsideId == 50002 && first.expiresAt == now+NAT_UDP_IDLE_MS);
    assert(!table.FindTransportInbound(IP_PROTOCOL_TCP,outside,first.outsideId,remote,53,TCP_FLAG_ACK,now,reply));
    assert(!table.FindTransportInbound(IP_PROTOCOL_UDP,outside,first.outsideId,remote,54,0,now,reply));
    assert(table.FindTransportInbound(IP_PROTOCOL_UDP,outside,first.outsideId,remote,53,0,now+100,reply));
    assert(table.TrackTransportInbound(reply,0,now+100));
    assert(table.FindTransportInbound(IP_PROTOCOL_UDP,outside,first.outsideId,remote,53,0,now+NAT_UDP_IDLE_MS,reply));
    assert(!table.FindTransportInbound(IP_PROTOCOL_UDP,outside,first.outsideId,remote,53,0,now+100+NAT_UDP_IDLE_MS,reply));
    table.Expire(now+100+NAT_UDP_IDLE_MS); table.GetEntries(rows); assert(rows.empty());
    std::vector<CNatTable::PORT_FORWARD> rules; table.GetPortForwards(rules); assert(rules.size() == 2);
    for (const auto& rule : rules) table.DeletePortForward(rule.id);
    assert(!table.FindTransportInbound(IP_PROTOCOL_UDP,outside,50001,remote,44000,0,now,reply));
    for (int i = 0; i < NAT_TABLE_MAX_ENTRIES; ++i)
        assert(table.TrackTransportOutbound(IP_PROTOCOL_UDP,inside,uint16_t(1000+i),outside,remote,53,0,now,first));
    assert(!table.TrackTransportOutbound(IP_PROTOCOL_UDP,inside,999,outside,remote,53,0,now,first));
    uint16_t id;
    assert(!table.TrackRequest(inside,1,outside,remote,now,id)); // ICMP and transports share the bounded capacity
}

inline void TestRuleValidation()
{
    for (uint32_t invalid : {0xc0a80100u,0xc0a801ffu,0xc0a80101u,0xac100002u}) {
        CIPRouter router; CString error;
        Host inside(61,0xc0a80101), outside(62,0x0a000002);
        assert(router.AddPortForward(Rule(IP_PROTOCOL_TCP,invalid,8080,8000),error));
        Configure(router,inside,outside,true);
        assert(!router.ValidatePortForwards(error)); // network/broadcast/own IP/wrong subnet are caught at Start
    }
}
} // namespace NatBonusTests
