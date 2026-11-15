#pragma once

// [assignment7][static-router] 실제 Ethernet/IP/ARP/Router 소스에 OS ping과 같은 ICMP Echo 패킷을 주입한다.
// [assignment7][static-router] Windows ICMP 스택/Npcap 실장치 실행을 대신했다고 주장하지 않는다.
inline std::vector<unsigned char> PingFrame(const Host& sender, uint32_t destination,
    const unsigned char* gatewayMac, unsigned char type = 8)
{
    std::vector<unsigned char> frame(ETHER_HEADER_SIZE + IP_HEADER_SIZE + 40,0);
    memcpy(frame.data(),gatewayMac,6); memcpy(frame.data()+6,sender.mac,6);
    frame[12] = 8; frame[13] = 0;
    auto* ip = frame.data()+ETHER_HEADER_SIZE;
    ip[0] = 0x45; ip[3] = IP_HEADER_SIZE + 40; ip[4] = 0x12; ip[5] = 0x34;
    ip[6] = 0x40; ip[8] = 128; ip[9] = IP_PROTOCOL_ICMP;
    WriteIp(ip+12,sender.address); WriteIp(ip+16,destination);
    auto* icmp = ip+IP_HEADER_SIZE;
    icmp[0] = type; icmp[4] = 0x56; icmp[5] = 0x78; icmp[7] = 1;
    for (int i = 8; i < 40; ++i) icmp[i] = static_cast<unsigned char>(i+40);
    const uint16_t check = Checksum(icmp,40);
    icmp[2] = static_cast<unsigned char>(check >> 8); icmp[3] = static_cast<unsigned char>(check);
    NetworkPackets::FixIpChecksum(ip,IP_HEADER_SIZE); return frame;
}
inline CIPRouter::STATIC_ROUTE StaticRoute(uint32_t destination, uint32_t mask, uint32_t gateway,
    int output, unsigned int flags = ROUTE_FLAG_UP, unsigned int metric = 1)
{
    CIPRouter::STATIC_ROUTE route;
    route.destination = destination; route.mask = mask; route.gateway = gateway;
    route.outgoing = output; route.flags = flags; route.metric = metric; return route;
}
inline std::vector<unsigned char> LastIpv4(const Wire& wire)
{
    for (auto it = wire.history.rbegin(); it != wire.history.rend(); ++it)
        if (Type(*it) == ETHERNET_TYPE_IPV4) return *it;
    assert(false); return {};
}
// Failed local ARP injection must not discard the original IP packet before its deadline.
inline void TestStaticRouterArpSendRecovery()
{
    for (bool originated : {false,true}) for (bool permanentFailure : {false,true}) {
        Host a(1,0xc0a83c0a), left(10,0xc0a83c01), right(20,0xac141e01), b(2,0xac141e14);
        CIPRouter router; CString error;
        router.SetStaticRouting(true);
        router.BindInterface(0,left.address,0xffffff00,&left.arp,&left.ethernet);
        router.BindInterface(1,right.address,0xffffff00,&right.arp,&right.ethernet);
        left.ip.SetRouter(&router,0); left.arp.SetRouter(&router,0);
        right.ip.SetRouter(&router,1); right.arp.SetRouter(&router,1);
        CIPRouter::STATIC_ROUTE route;
        route.destination = 0xc0a83c00; route.mask = 0xffffff00;
        route.outgoing = 0; route.flags = ROUTE_FLAG_UP;
        assert(router.AddStaticRoute(route,error));
        route.destination = 0xac141e00; route.outgoing = 1;
        assert(router.AddStaticRoute(route,error));
        a.arp.SendRequest(left.address); PumpRouted(a,left,right,b);
        auto request = PingFrame(a,b.address,left.mac);
        right.ni.failArpSends = permanentFailure ? 100 : 1;
        const ULONGLONG started = testNow;
        if (originated) assert(router.SendOriginated(0,request.data()+14,int(request.size())-14,true));
        else Deliver(request,left);
        assert(router.GetStats().waiting == 1 && router.GetStats().dropped == 0);
        assert(right.ni.frames.empty());
        testNow += ARP_REQUEST_INTERVAL_MS;
        router.Tick(testNow);
        if (!permanentFailure) {
            PumpRouted(a,left,right,b);
            assert(router.GetStats().waiting == 0 && router.GetStats().dropped == 0);
            assert(router.GetStats().forwarded == (originated ? 0u : 1u));
            const auto sent = LastIpv4(right.ni);
            assert(sent[22] == (originated ? 128 : 127));
            assert(std::equal(request.begin()+34,request.end(),sent.begin()+34));
            assert(Checksum(sent.data()+14,20) == 0);
        } else {
            assert(router.GetStats().waiting == 1);
            testNow = started + IP_RESOLVE_TIMEOUT_MS;
            router.Tick(testNow);
            assert(router.GetStats().waiting == 0 && router.GetStats().dropped == 1);
            if (!originated) { const auto failure = LastIpv4(left.ni); assert(failure[34] == 3 && failure[35] == 1); }
            right.ni.failArpSends = 0;
            b.arp.SendRequest(right.address); PumpRouted(a,left,right,b);
            assert(router.GetStats().forwarded == 0 && router.GetStats().waiting == 0);
            for (const auto& sent : right.ni.history) assert(Type(sent) != ETHERNET_TYPE_IPV4);
        }
    }
}

