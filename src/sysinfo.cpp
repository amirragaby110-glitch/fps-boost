// FPS Booster Pro - System information & low-level actions
#include "app.h"
#include <stdio.h>
#include <shlobj.h>
#include <dxgi.h>

// ---------- Basic info ----------
std::wstring SysCpuName() {
    std::wstring s;
    if (RegGetString(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
        L"ProcessorNameString", s)) {
        // trim spaces
        while (!s.empty() && iswspace(s.front())) s.erase(s.begin());
        while (!s.empty() && iswspace(s.back())) s.pop_back();
        // collapse double spaces
        std::wstring o;
        for (size_t i = 0; i < s.size(); i++) {
            if (s[i] == L' ' && !o.empty() && o.back() == L' ') continue;
            o.push_back(s[i]);
        }
        return o;
    }
    return L"Unknown CPU";
}
int SysCpuCount() {
    SYSTEM_INFO si; GetSystemInfo(&si);
    return (int)si.dwNumberOfProcessors;
}
int SysCpuMHz() {
    DWORD mhz = 0;
    RegGetDword(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", L"~MHz", mhz);
    return (int)mhz;
}
int SysGpuVramMB() {
    IDXGIFactory* f = NULL;
    if (FAILED(CreateDXGIFactory(IID_IDXGIFactory, (void**)&f)) || !f) return 0;
    SIZE_T best = 0;
    for (UINT i = 0; ; i++) {
        IDXGIAdapter* a = NULL;
        if (f->EnumAdapters(i, &a) != S_OK || !a) break;
        DXGI_ADAPTER_DESC d;
        if (a->GetDesc(&d) == S_OK && d.VendorId != 0x1414 && d.DedicatedVideoMemory > best)
            best = d.DedicatedVideoMemory;
        a->Release();
    }
    f->Release();
    return (int)(best / (1024 * 1024));
}
std::wstring SysGpuName() {
    // Prefer a discrete GPU over the integrated one (laptops list Intel first).
    const wchar_t* cls = L"SYSTEM\\CurrentControlSet\\Control\\Class\\{4D36E968-E325-11CE-BFC1-08002BE10318}";
    std::wstring first, best;
    for (int i = 0; i < 8; i++) {
        wchar_t sub[256];
        StringCchPrintfW(sub, 256, L"%s\\%04d", cls, i);
        std::wstring desc;
        if (!RegGetString(HKEY_LOCAL_MACHINE, sub, L"DriverDesc", desc) || desc.empty()) continue;
        if (first.empty()) first = desc;
        std::wstring low = ToLower(desc);
        if (low.find(L"nvidia") != std::wstring::npos ||
            low.find(L"radeon rx") != std::wstring::npos ||
            low.find(L"radeon pro") != std::wstring::npos ||
            low.find(L" arc ") != std::wstring::npos ||
            low.find(L"quadro") != std::wstring::npos) { best = desc; break; }
    }
    if (!best.empty()) return best;
    return first.empty() ? L"Unknown GPU" : first;
}
std::wstring SysRamString() {
    MEMORYSTATUSEX ms; ms.dwLength = sizeof(ms);
    if (!GlobalMemoryStatusEx(&ms)) return L"?";
    double gb = (double)ms.ullTotalPhys / (1024.0*1024.0*1024.0);
    return WFormat(L"%.1f GB", gb);
}
int RamTotalMB() {
    MEMORYSTATUSEX ms; ms.dwLength = sizeof(ms);
    if (!GlobalMemoryStatusEx(&ms)) return 0;
    return (int)(ms.ullTotalPhys / (1024*1024));
}
int RamUsagePercent() {
    MEMORYSTATUSEX ms; ms.dwLength = sizeof(ms);
    if (!GlobalMemoryStatusEx(&ms)) return 0;
    return (int)ms.dwMemoryLoad;
}
int RamAvailMB() {
    MEMORYSTATUSEX ms; ms.dwLength = sizeof(ms);
    if (!GlobalMemoryStatusEx(&ms)) return 0;
    return (int)(ms.ullAvailPhys / (1024*1024));
}
std::wstring SysOsString() {
    // RtlGetVersion for true build number
    typedef LONG (WINAPI *RtlGetVersionFn)(PRTL_OSVERSIONINFOW);
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    int major = 10, minor = 0, build = 0;
    if (ntdll) {
        RtlGetVersionFn fn = (RtlGetVersionFn)GetProcAddress(ntdll, "RtlGetVersion");
        if (fn) {
            RTL_OSVERSIONINFOW vi; vi.dwOSVersionInfoSize = sizeof(vi);
            if (fn(&vi) == 0) { major = vi.dwMajorVersion; minor = vi.dwMinorVersion; build = vi.dwBuildNumber; }
        }
    }
    std::wstring edition;
    RegGetString(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", L"EditionID", edition);
    std::wstring name = (build >= 22000) ? L"Windows 11" : L"Windows 10";
    if (edition.empty()) edition = L"Windows";
    return WFormat(L"%s %s (Build %d)", name.c_str(), edition.c_str(), build);
}
std::wstring SysUptimeString() {
    ULONGLONG ms = GetTickCount64();
    ULONGLONG s = ms / 1000;
    int d = (int)(s / 86400), h = (int)((s % 86400) / 3600), m = (int)((s % 3600) / 60);
    if (d > 0) return WFormat(L"%dd %dh %dm", d, h, m);
    if (h > 0) return WFormat(L"%dh %dm", h, m);
    return WFormat(L"%dm", m);
}
std::wstring SysDisplayString() {
    int w = GetSystemMetrics(SM_CXSCREEN), h = GetSystemMetrics(SM_CYSCREEN);
    DEVMODEW dm; dm.dmSize = sizeof(dm);
    int hz = 0;
    if (EnumDisplaySettingsW(NULL, ENUM_CURRENT_SETTINGS, &dm)) hz = dm.dmDisplayFrequency;
    if (hz > 0) return WFormat(L"%dx%d @ %dHz", w, h, hz);
    return WFormat(L"%dx%d", w, h);
}

// ---------- CPU meter ----------
static ULONGLONG U64(const ULARGE_INTEGER& u) { return ((ULONGLONG)u.HighPart << 32) | u.LowPart; }
CpuMeter::CpuMeter() : first(true) {
    prevIdle.QuadPart = 0; prevKernel.QuadPart = 0; prevUser.QuadPart = 0;
}
int CpuMeter::sample() {
    FILETIME idle, kernel, user;
    if (!GetSystemTimes(&idle, &kernel, &user)) return 0;
    ULARGE_INTEGER i, k, u;
    i.LowPart = idle.dwLowDateTime; i.HighPart = idle.dwHighDateTime;
    k.LowPart = kernel.dwLowDateTime; k.HighPart = kernel.dwHighDateTime;
    u.LowPart = user.dwLowDateTime; u.HighPart = user.dwHighDateTime;
    int pct = 0;
    if (!first) {
        ULONGLONG di = U64(i) - U64(prevIdle);
        ULONGLONG dk = U64(k) - U64(prevKernel);
        ULONGLONG du = U64(u) - U64(prevUser);
        ULONGLONG tot = dk + du;
        if (tot > 0) pct = (int)((tot - di) * 100 / tot);
        if (pct < 0) pct = 0; if (pct > 100) pct = 100;
    }
    prevIdle = i; prevKernel = k; prevUser = u; first = false;
    return pct;
}

// ---------- Power ----------
std::wstring GuidToWString(const GUID& g) {
    wchar_t* s = NULL;
    if (StringFromCLSID(g, &s) != S_OK || !s) return L"";
    std::wstring r = s;
    CoTaskMemFree(s);
    // strip braces, lowercase
    if (r.size() >= 2 && r.front() == L'{' && r.back() == L'}')
        r = r.substr(1, r.size() - 2);
    return ToLower(r);
}
bool WStringToGuid(const std::wstring& s, GUID& g) {
    std::wstring t = s;
    if (t.empty()) return false;
    if (t.front() != L'{') t = L"{" + t + L"}";
    return CLSIDFromString(t.c_str(), &g) == S_OK;
}
bool PowerGetActiveGuid(GUID& g) {
    GUID* p = NULL;
    if (PowerGetActiveScheme(NULL, &p) != ERROR_SUCCESS || !p) return false;
    g = *p; CoTaskMemFree(p);
    return true;
}
std::wstring PowerGetName(const GUID& g) {
    UCHAR* buf = NULL; DWORD sz = 0;
    if (PowerReadFriendlyName(NULL, &g, NULL, NULL, NULL, &sz) != ERROR_SUCCESS || sz == 0)
        return L"Unknown";
    buf = (UCHAR*)malloc(sz + 2);
    if (!buf) return L"Unknown";
    std::wstring name = L"Unknown";
    if (PowerReadFriendlyName(NULL, &g, NULL, NULL, buf, &sz) == ERROR_SUCCESS)
        name = (wchar_t*)buf;
    free(buf);
    return name;
}
std::wstring PowerGetActiveName() {
    GUID g;
    if (!PowerGetActiveGuid(g)) return L"Unknown";
    return PowerGetName(g);
}
bool PowerSetActive(const GUID& g) {
    return PowerSetActiveScheme(NULL, &g) == ERROR_SUCCESS;
}
bool PowerEnsureUltimate(GUID& g) {
    // Well-known Ultimate Performance GUID
    const GUID ULT = {0xe9a42b02,0xd5df,0x448d,{0xaa,0x00,0x03,0xf1,0x47,0x49,0xeb,0x61}};
    // Check if present
    DWORD idx = 0; GUID tmp; DWORD sz = sizeof(tmp);
    bool found = false;
    while (PowerEnumerate(NULL, NULL, NULL, ACCESS_SCHEME, idx, (UCHAR*)&tmp, &sz) == ERROR_SUCCESS) {
        if (memcmp(&tmp, &ULT, sizeof(GUID)) == 0) { found = true; break; }
        idx++; sz = sizeof(tmp);
    }
    if (!found) {
        wchar_t sysdir[MAX_PATH]; GetSystemDirectoryW(sysdir, MAX_PATH);
        std::wstring ppcfg = JoinPath(sysdir, L"powercfg.exe");
        RunHidden(ppcfg.c_str(), L"/duplicatescheme e9a42b02-d5df-448d-aa00-03f14749eb61");
        // re-check
        idx = 0; sz = sizeof(tmp); found = false;
        while (PowerEnumerate(NULL, NULL, NULL, ACCESS_SCHEME, idx, (UCHAR*)&tmp, &sz) == ERROR_SUCCESS) {
            if (memcmp(&tmp, &ULT, sizeof(GUID)) == 0) { found = true; break; }
            idx++; sz = sizeof(tmp);
        }
    }
    if (found) { g = ULT; return true; }
    // Fallback: High performance well-known GUID (verify it exists first)
    const GUID HIP = {0x8c5e7fda,0xe8bf,0x4a96,{0x9a,0x85,0xa6,0xe2,0x3a,0x8c,0x63,0x5c}};
    idx = 0; sz = sizeof(tmp); found = false;
    while (PowerEnumerate(NULL, NULL, NULL, ACCESS_SCHEME, idx, (UCHAR*)&tmp, &sz) == ERROR_SUCCESS) {
        if (memcmp(&tmp, &HIP, sizeof(GUID)) == 0) { found = true; break; }
        idx++; sz = sizeof(tmp);
    }
    if (!found) return false;
    g = HIP;
    return true;
}
bool PowerReadSetting(const GUID& scheme, const GUID& sub, const GUID& setting, DWORD& ac, DWORD& dc) {
    ac = dc = 0;
    LONG a = PowerReadACValueIndex(NULL, &scheme, &sub, &setting, &ac);
    LONG b = PowerReadDCValueIndex(NULL, &scheme, &sub, &setting, &dc);
    return a == ERROR_SUCCESS && b == ERROR_SUCCESS;
}
bool PowerWriteSetting(const GUID& scheme, const GUID& sub, const GUID& setting, DWORD ac, DWORD dc) {
    LONG a = PowerWriteACValueIndex(NULL, &scheme, &sub, &setting, ac);
    LONG b = PowerWriteDCValueIndex(NULL, &scheme, &sub, &setting, dc);
    if (a != ERROR_SUCCESS || b != ERROR_SUCCESS) return false;
    PowerSetActiveScheme(NULL, &scheme); // re-apply
    return true;
}

// ---------- Display ----------
int SetMaxRefreshRate() {
    DEVMODEW cur; cur.dmSize = sizeof(cur);
    if (!EnumDisplaySettingsW(NULL, ENUM_CURRENT_SETTINGS, &cur)) return 0;
    int best = cur.dmDisplayFrequency;
    DEVMODEW dm;
    for (int i = 0; ; i++) {
        ZeroMemory(&dm, sizeof(dm)); dm.dmSize = sizeof(dm);
        if (!EnumDisplaySettingsW(NULL, i, &dm)) break;
        if (dm.dmPelsWidth == cur.dmPelsWidth && dm.dmPelsHeight == cur.dmPelsHeight &&
            dm.dmBitsPerPel == cur.dmBitsPerPel) {
            if ((int)dm.dmDisplayFrequency > best) best = dm.dmDisplayFrequency;
        }
    }
    if (best <= cur.dmDisplayFrequency) return 0; // already max
    DEVMODEW set = cur;
    set.dmDisplayFrequency = best;
    set.dmFields = DM_DISPLAYFREQUENCY;
    LONG r = ChangeDisplaySettingsW(&set, CDS_UPDATEREGISTRY);
    if (r != DISP_CHANGE_SUCCESSFUL) return 0;
    return best;
}

// ---------- Memory ----------
typedef BOOL (WINAPI *SetSystemFileCacheSizeFn)(SIZE_T, SIZE_T, DWORD);
bool PurgeStandbyList() {
    EnablePrivilege(SE_INCREASE_QUOTA_NAME);
    HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
    if (!k32) return false;
    SetSystemFileCacheSizeFn fn = (SetSystemFileCacheSizeFn)GetProcAddress(k32, "SetSystemFileCacheSize");
    if (!fn) return false;
    // Purge standby list: Min=-1, Max=-1, Flags=0
    if (!fn((SIZE_T)-1, (SIZE_T)-1, 0)) return false;
    return true;
}

// ---------- Timer resolution ----------
typedef LONG (WINAPI *NtSetTimerResFn)(ULONG, BOOLEAN, PULONG);
typedef LONG (WINAPI *NtQueryTimerResFn)(PULONG, PULONG, PULONG);
bool TimerSetMin(ULONG* prev) {
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (!ntdll) return false;
    NtQueryTimerResFn q = (NtQueryTimerResFn)GetProcAddress(ntdll, "NtQueryTimerResolution");
    NtSetTimerResFn s = (NtSetTimerResFn)GetProcAddress(ntdll, "NtSetTimerResolution");
    if (!q || !s) return false;
    ULONG minR = 0, maxR = 0, cur = 0;
    if (q(&minR, &maxR, &cur) != 0) return false;
    if (prev) *prev = cur;
    ULONG actual = 0;
    return s(minR, TRUE, &actual) == 0; // minR = finest resolution (e.g. 0.5ms)
}
void TimerRestore(ULONG prev) {
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (!ntdll) return;
    NtSetTimerResFn s = (NtSetTimerResFn)GetProcAddress(ntdll, "NtSetTimerResolution");
    if (!s) return;
    ULONG actual = 0;
    s(prev, FALSE, &actual);
}
bool TimerQuery(ULONG& cur, ULONG& min_, ULONG& max_) {
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (!ntdll) return false;
    NtQueryTimerResFn q = (NtQueryTimerResFn)GetProcAddress(ntdll, "NtQueryTimerResolution");
    if (!q) return false;
    return q(&min_, &max_, &cur) == 0;
}

// ---------- Restore point ----------
// Layout-compatible with RESTOREPOINTINFOW / STATEMGRSTATUS (srrestoreptapi.h)
struct RPInfoW { DWORD dwEventType; DWORD dwRestorePtType; INT64 llSequenceNumber; wchar_t szDescription[256]; };
struct SMStatusW { LONG nStatus; INT64 llSequenceNumber; };
typedef BOOL (WINAPI *SRSetRestorePointWFn)(RPInfoW*, SMStatusW*);
bool CreateRestorePoint(const std::wstring& name) {
    HMODULE sr = LoadLibraryW(L"srclient.dll");
    if (!sr) return false;
    SRSetRestorePointWFn fn = (SRSetRestorePointWFn)GetProcAddress(sr, "SRSetRestorePointW");
    bool ok = false;
    if (fn) {
        RPInfoW begin; ZeroMemory(&begin, sizeof(begin));
        begin.dwEventType = 100; // BEGIN_SYSTEM_CHANGE
        begin.dwRestorePtType = 12; // MODIFY_SETTINGS
        StringCchCopyW(begin.szDescription, 256, name.c_str());
        SMStatusW st; ZeroMemory(&st, sizeof(st));
        if (fn(&begin, &st)) {
            RPInfoW end = begin;
            end.dwEventType = 101; // END_SYSTEM_CHANGE
            end.llSequenceNumber = st.llSequenceNumber;
            ok = fn(&end, &st) != FALSE;
        }
    }
    FreeLibrary(sr);
    return ok;
}

// ---------- Processes ----------
DWORD_PTR SystemAffinityMask() {
    DWORD_PTR pam = 0, sam = 0;
    GetProcessAffinityMask(GetCurrentProcess(), &pam, &sam);
    return pam ? pam : 1;
}
bool FindPidsByName(const std::wstring& exe, std::vector<DWORD>& pids) {
    pids.clear();
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32W pe; pe.dwSize = sizeof(pe);
    std::wstring want = ToLower(exe);
    if (Process32FirstW(snap, &pe)) {
        do {
            if (ToLower(pe.szExeFile) == want) pids.push_back(pe.th32ProcessID);
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return !pids.empty();
}
int KillProcessesByNames(const std::vector<std::wstring>& names) {
    DWORD self = GetCurrentProcessId();
    int killed = 0;
    for (size_t i = 0; i < names.size(); i++) {
        std::vector<DWORD> pids;
        if (!FindPidsByName(names[i], pids)) continue;
        for (size_t j = 0; j < pids.size(); j++) {
            if (pids[j] == self || pids[j] < 100) continue;
            HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, pids[j]);
            if (h) {
                if (TerminateProcess(h, 1)) killed++;
                CloseHandle(h);
            }
        }
    }
    return killed;
}
bool SetProcessGameOpts(DWORD pid, int priority, int affinityMode) {
    HANDLE h = OpenProcess(PROCESS_SET_INFORMATION | PROCESS_QUERY_INFORMATION, FALSE, pid);
    if (!h) return false;
    DWORD cls = NORMAL_PRIORITY_CLASS;
    if (priority == 1) cls = ABOVE_NORMAL_PRIORITY_CLASS;
    else if (priority >= 2) cls = HIGH_PRIORITY_CLASS;
    SetPriorityClass(h, cls); // best effort
    if (affinityMode == 1) {
        DWORD_PTR sys = SystemAffinityMask();
        DWORD_PTR mask = sys & ~(DWORD_PTR)1; // drop CPU0
        if (mask) SetProcessAffinityMask(h, mask);
    }
    CloseHandle(h);
    return true;
}

// ---------- Junk cleaner ----------
static unsigned long long DeleteTreeContents(const std::wstring& dir) {
    unsigned long long freed = 0;
    std::wstring pat = JoinPath(dir, L"*");
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pat.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    do {
        if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
        std::wstring full = JoinPath(dir, fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            freed += DeleteTreeContents(full);
            RemoveDirectoryW(full.c_str());
        } else {
            ULARGE_INTEGER sz; sz.LowPart = fd.nFileSizeLow; sz.HighPart = fd.nFileSizeHigh;
            if (DeleteFileW(full.c_str())) freed += sz.QuadPart;
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return freed;
}
unsigned long long CleanJunkFiles() {
    unsigned long long freed = 0;
    wchar_t tmp[MAX_PATH], win[MAX_PATH];
    if (GetTempPathW(MAX_PATH, tmp)) freed += DeleteTreeContents(tmp);
    if (GetWindowsDirectoryW(win, MAX_PATH)) {
        freed += DeleteTreeContents(JoinPath(win, L"Temp"));
        freed += DeleteTreeContents(JoinPath(win, L"SoftwareDistribution\\Download"));
        // DirectX shader cache (per-user + system)
        wchar_t local[MAX_PATH];
        if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, local))) {
            freed += DeleteTreeContents(JoinPath(local, L"D3DSCache"));
            freed += DeleteTreeContents(JoinPath(local, L"Microsoft\\Windows\\INetCache"));
        }
    }
    return freed;
}

// ---------- Services ----------
bool ServiceGetStart(const wchar_t* svc, DWORD& start) {
    SC_HANDLE mgr = OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT);
    if (!mgr) return false;
    SC_HANDLE h = OpenServiceW(mgr, svc, SERVICE_QUERY_CONFIG);
    bool ok = false;
    if (h) {
        BYTE buf[4096]; DWORD need = 0;
        if (QueryServiceConfigW(h, (QUERY_SERVICE_CONFIGW*)buf, sizeof(buf), &need)) {
            start = ((QUERY_SERVICE_CONFIGW*)buf)->dwStartType;
            ok = true;
        }
        CloseHandle(h);
    }
    CloseHandle(mgr);
    return ok;
}
bool ServiceSetStart(const wchar_t* svc, DWORD start) {
    SC_HANDLE mgr = OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT);
    if (!mgr) return false;
    SC_HANDLE h = OpenServiceW(mgr, svc, SERVICE_CHANGE_CONFIG);
    bool ok = false;
    if (h) {
        ok = ChangeServiceConfigW(h, SERVICE_NO_CHANGE, start, SERVICE_NO_CHANGE,
            NULL, NULL, NULL, NULL, NULL, NULL, NULL) != FALSE;
        CloseHandle(h);
    }
    CloseHandle(mgr);
    return ok;
}

