// src/monitor_dns.cpp
// Simple DNS monitor using pcap (Npcap/WinPcap) on Windows.
// - lists adapters if none specified
// - captures UDP/TCP port 53 traffic
// - parses DNS question names and prints query/response names
// - differentiates inbound/outbound by comparing IPs to local adapter addresses

#define _WIN32_WINNT 0x0601
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#include <pcap.h>

#include <iostream>
#include <string>
#include <vector>
#include <unordered_set>
#include <thread>
#include <chrono>
#include <atomic>
#include <mutex>
#include <sstream>
#include <fstream>

#pragma comment(lib, "iphlpapi.lib")

struct Config {
    std::string iface;
    std::string output;
    bool json = false;
    int snaplen = 65535;
};

static std::atomic<bool> running{true};

BOOL WINAPI consoleHandler(DWORD signal) {
    if (signal == CTRL_C_EVENT || signal == CTRL_CLOSE_EVENT || signal == CTRL_BREAK_EVENT) {
        running = false;
        return TRUE;
    }
    return FALSE;
}

static void collectLocalIPs(std::unordered_set<std::string> &out) {
    DWORD flags = GAA_FLAG_INCLUDE_PREFIX;
    ULONG family = AF_INET;
    ULONG bufferSize = 0;
    GetAdaptersAddresses(family, flags, NULL, NULL, &bufferSize);
    std::vector<unsigned char> buffer(bufferSize);
    PIP_ADAPTER_ADDRESSES adapters = (PIP_ADAPTER_ADDRESSES)buffer.data();
    if (GetAdaptersAddresses(family, flags, NULL, adapters, &bufferSize) != NO_ERROR) return;
    for (PIP_ADAPTER_ADDRESSES a = adapters; a; a = a->Next) {
        for (PIP_ADAPTER_UNICAST_ADDRESS ua = a->FirstUnicastAddress; ua; ua = ua->Next) {
            SOCKADDR *sa = ua->Address.lpSockaddr;
            char buf[64] = {};
            if (sa->sa_family == AF_INET) {
                sockaddr_in *in = (sockaddr_in*)sa;
                inet_ntop(AF_INET, &in->sin_addr, buf, (socklen_t)sizeof(buf));
                out.insert(std::string(buf));
            }
        }
    }
}

// parse DNS name (with compression) from a DNS message buffer
static bool parseDnsName(const u_char *msg, int msglen, int &offset, std::string &out) {
    out.clear();
    int orig = offset;
    int jumps = 0;
    while (offset < msglen) {
        unsigned char len = msg[offset];
        if (len == 0) { offset++; return true; }
        if ((len & 0xC0) == 0xC0) {
            if (offset + 1 >= msglen) return false;
            int ptr = ((len & 0x3F) << 8) | msg[offset+1];
            if (ptr >= msglen) return false;
            if (++jumps > 10) return false; // avoid loops
            int saved = offset + 2;
            offset = ptr;
            if (!parseDnsName(msg, msglen, offset, out)) return false;
            offset = saved;
            return true;
        } else {
            if (offset + 1 + len > msglen) return false;
            if (!out.empty()) out.push_back('.');
            out.append((const char*)&msg[offset+1], len);
            offset += 1 + len;
        }
    }
    offset = orig;
    return false;
}

struct Logger {
    std::mutex mu;
    std::unique_ptr<std::ofstream> ofs;
    bool json = false;
    void open(const std::string &path) { ofs = std::make_unique<std::ofstream>(path, std::ios::app); }
    void setJson(bool v) { json = v; }
    void log(const std::string &line) {
        std::lock_guard<std::mutex> lk(mu);
        std::cout << line << std::endl;
        if (ofs && ofs->is_open()) {
            if (json) {
                std::string j = "{ \"message\": \"";
                for (char c : line) { if (c=='"') j.push_back('\\'); j.push_back(c); }
                j += "\" }";
                (*ofs) << j << std::endl;
            } else {
                (*ofs) << line << std::endl;
            }
            ofs->flush();
        }
    }
};

struct CaptureContext { Logger *logger; std::unordered_set<std::string> localIPs; };