inline void TestGatewayArpVirtualMac()
{
    Host a(1,0xc0a8320a), left(10,0xc0a83201), right(20,0x0a0a0114);
    CIPRouter router; CString error; router.SetStaticRouting(true);
    router.BindInterface(0,left.address,0xffffff00,&left.arp,&left.ethernet);
    router.BindInterface(1,right.address,0xffffff00,&right.arp,&right.ethernet);
    left.ip.SetRouter(&router,0); left.arp.SetRouter(&router,0);
    right.ip.SetRouter(&router,1); right.arp.SetRouter(&router,1);
    assert(router.AddStaticRoute(StaticRoute(0xc0a83200,0xffffff00,0,0),error));
    assert(router.AddStaticRoute(StaticRoute(0x0a0a0100,0xffffff00,0,1),error));
    assert(router.AddStaticRoute(StaticRoute(0xac140100,0xffffff00,0x0a0a01fe,1,
        ROUTE_FLAG_UP | ROUTE_FLAG_GATEWAY),error));
    const auto request = PingFrame(a,0xac14010a,left.mac);
    Deliver(request,left);
    assert(router.GetStats().waiting == 1);
    const auto query = right.ni.frames.back();
    assert(Type(query) == ETHERNET_TYPE_ARP && query.size() == 60);
    assert(ReadIp(query.data()+28) == right.address && ReadIp(query.data()+38) == 0x0a0a01fe);
    assert(!memcmp(query.data()+6,right.mac,6) && !memcmp(query.data()+22,right.mac,6));
    const unsigned char broadcast[6] = {255,255,255,255,255,255};
    assert(!memcmp(query.data(),broadcast,6));
    // Independent ARP wire bytes: RFC 9568 8.1.2 uses physical Ethernet source and virtual SHA.
    const unsigned char physical[6] = {0,0x11,0x22,0x33,0x44,0x55};
    const unsigned char virtualMac[6] = {0,0,0x5e,0,1,1};
    std::vector<unsigned char> reply(60,0);
    memcpy(reply.data(),right.mac,6); memcpy(reply.data()+6,physical,6);
    const unsigned char fields[] = {8,6,0,1,8,0,6,4,0,2};
    memcpy(reply.data()+12,fields,sizeof(fields)); memcpy(reply.data()+22,virtualMac,6);
    WriteIp(reply.data()+28,0x0a0a01fe); memcpy(reply.data()+32,right.mac,6); WriteIp(reply.data()+38,right.address);
    for (int kind = 0; kind < 3; ++kind) {
        auto bad = reply; memset(bad.data()+22,kind == 0 ? 255 : 0,6);
        if (kind == 2) bad[22] = 1;
        Deliver(bad,right); unsigned char rejected[6];
        assert(!right.arp.Lookup(0x0a0a01fe,rejected));
        assert(router.GetStats().waiting == 1 && router.GetStats().forwarded == 0);
    }
    Deliver(reply,right);
    unsigned char learned[6]; assert(right.arp.Lookup(0x0a0a01fe,learned) && !memcmp(learned,virtualMac,6));
    const auto sent = LastIpv4(right.ni);
    assert(!memcmp(sent.data(),virtualMac,6) && !memcmp(sent.data()+6,right.mac,6));
    assert(ReadIp(sent.data()+26) == a.address && ReadIp(sent.data()+30) == 0xac14010a);
    assert(sent[22] == 127 && Checksum(sent.data()+14,20) == 0);
    assert(std::equal(request.begin()+34,request.end(),sent.begin()+34));
    assert(router.GetStats().waiting == 0 && router.GetStats().forwarded == 1);
    // The subsequent remote echo reply also routes back across the other port.
    Host remote(30,0xac14010a);
    a.arp.SendRequest(left.address); Pump(a,left);
    auto response = PingFrame(remote,a.address,right.mac,0);
    memcpy(response.data()+6,physical,6); Deliver(response,right);
    const auto returned = LastIpv4(left.ni);
    assert(!memcmp(returned.data(),a.mac,6) && returned[34] == 0 && returned[22] == 127);
    assert(router.GetStats().forwarded == 2);
}