// ---------- BCD ----------
static std::wstring BcdExe() {
    wchar_t sysdir[MAX_PATH]; GetSystemDirectoryW(sysdir, MAX_PATH);
    return JoinPath(sysdir, L"bcdedit.exe");
}
bool BcdQuery(const wchar_t* token, std::wstring& value, bool& present) {
    value.clear(); present = false;
    std::wstring out;
    if (!RunCapture(BcdExe().c_str(), L"/enum {current}", out)) return false;
    std::wstring low = ToLower(out), tok = ToLower(token);
    size_t p = low.find(tok);
    if (p == std::wstring::npos) return true; // not present
    present = true;
    // rest of line after token
    size_t e = low.find(L'\n', p);
    std::wstring line = out.substr(p, (e == std::wstring::npos) ? std::wstring::npos : e - p);
    // value is last word
    size_t a = line.find_last_of(L" \t");
    value = (a == std::wstring::npos) ? L"" : line.substr(a + 1);
    return true;
}
bool BcdSet(const wchar_t* token, const wchar_t* value) {
    DWORD code = 0;
    std::wstring args = WFormat(L"/set {current} %s %s", token, value);
    if (!RunHidden(BcdExe().c_str(), args.c_str(), &code)) return false;
    return code == 0;
}
bool BcdDelete(const wchar_t* token) {
    DWORD code = 0;
    std::wstring args = WFormat(L"/deletevalue {current} %s", token);
    if (!RunHidden(BcdExe().c_str(), args.c_str(), &code)) return false;
    return code == 0;
}

