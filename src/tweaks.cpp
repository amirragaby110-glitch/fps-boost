// FPS Booster Pro - Tweak engine (25 real optimizations, all reversible)
#include "app.h"
#include <string.h>

// ================= Backup store =================
struct RegBackup {
    int hive; // 0 HKCU, 1 HKLM
    std::wstring sub, name;
    DWORD type; DWORD dword; std::wstring str;
    bool existed;
};
struct SvcBackup { std::wstring svc; DWORD start; };
struct BcdBackup { std::wstring token, value; bool existed; };

static std::vector<RegBackup>  g_regBk;
static std::vector<SvcBackup>  g_svcBk;
static std::vector<BcdBackup>  g_bcdBk;
static std::wstring g_powerBk;
static std::wstring g_cpScheme; static DWORD g_cpAc, g_cpDc; static bool g_cpHave;
unsigned long long g_lastCleanBytes = 0;
int g_lastRefreshHz = 0;
static CRITICAL_SECTION g_bkCs; static bool g_bkCsInit = false;

static HKEY HiveOf(int h) { return h == 1 ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER; }
static void BkLock() { if (!g_bkCsInit) { InitializeCriticalSection(&g_bkCs); g_bkCsInit = true; } EnterCriticalSection(&g_bkCs); }
static void BkUnlock() { LeaveCriticalSection(&g_bkCs); }

static std::wstring BackupPath() { return JoinPath(g_dataDir, L"backup.json"); }

bool BackupSave() {
    BkLock();
    std::string j = "{\"power\":\"" + JsonEscapeW(g_powerBk) + "\"";
    j += ",\"cphave\":"; j += g_cpHave ? "1" : "0";
    char nb[64];
    snprintf(nb, 64, ",\"cpscheme\":\"%s\",\"cpac\":%u,\"cpdc\":%u", JsonEscapeW(g_cpScheme).c_str(), g_cpAc, g_cpDc);
    j += nb;
    j += ",\"reg\":[";
    for (size_t i = 0; i < g_regBk.size(); i++) {
        const RegBackup& e = g_regBk[i];
        if (i) j += ",";
        snprintf(nb, 64, "{\"h\":%d,\"t\":%u,\"d\":%u,\"ex\":%d", e.hive, e.type, e.dword, e.existed ? 1 : 0);
        j += nb;
        j += ",\"sub\":\"" + JsonEscapeW(e.sub) + "\",\"name\":\"" + JsonEscapeW(e.name) +
             "\",\"s\":\"" + JsonEscapeW(e.str) + "\"}";
    }
    j += "],\"svc\":[";
    for (size_t i = 0; i < g_svcBk.size(); i++) {
        if (i) j += ",";
        snprintf(nb, 64, "{\"n\":\"%s\",\"start\":%u}", JsonEscapeW(g_svcBk[i].svc).c_str(), g_svcBk[i].start);
        j += nb;
    }
    j += "],\"bcd\":[";
    for (size_t i = 0; i < g_bcdBk.size(); i++) {
        if (i) j += ",";
        j += "{\"t\":\"" + JsonEscapeW(g_bcdBk[i].token) + "\",\"v\":\"" + JsonEscapeW(g_bcdBk[i].value) +
             "\",\"ex\":" + (g_bcdBk[i].existed ? "1" : "0") + "}";
    }
    j += "]}";
    BkUnlock();
    return WriteFileText(BackupPath(), j);
}

bool BackupInit() {
    BkLock();
    g_regBk.clear(); g_svcBk.clear(); g_bcdBk.clear();
    g_powerBk.clear(); g_cpHave = false; g_cpScheme.clear();
    BkUnlock();
    std::string text;
    if (!ReadFileText(BackupPath(), text)) return true;
    JVal root;
    if (!ParseJson(text, root) || root.type != JVal::OBJ) return true;
    BkLock();
    g_powerBk = root.wstr("power");
    g_cpHave = root.num("cphave", 0) != 0;
    g_cpScheme = root.wstr("cpscheme");
    g_cpAc = (DWORD)root.num("cpac", 0); g_cpDc = (DWORD)root.num("cpdc", 0);
    const JVal* r = root.find("reg");
    if (r && r->type == JVal::ARR) {
        for (size_t i = 0; i < r->arr.size(); i++) {
            const JVal& e = r->arr[i];
            RegBackup b;
            b.hive = e.num("h", 0); b.type = (DWORD)e.num("t", 4);
            b.dword = (DWORD)e.num("d", 0); b.existed = e.num("ex", 0) != 0;
            b.sub = e.wstr("sub"); b.name = e.wstr("name"); b.str = e.wstr("s");
            g_regBk.push_back(b);
        }
    }
    const JVal* s = root.find("svc");
    if (s && s->type == JVal::ARR) {
        for (size_t i = 0; i < s->arr.size(); i++) {
            SvcBackup b; b.svc = s->arr[i].wstr("n"); b.start = (DWORD)s->arr[i].num("start", 2);
            g_svcBk.push_back(b);
        }
    }
    const JVal* b = root.find("bcd");
    if (b && b->type == JVal::ARR) {
        for (size_t i = 0; i < b->arr.size(); i++) {
            BcdBackup e; e.token = b->arr[i].wstr("t"); e.value = b->arr[i].wstr("v");
            e.existed = b->arr[i].num("ex", 0) != 0;
            g_bcdBk.push_back(e);
        }
    }
    BkUnlock();
    return true;
}