static void dnsPacketHandler(u_char *user, const struct pcap_pkthdr *h, const u_char *bytes) {
    CaptureContext *ctx = (CaptureContext*)user;
    // minimal parsing: assume Ethernet + IPv4
    if (h->caplen < 14 + 20) return;
    const u_char *ip = bytes + 14;
    int iphl = (ip[0] & 0x0F) * 4;
    if (iphl < 20) return;
    unsigned char proto = ip[9];
    char src[64] = {}, dst[64] = {};
    sprintf_s(src, "%u.%u.%u.%u", ip[12], ip[13], ip[14], ip[15]);
    sprintf_s(dst, "%u.%u.%u.%u", ip[16], ip[17], ip[18], ip[19]);

    bool srcLocal = ctx->localIPs.count(src) > 0;
    bool dstLocal = ctx->localIPs.count(dst) > 0;
    std::string direction = srcLocal ? "OUT" : (dstLocal ? "IN" : "OTHER");

    if (proto == IPPROTO_UDP) {
        if (h->caplen < 14 + iphl + 8 + 12) return;
        const u_char *udp = ip + iphl;
        uint16_t sport = (udp[0]<<8)|udp[1];
        uint16_t dport = (udp[2]<<8)|udp[3];
        if (sport != 53 && dport != 53) return;
        const u_char *dns = udp + 8;
        int dnssz = h->caplen - (14 + iphl + 8);
        if (dnssz < 12) return;
        uint16_t qdcount = (dns[4]<<8)|dns[5];
        uint16_t ancount = (dns[6]<<8)|dns[7];
        int off = 12;
        for (int i=0;i<qdcount;i++) {
            std::string name;
            if (!parseDnsName(dns, dnssz, off, name)) break;
            if (off + 4 > dnssz) break;
            uint16_t qtype = (dns[off]<<8)|dns[off+1];
            off += 4;
            std::ostringstream ss;
            ss << "[DNS] " << direction << " " << src << ":" << sport << " -> " << dst << ":" << dport << " QUERY " << name << " type=" << qtype;
            ctx->logger->log(ss.str());
        }
        // skip parsing answers for brevity
    } else if (proto == IPPROTO_TCP) {
        // TCP port 53 (DNS over TCP) - payload has 2-byte length prefix
        if (h->caplen < 14 + iphl + 20) return;
        const u_char *tcp = ip + iphl;
        int tcphl = ((tcp[12] & 0xF0) >> 4) * 4;
        uint16_t sport = (tcp[0]<<8)|tcp[1];
        uint16_t dport = (tcp[2]<<8)|tcp[3];
        if (sport != 53 && dport != 53) return;
        const u_char *payload = tcp + tcphl;
        int payloadLen = h->caplen - (14 + iphl + tcphl);
        if (payloadLen < 2) return;
        int dnssz = (payload[0]<<8)|payload[1];
        if (payloadLen < 2 + 12) return;
        const u_char *dns = payload + 2;
        int dnslen = payloadLen - 2;
        uint16_t qdcount = (dns[4]<<8)|dns[5];
        int off = 12;
        for (int i=0;i<qdcount;i++) {
            std::string name;
            if (!parseDnsName(dns, dnslen, off, name)) break;
            if (off + 4 > dnslen) break;
            uint16_t qtype = (dns[off]<<8)|dns[off+1];
            off += 4;
            std::ostringstream ss;
            ss << "[DNS] " << direction << " " << src << ":" << sport << " -> " << dst << ":" << dport << " QUERY " << name << " type=" << qtype;
            ctx->logger->log(ss.str());
        }
    }
}

int main(int argc, char **argv) {
    Config cfg;
    for (int i=1;i<argc;i++) {
        std::string a = argv[i];
        if (a.rfind("--iface=",0)==0) cfg.iface = a.substr(8);
        else if (a.rfind("--output=",0)==0) cfg.output = a.substr(9);
        else if (a == "--json") cfg.json = true;
        else if (a == "--help" || a=="-h") {
            std::cout << "Usage: monitor_dns [--iface=name|index] [--output=path] [--json]\n";
            return 0;
        }
    }

    char errbuf[PCAP_ERRBUF_SIZE];
    pcap_if_t *alldevs;
    if (pcap_findalldevs(&alldevs, errbuf) == -1) {
        std::cerr << "pcap_findalldevs failed: " << errbuf << std::endl;
        return 1;
    }
    pcap_if_t *d;
    int idx = 0;
    pcap_if_t *selected = nullptr;
    for (d = alldevs; d; d = d->next, idx++) {
        if (cfg.iface.empty()) {
            std::cout << "[" << idx << "] " << (d->description?d->description:d->name) << " (" << d->name << ")" << std::endl;
        }
        if (!cfg.iface.empty() && (cfg.iface == d->name || cfg.iface == d->description || cfg.iface == std::to_string(idx))) selected = d;
    }

    if (cfg.iface.empty()) {
        std::cout << "Select interface index or pass --iface=name|index" << std::endl;
        pcap_freealldevs(alldevs);
        return 0;
    }
    if (!selected) {
        std::cerr << "Interface not found: " << cfg.iface << std::endl;
        pcap_freealldevs(alldevs);
        return 1;
    }

    pcap_t *handle = pcap_open_live(selected->name, cfg.snaplen, 1, 1000, errbuf);
    if (!handle) {
        std::cerr << "pcap_open_live failed: " << errbuf << std::endl;
        pcap_freealldevs(alldevs);
        return 1;
    }

    // set BPF filter for DNS (udp/tcp port 53)
    bpf_program fp;
    if (pcap_compile(handle, &fp, "port 53", 1, PCAP_NETMASK_UNKNOWN) == -1) {
        std::cerr << "pcap_compile failed" << std::endl;
    } else {
        pcap_setfilter(handle, &fp);
        pcap_freecode(&fp);
    }

    CaptureContext ctx;
    Logger logger;
    if (!cfg.output.empty()) logger.open(cfg.output);
    logger.setJson(cfg.json);
    ctx.logger = &logger;
    collectLocalIPs(ctx.localIPs);
    SetConsoleCtrlHandler(consoleHandler, TRUE);

    std::ostringstream ss;
    ss << "Starting DNS capture on " << selected->name;
    logger.log(ss.str());

    pcap_loop(handle, 0, dnsPacketHandler, (u_char*)&ctx);

    pcap_close(handle);
    pcap_freealldevs(alldevs);
    return 0;
}
