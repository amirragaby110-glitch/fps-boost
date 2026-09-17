// FPS Booster Pro - Auto-boost watcher (boosts library games automatically)
#include "app.h"
#include <string.h>

static bool g_watchOn = false, g_watchStop = false;
static HANDLE g_watchThread = NULL;
static ULONG g_wTimerPrev = 0;
static bool g_wTimer = false;
static GUID g_wPower;
static bool g_wPowerSet = false;
static std::vector<DWORD> g_wPids;
static int g_wLastCount = -1;
static std::wstring g_wLastNames;

static bool PidAlive(DWORD pid) {
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return false;
    DWORD c = 0;
    BOOL ok = GetExitCodeProcess(h, &c);
    CloseHandle(h);
    return ok && c == STILL_ACTIVE;
}
static std::wstring ExeNameOf(const std::wstring& path) {
    size_t p = path.find_last_of(L"\\/");
    std::wstring f = (p == std::wstring::npos) ? path : path.substr(p + 1);
    return ToLower(f);
}
static void WSendStatus(HWND w, int count, const std::wstring& names) {
    if (count == g_wLastCount && names == g_wLastNames) return;
    g_wLastCount = count;
    g_wLastNames = names;
    wchar_t* p = new wchar_t[names.size() + 1];
    wcscpy(p, names.c_str());
    PostMessageW(w, WM_APP_AUTO, (WPARAM)count, (LPARAM)p);
}

static DWORD WINAPI WatchThread(LPVOID a) {
    HWND w = (HWND)a;
    LogW(L"Auto-boost watcher started");
    while (!g_watchStop) {
        std::map<std::wstring, std::vector<DWORD>> procs;
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snap != INVALID_HANDLE_VALUE) {
            PROCESSENTRY32W pe;
            pe.dwSize = sizeof(pe);
            if (Process32FirstW(snap, &pe)) {
                do { procs[ToLower(pe.szExeFile)].push_back(pe.th32ProcessID); }
                while (Process32NextW(snap, &pe));
            }
            CloseHandle(snap);
        }
        for (size_t i = 0; i < g_wPids.size();) {
            if (PidAlive(g_wPids[i])) i++;
            else g_wPids.erase(g_wPids.begin() + i);
        }
        Games_Lock();
        std::vector<GameProfile> gs = Games_All();
        Games_Unlock();
        std::wstring names;
        int active = 0;
        bool needTimer = false, needPower = false;
        for (size_t i = 0; i < gs.size() && !g_watchStop; i++) {
            std::map<std::wstring, std::vector<DWORD>>::iterator it = procs.find(ExeNameOf(gs[i].path));
            if (it == procs.end()) continue;
            for (size_t k = 0; k < it->second.size(); k++) {
                DWORD pid = it->second[k];
                bool known = false;
                for (size_t j = 0; j < g_wPids.size(); j++)
                    if (g_wPids[j] == pid) { known = true; break; }
                if (!known && SetProcessGameOpts(pid, gs[i].priority, gs[i].affinity)) {
                    g_wPids.push_back(pid);
                    LogW(L"Auto-boosted %s (pid %d)", gs[i].name.c_str(), pid);
                }
            }
            active++;
            if (!names.empty()) names += L", ";
            names += gs[i].name;
            if (gs[i].timerBoost) needTimer = true;
            if (gs[i].powerBoost) needPower = true;
        }
        bool manual = g_inGame || g_boosting; // manual session owns timer/power
        if (needTimer && !g_wTimer && !manual) {
            ULONG pr = 0;
            if (TimerSetMin(&pr)) { g_wTimerPrev = pr; g_wTimer = true; }
        }
        if ((!needTimer || manual) && g_wTimer) { TimerRestore(g_wTimerPrev); g_wTimer = false; }
        if (needPower && !g_wPowerSet && !manual) {
            GUID cur;
            if (PowerGetActiveGuid(cur)) g_wPower = cur;
            GUID u;
            if (PowerEnsureUltimate(u) && PowerSetActive(u)) g_wPowerSet = true;
        }
        if ((!needPower || manual) && g_wPowerSet) { PowerSetActive(g_wPower); g_wPowerSet = false; }
        WSendStatus(w, active, names);
        for (int i = 0; i < 16 && !g_watchStop; i++) Sleep(250);
    }
    if (g_wTimer) { TimerRestore(g_wTimerPrev); g_wTimer = false; }
    if (g_wPowerSet) { PowerSetActive(g_wPower); g_wPowerSet = false; }
    g_wPids.clear();
    g_wLastCount = -1;
    g_wLastNames.clear();
    g_watchOn = false;
    LogW(L"Auto-boost watcher stopped");
    return 0;
}

void AutoBoostStart(HWND notifyWnd) {
    if (g_watchOn) return;
    g_watchStop = false;
    g_wLastCount = -1;
    g_watchThread = CreateThread(NULL, 0, WatchThread, notifyWnd, 0, NULL);
    if (g_watchThread) g_watchOn = true;
}
void AutoBoostStop() {
    if (!g_watchOn) return;
    g_watchStop = true;
    if (g_watchThread) {
        WaitForSingleObject(g_watchThread, 6000);
        CloseHandle(g_watchThread);
        g_watchThread = NULL;
    }
    g_watchOn = false;
}
bool AutoBoostWatching() { return g_watchOn; }