// ---------- Display resolution ----------
bool GetCurrentResolution(int& w, int& h) {
    w = GetSystemMetrics(SM_CXSCREEN);
    h = GetSystemMetrics(SM_CYSCREEN);
    return w > 0 && h > 0;
}
bool SetDisplayResolution(int w, int h) {
    DEVMODEW cur; cur.dmSize = sizeof(cur);
    if (!EnumDisplaySettingsW(NULL, ENUM_CURRENT_SETTINGS, &cur)) return false;
    int bestHz = 0;
    DEVMODEW dm;
    for (int i = 0; ; i++) {
        ZeroMemory(&dm, sizeof(dm)); dm.dmSize = sizeof(dm);
        if (!EnumDisplaySettingsW(NULL, i, &dm)) break;
        if (dm.dmPelsWidth == w && dm.dmPelsHeight == h && dm.dmBitsPerPel == cur.dmBitsPerPel) {
            if ((int)dm.dmDisplayFrequency > bestHz) bestHz = dm.dmDisplayFrequency;
        }
    }
    if (bestHz <= 0) return false;
    DEVMODEW set = cur;
    set.dmPelsWidth = w; set.dmPelsHeight = h; set.dmDisplayFrequency = bestHz;
    set.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_DISPLAYFREQUENCY;
    return ChangeDisplaySettingsW(&set, CDS_UPDATEREGISTRY) == DISP_CHANGE_SUCCESSFUL;
}
bool GetNativeResolution(int& w, int& h) {
    w = 0; h = 0;
    long best = 0;
    DEVMODEW dm;
    for (int i = 0; ; i++) {
        ZeroMemory(&dm, sizeof(dm)); dm.dmSize = sizeof(dm);
        if (!EnumDisplaySettingsW(NULL, i, &dm)) break;
        long area = (long)dm.dmPelsWidth * dm.dmPelsHeight;
        if (area > best) { best = area; w = dm.dmPelsWidth; h = dm.dmPelsHeight; }
    }
    return w > 0;
}

