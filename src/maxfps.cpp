// FPS Booster Pro - MAXIMUM FPS extreme mode (everything incl. lower quality)
#include "app.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

static void MLog(HWND w, const wchar_t* fmt, ...) {
    wchar_t b[1024];
    va_list ap; va_start(ap, fmt);
    _vsnwprintf(b, 1024, fmt, ap);
    va_end(ap);
    b[1023] = 0;
    size_t n = wcslen(b);
    wchar_t* p = new wchar_t[n + 1];
    wcscpy(p, b);
    PostMessageW(w, WM_APP_LOG, 0, (LPARAM)p);
}
static void MProg(HWND w, int pct) { PostMessageW(w, WM_APP_PROGRESS, (WPARAM)pct, 0); }

static std::wstring MaxFpsPath() { return JoinPath(g_dataDir, L"maxfps.json"); }
bool MaxFpsIsActive() {
    std::string text;
    if (!ReadFileText(MaxFpsPath(), text)) return false;
    JVal root;
    if (!ParseJson(text, root) || root.type != JVal::OBJ) return false;
    return root.num("active", 0) == 1;
}

struct SvcState { std::wstring name; DWORD start; bool wasRunning; };

static bool SvcQuery(const wchar_t* svc, DWORD& state) {
    SC_HANDLE mgr = OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT);
    if (!mgr) return false;
    SC_HANDLE h = OpenServiceW(mgr, svc, SERVICE_QUERY_STATUS);
    bool ok = false;
    if (h) {
        SERVICE_STATUS s;
        if (QueryServiceStatus(h, &s)) { state = s.dwCurrentState; ok = true; }
        CloseHandle(h);
    }
    CloseHandle(mgr);
    return ok;
}
static bool SvcStopWait(const wchar_t* svc) {
    SC_HANDLE mgr = OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT);
    if (!mgr) return false;
    SC_HANDLE h = OpenServiceW(mgr, svc, SERVICE_STOP | SERVICE_QUERY_STATUS);
    bool ok = false;
    if (h) {
        SERVICE_STATUS s;
        if (ControlService(h, SERVICE_CONTROL_STOP, &s)) {
            for (int i = 0; i < 20; i++) {
                if (!QueryServiceStatus(h, &s)) break;
                if (s.dwCurrentState == SERVICE_STOPPED) break;
                Sleep(250);
            }
            ok = true;
        } else ok = (GetLastError() == ERROR_SERVICE_NOT_ACTIVE);
        CloseHandle(h);
    }
    CloseHandle(mgr);
    return ok;
}
static bool SvcStartIt(const wchar_t* svc) {
    SC_HANDLE mgr = OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT);
    if (!mgr) return false;
    SC_HANDLE h = OpenServiceW(mgr, svc, SERVICE_START);
    bool ok = false;
    if (h) {
        ok = StartServiceW(h, 0, NULL) != FALSE;
        if (!ok && GetLastError() == ERROR_SERVICE_ALREADY_RUNNING) ok = true;
        CloseHandle(h);
    }
    CloseHandle(mgr);
    return ok;
}

static const wchar_t* kMaxSvc[] = {
    L"DiagTrack", L"dmwappushservice", L"SysMain", L"WSearch",
    L"BITS", L"Spooler", L"WMPNetworkSvc", L"wuauserv"
};

struct MaxCtx { HWND w; bool undo; };

