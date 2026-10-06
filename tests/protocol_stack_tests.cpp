#include "LayerManager.h"
#include "EthernetLayer.h"
#include "IPLayer.h"
#include "ARPLayer.h"
#include "ChatAppLayer.h"
#include <deque>
#include <iostream>

using namespace NetworkPackets;

struct Wire : CBaseLayer {
    std::deque<std::vector<unsigned char>> frames;
    Wire() : CBaseLayer("NI") {}
    BOOL Send(unsigned char* data, int length) override {
        assert(length >= 60 && length <= 1514);
        frames.emplace_back(data, data + length); return TRUE;
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
    explicit Host(unsigned char id) : address(0xc0a80a00u | id) {
        mac[5] = id;
        for (CBaseLayer* layer : std::vector<CBaseLayer*>{&ni,&ethernet,&ip,&arp,&chat,&file,&ui}) manager.AddLayer(layer,FALSE);
        manager.ConnectLayers("NI ( *Ethernet ( *IP ( *ChatApp ( *ChatDlg ) *FileApp ( +ChatDlg ) ) *ARP ) )");
        assert(chat.GetUnderLayer() == &ip && file.GetUnderLayer() == &ip);
        assert(ip.GetUnderLayer() == &ethernet && arp.GetUnderLayer() == &ethernet);
        assert(ui.GetUnderLayer() == &chat);
        ethernet.SetSourceAddress(mac); ip.Configure(address, &arp); arp.Configure(address, mac, &ip);
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
        auto garp=a.ni.frames.front();
        assert(Type(garp)==0x0806 && garp[21]==1 && ReadIp(garp.data()+28)==ReadIp(garp.data()+38));
        assert(!memcmp(garp.data()+6,advertised,6) && !memcmp(garp.data()+22,advertised,6));
        Pump(a,b); assert(b.arp.Lookup(a.address,mac) && !memcmp(mac,advertised,6));
        // Bad ARP header lengths and Ethernet/SHA disagreement are rejected.
        garp[18]=5; Deliver(garp,b); garp[18]=6; garp[22]^=2; Deliver(garp,b);
        assert(b.arp.Lookup(a.address,mac) && !memcmp(mac,advertised,6));
        testNow+=ARP_COMPLETE_TIMEOUT_MS; a.arp.Tick(testNow); b.arp.Tick(testNow);
        assert(!a.arp.Lookup(b.address,mac) && !b.arp.Lookup(a.address,mac));
    }
    {
        Host a(1), proxy(2); const uint32_t target=0xc0a80a63;
        assert(proxy.arp.AddProxy(target,_T("NIC 1")));
        assert(!proxy.arp.AddProxy(target,_T("duplicate")));
        assert(a.arp.SendRequest(target,true)); Pump(a,proxy);
        unsigned char mac[6]; assert(a.arp.Lookup(target,mac) && !memcmp(mac,proxy.mac,6));
        proxy.arp.DeleteProxy(target); a.arp.ClearCache();
        assert(a.arp.SendRequest(target,true)); Pump(a,proxy); assert(!a.arp.Lookup(target,mac));
        std::vector<ARP_CACHE_ENTRY> entries; std::vector<ARP_PROXY_ENTRY> proxies;
        a.arp.GetSnapshot(entries,proxies); assert(entries.size()==1 && !entries[0].complete);
        testNow+=ARP_INCOMPLETE_TIMEOUT_MS; a.arp.Tick(testNow);
        a.arp.GetSnapshot(entries,proxies); assert(entries.empty());
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
    std::cout << "PASS: real layer linkage, ARP, Proxy ARP, GARP, IPv4 demux/checksum/MTU, chat reassembly, cache expiry, late reply and broadcast\n";
}
