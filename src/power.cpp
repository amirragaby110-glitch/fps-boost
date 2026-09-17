// FPS Booster Pro - Beast Mode power management (full hardware power for gaming)
#include "app.h"
#include <string.h>

#define BEAST_NAME L"FPS Booster Beast Mode"

// ---- Subgroups ----
static const GUID SUB_PROC  = {0x54533251,0x82be,0x4824,{0x96,0xc1,0x47,0xb6,0x0b,0x74,0x0d,0x00}};
static const GUID SUB_USB   = {0x2a737441,0x1930,0x4402,{0x8d,0x77,0xb2,0xbe,0xbb,0xa3,0x08,0xa3}};
static const GUID SUB_PCIE  = {0x501a4d13,0x42af,0x4429,{0x9f,0xd1,0xa8,0x21,0x8c,0x26,0x8e,0x20}};
static const GUID SUB_VIDEO = {0x7516b95f,0xf776,0x4464,{0x8c,0x53,0x06,0x16,0x7f,0x40,0xcc,0x99}};
static const GUID SUB_SLEEP = {0x238c9fa8,0x0aad,0x41ed,{0x83,0xf4,0x97,0xbe,0x24,0x2c,0x8f,0x20}};
static const GUID SUB_DISK  = {0x0012ee47,0x9041,0x4b5d,{0x9b,0x77,0x53,0x5f,0xba,0x8b,0x14,0x42}};
static const GUID SUB_WIFI  = {0x19cbb8fa,0x5279,0x450e,{0x9f,0xac,0x8a,0x3d,0x5f,0xed,0xd0,0xc1}};
// ---- Settings ----
static const GUID SET_CPUMIN   = {0x893dee8e,0x2bef,0x41e0,{0x89,0xc6,0xb5,0x5d,0x09,0x29,0x96,0x4c}};
static const GUID SET_CPUMAX   = {0xbc5038f7,0x23e0,0x4960,{0x96,0xda,0x33,0xab,0xaf,0x59,0x35,0xec}};
static const GUID SET_COREMIN  = {0x0cc5b647,0xc1df,0x4637,{0x89,0x1a,0xde,0xc3,0x5c,0x31,0x85,0x83}};
static const GUID SET_COREMAX  = {0xea062031,0x0e34,0x4ff1,{0x9b,0x6d,0xeb,0x10,0x59,0x33,0x40,0x28}};
static const GUID SET_BOOSTMODE= {0xbe337238,0x0d82,0x4146,{0xa9,0x60,0x4f,0x37,0x49,0xd4,0x70,0xc7}};
static const GUID SET_BOOSTPOL = {0x45bcc044,0xd885,0x43e2,{0x86,0x05,0xee,0x0e,0xc6,0xe9,0x6b,0x59}};
static const GUID SET_EPP      = {0x36687f9f,0xe3a5,0x4dbf,{0xb1,0xdc,0x15,0xeb,0x38,0x1c,0x68,0x63}};
static const GUID SET_COOLING  = {0x94d3a615,0xa899,0x4ac5,{0xae,0x2b,0xe4,0xd8,0xf6,0x34,0x36,0x7f}};
static const GUID SET_USBSEL   = {0x48e6b7a6,0x50f5,0x4782,{0xa5,0xd4,0x53,0xbb,0x8f,0x07,0xe2,0x26}};
static const GUID SET_PCIELINK = {0xee12f906,0xd277,0x404b,{0xb6,0xda,0xe5,0xfa,0x1a,0x57,0x6d,0xf5}};
static const GUID SET_WIFISAVE = {0x12bbebe6,0x58d6,0x4636,{0x95,0xbb,0x32,0x17,0xf8,0xcb,0xdc,0xc3}};
static const GUID SET_VIDEOIDLE= {0x3c0bc021,0xc8a8,0x4e07,{0xa9,0x73,0x6b,0x14,0xcb,0xcb,0x2b,0x7e}};
static const GUID SET_SLEEPIDLE= {0x29f6c1db,0x86da,0x432c,{0x9f,0x9c,0xf2,0xbd,0xe8,0xcb,0x68,0x39}};
static const GUID SET_HIBIDLE  = {0x9d7815a6,0x7ee4,0x497e,{0x88,0x8a,0x51,0x5a,0x05,0xf3,0x1d,0x9b}};
static const GUID SET_DISKIDLE = {0x6738e2c4,0xe8a5,0x4a42,{0xb1,0x6a,0xe0,0x40,0xe7,0x69,0x75,0x6e}};
static const GUID SCHEME_HIP   = {0x8c5e7fda,0xe8bf,0x4a96,{0x9a,0x85,0xa6,0xe2,0x3a,0x8c,0x63,0x5c}};