static DWORD WINAPI MaxThread(LPVOID arg) {
    MaxCtx* ctx = (MaxCtx*)arg;
    HWND w = ctx->w; bool undo = ctx->undo;
    delete ctx;
    if (!undo) {
        LogW(L"=== MAXIMUM FPS started ===");
        if (MaxFpsIsActive()) {
            MLog(w, L"(already active)");
            PostMessageW(w, WM_APP_BOOSTDONE, (WPARAM)0, 0);
            return 0;
        }
        MProg(w, 2); MLog(w, T(SID_B_STEP_RESTORE));
        if (CreateRestorePoint(L"FPS Booster Pro - Before Maximum FPS")) MLog(w, L"  [OK]");
        else MLog(w, L"  (skipped)");
        MProg(w, 6); MLog(w, T(SID_B_STEP_BACKUP));
        BackupInit(); BackupSave();
        // all tweaks incl. advanced
        MProg(w, 10); MLog(w, T(SID_B_STEP_TWEAKS));
        const std::vector<Tweak>& v = Tweaks_All();
        int fails = 0, done = 0, tot = 0;
        for (size_t i = 0; i < v.size(); i++) if (!v[i].isAction) tot++;
        bool needReboot = false;
        for (size_t i = 0; i < v.size(); i++) {
            if (v[i].isAction) continue;
            std::wstring nm; TweakDisplayName(v[i], nm);
            if (TweakCheck(v[i]) == 1) MLog(w, L"  [=] %s", nm.c_str());
            else if (TweakApply(v[i])) {
                MLog(w, L"  [+] %s", nm.c_str());
                if (v[i].needsReboot) needReboot = true;
            }
            else { MLog(w, L"  [X] %s", nm.c_str()); fails++; }
            done++;
            MProg(w, 10 + done * 45 / (tot ? tot : 1));
        }
        // beast
        MProg(w, 58); MLog(w, T(SID_M_BEAST));
        bool beastWas = BeastIsActive();
        bool beastOn = false;
        if (beastWas) MLog(w, L"  [=]");
        else if (BeastActivate()) { MLog(w, L"  [OK]"); beastOn = true; }
        else { MLog(w, L"  [X]"); fails++; }
        // timer
        MProg(w, 64); MLog(w, T(SID_M_TIMER));
        ULONG timerPrev = 0;
        bool timerSet = TimerSetMin(&timerPrev);
        MLog(w, L"  %s", timerSet ? L"[OK]" : L"[X]");
        if (!timerSet) fails++;
        // services
        MProg(w, 70); MLog(w, T(SID_M_SVC));
        std::vector<SvcState> svcs;
        for (int i = 0; i < 8; i++) {
            DWORD start = 0, state = 0;
            if (!ServiceGetStart(kMaxSvc[i], start)) continue;
            if (start == SERVICE_DISABLED) continue;
            SvcQuery(kMaxSvc[i], state);
            SvcState st;
            st.name = kMaxSvc[i]; st.start = start;
            st.wasRunning = (state == SERVICE_RUNNING);
            if (start == SERVICE_AUTO_START) ServiceSetStart(kMaxSvc[i], SERVICE_DEMAND_START);
            if (st.wasRunning) SvcStopWait(kMaxSvc[i]);
            svcs.push_back(st);
            MLog(w, L"  [-] %s", kMaxSvc[i]);
        }
        // kill bloat
        MProg(w, 78); MLog(w, T(SID_M_KILL));
        std::vector<std::wstring> kill;
        kill.push_back(L"OneDrive.exe");
        kill.push_back(L"Teams.exe");
        kill.push_back(L"Skype.exe");
        int killed = KillProcessesByNames(kill);
        MLog(w, L"  %d closed", killed);
        // foreground game boost: priority + RAM focus on the game window
        MProg(w, 81); MLog(w, T(SID_M_GAME));
        DWORD fpid = 0;
        HWND fg = GetForegroundWindow();
        DWORD fgPid = 0;
        if (fg) GetWindowThreadProcessId(fg, &fgPid);
        if (fgPid && fgPid != GetCurrentProcessId()) {
            std::wstring bm, rm;
            if (Proc_Boost(fgPid, bm)) { MLog(w, L"  [+] PID %u", fgPid); fpid = fgPid; }
            if (fpid && Proc_RamFocus(fgPid, rm)) MLog(w, L"  %s", rm.c_str());
            if (!fpid) MLog(w, L"  [=]");
        } else MLog(w, L"  [=]");
        // junk + ram + refresh
        MProg(w, 84); MLog(w, T(SID_B_STEP_TEMP));
        unsigned long long freedBytes = CleanJunkFiles();
        MLog(w, L"  %s: %.1f MB", T(SID_S_FREED), (double)freedBytes / 1048576.0);
        MProg(w, 88); MLog(w, T(SID_B_STEP_RAM));
        MLog(w, L"  %s", PurgeStandbyList() ? L"[OK]" : L"(partial)");
        MLog(w, L"  %d trimmed", Proc_TrimAll());
        MProg(w, 91); MLog(w, T(SID_B_STEP_DISP));
        int hz = SetMaxRefreshRate();
        if (hz > 0) MLog(w, L"  %s %dHz", T(SID_S_REFRESH_DONE), hz);
        else MLog(w, L"  %s", T(SID_S_REFRESH_NONE));
        // resolution step-down for max fps
        MProg(w, 94); MLog(w, T(SID_M_RES));
        int cw = 0, ch = 0, rw = 0, rh = 0;
        GetCurrentResolution(cw, ch);
        int tw = 0, th = 0;
        if (cw > 1600) { tw = 1600; th = 900; }
        else if (cw > 1280) { tw = 1280; th = 720; }
        if (tw > 0 && SetDisplayResolution(tw, th)) {
            rw = cw; rh = ch;
            MLog(w, L"  %dx%d -> %dx%d", cw, ch, tw, th);
        }
        else MLog(w, L"  [=] %dx%d", cw, ch);
        // save state
        char nb[512];
        snprintf(nb, 512, "{\"active\":1,\"timer\":%lu,\"resw\":%d,\"resh\":%d,\"beast\":%d,\"fpid\":%u,\"svc\":\"",
            timerSet ? timerPrev : 0, rw, rh, beastOn ? 1 : 0, fpid);
        std::string j = nb;
        for (size_t i = 0; i < svcs.size(); i++) {
            if (i) j += ";";
            char sb[128];
            snprintf(sb, 128, "%ls,%d,%d", svcs[i].name.c_str(), (int)svcs[i].start, svcs[i].wasRunning ? 1 : 0);
            j += sb;
        }
        j += "\"}";
        WriteFileText(MaxFpsPath(), j);
        MProg(w, 97); MLog(w, T(SID_B_STEP_SCORE));
        int ap = 0, tt = 0;
        g_lastScore = Tweaks_Score(&ap, &tt);
        MLog(w, L"  %s: %d/100 (%d/%d)", T(SID_DASH_SCORE), g_lastScore, ap, tt);
        if (needReboot) MLog(w, L"  [!] %s", T(SID_B_REBOOT_MSG));
        MProg(w, 100);
        PostMessageW(w, WM_APP_BOOSTDONE, (WPARAM)fails, 0);
        LogW(L"=== MAXIMUM FPS finished, fails=%d ===", fails);
    } else {
        LogW(L"=== MAXIMUM FPS undo started ===");
        std::string text;
        JVal root;
        if (!ReadFileText(MaxFpsPath(), text) || !ParseJson(text, root) || root.num("active", 0) != 1) {
            PostMessageW(w, WM_APP_BOOSTDONE, (WPARAM)(0x8000 | 0), 0);
            return 0;
        }
        MProg(w, 5);
        int fails = 0;
        // resolution restore
        int rw = root.num("resw", 0), rh = root.num("resh", 0);
        if (rw > 0 && rh > 0) {
            if (SetDisplayResolution(rw, rh)) MLog(w, L"  [+] %dx%d", rw, rh);
            else { MLog(w, L"  [X] resolution"); fails++; }
        }
        // timer restore
        ULONG tp = (ULONG)root.num("timer", 0);
        if (tp) TimerRestore(tp);
        DWORD fp = (DWORD)root.num("fpid", 0);
        if (fp) {
            if (Proc_Restore(fp)) MLog(w, L"  [+] game");
            else MLog(w, L"  [X] game");
        }
        // services restore
        std::wstring ss = root.wstr("svc");
        size_t p = 0;
        while (p < ss.size()) {
            size_t q = ss.find(L';', p);
            std::wstring tok = (q == std::wstring::npos) ? ss.substr(p) : ss.substr(p, q - p);
            size_t c1 = tok.find(L',');
            size_t c2 = (c1 == std::wstring::npos) ? c1 : tok.find(L',', c1 + 1);
            if (c1 != std::wstring::npos && c2 != std::wstring::npos) {
                std::wstring nm = tok.substr(0, c1);
                DWORD st = (DWORD)_wtoi(tok.substr(c1 + 1, c2 - c1 - 1).c_str());
                int wasRun = _wtoi(tok.substr(c2 + 1).c_str());
                ServiceSetStart(nm.c_str(), st);
                if (wasRun) SvcStartIt(nm.c_str());
                MLog(w, L"  [+] %s", nm.c_str());
            }
            if (q == std::wstring::npos) break;
            p = q + 1;
        }
        // beast restore
        if (root.num("beast", 0) == 1) {
            if (BeastDeactivate()) MLog(w, L"  [+] Beast");
            else { MLog(w, L"  [X] Beast"); fails++; }
        }
        // tweaks revert
        MProg(w, 40);
        const std::vector<Tweak>& v = Tweaks_All();
        int done = 0, tot = 0;
        for (size_t i = 0; i < v.size(); i++) if (!v[i].isAction) tot++;
        for (int i = (int)v.size() - 1; i >= 0; i--) {
            if (v[i].isAction) continue;
            std::wstring nm; TweakDisplayName(v[i], nm);
            if (TweakRevert(v[i])) MLog(w, L"  [-] %s", nm.c_str());
            else { MLog(w, L"  [X] %s", nm.c_str()); fails++; }
            done++;
            MProg(w, 40 + done * 55 / (tot ? tot : 1));
        }
        WriteFileText(MaxFpsPath(), "{\"active\":0}");
        int ap = 0, tt = 0;
        g_lastScore = Tweaks_Score(&ap, &tt);
        MProg(w, 100);
        PostMessageW(w, WM_APP_BOOSTDONE, (WPARAM)(0x8000 | fails), 0);
        LogW(L"=== MAXIMUM FPS undo finished, fails=%d ===", fails);
    }
    return 0;
}

void MaxFpsRun(HWND notifyWnd) {
    MaxCtx* c = new MaxCtx; c->w = notifyWnd; c->undo = false;
    HANDLE h = CreateThread(NULL, 0, MaxThread, c, 0, NULL);
    if (h) CloseHandle(h);
}
void MaxFpsUndo(HWND notifyWnd) {
    MaxCtx* c = new MaxCtx; c->w = notifyWnd; c->undo = true;
    HANDLE h = CreateThread(NULL, 0, MaxThread, c, 0, NULL);
    if (h) CloseHandle(h);
}
