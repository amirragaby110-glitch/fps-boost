// FPS Booster Pro - Game profiles, store scanning, Boost & Launch
#include "app.h"
#include <string.h>
#include <ctype.h>
#include <wctype.h>

// ================= Storage =================
static std::vector<GameProfile> g_games;
static CRITICAL_SECTION g_gCs; static bool g_gCsInit = false;
static void GLock() { if (!g_gCsInit) { InitializeCriticalSection(&g_gCs); g_gCsInit = true; } EnterCriticalSection(&g_gCs); }
static void GUnlock() { LeaveCriticalSection(&g_gCs); }
void Games_Lock() { GLock(); }
void Games_Unlock() { GUnlock(); }

static std::wstring GamesPath() { return JoinPath(g_dataDir, L"games.json"); }

void Games_Load() {
    GLock(); g_games.clear(); GUnlock();
    std::string text;
    if (!ReadFileText(GamesPath(), text)) return;
    JVal root;
    if (!ParseJson(text, root) || root.type != JVal::OBJ) return;
    const JVal* a = root.find("games");
    if (!a || a->type != JVal::ARR) return;
    GLock();
    for (size_t i = 0; i < a->arr.size(); i++) {
        const JVal& e = a->arr[i];
        GameProfile g;
        g.name = e.wstr("name"); g.path = e.wstr("path"); g.args = e.wstr("args");
        g.priority = e.num("prio", 2); g.affinity = e.num("aff", 0); g.gpuPref = e.num("gpu", 1);
        g.disableFSO = e.num("fso", 0) != 0;
        g.timerBoost = e.num("timer", 1) != 0;
        g.powerBoost = e.num("power", 1) != 0;
        g.killList = e.wstr("kill");
        g.playCount = e.num("plays", 0);
        g.lastPlayed = e.wstr("last");
        if (!g.path.empty()) g_games.push_back(g);
    }
    GUnlock();
}
void Games_Save() {
    GLock();
    std::string j = "{\"games\":[";
    for (size_t i = 0; i < g_games.size(); i++) {
        const GameProfile& g = g_games[i];
        if (i) j += ",";
        char nb[128];
        snprintf(nb, 128, "{\"prio\":%d,\"aff\":%d,\"gpu\":%d,\"fso\":%d,\"timer\":%d,\"power\":%d,\"plays\":%d",
            g.priority, g.affinity, g.gpuPref, g.disableFSO?1:0, g.timerBoost?1:0, g.powerBoost?1:0, g.playCount);
        j += nb;
        j += ",\"name\":\"" + JsonEscapeW(g.name) + "\",\"path\":\"" + JsonEscapeW(g.path) +
             "\",\"args\":\"" + JsonEscapeW(g.args) + "\",\"kill\":\"" + JsonEscapeW(g.killList) +
             "\",\"last\":\"" + JsonEscapeW(g.lastPlayed) + "\"}";
    }
    j += "]}";
    GUnlock();
    WriteFileText(GamesPath(), j);
}
std::vector<GameProfile>& Games_All() { return g_games; }

bool Games_Add(const std::wstring& exePath, const std::wstring& niceName) {
    if (!FileExists(exePath)) return false;
    std::wstring low = ToLower(exePath);
    GLock();
    for (size_t i = 0; i < g_games.size(); i++)
        if (ToLower(g_games[i].path) == low) { GUnlock(); return false; }
    GameProfile g;
    g.path = exePath;
    g.name = niceName.empty() ? BaseName(exePath) : niceName;
    g_games.push_back(g);
    GUnlock();
    Games_Save();
    LogW(L"Game added: %s", exePath.c_str());
    return true;
}
bool Games_Remove(int idx) {
    GLock();
    if (idx < 0 || idx >= (int)g_games.size()) { GUnlock(); return false; }
    g_games.erase(g_games.begin() + idx);
    GUnlock();
    Games_Save();
    return true;
}