// fmt: 0 plain, 1 percent, 2 time(0=never)
static const PowerSetting kPS[] = {
{"cpumin",  "CPU minimum speed", "حداقل سرعت پردازنده", SUB_PROC, SET_CPUMIN, 100, 100, 1},
{"cpumax",  "CPU maximum speed", "حداکثر سرعت پردازنده", SUB_PROC, SET_CPUMAX, 100, 100, 1},
{"coresmin","CPU cores online (min)", "حداقل هسته‌های فعال", SUB_PROC, SET_COREMIN, 100, 100, 1},
{"coresmax","CPU cores online (max)", "حداکثر هسته‌های فعال", SUB_PROC, SET_COREMAX, 100, 100, 1},
{"boostmode","Turbo Boost mode (Aggressive)", "حالت توربو بوست (تهاجمی)", SUB_PROC, SET_BOOSTMODE, 2, 2, 0},
{"boostpol","Turbo Boost policy", "سیاست توربو بوست", SUB_PROC, SET_BOOSTPOL, 100, 100, 1},
{"epp",     "Energy preference: performance", "ترجیح انرژی: کارایی", SUB_PROC, SET_EPP, 0, 0, 0},
{"cooling", "Cooling policy (Active)", "سیاست خنک‌سازی (فعال)", SUB_PROC, SET_COOLING, 1, 1, 0},
{"usb",     "USB selective suspend (Off)", "تعلیق انتخابی USB (خاموش)", SUB_USB, SET_USBSEL, 0, 0, 0},
{"pcie",    "PCIe power saving (Off)", "صرفه‌جویی PCIe (خاموش)", SUB_PCIE, SET_PCIELINK, 0, 0, 0},
{"wifi",    "Wi-Fi saving (Max performance)", "صرفه‌جویی وای‌فای (حداکثر کارایی)", SUB_WIFI, SET_WIFISAVE, 0, 0, 0},
{"display", "Turn off display after", "خاموشی نمایشگر بعد از", SUB_VIDEO, SET_VIDEOIDLE, 0, 300, 2},
{"sleep",   "Sleep after", "رفتن به خواب بعد از", SUB_SLEEP, SET_SLEEPIDLE, 0, 600, 2},
{"hiber",   "Hibernate after", "هایبرنیت بعد از", SUB_SLEEP, SET_HIBIDLE, 0, 3600, 2},
{"disk",    "Turn off hard disk after", "خاموشی هارد بعد از", SUB_DISK, SET_DISKIDLE, 0, 600, 2},
};

const PowerSetting* PowerSettings(int* count) {
    if (count) *count = (int)(sizeof(kPS) / sizeof(kPS[0]));
    return kPS;
}

// ================= Backup (pset.json) =================
struct PSetBk { std::wstring sch, sub, set; DWORD ac, dc; };
static std::vector<PSetBk> g_pset;
static CRITICAL_SECTION g_psCs; static bool g_psInit = false;
static bool g_psLoaded = false;
static void PLock() { if (!g_psInit) { InitializeCriticalSection(&g_psCs); g_psInit = true; } EnterCriticalSection(&g_psCs); }
static void PUnlock() { LeaveCriticalSection(&g_psCs); }