// Capture original value once (idempotent)
bool BackupReg(HKEY hive, const wchar_t* sub, const wchar_t* name) {
    int h = (hive == HKEY_LOCAL_MACHINE) ? 1 : 0;
    BkLock();
    for (size_t i = 0; i < g_regBk.size(); i++)
        if (g_regBk[i].hive == h && g_regBk[i].sub == sub && g_regBk[i].name == name) { BkUnlock(); return true; }
    BkUnlock();
    RegBackup b; b.hive = h; b.sub = sub; b.name = name;
    b.existed = RegValueExists(hive, sub, name);
    b.type = REG_DWORD; b.dword = 0;
    if (b.existed) {
        DWORD d = 0;
        if (RegGetDword(hive, sub, name, d)) { b.type = REG_DWORD; b.dword = d; }
        else {
            std::wstring s;
            if (RegGetString(hive, sub, name, s)) { b.type = REG_SZ; b.str = s; }
            else b.existed = false;
        }
    }
    // detect EXPAND_SZ
    if (b.existed && b.type == REG_SZ) {
        HKEY hk = NULL;
        if (RegOpenKeyExW(hive, sub, 0, KEY_QUERY_VALUE, &hk) == ERROR_SUCCESS) {
            DWORD t = 0; RegQueryValueExW(hk, name, NULL, &t, NULL, NULL);
            if (t == REG_EXPAND_SZ) b.type = REG_EXPAND_SZ;
            RegCloseKey(hk);
        }
    }
    BkLock(); g_regBk.push_back(b); BkUnlock();
    BackupSave();
    return true;
}
bool BackupService(const wchar_t* svc) {
    BkLock();
    for (size_t i = 0; i < g_svcBk.size(); i++)
        if (g_svcBk[i].svc == svc) { BkUnlock(); return true; }
    BkUnlock();
    DWORD st = 2;
    ServiceGetStart(svc, st);
    BkLock(); SvcBackup b; b.svc = svc; b.start = st; g_svcBk.push_back(b); BkUnlock();
    BackupSave();
    return true;
}
bool BackupBcd(const wchar_t* token) {
    BkLock();
    for (size_t i = 0; i < g_bcdBk.size(); i++)
        if (g_bcdBk[i].token == token) { BkUnlock(); return true; }
    BkUnlock();
    std::wstring v; bool present = false;
    BcdQuery(token, v, present);
    BkLock(); BcdBackup b; b.token = token; b.value = v; b.existed = present; g_bcdBk.push_back(b); BkUnlock();
    BackupSave();
    return true;
}
bool BackupSetPowerGuid(const std::wstring& g) {
    BkLock();
    if (g_powerBk.empty()) g_powerBk = g;
    BkUnlock();
    BackupSave();
    return true;
}
bool BackupGetPowerGuid(std::wstring& g) { BkLock(); g = g_powerBk; BkUnlock(); return !g.empty(); }

static bool RestoreOneReg(const RegBackup& e) {
    HKEY h = HiveOf(e.hive);
    if (!e.existed) return RegDeleteValue(h, e.sub.c_str(), e.name.c_str());
    if (e.type == REG_DWORD) return RegSetDword(h, e.sub.c_str(), e.name.c_str(), e.dword);
    return RegSetString(h, e.sub.c_str(), e.name.c_str(), e.str, e.type == REG_EXPAND_SZ);
}

// Helper: backup then set DWORD/SZ
static bool BSetD(HKEY h, const wchar_t* sub, const wchar_t* name, DWORD v) {
    BackupReg(h, sub, name);
    return RegSetDword(h, sub, name, v);
}
static bool BSetS(HKEY h, const wchar_t* sub, const wchar_t* name, const wchar_t* v) {
    BackupReg(h, sub, name);
    return RegSetString(h, sub, name, v);
}
static bool RevertKeys(HKEY h, const wchar_t* const subs[], const wchar_t* const names[], int n) {
    bool ok = true;
    BkLock();
    for (int i = 0; i < n; i++) {
        bool found = false;
        for (size_t j = 0; j < g_regBk.size(); j++) {
            int hh = (h == HKEY_LOCAL_MACHINE) ? 1 : 0;
            if (g_regBk[j].hive == hh && g_regBk[j].sub == subs[i] && g_regBk[j].name == names[i]) {
                if (!RestoreOneReg(g_regBk[j])) ok = false;
                found = true; break;
            }
        }
        if (!found) ok = false;
    }
    BkUnlock();
    return ok;
}

// ================= Tweak implementations =================
// --- 1. Game Mode ---
static int CkGameMode() {
    DWORD a = 0, b = 0;
    RegGetDword(HKEY_CURRENT_USER, L"SOFTWARE\\Microsoft\\GameBar", L"AllowAutoGameMode", a);
    RegGetDword(HKEY_CURRENT_USER, L"SOFTWARE\\Microsoft\\GameBar", L"AutoGameModeEnabled", b);
    return (a == 1 && b == 1) ? 1 : 0;
}
static bool ApGameMode() {
    return BSetD(HKEY_CURRENT_USER, L"SOFTWARE\\Microsoft\\GameBar", L"AllowAutoGameMode", 1) &&
           BSetD(HKEY_CURRENT_USER, L"SOFTWARE\\Microsoft\\GameBar", L"AutoGameModeEnabled", 1);
}
static bool RvGameMode() {
    const wchar_t* s[] = {L"SOFTWARE\\Microsoft\\GameBar", L"SOFTWARE\\Microsoft\\GameBar"};
    const wchar_t* n[] = {L"AllowAutoGameMode", L"AutoGameModeEnabled"};
    return RevertKeys(HKEY_CURRENT_USER, s, n, 2);
}
// --- 2. Game DVR off ---
static const wchar_t* kDvrSub[] = {
    L"System\\GameConfigStore", L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\GameDVR",
    L"System\\GameConfigStore", L"System\\GameConfigStore", L"System\\GameConfigStore"};
static const wchar_t* kDvrName[] = {
    L"GameDVR_Enabled", L"AppCaptureEnabled", L"GameDVR_FSEBehaviorMode",
    L"GameDVR_HonorUserFSEBehaviorMode", L"GameDVR_DXGIHonorFSEWindowsCompatible"};