// ================= Store scanning =================
static std::vector<std::string> VdfTokens(const std::string& text) {
    std::vector<std::string> toks; std::string cur; bool in = false;
    for (size_t i = 0; i < text.size(); i++) {
        char c = text[i];
        if (c == '"') {
            if (in) { toks.push_back(cur); cur.clear(); in = false; }
            else in = true;
        } else if (in) cur.push_back(c);
    }
    return toks;
}
static std::wstring SlashFix(std::wstring s) {
    for (size_t i = 0; i < s.size(); i++) if (s[i] == L'/') s[i] = L'\\';
    return s;
}
static bool IsJunkExe(const std::wstring& name) {
    std::wstring n = ToLower(name);
    const wchar_t* junk[] = {L"uninstall", L"unins", L"setup", L"vcredist", L"dxsetup", L"dotnet",
        L"oalinst", L"crash", L"report", L"unitycrash", L"eac_", L"battleye", L"xinput",
        L"redist", L"installer", L"update", L"cef", L"helper"};
    for (size_t i = 0; i < sizeof(junk)/sizeof(junk[0]); i++)
        if (n.find(junk[i]) != std::wstring::npos) return true;
    return false;
}
static void FindExes(const std::wstring& dir, int depth, std::vector<std::wstring>& out) {
    std::wstring pat = JoinPath(dir, L"*.exe");
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pat.c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && !IsJunkExe(fd.cFileName))
                out.push_back(JoinPath(dir, fd.cFileName));
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    if (depth <= 0) return;
    std::wstring pat2 = JoinPath(dir, L"*");
    h = FindFirstFileW(pat2.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && wcscmp(fd.cFileName, L".") && wcscmp(fd.cFileName, L"..")) {
            std::wstring sub = JoinPath(dir, fd.cFileName);
            std::wstring low = ToLower(fd.cFileName);
            if (low.find(L"redist") != std::wstring::npos || low == L"__installer" ||
                low.find(L"directx") != std::wstring::npos) continue;
            FindExes(sub, depth - 1, out);
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}
static void ScanSteam(std::vector<GameCandidate>& out) {
    std::wstring steam;
    if (!RegGetString(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath", steam)) {
        // default locations
        wchar_t pf[MAX_PATH];
        if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_PROGRAM_FILESX86, NULL, 0, pf))) {
            std::wstring d = JoinPath(pf, L"Steam");
            DWORD a = GetFileAttributesW(d.c_str());
            if (a != INVALID_FILE_ATTRIBUTES) steam = d;
        }
    }
    if (steam.empty()) return;
    steam = SlashFix(steam);
    std::vector<std::wstring> libs;
    libs.push_back(steam);
    std::string vdf;
    if (ReadFileText(JoinPath(JoinPath(steam, L"steamapps"), L"libraryfolders.vdf"), vdf)) {
        std::vector<std::string> toks = VdfTokens(vdf);
        for (size_t i = 0; i + 1 < toks.size(); i++) {
            std::string k = toks[i];
            for (size_t c = 0; c < k.size(); c++) k[c] = (char)tolower(k[c]);
            if (k == "path") {
                std::wstring p = SlashFix(Utf8ToWide(toks[i + 1]));
                bool dup = false;
                for (size_t j = 0; j < libs.size(); j++)
                    if (ToLower(libs[j]) == ToLower(p)) { dup = true; break; }
                if (!dup) libs.push_back(p);
            }
        }
    }
    for (size_t li = 0; li < libs.size(); li++) {
        std::wstring apps = JoinPath(libs[li], L"steamapps");
        std::wstring pat = JoinPath(apps, L"*.acf");
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW(pat.c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) continue;
        do {
            std::string acf;
            if (!ReadFileText(JoinPath(apps, fd.cFileName), acf)) continue;
            std::vector<std::string> toks = VdfTokens(acf);
            std::string name, inst;
            for (size_t i = 0; i + 1 < toks.size(); i++) {
                std::string k = toks[i];
                for (size_t c = 0; c < k.size(); c++) k[c] = (char)tolower(k[c]);
                if (k == "name") name = toks[i + 1];
                else if (k == "installdir") inst = toks[i + 1];
            }
            if (inst.empty()) continue;
            std::wstring gdir = JoinPath(JoinPath(apps, L"common"), Utf8ToWide(inst));
            std::vector<std::wstring> exes;
            FindExes(gdir, 1, exes);
            std::wstring gname = name.empty() ? Utf8ToWide(inst) : Utf8ToWide(name);
            for (size_t e = 0; e < exes.size(); e++) {
                GameCandidate c;
                c.exe = exes[e]; c.dir = gdir;
                c.name = (exes.size() > 1) ? gname + L" (" + BaseName(exes[e]) + L")" : gname;
                out.push_back(c);
                if (out.size() > 400) break;
            }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
}
static void ScanEpic(std::vector<GameCandidate>& out) {
    wchar_t prog[MAX_PATH];
    if (FAILED(SHGetFolderPathW(NULL, CSIDL_COMMON_APPDATA, NULL, 0, prog))) return;
    std::wstring mandir = JoinPath(JoinPath(prog, L"Epic\\EpicGamesLauncher\\Data\\Manifests"), L"*.item");
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(mandir.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        std::wstring full = JoinPath(JoinPath(prog, L"Epic\\EpicGamesLauncher\\Data\\Manifests"), fd.cFileName);
        std::string text;
        if (!ReadFileText(full, text)) continue;
        JVal root;
        if (!ParseJson(text, root) || root.type != JVal::OBJ) continue;
        std::wstring disp = root.wstr("DisplayName"), loc = root.wstr("InstallLocation"), exe = root.wstr("LaunchExecutable");
        if (disp.empty() || loc.empty() || exe.empty()) continue;
        std::wstring fullExe = JoinPath(SlashFix(loc), SlashFix(exe));
        if (!FileExists(fullExe)) {
            // try finding exes in install dir
            std::vector<std::wstring> exes;
            FindExes(SlashFix(loc), 1, exes);
            for (size_t i = 0; i < exes.size() && i < 3; i++) {
                GameCandidate c; c.exe = exes[i]; c.dir = SlashFix(loc);
                c.name = disp + L" (" + BaseName(exes[i]) + L")";
                out.push_back(c);
            }
            continue;
        }
        GameCandidate c; c.exe = fullExe; c.dir = SlashFix(loc); c.name = disp;
        out.push_back(c);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}
void Games_Scan(std::vector<GameCandidate>& out) {
    out.clear();
    ScanSteam(out);
    ScanEpic(out);
    // dedupe by exe path, and drop already-added games
    std::vector<GameCandidate> res;
    GLock();
    for (size_t i = 0; i < out.size(); i++) {
        std::wstring low = ToLower(out[i].exe);
        bool dup = false;
        for (size_t j = 0; j < res.size(); j++)
            if (ToLower(res[j].exe) == low) { dup = true; break; }
        for (size_t j = 0; !dup && j < g_games.size(); j++)
            if (ToLower(g_games[j].path) == low) { dup = true; break; }
        if (!dup && FileExists(out[i].exe)) res.push_back(out[i]);
    }
    GUnlock();
    out = res;
    LogW(L"Game scan found %d new candidates", (int)out.size());
}

// ================= Per-game persistent settings =================
bool Games_ApplyPersistent(const GameProfile& g) {
    // GPU preference
    const wchar_t* gsub = L"Software\\Microsoft\\DirectX\\UserGpuPreferences";
    if (g.gpuPref == 1)
        RegSetString(HKEY_CURRENT_USER, gsub, g.path.c_str(), L"GpuPreference=2;");
    else
        RegDeleteValue(HKEY_CURRENT_USER, gsub, g.path.c_str());
    // Fullscreen optimizations via AppCompat layers
    const wchar_t* csub = L"Software\\Microsoft\\Windows NT\\CurrentVersion\\AppCompatFlags\\Layers";
    std::wstring cur;
    RegGetString(HKEY_CURRENT_USER, csub, g.path.c_str(), cur);
    const wchar_t* tok = L"DISABLEDXMAXIMIZEDWINDOWEDMODE";
    bool has = cur.find(tok) != std::wstring::npos;
    if (g.disableFSO && !has) {
        if (cur.empty()) cur = L"~";
        cur += L" "; cur += tok;
        RegSetString(HKEY_CURRENT_USER, csub, g.path.c_str(), cur);
    } else if (!g.disableFSO && has) {
        std::wstring n;
        // remove token
        size_t p = cur.find(tok);
        n = cur.substr(0, p) + cur.substr(p + wcslen(tok));
        // trim
        while (!n.empty() && iswspace(n.front())) n.erase(n.begin());
        while (!n.empty() && iswspace(n.back())) n.pop_back();
        if (n.empty() || n == L"~") RegDeleteValue(HKEY_CURRENT_USER, csub, g.path.c_str());
        else RegSetString(HKEY_CURRENT_USER, csub, g.path.c_str(), n);
    }
    return true;
}

// ================= Embedded game-tips DB =================
static JVal g_db; static bool g_dbLoaded = false;
static void DbLoad() {
    if (g_dbLoaded) return;
    g_dbLoaded = true;
    HRSRC r = FindResourceW(g_hInst, MAKEINTRESOURCEW(RES_DB_GAMES), RT_RCDATA);
    if (!r) return;
    DWORD sz = SizeofResource(g_hInst, r);
    HGLOBAL hg = LoadResource(g_hInst, r);
    if (!hg || sz == 0 || sz > 8*1024*1024) return;
    const char* p = (const char*)LockResource(hg);
    if (!p) return;
    std::string text(p, sz);
    ParseJson(text, g_db);
}
std::wstring Games_TipFor(const std::wstring& exePath) {
    DbLoad();
    if (g_db.type != JVal::OBJ) return L"";
    const JVal* a = g_db.find("db");
    if (!a || a->type != JVal::ARR) return L"";
    std::wstring base = ToLower(BaseName(exePath));
    // also try with .exe
    size_t dot = exePath.find_last_of(L"\\/");
    std::wstring file = (dot == std::wstring::npos) ? exePath : exePath.substr(dot + 1);
    file = ToLower(file);
    for (size_t i = 0; i < a->arr.size(); i++) {
        const JVal& e = a->arr[i];
        std::string m = e.str("match");
        std::wstring wm = ToLower(Utf8ToWide(m));
        if (wm == file || wm == base) {
            std::string tip = (Strings_GetLang() == 1) ? e.str("fa") : e.str("en");
            return Utf8ToWide(tip);
        }
    }
    return L"";
}

// ================= Boost & Launch =================
static bool g_launchBusy = false;
static ULONG g_sessTimerPrev = 0; static bool g_sessTimer = false;
static GUID g_sessPower; static bool g_sessPowerSet = false;

bool Games_IsBusy() { return g_launchBusy || g_inGame; }

struct LaunchCtx { GameProfile g; HWND w; };

static DWORD WINAPI LaunchThread(LPVOID arg) {
    LaunchCtx* ctx = (LaunchCtx*)arg;
    GameProfile g = ctx->g;
    HWND w = ctx->w;
    delete ctx;
    g_launchBusy = true;

    PostMessageW(w, WM_APP_GAME, (WPARAM)GPH_PREP, 0);
    LogW(L"Boost&Launch: %s", g.path.c_str());

    if (!FileExists(g.path)) {
        g_launchBusy = false;
        PostMessageW(w, WM_APP_GAME, (WPARAM)GPH_ERROR, 0);
        return 1;
    }

    Games_ApplyPersistent(g);

    // pre-launch tuning
    if (g.powerBoost) {
        GUID cur;
        if (PowerGetActiveGuid(cur)) { g_sessPower = cur; }
        GUID u;
        if (PowerEnsureUltimate(u)) { PowerSetActive(u); g_sessPowerSet = true; }
    }
    std::vector<std::wstring> kills = SplitWs(g.killList, L';');
    if (!kills.empty()) {
        int k = KillProcessesByNames(kills);
        LogW(L"Auto-closed %d processes", k);
    }
    PurgeStandbyList();
    ULONG prev = 0;
    if (g.timerBoost && TimerSetMin(&prev)) { g_sessTimerPrev = prev; g_sessTimer = true; }

    // launch
    std::wstring dir = DirName(g.path);
    std::wstring cmd = L"\"" + g.path + L"\"";
    if (!g.args.empty()) cmd += L" " + g.args;
    STARTUPINFOW si; ZeroMemory(&si, sizeof(si)); si.cb = sizeof(si);
    PROCESS_INFORMATION pi; ZeroMemory(&pi, sizeof(pi));
    std::vector<wchar_t> buf(cmd.begin(), cmd.end()); buf.push_back(0);
    DWORD t0 = GetTickCount();
    if (!CreateProcessW(NULL, buf.data(), NULL, NULL, FALSE, 0, NULL, dir.c_str(), &si, &pi)) {
        LogW(L"CreateProcess failed: %d", GetLastError());
        Games_RestoreAfterSession();
        g_launchBusy = false;
        PostMessageW(w, WM_APP_GAME, (WPARAM)GPH_ERROR, 0);
        return 1;
    }
    CloseHandle(pi.hThread);

    // wait a bit, then tune the game process
    for (int i = 0; i < 8; i++) {
        Sleep(500);
        if (WaitForSingleObject(pi.hProcess, 0) == WAIT_OBJECT_0) break;
    }
    if (WaitForSingleObject(pi.hProcess, 0) != WAIT_OBJECT_0) {
        SetProcessGameOpts(pi.dwProcessId, g.priority, g.affinity);
        // bump counters
        GLock();
        for (size_t i = 0; i < g_games.size(); i++)
            if (ToLower(g_games[i].path) == ToLower(g.path)) {
                g_games[i].playCount++;
                g_games[i].lastPlayed = NowStamp();
                break;
            }
        GUnlock();
        Games_Save();
    }

    PostMessageW(w, WM_APP_GAME, (WPARAM)GPH_LAUNCHED, 0);
    g_inGame = true; g_activeGame = g.name;
    PostMessageW(w, WM_APP_GAME, (WPARAM)GPH_INGAME, 0);
    g_launchBusy = false;
    LogW(L"Game running: %s (pid %d)", g.name.c_str(), pi.dwProcessId);

    // wait for exit
    while (WaitForSingleObject(pi.hProcess, 2000) == WAIT_TIMEOUT) { /* in game */ }
    CloseHandle(pi.hProcess);

    DWORD mins = (GetTickCount() - t0) / 60000;
    Games_RestoreAfterSession();
    g_inGame = false;
    LogW(L"Game session ended: %s (%d min)", g.name.c_str(), mins);
    wchar_t* info = new wchar_t[64];
    StringCchPrintfW(info, 64, L"%d", mins);
    PostMessageW(w, WM_APP_GAME, (WPARAM)GPH_DONE, (LPARAM)info);
    return 0;
}

void Games_BoostLaunch(int idx, HWND notifyWnd) {
    GLock();
    if (idx < 0 || idx >= (int)g_games.size() || g_launchBusy || g_inGame) { GUnlock(); return; }
    GameProfile g = g_games[idx];
    GUnlock();
    LaunchCtx* c = new LaunchCtx; c->g = g; c->w = notifyWnd;
    HANDLE h = CreateThread(NULL, 0, LaunchThread, c, 0, NULL);
    if (h) CloseHandle(h);
}

void Games_RestoreAfterSession() {
    if (g_sessTimer) { TimerRestore(g_sessTimerPrev); g_sessTimer = false; }
    if (g_sessPowerSet) { PowerSetActive(g_sessPower); g_sessPowerSet = false; }
}
