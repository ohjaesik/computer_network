#include "LayerManager.h"
#include "EthernetLayer.h"
#include "IPLayer.h"
#include "ARPLayer.h"
#include "ChatAppLayer.h"
#include "IPRouter.h"
#include "FileAppLayer.h"
#include <deque>
#include <iostream>

using namespace NetworkPackets;

struct Wire : CBaseLayer {
    std::deque<std::vector<unsigned char>> frames;
    std::vector<std::vector<unsigned char>> history;
    Wire() : CBaseLayer("NI") {}
    BOOL Send(unsigned char* data, int length) override {
        assert(length >= 60 && length <= 1514);
        frames.emplace_back(data, data + length); history.push_back(frames.back()); return TRUE;
    }
};
struct Sink : CBaseLayer {
    std::vector<std::vector<unsigned char>> messages;
    unsigned char lastSource[6] = {};
    explicit Sink(const char* name) : CBaseLayer(name) {}
    BOOL Receive(unsigned char* data, int length, const unsigned char* source = nullptr) override {
        messages.emplace_back(data, data + length);
        if (source) memcpy(lastSource, source, 6);
        return TRUE;
    }
};
struct Host {
    Wire ni;
    CEthernetLayer ethernet{"Ethernet"};
    CIPLayer ip{"IP"};
    CARPLayer arp{"ARP"};
    CChatAppLayer chat{"ChatApp"};
    Sink file{"FileApp"}, ui{"ChatDlg"};
    CLayerManager manager;
    uint32_t address;
    unsigned char mac[6] = {0x02,0,0,0,0,0};
    explicit Host(unsigned char id, uint32_t ipAddress = 0, CBaseLayer* fileLayer = nullptr) : address(ipAddress ? ipAddress : (0xc0a80a00u | id)) {
        mac[5] = id;
        CBaseLayer* actualFile = fileLayer ? fileLayer : &file;
        for (CBaseLayer* layer : std::vector<CBaseLayer*>{&ni,&ethernet,&ip,&arp,&chat,actualFile,&ui}) manager.AddLayer(layer,FALSE);
        manager.ConnectLayers("NI ( *Ethernet ( *IP ( *ChatApp ( *ChatDlg ) *FileApp ( +ChatDlg ) ) *ARP ) )");
        assert(chat.GetUnderLayer() == &ip && actualFile->GetUnderLayer() == &ip);
        assert(ip.GetUnderLayer() == &ethernet && arp.GetUnderLayer() == &ethernet);
        assert(ui.GetUnderLayer() == &chat);
        ethernet.SetSourceAddress(mac); ip.Configure(address, &arp); arp.Configure(address, mac, &ip);
        ethernet.SetProtocolLayers(&ip,&arp);
    }
    ~Host() { manager.DeAllocLayer(); }
};
void Deliver(const std::vector<unsigned char>& input, Host& host) {
    auto frame = input; host.ethernet.Receive(frame.data(), int(frame.size()));
}
void Pump(Host& a, Host& b) {
    int limit = 1000;
    while ((!a.ni.frames.empty() || !b.ni.frames.empty()) && --limit) {
        for (Host* sender : {&a, &b}) {
            if (sender->ni.frames.empty()) continue;
            auto frame = sender->ni.frames.front(); sender->ni.frames.pop_front();
            Deliver(frame,a); Deliver(frame,b); // include outgoing capture to test self filtering
        }
    }
    assert(limit);
}
// [assignment6] 서로 다른 LAN 두 개를 연결한다. 같은 링크에서만 broadcast를 전달한다.
void PumpRouted(Host& a, Host& left, Host& right, Host& b) {
    int limit = 10000;
    while ((!a.ni.frames.empty() || !left.ni.frames.empty() || !right.ni.frames.empty() || !b.ni.frames.empty()) && --limit) {
        for (auto link : {std::make_pair(&a,&left),std::make_pair(&right,&b)}) {
            for (Host* sender : {link.first,link.second}) if (!sender->ni.frames.empty()) {
                auto frame = sender->ni.frames.front(); sender->ni.frames.pop_front();
                Deliver(frame,*link.first); Deliver(frame,*link.second);
            }
        }
    }
    assert(limit);
}
void FinishAddressCheck(Host& changing, Host& peer) {
    for (int i = 0; i < 48; ++i) {
        testNow += 250; changing.arp.Tick(testNow); peer.arp.Tick(testNow); Pump(changing,peer);
    }
    assert(changing.arp.GetAddressState() == CARPLayer::ADDRESS_STATE::Ready);
}
unsigned short Type(const std::vector<unsigned char>& frame) { return (frame[12] << 8) | frame[13]; }
void FixIpChecksum(std::vector<unsigned char>& frame) {
    frame[24] = frame[25] = 0;
    const uint16_t checksum = Checksum(frame.data()+14,20);
    frame[24] = static_cast<unsigned char>(checksum >> 8); frame[25] = static_cast<unsigned char>(checksum);
}