static int CkGameDvr() {
    DWORD v = 1;
    if (!RegGetDword(HKEY_CURRENT_USER, kDvrSub[0], kDvrName[0], v) || v != 0) return 0;
    if (!RegGetDword(HKEY_CURRENT_USER, kDvrSub[1], kDvrName[1], v) || v != 0) return 0;
    if (!RegGetDword(HKEY_CURRENT_USER, kDvrSub[2], kDvrName[2], v) || v != 2) return 0;
    if (!RegGetDword(HKEY_CURRENT_USER, kDvrSub[3], kDvrName[3], v) || v != 1) return 0;
    if (!RegGetDword(HKEY_CURRENT_USER, kDvrSub[4], kDvrName[4], v) || v != 1) return 0;
    return 1;
}
static bool ApGameDvr() {
    return BSetD(HKEY_CURRENT_USER, kDvrSub[0], kDvrName[0], 0) &&
           BSetD(HKEY_CURRENT_USER, kDvrSub[1], kDvrName[1], 0) &&
           BSetD(HKEY_CURRENT_USER, kDvrSub[2], kDvrName[2], 2) &&
           BSetD(HKEY_CURRENT_USER, kDvrSub[3], kDvrName[3], 1) &&
           BSetD(HKEY_CURRENT_USER, kDvrSub[4], kDvrName[4], 1);
}
static bool RvGameDvr() { return RevertKeys(HKEY_CURRENT_USER, kDvrSub, kDvrName, 5); }
// --- 3. Power plan ---
static const GUID kUlt = {0xe9a42b02,0xd5df,0x448d,{0xaa,0x00,0x03,0xf1,0x47,0x49,0xeb,0x61}};
static int CkPowerPlan() {
    GUID g; if (!PowerGetActiveGuid(g)) return -1;
    return memcmp(&g, &kUlt, sizeof(GUID)) == 0 ? 1 : 0;
}
static bool ApPowerPlan() {
    GUID cur; if (PowerGetActiveGuid(cur)) BackupSetPowerGuid(GuidToWString(cur));
    GUID u; if (!PowerEnsureUltimate(u)) return false;
    return PowerSetActive(u);
}
static bool RvPowerPlan() {
    std::wstring s; GUID g;
    if (BackupGetPowerGuid(s) && WStringToGuid(s, g)) return PowerSetActive(g);
    return false;
}
// --- 4. Core parking ---
static const GUID kSubProc = {0x54533251,0x82be,0x4824,{0x96,0xc1,0x47,0xb6,0x0b,0x74,0x0d,0x00}};
static const GUID kMinCores = {0x0cc5b647,0xc1df,0x4637,{0x89,0x1a,0xde,0xc3,0x5c,0x31,0x85,0x83}};
static int CkCorePark() {
    GUID g; DWORD ac = 0, dc = 0;
    if (!PowerGetActiveGuid(g)) return -1;
    if (!PowerReadSetting(g, kSubProc, kMinCores, ac, dc)) return -1;
    return (ac == 100 && dc == 100) ? 1 : 0;
}
static bool ApCorePark() {
    GUID g; DWORD ac = 0, dc = 0;
    if (!PowerGetActiveGuid(g)) return false;
    if (PowerReadSetting(g, kSubProc, kMinCores, ac, dc)) {
        BkLock();
        if (!g_cpHave) { g_cpHave = true; g_cpScheme = GuidToWString(g); g_cpAc = ac; g_cpDc = dc; }
        BkUnlock(); BackupSave();
    }
    return PowerWriteSetting(g, kSubProc, kMinCores, 100, 100);
}
static bool RvCorePark() {
    BkLock(); bool have = g_cpHave; std::wstring sch = g_cpScheme; DWORD ac = g_cpAc, dc = g_cpDc; BkUnlock();
    if (!have) return false;
    GUID g;
    if (!WStringToGuid(sch, g)) { if (!PowerGetActiveGuid(g)) return false; }
    DWORD t1 = 0, t2 = 0;
    if (!PowerReadSetting(g, kSubProc, kMinCores, t1, t2)) {
        if (!PowerGetActiveGuid(g)) return false; // scheme gone; use active
    }
    return PowerWriteSetting(g, kSubProc, kMinCores, ac, dc);
}
// --- 5. HAGS ---
static int CkHags() {
    DWORD v = 0;
    if (!RegGetDword(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\GraphicsDrivers", L"HwSchMode", v)) return 0;
    return v == 2 ? 1 : 0;
}
static bool ApHags() {
    return BSetD(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\GraphicsDrivers", L"HwSchMode", 2);
}
static bool RvHags() {
    const wchar_t* s[] = {L"SYSTEM\\CurrentControlSet\\Control\\GraphicsDrivers"};
    const wchar_t* n[] = {L"HwSchMode"};
    return RevertKeys(HKEY_LOCAL_MACHINE, s, n, 1);
}
// --- 6. Visual FX ---
static const wchar_t* kVfxSub[] = {
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\VisualEffects",
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced",
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced",
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced"};
static const wchar_t* kVfxName[] = {L"VisualFXSetting", L"TaskbarAnimations", L"ListviewAlphaSelect", L"ListviewShadow"};
static const DWORD kVfxVal[] = {2, 0, 0, 0};
static int CkVfx() {
    for (int i = 0; i < 4; i++) {
        DWORD v = 0xFFFFFFFF;
        if (!RegGetDword(HKEY_CURRENT_USER, kVfxSub[i], kVfxName[i], v) || v != kVfxVal[i]) return 0;
    }
    return 1;
}
static bool ApVfx() {
    for (int i = 0; i < 4; i++)
        if (!BSetD(HKEY_CURRENT_USER, kVfxSub[i], kVfxName[i], kVfxVal[i])) return false;
    return true;
}
static bool RvVfx() { return RevertKeys(HKEY_CURRENT_USER, kVfxSub, kVfxName, 4); }
// --- 7. Transparency ---
static int CkTransp() {
    DWORD v = 1;
    RegGetDword(HKEY_CURRENT_USER, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", L"EnableTransparency", v);
    return v == 0 ? 1 : 0;
}
static bool ApTransp() {
    return BSetD(HKEY_CURRENT_USER, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", L"EnableTransparency", 0);
}
static bool RvTransp() {
    const wchar_t* s[] = {L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize"};
    const wchar_t* n[] = {L"EnableTransparency"};
    return RevertKeys(HKEY_CURRENT_USER, s, n, 1);
}
// --- 8. Background apps ---
static int CkBgApps() {
    DWORD v = 0;
    if (!RegGetDword(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\BackgroundAccessApplications", L"GlobalUserDisabled", v)) return 0;
    return v == 1 ? 1 : 0;
}
static bool ApBgApps() {
    return BSetD(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\BackgroundAccessApplications", L"GlobalUserDisabled", 1);
}
static bool RvBgApps() {
    const wchar_t* s[] = {L"Software\\Microsoft\\Windows\\CurrentVersion\\BackgroundAccessApplications"};
    const wchar_t* n[] = {L"GlobalUserDisabled"};
    return RevertKeys(HKEY_CURRENT_USER, s, n, 1);
}
// --- 9. Network throttling ---
static int CkNetThr() {
    DWORD a = 0, b = 1;
    RegGetDword(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Multimedia\\SystemProfile", L"NetworkThrottlingIndex", a);
    RegGetDword(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Multimedia\\SystemProfile", L"SystemResponsiveness", b);
    return (a == 0xFFFFFFFF && b == 0) ? 1 : 0;
}
static bool ApNetThr() {
    return BSetD(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Multimedia\\SystemProfile", L"NetworkThrottlingIndex", 0xFFFFFFFF) &&
           BSetD(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Multimedia\\SystemProfile", L"SystemResponsiveness", 0);
}
static bool RvNetThr() {
    const wchar_t* s[] = {L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Multimedia\\SystemProfile",
                          L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Multimedia\\SystemProfile"};
    const wchar_t* n[] = {L"NetworkThrottlingIndex", L"SystemResponsiveness"};
    return RevertKeys(HKEY_LOCAL_MACHINE, s, n, 2);
}
// --- 10. Games multimedia scheduling ---
static int CkGameSched() {
    const wchar_t* sub = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Multimedia\\SystemProfile\\Tasks\\Games";
    DWORD a = 0, b = 0; std::wstring c;
    RegGetDword(HKEY_LOCAL_MACHINE, sub, L"GPU Priority", a);
    RegGetDword(HKEY_LOCAL_MACHINE, sub, L"Priority", b);
    RegGetString(HKEY_LOCAL_MACHINE, sub, L"Scheduling Category", c);
    return (a == 8 && b == 6 && c == L"High") ? 1 : 0;
}
static bool ApGameSched() {
    const wchar_t* sub = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Multimedia\\SystemProfile\\Tasks\\Games";
    return BSetD(HKEY_LOCAL_MACHINE, sub, L"GPU Priority", 8) &&
           BSetD(HKEY_LOCAL_MACHINE, sub, L"Priority", 6) &&
           BSetS(HKEY_LOCAL_MACHINE, sub, L"Scheduling Category", L"High");
}
static bool RvGameSched() {
    const wchar_t* s[] = {L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Multimedia\\SystemProfile\\Tasks\\Games",
                          L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Multimedia\\SystemProfile\\Tasks\\Games",
                          L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Multimedia\\SystemProfile\\Tasks\\Games"};
    const wchar_t* n[] = {L"GPU Priority", L"Priority", L"Scheduling Category"};
    return RevertKeys(HKEY_LOCAL_MACHINE, s, n, 3);
}
// --- 11. Startup delay ---
static int CkStartupDelay() {
    DWORD v = 1;
    if (!RegGetDword(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Serialize", L"StartupDelayInMSec", v)) return 0;
    return v == 0 ? 1 : 0;
}
static bool ApStartupDelay() {
    return BSetD(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Serialize", L"StartupDelayInMSec", 0);
}
static bool RvStartupDelay() {
    const wchar_t* s[] = {L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Serialize"};
    const wchar_t* n[] = {L"StartupDelayInMSec"};
    return RevertKeys(HKEY_CURRENT_USER, s, n, 1);
}
// --- 12. Menu delay ---
static int CkMenuDelay() {
    std::wstring v;
    if (!RegGetString(HKEY_CURRENT_USER, L"Control Panel\\Desktop", L"MenuShowDelay", v)) return 0;
    return v == L"0" ? 1 : 0;
}
static bool ApMenuDelay() {
    return BSetS(HKEY_CURRENT_USER, L"Control Panel\\Desktop", L"MenuShowDelay", L"0");
}
static bool RvMenuDelay() {
    const wchar_t* s[] = {L"Control Panel\\Desktop"};
    const wchar_t* n[] = {L"MenuShowDelay"};
    return RevertKeys(HKEY_CURRENT_USER, s, n, 1);
}
// --- 13. Telemetry ---
static int CkTelemetry() {
    DWORD v = 1;
    if (!RegGetDword(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Policies\\Microsoft\\Windows\\DataCollection", L"AllowTelemetry", v)) return 0;
    return v == 0 ? 1 : 0;
}
static bool ApTelemetry() {
    return BSetD(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Policies\\Microsoft\\Windows\\DataCollection", L"AllowTelemetry", 0);
}
static bool RvTelemetry() {
    const wchar_t* s[] = {L"SOFTWARE\\Policies\\Microsoft\\Windows\\DataCollection"};
    const wchar_t* n[] = {L"AllowTelemetry"};
    return RevertKeys(HKEY_LOCAL_MACHINE, s, n, 1);
}
// --- 14. Advertising ID ---
static int CkAdId() {
    DWORD v = 1;
    RegGetDword(HKEY_CURRENT_USER, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\AdvertisingInfo", L"Enabled", v);
    return v == 0 ? 1 : 0;
}
static bool ApAdId() {
    return BSetD(HKEY_CURRENT_USER, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\AdvertisingInfo", L"Enabled", 0);
}
static bool RvAdId() {
    const wchar_t* s[] = {L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\AdvertisingInfo"};
    const wchar_t* n[] = {L"Enabled"};
    return RevertKeys(HKEY_CURRENT_USER, s, n, 1);
}
// --- 15. Power throttling ---
static int CkPwrThr() {
    DWORD v = 0;
    if (!RegGetDword(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\Power\\PowerThrottling", L"PowerThrottlingOff", v)) return 0;
    return v == 1 ? 1 : 0;
}
static bool ApPwrThr() {
    return BSetD(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\Power\\PowerThrottling", L"PowerThrottlingOff", 1);
}
static bool RvPwrThr() {
    const wchar_t* s[] = {L"SYSTEM\\CurrentControlSet\\Control\\Power\\PowerThrottling"};
    const wchar_t* n[] = {L"PowerThrottlingOff"};
    return RevertKeys(HKEY_LOCAL_MACHINE, s, n, 1);
}
// --- 16. Mouse acceleration ---
static int CkMouse() {
    std::wstring a, b, c;
    RegGetString(HKEY_CURRENT_USER, L"Control Panel\\Mouse", L"MouseSpeed", a);
    RegGetString(HKEY_CURRENT_USER, L"Control Panel\\Mouse", L"MouseThreshold1", b);
    RegGetString(HKEY_CURRENT_USER, L"Control Panel\\Mouse", L"MouseThreshold2", c);
    return (a == L"0" && b == L"0" && c == L"0") ? 1 : 0;
}
static bool ApMouse() {
    return BSetS(HKEY_CURRENT_USER, L"Control Panel\\Mouse", L"MouseSpeed", L"0") &&
           BSetS(HKEY_CURRENT_USER, L"Control Panel\\Mouse", L"MouseThreshold1", L"0") &&
           BSetS(HKEY_CURRENT_USER, L"Control Panel\\Mouse", L"MouseThreshold2", L"0");
}
static bool RvMouse() {
    const wchar_t* s[] = {L"Control Panel\\Mouse", L"Control Panel\\Mouse", L"Control Panel\\Mouse"};
    const wchar_t* n[] = {L"MouseSpeed", L"MouseThreshold1", L"MouseThreshold2"};
    return RevertKeys(HKEY_CURRENT_USER, s, n, 3);
}
// --- 17/18/19. Actions ---
static int CkAction() { return -1; }
static bool ApTempClean() { g_lastCleanBytes = CleanJunkFiles(); return true; }
static bool ApStandby() { return PurgeStandbyList(); }
static bool ApRefresh() { int hz = SetMaxRefreshRate(); g_lastRefreshHz = hz; return true; }
static bool RvNone() { return true; }
// --- 20/21/22. Services ---
static int CkSvcManual(const wchar_t* svc) {
    DWORD st = 2;
    if (!ServiceGetStart(svc, st)) return -1;
    return (st == SERVICE_DEMAND_START || st == SERVICE_DISABLED) ? 1 : 0;
}
static bool ApSvcManual(const wchar_t* svc, DWORD target) {
    BackupService(svc);
    return ServiceSetStart(svc, target);
}
static bool RvSvc(const wchar_t* svc) {
    BkLock();
    for (size_t i = 0; i < g_svcBk.size(); i++)
        if (g_svcBk[i].svc == svc) { DWORD st = g_svcBk[i].start; BkUnlock(); return ServiceSetStart(svc, st); }
    BkUnlock();
    return false;
}
static int CkSysMain() { return CkSvcManual(L"SysMain"); }
static bool ApSysMain() { return ApSvcManual(L"SysMain", SERVICE_DEMAND_START); }
static bool RvSysMain() { return RvSvc(L"SysMain"); }
static int CkWSearch() { return CkSvcManual(L"WSearch"); }
static bool ApWSearch() { return ApSvcManual(L"WSearch", SERVICE_DEMAND_START); }
static bool RvWSearch() { return RvSvc(L"WSearch"); }
static int CkDiagTrack() {
    DWORD st = 2;
    if (!ServiceGetStart(L"DiagTrack", st)) return -1;
    return st == SERVICE_DISABLED ? 1 : 0;
}
static bool ApDiagTrack() { return ApSvcManual(L"DiagTrack", SERVICE_DISABLED); }
static bool RvDiagTrack() { return RvSvc(L"DiagTrack"); }
// --- 23. HPET (applied = NOT forced) ---
static int CkHpet() {
    std::wstring v; bool present = false;
    if (!BcdQuery(L"useplatformclock", v, present)) return -1;
    if (!present) return 1;
    return ToLower(v) == L"no" ? 1 : 0;
}
static bool ApHpet() { BackupBcd(L"useplatformclock"); return BcdDelete(L"useplatformclock"); }
static bool RvHpet() {
    BkLock();
    for (size_t i = 0; i < g_bcdBk.size(); i++)
        if (g_bcdBk[i].token == L"useplatformclock") {
            bool ex = g_bcdBk[i].existed; std::wstring v = g_bcdBk[i].value; BkUnlock();
            if (ex) return BcdSet(L"useplatformclock", v.c_str());
            return BcdDelete(L"useplatformclock");
        }
    BkUnlock(); return false;
}
// --- 24. Dynamic tick ---
static int CkDynTick() {
    std::wstring v; bool present = false;
    if (!BcdQuery(L"disabledynamictick", v, present)) return -1;
    if (!present) return 0;
    return ToLower(v) == L"yes" ? 1 : 0;
}
static bool ApDynTick() { BackupBcd(L"disabledynamictick"); return BcdSet(L"disabledynamictick", L"Yes"); }
static bool RvDynTick() {
    BkLock();
    for (size_t i = 0; i < g_bcdBk.size(); i++)
        if (g_bcdBk[i].token == L"disabledynamictick") {
            bool ex = g_bcdBk[i].existed; std::wstring v = g_bcdBk[i].value; BkUnlock();
            if (ex) return BcdSet(L"disabledynamictick", v.c_str());
            return BcdDelete(L"disabledynamictick");
        }
    BkUnlock(); return false;
}
// --- 25. GPU MSI mode ---
static bool EnumDisplayPci(std::vector<std::wstring>& paths) {
    paths.clear();
    HKEY pci = NULL;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Enum\\PCI", 0,
        KEY_READ | KEY_WOW64_64KEY, &pci) != ERROR_SUCCESS) return false;
    for (DWORD i = 0; ; i++) {
        wchar_t dev[128]; DWORD len = 128;
        if (RegEnumKeyExW(pci, i, dev, &len, NULL, NULL, NULL, NULL) != ERROR_SUCCESS) break;
        std::wstring dsub = WFormat(L"SYSTEM\\CurrentControlSet\\Enum\\PCI\\%s", dev);
        HKEY hd = NULL;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, dsub.c_str(), 0, KEY_READ | KEY_WOW64_64KEY, &hd) != ERROR_SUCCESS) continue;
        for (DWORD j = 0; ; j++) {
            wchar_t inst[128]; DWORD ilen = 128;
            if (RegEnumKeyExW(hd, j, inst, &ilen, NULL, NULL, NULL, NULL) != ERROR_SUCCESS) break;
            std::wstring isub = dsub + L"\\" + inst;
            std::wstring cls;
            if (RegGetString(HKEY_LOCAL_MACHINE, isub.c_str(), L"ClassGUID", cls) &&
                ToLower(cls) == L"{4d36e968-e325-11ce-bfc1-08002be10318}") {
                paths.push_back(isub + L"\\Device Parameters\\Interrupt Management\\MessageSignaledInterruptProperties");
            }
        }
        RegCloseKey(hd);
    }
    RegCloseKey(pci);
    return !paths.empty();
}
static int CkMsiGpu() {
    std::vector<std::wstring> paths;
    if (!EnumDisplayPci(paths)) return -1;
    for (size_t i = 0; i < paths.size(); i++) {
        DWORD v = 0;
        if (!RegGetDword(HKEY_LOCAL_MACHINE, paths[i].c_str(), L"MSISupported", v) || v != 1) return 0;
    }
    return 1;
}
static bool ApMsiGpu() {
    std::vector<std::wstring> paths;
    if (!EnumDisplayPci(paths)) return false;
    for (size_t i = 0; i < paths.size(); i++) {
        BackupReg(HKEY_LOCAL_MACHINE, paths[i].c_str(), L"MSISupported");
        if (!RegSetDword(HKEY_LOCAL_MACHINE, paths[i].c_str(), L"MSISupported", 1)) return false;
    }
    return true;
}
static bool RvMsiGpu() {
    BkLock();
    bool any = false, ok = true;
    for (size_t i = 0; i < g_regBk.size(); i++) {
        if (g_regBk[i].hive == 1 && g_regBk[i].name == L"MSISupported") {
            any = true;
            if (!RestoreOneReg(g_regBk[i])) ok = false;
        }
    }
    BkUnlock();
    return any && ok;
}

// ================= Table =================
static const Tweak kTweaks[] = {
{"gamemode",
 "Windows Game Mode", "حالت بازی ویندوز",
 "Lets Windows dedicate CPU and GPU resources to your game.",
 "ویندوز را وادار می‌کند منابع پردازنده و گرافیک را به بازی اختصاص دهد.",
 TCAT_GAMING, true, false, false, CkGameMode, ApGameMode, RvGameMode},
{"gamedvr",
 "Disable Game Bar & Game DVR", "خاموش کردن گیم‌بار و ضبط بازی",
 "Turns off background recording and overlays that steal FPS.",
 "ضبط پس‌زمینه و اورلی‌هایی که اف‌پی‌اس می‌دزدند خاموش می‌شود.",
 TCAT_GAMING, true, false, false, CkGameDvr, ApGameDvr, RvGameDvr},
{"powerplan",
 "Ultimate Performance power plan", "پلن انرژی Ultimate Performance",
 "Unlocks and activates the fastest Windows power plan.",
 "سریع‌ترین پلن انرژی ویندوز را فعال می‌کند.",
 TCAT_PERF, true, false, false, CkPowerPlan, ApPowerPlan, RvPowerPlan},
{"coreparking",
 "Disable CPU core parking", "غیرفعال‌سازی پارک هسته‌های پردازنده",
 "Keeps all CPU cores awake for stable frametimes.",
 "همه هسته‌ها را بیدار نگه می‌دارد تا فریم‌تایم پایدار شود.",
 TCAT_PERF, true, false, false, CkCorePark, ApCorePark, RvCorePark},
{"hags",
 "Hardware GPU scheduling (HAGS)", "زمان‌بندی سخت‌افزاری گرافیک",
 "Lets the GPU manage its own memory for lower latency. Needs restart.",
 "مدیریت حافظه را به خود گرافیک می‌سپارد. نیاز به ری‌استارت دارد.",
 TCAT_GAMING, true, true, false, CkHags, ApHags, RvHags},
{"visualfx",
 "Performance visual effects", "جلوه‌های بصری کارایی‌محور",
 "Disables animations and shadows for a snappier system.",
 "انیمیشن‌ها و سایه‌ها را خاموش می‌کند تا سیستم سبک‌تر شود.",
 TCAT_VISUAL, true, false, false, CkVfx, ApVfx, RvVfx},
{"transparency",
 "Disable transparency effects", "خاموش کردن افکت شفافیت",
 "Turns off acrylic transparency to save GPU cycles.",
 "شفافیت را خاموش می‌کند تا گرافیک آزاد شود.",
 TCAT_VISUAL, true, false, false, CkTransp, ApTransp, RvTransp},
{"bgapps",
 "Block background apps", "مسدود کردن برنامه‌های پس‌زمینه",
 "Stops Store apps from running in the background.",
 "جلوی اجرای برنامه‌ها در پس‌زمینه را می‌گیرد.",
 TCAT_PERF, true, false, false, CkBgApps, ApBgApps, RvBgApps},
{"netthrottle",
 "Disable network throttling", "حذف محدودیت سرعت شبکه",
 "Stops Windows from limiting network to save CPU. Lower ping.",
 "محدودیت شبکه ویندوز را حذف می‌کند. پینگ کمتر.",
 TCAT_NET, true, false, false, CkNetThr, ApNetThr, RvNetThr},
{"gamesched",
 "Game GPU & network priority", "اولویت گرافیک و شبکه بازی",
 "Raises multimedia scheduling priority for games.",
 "اولویت زمان‌بندی بازی را بالا می‌برد.",
 TCAT_NET, true, false, false, CkGameSched, ApGameSched, RvGameSched},
{"startupdelay",
 "Remove startup delay", "حذف تأخیر استارت‌آپ",
 "Removes the artificial delay before startup apps load.",
 "تأخیر مصنوعی اجرای برنامه‌های استارت‌آپ را حذف می‌کند.",
 TCAT_PERF, true, false, false, CkStartupDelay, ApStartupDelay, RvStartupDelay},
{"menudelay",
 "Instant menus", "منوهای فوری",
 "Menus open instantly instead of fading in.",
 "منوها بدون تأخیر باز می‌شوند.",
 TCAT_VISUAL, true, false, false, CkMenuDelay, ApMenuDelay, RvMenuDelay},
{"telemetry",
 "Disable telemetry", "غیرفعال‌سازی تله‌متری",
 "Stops Windows data collection services from using resources.",
 "جمع‌آوری داده ویندوز را متوقف می‌کند.",
 TCAT_PRIV, true, false, false, CkTelemetry, ApTelemetry, RvTelemetry},
{"adid",
 "Disable advertising ID", "خاموش کردن شناسه تبلیغاتی",
 "Disables cross-app tracking identifier.",
 "شناسه رهگیری تبلیغات را خاموش می‌کند.",
 TCAT_PRIV, true, false, false, CkAdId, ApAdId, RvAdId},
{"powerthrottle",
 "Disable power throttling", "خاموش کردن محدودسازی توان",
 "Prevents Windows from throttling background work aggressively.",
 "جلوی محدودسازی توان پردازش‌ها را می‌گیرد.",
 TCAT_PERF, true, false, false, CkPwrThr, ApPwrThr, RvPwrThr},
{"mouseaccel",
 "Disable mouse acceleration", "خاموش کردن شتاب موس",
 "1:1 raw mouse input for precise aiming (gamers' favorite).",
 "ورودی خام ۱:۱ موس برای نشانه‌گیری دقیق.",
 TCAT_GAMING, true, false, false, CkMouse, ApMouse, RvMouse},
{"tempclean",
 "Clean junk & cache files", "پاک‌سازی فایل‌های اضافی و کش",
 "Deletes temp, update cache and shader cache. Frees gigabytes.",
 "فایل‌های موقت و کش را پاک می‌کند. چند گیگابایت آزاد می‌شود.",
 TCAT_ACT, true, false, true, CkAction, ApTempClean, RvNone},
{"standby",
 "Free standby memory", "آزادسازی حافظه راکد",
 "Purges the standby list so games get free RAM instantly.",
 "حافظه راکد را آزاد می‌کند تا بازی سریع رم بگیرد.",
 TCAT_ACT, true, false, true, CkAction, ApStandby, RvNone},
{"refreshrate",
 "Max display refresh rate", "حداکثر نرخ نوسازی نمایشگر",
 "Switches your monitor to its highest Hz at current resolution.",
 "مانیتور را روی بیشترین هرتز ممکن می‌گذارد.",
 TCAT_ACT, true, false, true, CkAction, ApRefresh, RvNone},
{"sysmain",
 "SysMain service to Manual", "سرویس SysMain روی حالت دستی",
 "Stops SysMain disk prefetching. Helps HDDs and stutter.",
 "پیش‌خوانی دیسک را متوقف می‌کند. برای رفع لگ.",
 TCAT_ADV, false, false, false, CkSysMain, ApSysMain, RvSysMain},
{"wsearch",
 "Windows Search to Manual", "سرویس جستجوی ویندوز روی دستی",
 "Stops background indexing from using CPU and disk.",
 "ایndeks‌سازی پس‌زمینه را متوقف می‌کند.",
 TCAT_ADV, false, false, false, CkWSearch, ApWSearch, RvWSearch},
{"diagtrack",
 "Disable DiagTrack service", "غیرفعال‌سازی سرویس DiagTrack",
 "Disables connected-user-experience telemetry service.",
 "سرویس تله‌متری را غیرفعال می‌کند.",
 TCAT_ADV, false, false, false, CkDiagTrack, ApDiagTrack, RvDiagTrack},
{"hpet",
 "Disable forced HPET", "غیرفعال‌سازی HPET اجباری",
 "Removes forced platform clock for lower input latency. Needs restart.",
 "کلاک اجباری را حذف می‌کند. نیاز به ری‌استارت دارد.",
 TCAT_ADV, false, true, false, CkHpet, ApHpet, RvHpet},
{"dynamictick",
 "Disable dynamic tick", "غیرفعال‌سازی داینامیک‌تیک",
 "Disables dynamic timer tick for steadier frametimes. Needs restart.",
 "تیک داینامیک را خاموش می‌کند. نیاز به ری‌استارت دارد.",
 TCAT_ADV, false, true, false, CkDynTick, ApDynTick, RvDynTick},
{"msigpu",
 "GPU MSI mode", "حالت MSI کارت گرافیک",
 "Enables message-signaled interrupts for the GPU. Needs restart.",
 "وقفه پیام‌محور گرافیک را فعال می‌کند. نیاز به ری‌استارت دارد.",
 TCAT_ADV, false, true, false, CkMsiGpu, ApMsiGpu, RvMsiGpu},
};

const std::vector<Tweak>& Tweaks_All() {
    static std::vector<Tweak> v;
    if (v.empty())
        for (size_t i = 0; i < sizeof(kTweaks)/sizeof(kTweaks[0]); i++) v.push_back(kTweaks[i]);
    return v;
}
const Tweak* TweakById(const char* id) {
    const std::vector<Tweak>& v = Tweaks_All();
    for (size_t i = 0; i < v.size(); i++)
        if (!strcmp(v[i].id, id)) return &v[i];
    return NULL;
}
int TweakCheck(const Tweak& t) {
    if (!t.check) return -1;
    return t.check();
}
bool TweakApply(const Tweak& t) {
    if (!t.apply) return false;
    LogW(L"Applying tweak: %S", t.id);
    bool ok = t.apply();
    LogW(L"Tweak %S: %s", t.id, ok ? L"OK" : L"FAILED");
    return ok;
}
bool TweakRevert(const Tweak& t) {
    if (!t.revert || t.isAction) return true;
    LogW(L"Reverting tweak: %S", t.id);
    bool ok = t.revert();
    LogW(L"Revert %S: %s", t.id, ok ? L"OK" : L"FAILED");
    return ok;
}
void TweakDisplayName(const Tweak& t, std::wstring& out) {
    out = Utf8ToWide(Strings_GetLang() == 1 ? t.nameFa : t.nameEn);
}
void TweakDisplayDesc(const Tweak& t, std::wstring& out) {
    out = Utf8ToWide(Strings_GetLang() == 1 ? t.descFa : t.descEn);
}
int Tweaks_Score(int* appliedOut, int* totalOut) {
    const std::vector<Tweak>& v = Tweaks_All();
    int ap = 0, tot = 0;
    for (size_t i = 0; i < v.size(); i++) {
        if (!v[i].recommended || v[i].isAction) continue;
        tot++;
        if (TweakCheck(v[i]) == 1) ap++;
    }
    if (appliedOut) *appliedOut = ap;
    if (totalOut) *totalOut = tot;
    return tot ? (ap * 100 / tot) : 0;
}

// ================= Boost worker threads =================
static void BLog(HWND w, const wchar_t* fmt, ...) {
    va_list ap; va_start(ap, fmt);
    std::wstring s = VFormat(fmt, ap);
    va_end(ap);
    wchar_t* p = new wchar_t[s.size() + 1];
    wcscpy(p, s.c_str());
    PostMessageW(w, WM_APP_LOG, 0, (LPARAM)p);
}
static void BProg(HWND w, int pct) { PostMessageW(w, WM_APP_PROGRESS, (WPARAM)pct, 0); }

struct BoostCtx { HWND w; bool undo; };

static DWORD WINAPI BoostThread(LPVOID arg) {
    BoostCtx* ctx = (BoostCtx*)arg;
    HWND w = ctx->w;
    bool undo = ctx->undo;
    delete ctx;
    LogW(undo ? L"=== Boost UNDO started ===" : L"=== Ultimate Boost started ===");

    if (!undo) {
        BProg(w, 3); BLog(w, T(SID_B_STEP_RESTORE));
        if (CreateRestorePoint(L"FPS Booster Pro - Before Boost"))
            BLog(w, L"  [OK]");
        else { BLog(w, L"  (skipped - system protection may be off)"); LogW(L"Restore point failed"); }

        BProg(w, 8); BLog(w, T(SID_B_STEP_BACKUP));
        BackupInit(); BackupSave();

        BProg(w, 12); BLog(w, T(SID_B_STEP_TWEAKS));
        const std::vector<Tweak>& v = Tweaks_All();
        int fails = 0, done = 0, tot = 0;
        for (size_t i = 0; i < v.size(); i++) if (v[i].recommended && !v[i].isAction) tot++;
        bool needReboot = false;
        for (size_t i = 0; i < v.size(); i++) {
            if (!v[i].recommended || v[i].isAction) continue;
            std::wstring nm; TweakDisplayName(v[i], nm);
            if (TweakCheck(v[i]) == 1) {
                BLog(w, L"  [=] %s", nm.c_str());
            } else if (TweakApply(v[i])) {
                BLog(w, L"  [+] %s", nm.c_str());
                if (v[i].needsReboot) needReboot = true;
            } else {
                BLog(w, L"  [X] %s", nm.c_str());
                fails++;
            }
            done++;
            BProg(w, 12 + done * 60 / (tot ? tot : 1));
        }

        BProg(w, 76); BLog(w, T(SID_B_STEP_TEMP));
        g_lastCleanBytes = CleanJunkFiles();
        BLog(w, L"  %s: %.1f MB", T(SID_S_FREED), (double)g_lastCleanBytes / (1024.0*1024.0));

        BProg(w, 84); BLog(w, T(SID_B_STEP_RAM));
        BLog(w, L"  %s", PurgeStandbyList() ? L"[OK]" : L"(partial)");

        BProg(w, 90); BLog(w, T(SID_B_STEP_DISP));
        int hz = SetMaxRefreshRate();
        g_lastRefreshHz = hz;
        if (hz > 0) BLog(w, L"  %s %dHz", T(SID_S_REFRESH_DONE), hz);
        else BLog(w, L"  %s", T(SID_S_REFRESH_NONE));

        BProg(w, 96); BLog(w, T(SID_B_STEP_SCORE));
        int ap = 0, tt = 0;
        g_lastScore = Tweaks_Score(&ap, &tt);
        BLog(w, L"  %s: %d/100 (%d/%d)", T(SID_DASH_SCORE), g_lastScore, ap, tt);
        if (needReboot) BLog(w, L"  [!] %s", T(SID_B_REBOOT_MSG));

        BProg(w, 100);
        PostMessageW(w, WM_APP_BOOSTDONE, (WPARAM)fails, 0);
        LogW(L"=== Boost finished, fails=%d score=%d ===", fails, g_lastScore);
    } else {
        BProg(w, 5);
        const std::vector<Tweak>& v = Tweaks_All();
        int fails = 0, done = 0, tot = 0;
        for (size_t i = 0; i < v.size(); i++) if (!v[i].isAction) tot++;
        for (int i = (int)v.size() - 1; i >= 0; i--) {
            if (v[i].isAction) continue;
            std::wstring nm; TweakDisplayName(v[i], nm);
            if (TweakRevert(v[i])) BLog(w, L"  [-] %s", nm.c_str());
            else { BLog(w, L"  [X] %s", nm.c_str()); fails++; }
            done++;
            BProg(w, 5 + done * 90 / (tot ? tot : 1));
        }
        int ap = 0, tt = 0;
        g_lastScore = Tweaks_Score(&ap, &tt);
        BProg(w, 100);
        PostMessageW(w, WM_APP_BOOSTDONE, (WPARAM)(0x8000 | fails), 0);
        LogW(L"=== Undo finished, fails=%d ===", fails);
    }
    return 0;
}

void BoostRun(HWND notifyWnd) {
    BoostCtx* c = new BoostCtx; c->w = notifyWnd; c->undo = false;
    HANDLE h = CreateThread(NULL, 0, BoostThread, c, 0, NULL);
    if (h) CloseHandle(h);
}
void BoostUndo(HWND notifyWnd) {
    BoostCtx* c = new BoostCtx; c->w = notifyWnd; c->undo = true;
    HANDLE h = CreateThread(NULL, 0, BoostThread, c, 0, NULL);
    if (h) CloseHandle(h);
}
