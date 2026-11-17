#pragma once

// These tests run the real IP/ARP/NAT layers on in-memory Ethernet links. Host ping is emulated.
namespace NatTests {
constexpr size_t ipOffset = ETHER_HEADER_SIZE;
constexpr size_t icmpOffset = ipOffset + IP_HEADER_SIZE;
constexpr uint32_t mask = 0xffffff00;
constexpr unsigned UG = ROUTE_FLAG_UP | ROUTE_FLAG_GATEWAY;

inline uint16_t Id(const std::vector<unsigned char>& frame)
{
    return static_cast<uint16_t>((frame[icmpOffset+4] << 8) | frame[icmpOffset+5]);
}

inline std::vector<unsigned char> Echo(const Host& host, uint32_t destination, const unsigned char* nextMac,
    uint16_t id, unsigned char type = ICMP_ECHO_REQUEST)
{
    auto frame = PingFrame(host,destination,nextMac,type);
    frame.push_back(0x71); // Odd payload length exercises the last padded checksum word.
    auto* ip = frame.data()+ipOffset;
    ip[3] = static_cast<unsigned char>(frame.size()-ipOffset);
    auto* icmp = frame.data()+icmpOffset;
    icmp[4] = static_cast<unsigned char>(id >> 8); icmp[5] = static_cast<unsigned char>(id);
    icmp[2] = icmp[3] = 0;
    const auto checksum = Checksum(icmp,frame.size()-icmpOffset);
    icmp[2] = static_cast<unsigned char>(checksum >> 8); icmp[3] = static_cast<unsigned char>(checksum);
    FixIpChecksum(frame);
    return frame;
}

inline void Configure(CIPRouter& router, Host& first, Host& second, bool useNat)
{
    router.SetStaticRouting(true); router.SetNatEnabled(useNat);
    router.BindInterface(0,first.address,mask,&first.arp,&first.ethernet);
    first.ip.SetRouter(&router,0); first.arp.SetRouter(&router,0);
    router.BindInterface(1,second.address,mask,&second.arp,&second.ethernet);
    second.ip.SetRouter(&router,1); second.arp.SetRouter(&router,1);
    CString error;
    assert(router.AddStaticRoute(StaticRoute(first.address&mask,mask,0,0),error));
    assert(router.AddStaticRoute(StaticRoute(second.address&mask,mask,0,1),error));
    assert(router.ValidateStaticRoutes(error));
}

inline void CheckEcho(const std::vector<unsigned char>& frame, uint32_t sender, uint32_t destination,
    unsigned char type, uint16_t id, unsigned char ttl, const unsigned char* srcMac, const unsigned char* dstMac)
{
    assert(Type(frame) == ETHERNET_TYPE_IPV4 && frame.size() == icmpOffset+41);
    const unsigned char* ip = frame.data()+ipOffset;
    const unsigned char* icmp = frame.data()+icmpOffset;
    assert(!memcmp(frame.data(),dstMac,6) && !memcmp(frame.data()+6,srcMac,6));
    assert(ReadIp(ip+12) == sender && ReadIp(ip+16) == destination && ip[8] == ttl && ip[9] == IP_PROTOCOL_ICMP);
    assert(icmp[0] == type && icmp[1] == 0 && Id(frame) == id);
    assert(ip[4] == 0x12 && ip[5] == 0x34 && icmp[6] == 0 && icmp[7] == 1 && frame.back() == 0x71);
    assert(Checksum(ip,IP_HEADER_SIZE) == 0 && Checksum(icmp,frame.size()-icmpOffset) == 0);
}

inline void TestDirect()
{
    Host a(11,0xc0a84d02), a2(12,0xc0a84d03), inside(13,0xc0a84d01);
    Host outside(14,0x0a250002), b(15,0x0a250001), other(16,0x0a250004);
    CIPRouter router; Configure(router,inside,outside,true);
    std::vector<CNatTable::ENTRY> entries;
    auto snapshot = [&] { router.GetNatEntries(entries,testNow); };

    // Both port IPs can answer a direct diagnostic ping without creating translation state.
    Deliver(Echo(a,inside.address,inside.mac,1),inside);
    assert(LastIpv4(inside.ni)[icmpOffset] == ICMP_ECHO_REPLY);
    Deliver(Echo(b,outside.address,outside.mac,1),outside);
    assert(LastIpv4(outside.ni)[icmpOffset] == ICMP_ECHO_REPLY);
    snapshot(); assert(entries.empty());
    inside.ni.frames.clear(); outside.ni.frames.clear();

    auto request = Echo(a,b.address,inside.mac,1);
    auto broken = request; broken.back() ^= 1; Deliver(broken,inside);
    auto fragment = request; fragment[ipOffset+6] |= 0x20; FixIpChecksum(fragment); Deliver(fragment,inside);
    auto udp = request; udp[ipOffset+9] = 17; FixIpChecksum(udp); Deliver(udp,inside);
    snapshot(); assert(entries.empty() && outside.ni.frames.empty());

    const auto start = testNow;
    Deliver(request,inside);
    assert(router.GetStats().waiting == 1);
    assert(Type(outside.ni.frames.front()) == ETHERNET_TYPE_ARP && ReadIp(outside.ni.frames.front().data()+38) == b.address);
    Pump(outside,b);
    const auto first = LastIpv4(outside.ni); const uint16_t id = Id(first);
    assert(id == NAT_ICMP_FIRST_ID && IsHostOnSubnet(ReadIp(first.data()+ipOffset+12),b.address,mask));
    CheckEcho(first,outside.address,b.address,ICMP_ECHO_REQUEST,id,127,outside.mac,b.mac);
    // B replies directly to the on-link outside address; no default/private-network route is needed on B.
    Deliver(Echo(b,outside.address,outside.mac,id,ICMP_ECHO_REPLY),outside);
    assert(router.GetStats().waiting == 1);
    assert(Type(inside.ni.frames.front()) == ETHERNET_TYPE_ARP && ReadIp(inside.ni.frames.front().data()+38) == a.address);
    Pump(a,inside);
    CheckEcho(LastIpv4(inside.ni),b.address,a.address,ICMP_ECHO_REPLY,1,127,inside.mac,a.mac);

    // Same original ICMP ID from a second client must use a distinct outside ID and be demultiplexed.
    Deliver(Echo(a2,b.address,inside.mac,1),inside); Pump(outside,b);
    const uint16_t id2 = Id(LastIpv4(outside.ni)); assert(id2 != id && id2 != 1);
    Deliver(Echo(b,outside.address,outside.mac,id2,ICMP_ECHO_REPLY),outside); Pump(a2,inside);
    CheckEcho(LastIpv4(inside.ni),b.address,a2.address,ICMP_ECHO_REPLY,1,127,inside.mac,a2.mac);
    snapshot(); assert(entries.size() == 2 && entries[0].expiresAt == start+NAT_ICMP_IDLE_MS);

    testNow = start + 55000;
    const auto sentInside = inside.ni.history.size();
    Deliver(Echo(other,outside.address,outside.mac,id2,ICMP_ECHO_REPLY),outside); // wrong external source
    Deliver(Echo(b,outside.address,outside.mac,33,ICMP_ECHO_REPLY),outside); // untracked ID
    Deliver(Echo(b,a.address,outside.mac,1),outside); // unsolicited outside-to-inside, even with a route
    assert(inside.ni.history.size() == sentInside);
    Deliver(request,inside); assert(Id(LastIpv4(outside.ni)) == id); // retransmission/next ping refreshes the original row
    testNow = start + NAT_ICMP_IDLE_MS; router.Tick(testNow); snapshot();
    assert(entries.size() == 1 && entries.front().insideIp == a.address);
    const auto forwarded = router.GetStats().forwarded;
    Deliver(Echo(b,outside.address,outside.mac,id2,ICMP_ECHO_REPLY),outside);
    assert(router.GetStats().forwarded == forwarded);

    testNow += 1000;
    Deliver(Echo(b,outside.address,outside.mac,id,ICMP_ECHO_REPLY),outside);
    snapshot(); assert(entries[0].expiresAt == testNow + NAT_ICMP_IDLE_MS);
    testNow += NAT_ICMP_IDLE_MS; router.Tick(testNow); snapshot(); assert(entries.empty());
    const auto expiredForwarded = router.GetStats().forwarded;
    Deliver(Echo(b,outside.address,outside.mac,id,ICMP_ECHO_REPLY),outside);
    assert(router.GetStats().forwarded == expiredForwarded);
    Deliver(request,inside); snapshot(); assert(entries.size() == 1);
    router.Suspend(); router.Reset(); snapshot(); assert(entries.empty());
}

inline void TestCiscoPath()
{
    // Three links; the upstream router has only its two connected routes, no route toward A.
    Host a(21,0xc0a84602), inside(22,0xc0a84601), outside(23,0x0a4a0002);
    Host ciscoNear(24,0x0a4a0001), ciscoFar(25,0xac156401), b(26,0xac156402);
    CIPRouter nat, cisco;
    Configure(nat,inside,outside,false); Configure(cisco,ciscoNear,ciscoFar,false);
    CString error;
    assert(nat.AddStaticRoute(StaticRoute(b.address&mask,mask,ciscoNear.address,1,UG),error));
    const uint16_t insideId = 0x1234;
    const auto outgoing = Echo(a,b.address,inside.mac,insideId);

    Deliver(outgoing,inside); Pump(outside,ciscoNear); Pump(ciscoFar,b);
    CheckEcho(LastIpv4(ciscoFar.ni),a.address,b.address,ICMP_ECHO_REQUEST,insideId,126,ciscoFar.mac,b.mac);
    const auto ciscoForwarded = cisco.GetStats().forwarded;
    Deliver(Echo(b,a.address,ciscoFar.mac,insideId,ICMP_ECHO_REPLY),ciscoFar);
    assert(cisco.GetStats().forwarded == ciscoForwarded && cisco.GetStats().dropped == 1);
    assert(LastIpv4(ciscoFar.ni)[icmpOffset] == 3 && inside.ni.history.empty()); // no return route when NAT is off
    Pump(ciscoFar,b);

    nat.SetNatEnabled(true); outside.arp.DeleteEntry(ciscoNear.address);
    Deliver(outgoing,inside);
    assert(nat.GetStats().waiting == 1);
    assert(Type(outside.ni.frames.front()) == ETHERNET_TYPE_ARP);
    assert(ReadIp(outside.ni.frames.front().data()+38) == ciscoNear.address); // not the remote B IP
    Pump(outside,ciscoNear); Pump(ciscoFar,b);
    const auto externalFrame = LastIpv4(outside.ni);
    const auto translated = Id(externalFrame); assert(translated == NAT_ICMP_FIRST_ID);
    CheckEcho(externalFrame,outside.address,b.address,ICMP_ECHO_REQUEST,translated,127,outside.mac,ciscoNear.mac);
    CheckEcho(LastIpv4(ciscoFar.ni),outside.address,b.address,ICMP_ECHO_REQUEST,translated,126,ciscoFar.mac,b.mac);

    ciscoNear.arp.DeleteEntry(outside.address); // require a real ARP reply for NAT Port 2 on the return leg
    Deliver(Echo(b,outside.address,ciscoFar.mac,translated,ICMP_ECHO_REPLY),ciscoFar);
    assert(cisco.GetStats().waiting == 1 && Type(ciscoNear.ni.frames.front()) == ETHERNET_TYPE_ARP);
    assert(ReadIp(ciscoNear.ni.frames.front().data()+38) == outside.address);
    Pump(outside,ciscoNear);
    CheckEcho(LastIpv4(ciscoNear.ni),b.address,outside.address,ICMP_ECHO_REPLY,translated,127,ciscoNear.mac,outside.mac);
    assert(nat.GetStats().waiting == 1 && ReadIp(inside.ni.frames.front().data()+38) == a.address);
    // An unrelated route edit must validate the de-translated destination, not the original public IP.
    assert(nat.AddStaticRoute(StaticRoute(0xac140000,0xffff0000,ciscoNear.address,1,UG),error));
    assert(nat.GetStats().waiting == 1);
    Pump(a,inside);
    CheckEcho(LastIpv4(inside.ni),b.address,a.address,ICMP_ECHO_REPLY,insideId,126,inside.mac,a.mac);
    assert(nat.GetStats().waiting == 0 && nat.GetStats().forwarded == 3);
}

inline void TestMappingReuse()
{
    CNatTable table;
    CNatTable::ENTRY reply;
    const uint64_t now = 27000;
    uint16_t id, again;
    assert(table.TrackRequest(0xc0a80102,7,0x0a000002,0x0a000003,now,id));
    assert(table.TrackRequest(0xc0a80102,7,0x0a000002,0x0a000004,now+1000,again) && id == again);
    assert(table.FindReply(0x0a000002,id,0x0a000003,now,reply) && reply.insideId == 7);
    assert(!table.FindReply(0x0a000002,id,0x0a000003,now+NAT_ICMP_IDLE_MS,reply));
    assert(table.FindReply(0x0a000002,id,0x0a000004,now+NAT_ICMP_IDLE_MS,reply));
}
} // namespace NatTests