inline void TestStaticRouting()
{
    Host a(1,0xc0a83c0a), left(10,0xc0a83c01), right(20,0xac141e01), b(2,0xac141e14);
    CIPRouter router; CString error;
    router.SetStaticRouting(true); assert(router.IsStaticRouting());
    // [assignment7][static-router] Start 전에 입력한 표를 보관하고 연결 시 실제 NIC subnet과 검증한다.
    assert(router.AddStaticRoute(StaticRoute(0xc0a83c00,0xffffff00,0,0),error));
    assert(!router.ValidateStaticRoutes(error));
    router.BindInterface(0,left.address,0xffffff00,&left.arp,&left.ethernet);
    router.BindInterface(1,right.address,0xffffff00,&right.arp,&right.ethernet);
    left.ip.SetRouter(&router,0); left.arp.SetRouter(&router,0);
    right.ip.SetRouter(&router,1); right.arp.SetRouter(&router,1);
    assert(router.ValidateStaticRoutes(error));
    std::vector<CIPRouter::STATIC_ROUTE> routes; router.GetStaticRoutes(routes);
    router.DeleteStaticRoute(routes[0].id); assert(router.ValidateStaticRoutes(error));
    a.arp.SendRequest(left.address); PumpRouted(a,left,right,b);
    // [assignment7][static-router] 두 NIC를 Bind했어도 빈 수동 표에는 connected route가 생기지 않는다.
    const auto request = PingFrame(a,b.address,left.mac);
    Deliver(request,left);
    assert(router.GetStats().forwarded == 0 && router.GetStats().dropped == 1);
    auto failure = LastIpv4(left.ni); assert(failure[34] == 3 && failure[35] == 0);
    assert(right.ni.history.empty()); PumpRouted(a,left,right,b);
    const unsigned UG = ROUTE_FLAG_UP | ROUTE_FLAG_GATEWAY;
    assert(router.AddStaticRoute(StaticRoute(0xc0a83c00,0xffffff00,left.address,0),error));
    assert(router.AddStaticRoute(StaticRoute(0xac141e00,0xffffff00,0,1),error));
    router.GetStaticRoutes(routes); assert(routes[0].gateway == 0); // G가 없으면 표의 Gateway는 사용하지 않는다.
    assert(!router.AddStaticRoute(StaticRoute(0xac141e00,0xffffff00,0,1),error));
    assert(!router.AddStaticRoute(StaticRoute(0xac141e01,0xffffff00,0,1),error));
    assert(!router.AddStaticRoute(StaticRoute(0xac141e00,0xff00ff00,0,1),error));
    assert(!router.AddStaticRoute(StaticRoute(0xac141e00,0xffffff00,0,2),error));
    assert(!router.AddStaticRoute(StaticRoute(0x0a2c0000,0xffff0000,0,1),error)); // off-link U
    assert(!router.AddStaticRoute(StaticRoute(0x0a2c0000,0xffff0000,a.address,1,UG),error));
    assert(!router.AddStaticRoute(StaticRoute(0x0a2c0000,0xffff0000,right.address,1,UG),error));
    assert(!router.AddStaticRoute(StaticRoute(0x0a2c0000,0xffff0000,0,1,UG),error));
    assert(!router.AddStaticRoute(StaticRoute(0xac141e00,0xffffff00,0,1,ROUTE_FLAG_UP | ROUTE_FLAG_HOST),error));
    assert(!router.AddStaticRoute(StaticRoute(0xac141e00,0xffffff00,0,1,ROUTE_FLAG_UP,65536),error));
    assert(!router.AddStaticRoute(StaticRoute(0xac141e00,0xffffff00,0,1,8),error));
    assert(!router.AddStaticRoute(StaticRoute(0,0xffffffff,b.address,1,UG),error));
    Deliver(request,left);
    assert(router.GetStats().waiting == 1);
    const auto arp = right.ni.frames.front();
    assert(Type(arp) == ETHERNET_TYPE_ARP && ReadIp(arp.data()+38) == b.address);
    PumpRouted(a,left,right,b);
    auto sent = LastIpv4(right.ni);
    assert(!memcmp(sent.data(),b.mac,6) && !memcmp(sent.data()+6,right.mac,6));
    assert(ReadIp(sent.data()+26) == a.address && ReadIp(sent.data()+30) == b.address);
    assert(sent[22] == 127 && sent[23] == IP_PROTOCOL_ICMP && sent[34] == 8);
    assert(Checksum(sent.data()+14,20) == 0 && Checksum(sent.data()+34,40) == 0);
    assert(std::equal(request.begin()+34,request.end(),sent.begin()+34));
    assert(sent[18] == request[18] && sent[19] == request[19]);
    Deliver(PingFrame(b,a.address,right.mac,0),right); PumpRouted(a,left,right,b);
    sent = LastIpv4(left.ni);
    assert(!memcmp(sent.data(),a.mac,6) && !memcmp(sent.data()+6,left.mac,6));
    assert(ReadIp(sent.data()+26) == b.address && ReadIp(sent.data()+30) == a.address && sent[34] == 0 && sent[22] == 127);
    assert(router.GetStats().forwarded == 2 && router.GetStats().waiting == 0);
    std::vector<ARP_CACHE_ENTRY> cache; std::vector<ARP_PROXY_ENTRY> proxies;
    left.arp.GetSnapshot(cache,proxies); assert(proxies.empty());
    right.arp.GetSnapshot(cache,proxies); assert(proxies.empty()); // gateway routing에는 PARP 등록이 필요 없다.
    // [assignment7][static-router] U 경로의 출력이 유입 Port와 같아도 일반 IP routing은 가능하다(PARP의 반대 NIC 제한과 구분).
    Host sameLan(30,0xc0a83c14);
    sameLan.arp.SendRequest(left.address); Pump(sameLan,left);
    Deliver(PingFrame(a,sameLan.address,left.mac),left);
    sent = LastIpv4(left.ni); assert(!memcmp(sent.data(),sameLan.mac,6) && sent[22] == 127);
    left.ni.frames.clear();
    // [assignment7][static-router] 서로 다른 gateway MAC을 먼저 학습한 뒤 /0, /16, /24, /32 및 Metric 우선순위를 확인한다.
    Host gateway2(30,0xac141e1e), gateway3(31,0xac141e1f);
    gateway2.arp.SendRequest(right.address); Pump(gateway2,right);
    gateway3.arp.SendRequest(right.address); Pump(gateway3,right);
    assert(router.AddStaticRoute(StaticRoute(0,0,b.address,1,UG,50),error));
    assert(router.AddStaticRoute(StaticRoute(0x0a2c0000,0xffff0000,b.address,1,UG,1),error));
    assert(router.AddStaticRoute(StaticRoute(0x0a2c0500,0xffffff00,gateway2.address,1,UG,90),error));
    auto expectGateway = [&](uint32_t destination, const unsigned char* mac) {
        Deliver(PingFrame(a,destination,left.mac),left);
        const auto frame = LastIpv4(right.ni);
        assert(ReadIp(frame.data()+30) == destination && !memcmp(frame.data(),mac,6));
        assert(frame[22] == 127 && Checksum(frame.data()+14,20) == 0);
        right.ni.frames.clear();
    };
    expectGateway(0x0a2c0508,gateway2.mac); // more specific /24 wins despite higher Metric
    assert(router.AddStaticRoute(StaticRoute(0x0a2c0508,0xffffffff,gateway3.address,1,UG | ROUTE_FLAG_HOST,100),error));
    expectGateway(0x0a2c0508,gateway3.mac); // host /32 wins
    expectGateway(0x0a2c0608,b.mac); // /16
    expectGateway(0xc0a86308,b.mac); // default /0
    assert(router.AddStaticRoute(StaticRoute(0x0a2c0500,0xffffff00,gateway3.address,1,UG,2),error));
    expectGateway(0x0a2c0509,gateway3.mac); // equal prefix -> lower Metric
    assert(router.AddStaticRoute(StaticRoute(0x0a2c0509,0xffffffff,b.address,1,ROUTE_FLAG_GATEWAY | ROUTE_FLAG_HOST,0),error));
    expectGateway(0x0a2c0509,gateway3.mac); // U absent -> inactive host route ignored
    // UG: missing gateway triggers ARP for that gateway, never for the remote destination.
    const uint32_t firstGateway = 0xac141e62, remoteTarget = 0x0a620009;
    assert(router.AddStaticRoute(StaticRoute(remoteTarget,0xffffffff,firstGateway,1,UG),error));
    Deliver(PingFrame(a,remoteTarget,left.mac),left); assert(router.GetStats().waiting == 1);
    const auto gatewayRequest = right.ni.frames.back(); right.ni.frames.clear();
    assert(Type(gatewayRequest) == ETHERNET_TYPE_ARP && ReadIp(gatewayRequest.data()+38) == firstGateway);
    Host newlyResolved(98,firstGateway); Deliver(gatewayRequest,newlyResolved);
    Deliver(newlyResolved.ni.frames.front(),right);
    sent = LastIpv4(right.ni);
    assert(router.GetStats().waiting == 0 && !memcmp(sent.data(),newlyResolved.mac,6) && ReadIp(sent.data()+30) == remoteTarget);
    right.ni.frames.clear();
    const uint32_t missingGateway = 0xac141e63, target = 0x0a630009;
    assert(router.AddStaticRoute(StaticRoute(target,0xffffffff,missingGateway,1,UG | ROUTE_FLAG_HOST),error));
    Deliver(PingFrame(a,target,left.mac),left); assert(router.GetStats().waiting == 1);
    assert(Type(right.ni.frames.back()) == ETHERNET_TYPE_ARP && ReadIp(right.ni.frames.back().data()+38) == missingGateway);
    router.GetStaticRoutes(routes); const uint32_t pendingRoute = routes.back().id;
    const auto beforeDelete = router.GetStats(); router.DeleteStaticRoute(pendingRoute);
    assert(router.GetStats().waiting == 0 && router.GetStats().dropped == beforeDelete.dropped+1);
    right.ni.frames.clear(); Host late(99,missingGateway);
    late.arp.SendRequest(right.address); Pump(late,right);
    assert(router.GetStats().forwarded == beforeDelete.forwarded); // late ARP cannot transmit old route data
    auto ttl = request; ttl[22] = 1; FixIpChecksum(ttl); Deliver(ttl,left);
    failure = LastIpv4(left.ni); assert(failure[34] == 11 && failure[35] == 0);
    assert(Checksum(failure.data()+14,20) == 0);
    // [assignment7][static-router] 자기 IP에 대한 Echo Reply를 가장하지 않고, 기본 채팅/File 기능은 수동 모드 밖에서 유지한다.
    const size_t ownResponses = left.ni.history.size();
    Deliver(PingFrame(a,left.address,left.mac),left); assert(left.ni.history.size() == ownResponses);
    router.GetStaticRoutes(routes); const size_t count = routes.size();
    router.Suspend(); router.Reset(); router.GetStaticRoutes(routes); assert(routes.size() == count);
    router.BindInterface(0,left.address,0xffff0000,&left.arp,&left.ethernet);
    router.BindInterface(1,0x0a500001,0xffffff00,&right.arp,&right.ethernet);
    assert(!router.ValidateStaticRoutes(error)); // reconnect with a different NIC subnet must reject old gateway settings
}