static bool PSetSave() {
    PLock();
    std::string j = "{\"pset\":[";
    for (size_t i = 0; i < g_pset.size(); i++) {
        if (i) j += ",";
        char nb[64];
        snprintf(nb, 64, "{\"ac\":%u,\"dc\":%u", g_pset[i].ac, g_pset[i].dc);
        j += nb;
        j += ",\"sch\":\"" + JsonEscapeW(g_pset[i].sch) + "\",\"sub\":\"" + JsonEscapeW(g_pset[i].sub) +
             "\",\"set\":\"" + JsonEscapeW(g_pset[i].set) + "\"}";
    }
    j += "]}";
    PUnlock();
    return WriteFileText(JoinPath(g_dataDir, L"pset.json"), j);
}
static void PSetLoad() {
    PLock();
    if (g_psLoaded) { PUnlock(); return; }
    g_psLoaded = true;
    PUnlock();
    std::string text;
    if (!ReadFileText(JoinPath(g_dataDir, L"pset.json"), text)) return;
    JVal root;
    if (!ParseJson(text, root) || root.type != JVal::OBJ) return;
    const JVal* a = root.find("pset");
    if (!a || a->type != JVal::ARR) return;
    PLock();
    for (size_t i = 0; i < a->arr.size(); i++) {
        const JVal& e = a->arr[i];
        PSetBk b;
        b.sch = e.wstr("sch"); b.sub = e.wstr("sub"); b.set = e.wstr("set");
        b.ac = (DWORD)e.num("ac", 0); b.dc = (DWORD)e.num("dc", 0);
        g_pset.push_back(b);
    }
    PUnlock();
}
// capture current values once per (scheme,sub,setting)
static void PBackup(const GUID& sch, const GUID& sub, const GUID& st) {
    PSetLoad();
    std::wstring S = GuidToWString(sch), U = GuidToWString(sub), T = GuidToWString(st);
    PLock();
    for (size_t i = 0; i < g_pset.size(); i++)
        if (g_pset[i].sch == S && g_pset[i].sub == U && g_pset[i].set == T) { PUnlock(); return; }
    PUnlock();
    DWORD ac = 0, dc = 0;
    PowerReadSetting(sch, sub, st, ac, dc); // best effort
    PLock();
    PSetBk b; b.sch = S; b.sub = U; b.set = T; b.ac = ac; b.dc = dc;
    g_pset.push_back(b);
    PUnlock();
    PSetSave();
}
static bool PGetBackup(const GUID& sch, const GUID& sub, const GUID& st, DWORD& ac, DWORD& dc) {
    PSetLoad();
    std::wstring S = GuidToWString(sch), U = GuidToWString(sub), T = GuidToWString(st);
    PLock();
    for (size_t i = 0; i < g_pset.size(); i++)
        if (g_pset[i].sch == S && g_pset[i].sub == U && g_pset[i].set == T) {
            ac = g_pset[i].ac; dc = g_pset[i].dc; PUnlock(); return true;
        }
    PUnlock();
    return false;
}

// ================= Active-scheme operations =================
bool PowerReadActive(const PowerSetting& ps, DWORD& ac, DWORD& dc) {
    GUID g;
    if (!PowerGetActiveGuid(g)) return false;
    return PowerReadSetting(g, ps.sub, ps.set, ac, dc);
}
int PowerApplyAllActive() {
    GUID g;
    if (!PowerGetActiveGuid(g)) return 0;
    int n = 0, ok = 0;
    const PowerSetting* ps = PowerSettings(&n);
    for (int i = 0; i < n; i++) {
        PBackup(g, ps[i].sub, ps[i].set);
        if (PowerWriteSetting(g, ps[i].sub, ps[i].set, ps[i].ac, ps[i].dc)) ok++;
        else LogW(L"power write failed: %S", ps[i].key);
    }
    LogW(L"PowerApplyAllActive: %d/%d", ok, n);
    return ok;
}
int PowerRestoreAllActive() {
    GUID g;
    if (!PowerGetActiveGuid(g)) return 0;
    int n = 0, ok = 0;
    const PowerSetting* ps = PowerSettings(&n);
    for (int i = 0; i < n; i++) {
        DWORD ac = 0, dc = 0;
        if (!PGetBackup(g, ps[i].sub, ps[i].set, ac, dc)) continue;
        if (PowerWriteSetting(g, ps[i].sub, ps[i].set, ac, dc)) ok++;
    }
    LogW(L"PowerRestoreAllActive: %d/%d", ok, n);
    return ok;
}

// ================= Beast Mode scheme =================
static int g_beastApplied = 0, g_beastTotal = 0;
void BeastLastOp(int& applied, int& total) { applied = g_beastApplied; total = g_beastTotal; }