int main() {
    static_assert(CHAT_APP_DATA_SIZE == 1476 && FILE_APP_DATA_SIZE == 1468, "IP MTU budgets");
    {
        Host a(1), b(2); assert(a.ip.SetDestination(b.address));
        std::string text; for (int i=0;i<1000;++i) text += "\xec\x95\x88";
        assert(a.chat.Send(reinterpret_cast<unsigned char*>(&text[0]), int(text.size())));
        assert(a.ni.frames.size()==1 && Type(a.ni.frames.front())==0x0806);
        const auto request=a.ni.frames.front();
        assert(request.size()==60 && request[20]==0 && request[21]==1);
        assert(ReadIp(request.data()+28)==a.address && ReadIp(request.data()+38)==b.address);
        for(int i=0;i<6;++i) assert(request[i]==255 && request[32+i]==0);
        Pump(a,b);
        assert(b.ui.messages.size()==1);
        assert(std::string(b.ui.messages[0].begin(),b.ui.messages[0].end())==text);
        assert(ReadIp(b.ui.lastSource)==a.address);
        unsigned char mac[6]; assert(a.arp.Lookup(b.address,mac) && !memcmp(mac,b.mac,6));
        assert(b.arp.Lookup(a.address,mac) && !memcmp(mac,a.mac,6));
        // IP removes Ethernet padding before handing a short message to ChatApp.
        unsigned char shortText='A'; assert(a.chat.Send(&shortText,1));
        auto frame=a.ni.frames.front(); assert(frame.size()==60 && Type(frame)==0x0800 && frame[23]==253);
        assert(frame[16]==0 && frame[17]==25 && Checksum(frame.data()+14,20)==0);
        Pump(a,b); assert(b.ui.messages.back()==std::vector<unsigned char>{'A'});
        // A full file-app payload uses Protocol 254 and does not enter ChatApp.
        std::vector<unsigned char> data(IP_MAX_DATA_SIZE,0x5a);
        assert(a.ip.Send(data.data(),int(data.size()),IP_PROTOCOL_FILE));
        frame=a.ni.frames.front(); a.ni.frames.pop_front();
        assert(frame.size()==1514 && frame[23]==254 && Type(frame)==0x0800);
        Deliver(frame,b); assert(b.file.messages.size()==1 && b.file.messages[0]==data && b.ui.messages.size()==2);
        assert(!a.ip.Send(data.data(),IP_MAX_DATA_SIZE+1,IP_PROTOCOL_FILE));
        // Corrupted/truncated/fragmented/wrong-destination packets must never reach the apps.
        auto corrupt=frame; corrupt[22]^=1; Deliver(corrupt,b);
        corrupt=frame; corrupt.resize(32); Deliver(corrupt,b);
        corrupt=frame; corrupt[20]=0x20; FixIpChecksum(corrupt); Deliver(corrupt,b);
        corrupt=frame; corrupt[14]=0x46; FixIpChecksum(corrupt); Deliver(corrupt,b);
        corrupt=frame; corrupt[33]=99; FixIpChecksum(corrupt); Deliver(corrupt,b);
        corrupt=frame; corrupt[23]=6; FixIpChecksum(corrupt); Deliver(corrupt,b);
        corrupt=frame; corrupt[22]=0; FixIpChecksum(corrupt); Deliver(corrupt,b);
        assert(b.file.messages.size()==1);
        // Receiver may update an existing mapping from a GARP announcement.
        const unsigned char advertised[6]={0x02,0x11,0x22,0x33,0x44,0x55};
        assert(a.arp.SendGratuitous(advertised));
        assert(!a.arp.IsUsable()); FinishAddressCheck(a,b);
        auto garp=a.ni.history.back();
        assert(Type(garp)==0x0806 && garp[21]==1 && ReadIp(garp.data()+28)==ReadIp(garp.data()+38));
        assert(!memcmp(garp.data()+6,advertised,6) && !memcmp(garp.data()+22,advertised,6));
        assert(b.arp.Lookup(a.address,mac) && !memcmp(mac,advertised,6));
        // Advertising a logical MAC must change subsequent transmission and receive filtering too.
        unsigned char effective[6]; a.arp.GetEffectiveMac(effective); assert(!memcmp(effective,advertised,6));
        assert(a.chat.Send(&shortText,1)); assert(!memcmp(a.ni.frames.back().data()+6,advertised,6));
        Pump(a,b); b.ip.SetDestination(a.address); assert(b.chat.Send(&shortText,1)); Pump(a,b);
        assert(a.ui.messages.size()==1);
        // Bad ARP header lengths and Ethernet/SHA disagreement are rejected.
        garp[18]=5; Deliver(garp,b); garp[18]=6; garp[22]^=2; Deliver(garp,b);
        assert(b.arp.Lookup(a.address,mac) && !memcmp(mac,advertised,6));
        testNow+=ARP_COMPLETE_TIMEOUT_MS; a.arp.Tick(testNow); b.arp.Tick(testNow);
        assert(!a.arp.Lookup(b.address,mac) && !b.arp.Lookup(a.address,mac));
    }
    {
        Host a(1), proxy(2); const uint32_t target=0xc0a80a63;
        unsigned char mac[6];
        assert(!proxy.arp.AddProxy(target,_T("NIC 1"))); // no real egress route: must not black-hole traffic
        assert(a.arp.SendRequest(target,true)); Pump(a,proxy); assert(!a.arp.Lookup(target,mac));
        std::vector<ARP_CACHE_ENTRY> entries; std::vector<ARP_PROXY_ENTRY> proxies;
        a.arp.GetSnapshot(entries,proxies); assert(entries.size()==1 && !entries[0].complete);
        testNow+=ARP_INCOMPLETE_TIMEOUT_MS; a.arp.Tick(testNow);
        a.arp.GetSnapshot(entries,proxies); assert(entries.empty());
    }
    {
        Host a(1,0xc0a80a01), left(10,0xc0a80afe), right(20,0xc0a814fe), b(2,0xc0a81402);
        CIPRouter router;
        router.BindInterface(0,left.address,0xffffff00,&left.arp,&left.ethernet);
        router.BindInterface(1,right.address,0xffffff00,&right.arp,&right.ethernet);
        left.ip.SetRouter(&router,0); left.arp.SetRouter(&router,0);
        right.ip.SetRouter(&router,1); right.arp.SetRouter(&router,1);
        assert(!left.arp.AddProxy(b.address,_T("NIC 1"),0));
        assert(left.arp.AddProxy(b.address,_T("NIC 2"),1));
        assert(!left.arp.AddProxy(b.address,_T("duplicate"),1));
        assert(right.arp.AddProxy(a.address,_T("NIC 1"),0));
        assert(a.ip.SetDestination(b.address));
        std::vector<unsigned char> chat(3000,0x48);
        assert(a.chat.Send(chat.data(),int(chat.size()))); PumpRouted(a,left,right,b);
        assert(b.ui.messages.size()==1 && b.ui.messages[0]==chat && ReadIp(b.ui.lastSource)==a.address);
        unsigned char mac[6]; assert(a.arp.Lookup(b.address,mac) && !memcmp(mac,left.mac,6));
        const auto forwarded=right.ni.history.back();
        assert(Type(forwarded)==0x0800 && forwarded[22]==IP_DEFAULT_TTL-1 && forwarded[23]==IP_PROTOCOL_CHAT);
        assert(ReadIp(forwarded.data()+26)==a.address && ReadIp(forwarded.data()+30)==b.address);
        assert(!memcmp(forwarded.data(),b.mac,6) && !memcmp(forwarded.data()+6,right.mac,6));
        assert(Checksum(forwarded.data()+14,20)==0);
        b.ip.SetDestination(a.address); unsigned char reply='R'; assert(b.chat.Send(&reply,1));
        PumpRouted(a,left,right,b); assert(a.ui.messages.size()==1 && a.ui.messages[0][0]=='R');
        std::vector<unsigned char> file(IP_MAX_DATA_SIZE,0x5a);
        assert(a.ip.Send(file.data(),int(file.size()),IP_PROTOCOL_FILE)); PumpRouted(a,left,right,b);
        assert(b.file.messages.size()==1 && b.file.messages[0]==file);
        // The router PC's own app must use its egress route too, without treating itself as a transit hop.
        left.ip.SetDestination(b.address); assert(left.chat.Send(&reply,1)); PumpRouted(a,left,right,b);
        assert(b.ui.messages.back()[0]=='R' && ReadIp(b.ui.lastSource)==left.address);
        assert(right.ni.history.back()[22]==IP_DEFAULT_TTL);
        b.ip.SetDestination(left.address); assert(b.chat.Send(&reply,1)); PumpRouted(a,left,right,b);
        // Production shares ChatApp/UI between both IP layers; the fixture has one sink per NIC.
        assert(right.ui.messages.size()==1 && right.ui.messages.back()[0]=='R');
        assert(b.arp.Lookup(left.address,mac) && !memcmp(mac,right.mac,6));
        // Router passes arbitrary IPv4 Protocol values, not just the app identifiers.
        auto generic=forwarded; memcpy(generic.data(),left.mac,6); memcpy(generic.data()+6,a.mac,6);
        generic[22]=64; generic[23]=6; FixIpChecksum(generic);
        Deliver(generic,left); PumpRouted(a,left,right,b);
        assert(right.ni.history.back()[23]==6);
        // An off-link target is accepted only with an on-link gateway; preserve the final destination IP.
        const uint32_t offLink=0x0a320008;
        assert(!left.arp.AddProxy(offLink,_T("invalid direct"),1));
        assert(!left.arp.AddProxy(offLink,_T("invalid gateway"),1,0xc0a80a01));
        assert(left.arp.AddProxy(offLink,_T("NIC 2 via gateway"),1,b.address));
        auto indirect=generic; WriteIp(indirect.data()+30,offLink); FixIpChecksum(indirect);
        Deliver(indirect,left); PumpRouted(a,left,right,b);
        assert(ReadIp(right.ni.history.back().data()+30)==offLink && !memcmp(right.ni.history.back().data(),b.mac,6));
        left.arp.DeleteProxy(offLink);
        assert(!router.CanProxyReply(0,offLink));
        // TTL expiry returns ICMP Time Exceeded quoting the pre-decrement original packet.
        generic[22]=1; FixIpChecksum(generic); Deliver(generic,left);
        auto error=left.ni.frames.back(); assert(error[23]==1 && error[34]==11 && error[35]==0);
        assert(error[50]==1 && Checksum(error.data()+34,error.size()-34)==0);
        PumpRouted(a,left,right,b);
        // No route returns ICMP Destination Unreachable; never invent a proxy reply for broadcasts.
        generic[22]=64; WriteIp(generic.data()+30,0x0a000032); FixIpChecksum(generic); Deliver(generic,left);
        error=left.ni.frames.back(); assert(error[34]==3 && error[35]==0); PumpRouted(a,left,right,b);
        assert(!left.arp.AddProxy(0xc0a814ff,_T("broadcast"),1));
        // Unresolved downstream ARP is bounded; a late Reply must not release an expired packet.
        const uint32_t missing=0xc0a81463; assert(left.arp.AddProxy(missing,_T("NIC 2"),1));
        a.ip.SetDestination(missing); assert(a.chat.Send(&reply,1)); PumpRouted(a,left,right,b);
        assert(router.GetStats().waiting==1);
        testNow += IP_RESOLVE_TIMEOUT_MS; router.Tick(testNow); PumpRouted(a,left,right,b);
        assert(router.GetStats().waiting==0 && router.GetStats().dropped>=3);
        // Transit IPv4 fragments are forwarded, whereas local apps still reject fragments.
        WriteIp(generic.data()+30,b.address); generic[20]=0x20; generic[21]=1; FixIpChecksum(generic);
        Deliver(generic,left); PumpRouted(a,left,right,b); assert(right.ni.history.back()[20]==0x20);
    }
    {
        Host a(1), b(2);
        assert(a.arp.BeginAddressCheck(a.mac)); assert(!a.arp.IsUsable());
        FinishAddressCheck(a,b);
        size_t probes=0,announcements=0;
        for(const auto& frame:a.ni.history) if(Type(frame)==ETHERNET_TYPE_ARP) {
            const uint32_t senderIp=ReadIp(frame.data()+28),targetIp=ReadIp(frame.data()+38);
            assert(frame[21]==ARP_OPERATION_REQUEST && !memcmp(frame.data()+6,frame.data()+22,6));
            if(!senderIp && targetIp==a.address) ++probes;
            if(senderIp==a.address && targetIp==a.address) ++announcements;
        }
        assert(probes==ARP_PROBE_COUNT && announcements==ARP_ANNOUNCE_COUNT);
        unsigned char mac[6]; assert(b.arp.Lookup(a.address,mac)); // new GARP entry, not only an existing cache
        // A second host asserting our active IP triggers one defense, then cessation within 10 seconds.
        auto conflict=b.ni.history.empty() ? a.ni.history.back() : b.ni.history.back();
        memcpy(conflict.data()+6,b.mac,6); memcpy(conflict.data()+22,b.mac,6);
        WriteIp(conflict.data()+28,a.address); WriteIp(conflict.data()+38,a.address);
        Deliver(conflict,a); assert(a.arp.IsUsable());
        const size_t defenses=a.ni.history.size(); Deliver(conflict,a);
        assert(a.arp.GetAddressState()==CARPLayer::ADDRESS_STATE::Conflict && a.ni.history.size()==defenses);
        assert(!a.ip.Send(&conflict[0],1,IP_PROTOCOL_CHAT));
    }
    {
        Host candidate(7,0xc0a80a01), owner(1);
        unsigned char proposal[6] = {0x02,0,0,0,0,8};
        assert(candidate.arp.BeginAddressCheck(proposal));
        for(int i=0;i<8;++i) { testNow+=250; candidate.arp.Tick(testNow); Pump(candidate,owner); }
        assert(candidate.arp.GetAddressState()==CARPLayer::ADDRESS_STATE::Conflict);
        // An ARP Reply to a Probe has target IP 0.0.0.0 and must still detect the conflicting sender IP.
        assert(owner.ni.history.back()[21]==ARP_OPERATION_REPLY && ReadIp(owner.ni.history.back().data()+38)==0);
        assert(!memcmp(owner.ni.history.back().data(),proposal,6));
        unsigned char effective[6]; candidate.arp.GetEffectiveMac(effective); assert(!memcmp(effective,candidate.mac,6));
        for(int attempt=1;attempt<ARP_MAX_CONFLICTS;++attempt) {
            assert(candidate.arp.BeginAddressCheck(proposal));
            Deliver(owner.ni.history.back(),candidate);
            assert(candidate.arp.GetAddressState()==CARPLayer::ADDRESS_STATE::Conflict);
        }
        assert(!candidate.arp.BeginAddressCheck(proposal));
        testNow+=ARP_RATE_LIMIT_MS; assert(candidate.arp.BeginAddressCheck(proposal));
        // A peer probing the same address simultaneously is a conflict even though its sender IP is zero.
        auto competing=owner.ni.history.back(); memset(competing.data(),0xff,6);
        competing[20]=0; competing[21]=ARP_OPERATION_REQUEST; WriteIp(competing.data()+28,0);
        memset(competing.data()+32,0,6); WriteIp(competing.data()+38,candidate.address);
        Deliver(competing,candidate); assert(candidate.arp.GetAddressState()==CARPLayer::ADDRESS_STATE::Conflict);
    }
    {
        // [assignment6] 실제 FileApp의 INFO/DATA/END 송신과 디스크 저장을 PARP 두 LAN 경로로 검증한다.
        CFileAppLayer sender("FileApp"), receiver("FileApp");
        Host a(1,0xc0a80a01,&sender), left(10,0xc0a80afe), right(20,0xc0a814fe), b(2,0xc0a81402,&receiver);
        CIPRouter router;
        router.BindInterface(0,left.address,0xffffff00,&left.arp,&left.ethernet);
        router.BindInterface(1,right.address,0xffffff00,&right.arp,&right.ethernet);
        left.ip.SetRouter(&router,0); left.arp.SetRouter(&router,0);
        right.ip.SetRouter(&router,1); right.arp.SetRouter(&router,1);
        assert(left.arp.AddProxy(b.address,_T("NIC 2"),1));
        assert(right.arp.AddProxy(a.address,_T("NIC 1"),0));
        a.ip.SetDestination(b.address); a.ip.RequestDestination(); PumpRouted(a,left,right,b);
        sender.SetNotifyWindow(reinterpret_cast<HWND>(1)); receiver.SetNotifyWindow(reinterpret_cast<HWND>(1));
        for (int size : {0,1,1467,1468,1469,2936,2937,12000,1024*1024}) {
            const std::string name="routed_"+std::to_string(size)+".bin";
            std::string original(size,'\0'); for(int i=0;i<size;++i) original[i]=char(i%251);
            { std::ofstream file(name,std::ios::binary); file.write(original.data(),original.size()); }
            assert(sender.StartSendFile(name.c_str())); PumpRouted(a,left,right,b);
            bool sendFinished=false,receiveFinished=false;
            for (LPARAM data : fileStatuses) {
                auto* status=reinterpret_cast<FILE_STATUS*>(data);
                if(status->finished && status->percent==100) {
                    assert(status->progress.completedBytes==uint64_t(size));
                    if(status->sending) sendFinished=true; else receiveFinished=true;
                }
                delete status;
            }
            fileStatuses.clear(); assert(sendFinished && receiveFinished);
            std::ifstream file("run/ReceivedFiles/"+name,std::ios::binary);
            const std::string received((std::istreambuf_iterator<char>(file)),{});
            assert(received==original && !std::filesystem::exists("run/ReceivedFiles/"+name+".part"));
        }
    }
    {
        Host a(1), b(2); assert(a.ip.SetDestination(b.address));
        unsigned char text='Q'; assert(a.chat.Send(&text,1));
        assert(!a.ip.SetDestination(0xc0a80a03));
        testNow+=IP_RESOLVE_TIMEOUT_MS; a.ip.Tick(testNow);
        Pump(a,b); assert(b.ui.messages.empty()); // A late reply must not send an expired chat.
        assert(a.ip.SetDestination(IPV4_BROADCAST));
        assert(a.chat.Send(&text,1)); assert(Type(a.ni.frames.front())==0x0800);
        Pump(a,b); assert(b.ui.messages.size()==1);
    }
    {
        Host a(1), b(2); a.ip.SetDestination(b.address);
        std::vector<unsigned char> maximum(CHAT_MAX_MESSAGE_SIZE,0x41);
        assert(a.chat.Send(maximum.data(),int(maximum.size()))); Pump(a,b);
        assert(b.ui.messages.size()==1 && b.ui.messages[0]==maximum);
        assert(!a.chat.Send(maximum.data(),CHAT_MAX_MESSAGE_SIZE+1));
    }
    std::cout << "PASS: real layer linkage, two-LAN PARP bidirectional forwarding, byte-exact routed files (9 sizes), next-hop ARP/timeout, TTL/ICMP/checksum, GARP Probe/Announcement/MAC switch/conflict defense, cache expiry, chat demux/MTU and broadcast\n";
}
