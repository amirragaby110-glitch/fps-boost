// FPS Booster Pro - Internet speed test (WinHTTP) + gaming DNS
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <stdlib.h>
#include "app.h"
#include <winhttp.h>
#include <iphlpapi.h>
#include <icmpapi.h>
#include <string.h>

#define NET_HOST L"speed.cloudflare.com"
#define NET_IP_HOST L"www.cloudflare.com"

// WM_APP_NET phases (wParam) - must match ui.cpp handler:
// 0 ping done (lParam=ms), 1 down live (lParam=new double Mbps),
// 2 down done, 3 up live (lParam=new double Mbps), 4 up done,
// 5 ip (lParam=new wchar_t[]), 6 grade done (lParam=0..3),
// 7 error, 8 cancelled, 10..16 dns ping (lParam=ms), 20 ping-all done

static volatile bool g_netCancel = false;
static bool g_netBusy = false;
bool NetTestBusy() { return g_netBusy; }
void NetTestCancel() { g_netCancel = true; }

// ---------- HTTP ----------
static bool HttpRequest(const wchar_t* host, const wchar_t* method, const wchar_t* path,
                        const void* sendData, DWORD sendLen,
                        std::string* recv, ULONGLONG maxRecv, ULONGLONG* msOut) {
    LARGE_INTEGER li;
    QueryPerformanceFrequency(&li);
    ULONGLONG f = li.QuadPart ? (ULONGLONG)li.QuadPart : 1;
    QueryPerformanceCounter(&li);
    ULONGLONG t0 = li.QuadPart;
    bool ok = false;
    HINTERNET ses = WinHttpOpen(L"FPSBooster/1.2", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    HINTERNET con = NULL, req = NULL;
    if (ses) {
        WinHttpSetTimeouts(ses, 8000, 8000, 15000, 60000);
        con = WinHttpConnect(ses, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
    }
    if (con)
        req = WinHttpOpenRequest(con, method, path, NULL, WINHTTP_NO_REFERER,
            WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (req && WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
            (LPVOID)sendData, sendLen, sendLen, 0) && WinHttpReceiveResponse(req, NULL)) {
        DWORD status = 0, sz = sizeof(status);
        WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            NULL, &status, &sz, NULL);
        if (status == 200) {
            ULONGLONG got = 0;
            for (;;) {
                if (g_netCancel) break;
                DWORD avail = 0;
                if (!WinHttpQueryDataAvailable(req, &avail) || avail == 0) break;
                DWORD chunk = avail > 65536 ? 65536 : avail;
                if (maxRecv && got + chunk > maxRecv) chunk = (DWORD)(maxRecv - got);
                char buf[65536];
                DWORD rd = 0;
                if (!WinHttpReadData(req, buf, chunk, &rd) || rd == 0) break;
                got += rd;
                if (recv) recv->append(buf, rd);
                if (maxRecv && got >= maxRecv) break;
            }
            ok = !g_netCancel;
        }
    }
    if (req) WinHttpCloseHandle(req);
    if (con) WinHttpCloseHandle(con);
    if (ses) WinHttpCloseHandle(ses);
    QueryPerformanceCounter(&li);
    if (msOut) *msOut = (ULONGLONG)((li.QuadPart - t0) * 1000.0 / (double)f);
    return ok;
}

bool NetGetIP(std::wstring& ip) {
    std::string s;
    ULONGLONG ms = 0;
    if (!HttpRequest(NET_IP_HOST, L"GET", L"/cdn-cgi/trace", NULL, 0, &s, 8192, &ms)) return false;
    const char* p = strstr(s.c_str(), "ip=");
    if (!p) return false;
    p += 3;
    std::string v;
    while (*p && *p != '\r' && *p != '\n') v.push_back(*p++);
    if (v.empty()) return false;
    ip = Utf8ToWide(v);
    return true;
}

static int PingTest() { // avg ms, or -1
    HINTERNET ses = WinHttpOpen(L"FPSBooster/1.2", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!ses) return -1;
    WinHttpSetTimeouts(ses, 5000, 5000, 5000, 8000);
    HINTERNET con = WinHttpConnect(ses, NET_HOST, INTERNET_DEFAULT_HTTPS_PORT, 0);
    double samples[4];
    int n = 0;
    if (con) {
        LARGE_INTEGER li;
        QueryPerformanceFrequency(&li);
        ULONGLONG f = li.QuadPart ? (ULONGLONG)li.QuadPart : 1;
        for (int i = 0; i < 4 && !g_netCancel; i++) {
            HINTERNET req = WinHttpOpenRequest(con, L"GET", L"/__down?bytes=1000", NULL,
                WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
            if (!req) break;
            QueryPerformanceCounter(&li);
            ULONGLONG t0 = li.QuadPart;
            bool ok = WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0, NULL, 0, 0, 0) &&
                      WinHttpReceiveResponse(req, NULL);
            if (ok) {
                char b[2048];
                DWORD rd = 0;
                do { rd = 0; WinHttpReadData(req, b, (DWORD)sizeof(b), &rd); } while (rd > 0);
                QueryPerformanceCounter(&li);
                samples[n++] = (li.QuadPart - t0) * 1000.0 / (double)f;
            }
            WinHttpCloseHandle(req);
            if (!ok) break;
        }
        WinHttpCloseHandle(con);
    }
    WinHttpCloseHandle(ses);
    if (n < 2 || g_netCancel) return -1;
    double s = 0;
    int from = (n > 3) ? 1 : 0, c = 0; // skip first (TLS handshake)
    for (int i = from; i < n; i++) { s += samples[i]; c++; }
    return (int)(s / (c ? c : 1));
}

static double DownTest(HWND w) { // Mbps
    ULONGLONG ms = 0;
    HttpRequest(NET_HOST, L"GET", L"/__down?bytes=1000000", NULL, 0, NULL, 0, &ms); // warmup
    ULONGLONG bytes = 0;
    double secs = 0;
    int chunks = 0;
    DWORD start = GetTickCount();
    while (!g_netCancel && GetTickCount() - start < 8000 && chunks < 12) {
        if (!HttpRequest(NET_HOST, L"GET", L"/__down?bytes=5000000", NULL, 0, NULL, 0, &ms)) break;
        if (ms < 1) ms = 1;
        bytes += 5000000;
        secs += ms / 1000.0;
        chunks++;
        double cur = bytes * 8.0 / secs / 1000000.0;
        PostMessageW(w, WM_APP_NET, 1, (LPARAM)(new double(cur)));
    }
    if (!g_netCancel) PostMessageW(w, WM_APP_NET, 2, 0);
    if (secs <= 0 || bytes == 0) return 0;
    return bytes * 8.0 / secs / 1000000.0;
}

static double UpTest(HWND w) { // Mbps
    char* buf = new char[1048576];
    memset(buf, 'x', 1048576);
    ULONGLONG bytes = 0;
    double secs = 0;
    int n = 0;
    DWORD start = GetTickCount();
    while (!g_netCancel && GetTickCount() - start < 6000 && n < 12) {
        ULONGLONG ms = 0;
        if (!HttpRequest(NET_HOST, L"POST", L"/__up", buf, 1048576, NULL, 0, &ms)) break;
        if (ms < 1) ms = 1;
        bytes += 1048576;
        secs += ms / 1000.0;
        n++;
        double cur = bytes * 8.0 / secs / 1000000.0;
        PostMessageW(w, WM_APP_NET, 3, (LPARAM)(new double(cur)));
    }
    if (!g_netCancel) PostMessageW(w, WM_APP_NET, 4, 0);
    delete[] buf;
    if (secs <= 0 || bytes == 0) return 0;
    return bytes * 8.0 / secs / 1000000.0;
}

struct NetCtx { HWND w; };
static DWORD WINAPI NetThread(LPVOID a) {
    NetCtx* c = (NetCtx*)a;
    HWND w = c->w;
    delete c;
    g_netBusy = true;
    g_netCancel = false;
    LogW(L"Speed test started");
    std::wstring ip;
    if (NetGetIP(ip) && !ip.empty()) {
        wchar_t* p = new wchar_t[ip.size() + 1];
        wcscpy(p, ip.c_str());
        PostMessageW(w, WM_APP_NET, 5, (LPARAM)p);
    }
    int ping = PingTest();
    if (g_netCancel) { g_netBusy = false; PostMessageW(w, WM_APP_NET, 8, 0); return 0; }
    if (ping < 0) { g_netBusy = false; PostMessageW(w, WM_APP_NET, 7, 0); return 0; }
    PostMessageW(w, WM_APP_NET, 0, (LPARAM)ping);
    double dn = DownTest(w);
    double up = 0;
    if (!g_netCancel) up = UpTest(w);
    if (g_netCancel) { g_netBusy = false; PostMessageW(w, WM_APP_NET, 8, 0); return 0; }
    int gi;
    if (ping <= 50 && dn >= 50) gi = 0;
    else if (ping <= 100 && dn >= 20) gi = 1;
    else if (dn >= 5) gi = 2;
    else gi = 3;
    g_netBusy = false;
    PostMessageW(w, WM_APP_NET, 6, (LPARAM)gi);
    LogW(L"Speed test done: ping=%d down=%.1f up=%.1f", ping, dn, up);
    return 0;
}
void NetTestRun(HWND notifyWnd) {
    if (g_netBusy) return;
    NetCtx* c = new NetCtx;
    c->w = notifyWnd;
    HANDLE h = CreateThread(NULL, 0, NetThread, c, 0, NULL);
    if (h) CloseHandle(h);
}

// ================= Gaming DNS =================
struct DnsAd { std::wstring name; std::vector<std::wstring> dns; bool dhcp; };

static bool DnsEnum(std::vector<DnsAd>& out) {
    out.clear();
    ULONG sz = 15000;
    IP_ADAPTER_ADDRESSES* addrs = (IP_ADAPTER_ADDRESSES*)malloc(sz);
    if (!addrs) return false;
    DWORD r = GetAdaptersAddresses(AF_INET, 0, NULL, addrs, &sz);
    if (r == ERROR_BUFFER_OVERFLOW) {
        free(addrs);
        addrs = (IP_ADAPTER_ADDRESSES*)malloc(sz);
        if (!addrs) return false;
        r = GetAdaptersAddresses(AF_INET, 0, NULL, addrs, &sz);
    }
    if (r != ERROR_SUCCESS) { free(addrs); return false; }
    for (IP_ADAPTER_ADDRESSES* a = addrs; a; a = a->Next) {
        if (a->OperStatus != IfOperStatusUp) continue;
        if (!a->Ipv4Enabled) continue;
        DnsAd d;
        d.name = a->FriendlyName ? a->FriendlyName : L"";
        if (d.name.empty()) continue;
        d.dhcp = a->Dhcpv4Enabled != FALSE;
        for (IP_ADAPTER_DNS_SERVER_ADDRESS* x = a->FirstDnsServerAddress; x; x = x->Next) {
            if (!x->Address.lpSockaddr || x->Address.lpSockaddr->sa_family != AF_INET) continue;
            SOCKADDR_IN* v = (SOCKADDR_IN*)x->Address.lpSockaddr;
            d.dns.push_back(WFormat(L"%d.%d.%d.%d",
                v->sin_addr.S_un.S_un_b.s_b1, v->sin_addr.S_un.S_un_b.s_b2,
                v->sin_addr.S_un.S_un_b.s_b3, v->sin_addr.S_un.S_un_b.s_b4));
        }
        out.push_back(d);
    }
    free(addrs);
    return !out.empty();
}

static std::wstring DnsCfgPath() { return JoinPath(g_dataDir, L"dnscfg.json"); }

static void DnsBackupOnce(const std::vector<DnsAd>& ads) {
    if (FileExists(DnsCfgPath())) return; // keep the true original
    std::string j = "{\"adapters\":[";
    for (size_t i = 0; i < ads.size(); i++) {
        if (i) j += ",";
        j += "{\"dhcp\":";
        j += ads[i].dhcp ? "1" : "0";
        j += ",\"name\":\"" + JsonEscapeW(ads[i].name) + "\",\"dns\":[";
        for (size_t k = 0; k < ads[i].dns.size(); k++) {
            if (k) j += ",";
            j += "\"" + JsonEscapeW(ads[i].dns[k]) + "\"";
        }
        j += "]}";
    }
    j += "]}";
    WriteFileText(DnsCfgPath(), j);
}

static std::wstring NetshExe() {
    wchar_t sysdir[MAX_PATH];
    GetSystemDirectoryW(sysdir, MAX_PATH);
    return JoinPath(sysdir, L"netsh.exe");
}
static bool NetshDns(const std::wstring& adapter, const wchar_t* d1, const wchar_t* d2) {
    DWORD code = 1;
    if (!d1) {
        std::wstring args = L"interface ip set dns \"" + adapter + L"\" dhcp";
        if (!RunHidden(NetshExe().c_str(), args.c_str(), &code) || code != 0) return false;
        return true;
    }
    std::wstring a1 = L"interface ip set dns \"" + adapter + L"\" static " + d1 + L" primary";
    if (!RunHidden(NetshExe().c_str(), a1.c_str(), &code) || code != 0) return false;
    if (d2 && *d2) {
        std::wstring a2 = L"interface ip add dns \"" + adapter + L"\" " + d2 + L" index=2";
        RunHidden(NetshExe().c_str(), a2.c_str(), &code); // best effort
    }
    return true;
}
static void DnsFlush() {
    wchar_t sysdir[MAX_PATH];
    GetSystemDirectoryW(sysdir, MAX_PATH);
    RunHidden(JoinPath(sysdir, L"ipconfig.exe").c_str(), L"/flushdns");
}

// preset: 0 restore/auto, 1 cloudflare, 2 google, 3 quad9, 4 opendns, 5 shecan, 6 electro, 7 adguard
static const wchar_t* kDns1[] = {L"1.1.1.1", L"8.8.8.8", L"9.9.9.9", L"208.67.222.222", L"178.22.122.100", L"78.157.42.100", L"94.140.14.14"};
static const wchar_t* kDns2[] = {L"1.0.0.1", L"8.8.4.4", L"149.112.112.112", L"208.67.220.220", L"185.51.200.2", L"78.157.42.101", L"94.140.15.15"};
bool DnsSetPreset(int preset) {
    std::vector<DnsAd> ads;
    if (!DnsEnum(ads)) return false;
    DnsBackupOnce(ads);
    int ok = 0;
    if (preset == 0) {
        // restore from backup if available
        std::string text;
        JVal root;
        bool haveBk = ReadFileText(DnsCfgPath(), text) && ParseJson(text, root);
        for (size_t i = 0; i < ads.size(); i++) {
            bool done = false;
            if (haveBk) {
                const JVal* a = root.find("adapters");
                if (a && a->type == JVal::ARR) {
                    for (size_t k = 0; k < a->arr.size(); k++) {
                        if (a->arr[k].wstr("name") == ads[i].name) {
                            if (a->arr[k].boolean("dhcp", true)) {
                                done = NetshDns(ads[i].name, NULL, NULL);
                            } else {
                                const JVal* dd = a->arr[k].find("dns");
                                std::wstring d1, d2;
                                if (dd && dd->type == JVal::ARR && dd->arr.size() > 0)
                                    d1 = Utf8ToWide(dd->arr[0].s);
                                if (dd && dd->type == JVal::ARR && dd->arr.size() > 1)
                                    d2 = Utf8ToWide(dd->arr[1].s);
                                done = d1.empty() ? NetshDns(ads[i].name, NULL, NULL)
                                                  : NetshDns(ads[i].name, d1.c_str(), d2.c_str());
                            }
                            break;
                        }
                    }
                }
            }
            if (!haveBk) done = NetshDns(ads[i].name, NULL, NULL);
            if (done) ok++;
        }
    } else {
        if (preset < 1 || preset > 7) return false;
        const wchar_t* d1 = kDns1[preset - 1];
        const wchar_t* d2 = kDns2[preset - 1];
        for (size_t i = 0; i < ads.size(); i++)
            if (NetshDns(ads[i].name, d1, d2)) ok++;
    }
    if (ok > 0) {
        DnsFlush();
        LogW(L"DNS preset %d applied on %d adapter(s)", preset, ok);
    }
    return ok > 0;
}

std::wstring DnsCurrent() {
    std::vector<DnsAd> ads;
    if (!DnsEnum(ads)) return L"?";
    std::wstring s;
    for (size_t i = 0; i < ads[0].dns.size(); i++) {
        if (i) s += L", ";
        s += ads[0].dns[i];
    }
    if (s.empty()) s = L"-";
    if (ads[0].dhcp) s += (Strings_GetLang() == 1) ? L" (خودکار)" : L" (auto)";
    if (ads.size() > 1) s += WFormat(L" +%d", (int)ads.size() - 1);
    return s;
}

// ================= Custom DNS + ping =================
static bool ValidIpv4(const wchar_t* s) {
    if (!s || !*s) return false;
    int dots = 0;
    for (const wchar_t* p = s; *p; p++) {
        if (*p == L'.') dots++;
        else if (*p < L'0' || *p > L'9') return false;
    }
    return dots == 3;
}
bool DnsSetCustom(const wchar_t* d1, const wchar_t* d2) {
    if (!ValidIpv4(d1)) return false;
    if (d2 && *d2 && !ValidIpv4(d2)) return false;
    std::vector<DnsAd> ads;
    if (!DnsEnum(ads)) return false;
    DnsBackupOnce(ads);
    int ok = 0;
    for (size_t i = 0; i < ads.size(); i++)
        if (NetshDns(ads[i].name, d1, (d2 && *d2) ? d2 : NULL)) ok++;
    if (ok > 0) { DnsFlush(); LogW(L"Custom DNS applied on %d adapter(s)", ok); }
    return ok > 0;
}

static bool g_pingBusy = false;
static bool g_pingCancel = false;

static int PingAvgMs(const wchar_t* ip) {
    IN_ADDR a;
    if (InetPtonW(AF_INET, ip, &a) != 1) return -1;
    HANDLE h = IcmpCreateFile();
    if (h == INVALID_HANDLE_VALUE) return -1;
    char send[32];
    memset(send, 'E', sizeof(send));
    char reply[sizeof(ICMP_ECHO_REPLY) + 64];
    int sum = 0, n = 0;
    for (int i = 0; i < 3 && !g_pingCancel; i++) {
        DWORD r = IcmpSendEcho(h, a.S_un.S_addr, send, sizeof(send), NULL, reply, sizeof(reply), 1200);
        if (r > 0) {
            ICMP_ECHO_REPLY* e = (ICMP_ECHO_REPLY*)reply;
            if (e->Status == IP_SUCCESS) { sum += (int)e->RoundTripTime; n++; }
        }
    }
    IcmpCloseHandle(h);
    return n ? sum / n : -1;
}
struct PingCtx { HWND w; };
static DWORD WINAPI PingThread(LPVOID arg) {
    PingCtx* c = (PingCtx*)arg;
    HWND w = c->w;
    delete c;
    for (int i = 0; i < 7 && !g_pingCancel; i++) {
        int ms = PingAvgMs(kDns1[i]);
        PostMessageW(w, WM_APP_NET, (WPARAM)(10 + i), (LPARAM)ms);
    }
    g_pingBusy = false;
    PostMessageW(w, WM_APP_NET, (WPARAM)20, 0); // 20 = done (10..16 are results)
    return 0;
}
void DnsPingAll(HWND notifyWnd) {
    if (g_pingBusy) return;
    g_pingBusy = true;
    g_pingCancel = false;
    PingCtx* c = new PingCtx; c->w = notifyWnd;
    HANDLE h = CreateThread(NULL, 0, PingThread, c, 0, NULL);
    if (h) CloseHandle(h);
}
void DnsPingCancel() { g_pingCancel = true; }
bool DnsPingBusy() { return g_pingBusy; }