static bool SchemeExists(const GUID& g) {
    DWORD idx = 0; GUID t; DWORD sz = sizeof(t);
    while (PowerEnumerate(NULL, NULL, NULL, ACCESS_SCHEME, idx, (UCHAR*)&t, &sz) == ERROR_SUCCESS) {
        if (memcmp(&t, &g, sizeof(GUID)) == 0) return true;
        idx++; sz = sizeof(t);
    }
    return false;
}

bool BeastEnsure(GUID& g) {
    GUID b;
    if (WStringToGuid(g_beastGuid, b) && SchemeExists(b)) { g = b; return true; }
    // adopt existing scheme with our name
    DWORD idx = 0; GUID t; DWORD sz = sizeof(t);
    while (PowerEnumerate(NULL, NULL, NULL, ACCESS_SCHEME, idx, (UCHAR*)&t, &sz) == ERROR_SUCCESS) {
        if (PowerGetName(t) == BEAST_NAME) {
            g = t; g_beastGuid = GuidToWString(t); Settings_Save(); return true;
        }
        idx++; sz = sizeof(t);
    }
    // duplicate high-performance (or active) scheme
    GUID src;
    if (SchemeExists(SCHEME_HIP)) src = SCHEME_HIP;
    else if (!PowerGetActiveGuid(src)) return false;
    GUID* dup = NULL;
    if (PowerDuplicateScheme(NULL, &src, &dup) != ERROR_SUCCESS || !dup) {
        LogW(L"PowerDuplicateScheme failed");
        return false;
    }
    g = *dup;
    LocalFree(dup);
    PowerWriteFriendlyName(NULL, &g, NULL, NULL, (UCHAR*)BEAST_NAME,
        (DWORD)((wcslen(BEAST_NAME) + 1) * sizeof(wchar_t)));
    g_beastGuid = GuidToWString(g);
    Settings_Save();
    LogW(L"Beast scheme created: %s", g_beastGuid.c_str());
    return true;
}

static int BeastWriteAll(const GUID& scheme) {
    int n = 0, ok = 0;
    const PowerSetting* ps = PowerSettings(&n);
    for (int i = 0; i < n; i++) {
        PBackup(scheme, ps[i].sub, ps[i].set);
        LONG a = PowerWriteACValueIndex(NULL, &scheme, &ps[i].sub, &ps[i].set, ps[i].ac);
        LONG d = PowerWriteDCValueIndex(NULL, &scheme, &ps[i].sub, &ps[i].set, ps[i].dc);
        if (a == ERROR_SUCCESS && d == ERROR_SUCCESS) ok++;
    }
    PowerSetActiveScheme(NULL, &scheme); // single re-apply
    g_beastApplied = ok; g_beastTotal = n;
    return ok;
}

bool BeastActivate() {
    GUID cur;
    if (!PowerGetActiveGuid(cur)) return false;
    GUID b;
    if (!BeastEnsure(b)) return false;
    if (memcmp(&cur, &b, sizeof(GUID)) == 0) {
        BeastWriteAll(b); // re-assert values
        return true;
    }
    g_beastPrev = GuidToWString(cur);
    Settings_Save();
    BeastWriteAll(b);
    bool ok = PowerSetActive(b);
    LogW(L"BeastActivate: %s (%d/%d)", ok ? L"OK" : L"FAILED", g_beastApplied, g_beastTotal);
    return ok;
}
bool BeastDeactivate() {
    GUID p;
    if (!WStringToGuid(g_beastPrev, p)) return false;
    bool ok = PowerSetActive(p);
    LogW(L"BeastDeactivate: %s", ok ? L"OK" : L"FAILED");
    return ok;
}
bool BeastIsActive() {
    if (g_beastGuid.empty()) return false;
    GUID a, b;
    if (!PowerGetActiveGuid(a)) return false;
    if (!WStringToGuid(g_beastGuid, b)) return false;
    return memcmp(&a, &b, sizeof(GUID)) == 0;
}
bool BeastDelete() {
    if (BeastIsActive()) return false; // deactivate first
    GUID b;
    if (!WStringToGuid(g_beastGuid, b)) return false;
    if (PowerDeleteScheme(NULL, &b) != ERROR_SUCCESS) return false;
    g_beastGuid.clear();
    Settings_Save();
    LogW(L"Beast scheme deleted");
    return true;
}