// ---------- Startup manager ----------
static const wchar_t* kRunSub = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run";
static const wchar_t* kRunBkSub = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\RunDisabledByFPS";

static void StartupEnumKey(HKEY root, int loc, std::vector<StartupItem>& out) {
    HKEY h = NULL;
    if (RegOpenKeyExW(root, kRunSub, 0, KEY_READ, &h) == ERROR_SUCCESS) {
        wchar_t name[256];
        BYTE data[1024];
        for (DWORD i = 0; ; i++) {
            DWORD nl = 256, dl = sizeof(data), tp = 0;
            if (RegEnumValueW(h, i, name, &nl, NULL, &tp, data, &dl) != ERROR_SUCCESS) break;
            name[255] = 0; data[sizeof(data) - 2] = 0; data[sizeof(data) - 1] = 0;
            if (tp != REG_SZ && tp != REG_EXPAND_SZ) continue;
            StartupItem it;
            it.name = name; it.cmd = (wchar_t*)data; it.loc = loc; it.enabled = true;
            out.push_back(it);
        }
        RegCloseKey(h);
    }
    if (RegOpenKeyExW(root, kRunBkSub, 0, KEY_READ, &h) == ERROR_SUCCESS) {
        wchar_t name[256];
        BYTE data[1024];
        for (DWORD i = 0; ; i++) {
            DWORD nl = 256, dl = sizeof(data), tp = 0;
            if (RegEnumValueW(h, i, name, &nl, NULL, &tp, data, &dl) != ERROR_SUCCESS) break;
            name[255] = 0; data[sizeof(data) - 2] = 0; data[sizeof(data) - 1] = 0;
            StartupItem it;
            it.name = name; it.cmd = (wchar_t*)data; it.loc = loc; it.enabled = false;
            out.push_back(it);
        }
        RegCloseKey(h);
    }
}
std::vector<StartupItem> StartupEnum() {
    std::vector<StartupItem> out;
    StartupEnumKey(HKEY_CURRENT_USER, 0, out);
    StartupEnumKey(HKEY_LOCAL_MACHINE, 1, out);
    wchar_t path[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_STARTUP, NULL, 0, path))) {
        std::wstring dis = JoinPath(path, L"DisabledByFPS");
        WIN32_FIND_DATAW fd;
        HANDLE f = FindFirstFileW(JoinPath(path, L"*.lnk").c_str(), &fd);
        if (f != INVALID_HANDLE_VALUE) {
            do {
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
                StartupItem it;
                it.name = fd.cFileName; it.cmd = JoinPath(path, fd.cFileName);
                it.loc = 2; it.enabled = true;
                out.push_back(it);
            } while (FindNextFileW(f, &fd));
            FindClose(f);
        }
        f = FindFirstFileW(JoinPath(dis, L"*.lnk").c_str(), &fd);
        if (f != INVALID_HANDLE_VALUE) {
            do {
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
                StartupItem it;
                it.name = fd.cFileName; it.cmd = JoinPath(dis, fd.cFileName);
                it.loc = 2; it.enabled = false;
                out.push_back(it);
            } while (FindNextFileW(f, &fd));
            FindClose(f);
        }
    }
    return out;
}
bool StartupSetEnabled(const StartupItem& it, bool on) {
    if (it.enabled == on) return true;
    if (it.loc == 2) {
        wchar_t path[MAX_PATH];
        if (FAILED(SHGetFolderPathW(NULL, CSIDL_STARTUP, NULL, 0, path))) return false;
        std::wstring dis = JoinPath(path, L"DisabledByFPS");
        EnsureDir(dis);
        std::wstring src = on ? JoinPath(dis, it.name) : JoinPath(path, it.name);
        std::wstring dst = on ? JoinPath(path, it.name) : JoinPath(dis, it.name);
        return MoveFileW(src.c_str(), dst.c_str()) != FALSE;
    }
    HKEY root = (it.loc == 0) ? HKEY_CURRENT_USER : HKEY_LOCAL_MACHINE;
    const wchar_t* from = on ? kRunBkSub : kRunSub;
    const wchar_t* to = on ? kRunSub : kRunBkSub;
    std::wstring v;
    if (!RegGetString(root, from, it.name.c_str(), v)) return false;
    HKEY h = NULL;
    if (RegCreateKeyExW(root, to, 0, NULL, 0, KEY_WRITE, NULL, &h, NULL) != ERROR_SUCCESS) return false;
    LSTATUS r = RegSetValueExW(h, it.name.c_str(), 0, REG_SZ,
        (const BYTE*)v.c_str(), (DWORD)((v.size() + 1) * 2));
    RegCloseKey(h);
    if (r != ERROR_SUCCESS) return false;
    HKEY h2 = NULL;
    if (RegOpenKeyExW(root, from, 0, KEY_WRITE, &h2) == ERROR_SUCCESS) {
        RegDeleteValueW(h2, it.name.c_str());
        RegCloseKey(h2);
    }
    return true;
}

// ---------- Specs summary ----------
std::wstring SysSummary() {
    std::wstring os = L"Windows", dv, bld;
    RegGetString(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", L"ProductName", os);
    if (!RegGetString(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", L"DisplayVersion", dv))
        RegGetString(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", L"ReleaseId", dv);
    RegGetString(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", L"CurrentBuild", bld);
    int w = 0, h = 0;
    GetCurrentResolution(w, h);
    std::wstring s = WFormat(L"%s %s (build %s)\r\nCPU: %s (%d cores)\r\nRAM: %s\r\nGPU: %s\r\nDisplay: %dx%d\r\nFPS Booster score: %d/100",
        os.c_str(), dv.c_str(), bld.c_str(), SysCpuName().c_str(), SysCpuCount(),
        SysRamString().c_str(), SysGpuName().c_str(), w, h, g_lastScore);
    return s;
}
bool CopyTextToClipboard(const std::wstring& s) {
    if (!OpenClipboard(NULL)) return false;
    EmptyClipboard();
    size_t n = (s.size() + 1) * 2;
    HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, n);
    if (!g) { CloseClipboard(); return false; }
    memcpy(GlobalLock(g), s.c_str(), n);
    GlobalUnlock(g);
    bool ok = SetClipboardData(CF_UNICODETEXT, g) != NULL;
    if (!ok) GlobalFree(g);
    CloseClipboard();
    return ok;
}
