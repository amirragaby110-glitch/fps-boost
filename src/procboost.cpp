// FPS Booster Pro - live process booster.
// Not every game is a plain .exe you launch: enumerate running processes,
// detect known games, raise priority, disable efficiency throttling and
// focus free RAM on the process you choose (RAM Focus).
#include "app.h"
#include <psapi.h>

// PROCESS_POWER_THROTTLING_STATE needs a new SDK; provide fallback.
#ifndef PROCESS_POWER_THROTTLING_CURRENT_VERSION
#define PROCESS_POWER_THROTTLING_CURRENT_VERSION 1
#define PROCESS_POWER_THROTTLING_EXECUTION_SPEED 1
struct PROCESS_POWER_THROTTLING_STATE_FB { ULONG Version; ULONG ControlMask; ULONG StateMask; };
#define ProcessPowerThrottling ((PROCESS_INFORMATION_CLASS)4)
#endif

static bool ProcIsGameExe(const std::wstring& lowerName) {
    static std::vector<GameSpec> specs;
    static bool loaded = false;
    if (!loaded) { Games_Specs(specs); loaded = true; }
    for (size_t i = 0; i < specs.size(); i++)
        if (specs[i].match == lowerName) return true;
    return false;
}

void Proc_Enum(std::vector<ProcInfo>& out) {
    out.clear();
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return;
    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(pe);
    DWORD self = GetCurrentProcessId();
    if (Process32FirstW(snap, &pe)) {
        do {
            if (pe.th32ProcessID == 0 || pe.th32ProcessID == 4) continue;
            ProcInfo pi;
            pi.pid = pe.th32ProcessID;
            pi.name = pe.szExeFile;
            pi.memMB = 0;
            pi.isSelf = (pe.th32ProcessID == self);
            HANDLE h = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pe.th32ProcessID);
            if (!h) h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID);
            if (h) {
                PROCESS_MEMORY_COUNTERS pmc;
                ZeroMemory(&pmc, sizeof(pmc));
                pmc.cb = sizeof(pmc);
                if (GetProcessMemoryInfo(h, &pmc, sizeof(pmc)))
                    pi.memMB = (unsigned long long)(pmc.WorkingSetSize / (1024 * 1024));
                CloseHandle(h);
            }
            pi.isGame = ProcIsGameExe(ToLower(pi.name));
            out.push_back(pi);
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    // sort: games first, then by RAM desc
    for (size_t i = 0; i < out.size(); i++)
        for (size_t j = i + 1; j < out.size(); j++) {
            bool swap = false;
            if (out[j].isGame && !out[i].isGame) swap = true;
            else if (out[j].isGame == out[i].isGame && out[j].memMB > out[i].memMB) swap = true;
            if (swap) { ProcInfo t = out[i]; out[i] = out[j]; out[j] = t; }
        }
}

static HANDLE ProcOpen(DWORD pid) {
    HANDLE h = OpenProcess(PROCESS_SET_INFORMATION | PROCESS_SET_QUOTA |
        PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    return h;
}

bool Proc_Boost(DWORD pid, std::wstring& msg) {
    bool fa = Strings_GetLang() == 1;
    EnablePrivilege(L"SeIncreaseBasePriorityPrivilege");
    HANDLE h = ProcOpen(pid);
    if (!h) {
        msg = fa ? L"خطا: دسترسی به پردازش ممکن نشد (خطای سیستمی)." : L"Error: cannot open process (access denied).";
        return false;
    }
    bool prio = SetPriorityClass(h, HIGH_PRIORITY_CLASS) != FALSE;
    // Disable efficiency-mode throttling (Win10+), best effort.
    bool eco = false;
#ifdef PROCESS_POWER_THROTTLING_CURRENT_VERSION
    PROCESS_POWER_THROTTLING_STATE st;
    ZeroMemory(&st, sizeof(st));
    st.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    st.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
    st.StateMask = 0; // 0 = want full speed, no eco throttling
    eco = SetProcessInformation(h, ProcessPowerThrottling, &st, sizeof(st)) != FALSE;
#else
    PROCESS_POWER_THROTTLING_STATE_FB st;
    ZeroMemory(&st, sizeof(st));
    st.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    st.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
    st.StateMask = 0;
    eco = SetProcessInformation(h, ProcessPowerThrottling, &st, sizeof(st)) != FALSE;
#endif
    CloseHandle(h);
    if (fa) {
        msg = WFormat(L"اولویت پردازش %u: %s | حالت کم‌مصرف: %s",
            pid, prio ? L"بالا (High) شد" : L"تغییر نکرد",
            eco ? L"خاموش شد" : L"پشتیبانی نشد");
    } else {
        msg = WFormat(L"Process %u: priority %s | efficiency throttle %s",
            pid, prio ? L"raised to HIGH" : L"unchanged",
            eco ? L"disabled" : L"not supported");
    }
    return prio;
}

bool Proc_RamFocus(DWORD pid, std::wstring& msg) {
    bool fa = Strings_GetLang() == 1;
    EnablePrivilege(L"SeIncreaseQuotaPrivilege");
    int beforeMB = RamAvailMB();
    PurgeStandbyList();
    // Trim every other accessible process so RAM is actually free.
    int trimmed = 0, skipped = 0;
    DWORD self = GetCurrentProcessId();
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W pe;
        pe.dwSize = sizeof(pe);
        if (Process32FirstW(snap, &pe)) {
            do {
                DWORD id = pe.th32ProcessID;
                if (id == 0 || id == 4 || id == self || id == pid) continue;
                HANDLE h = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, id);
                if (!h) { skipped++; continue; }
                if (EmptyWorkingSet(h)) trimmed++; else skipped++;
                CloseHandle(h);
            } while (Process32NextW(snap, &pe));
        }
        CloseHandle(snap);
    }
    // Lock a bigger minimum working set for the target.
    int availMB = RamAvailMB();
    SIZE_T minBytes = (SIZE_T)availMB * 1024 * 1024 / 2;
    if (minBytes < (SIZE_T)256 * 1024 * 1024) minBytes = (SIZE_T)256 * 1024 * 1024;
    if (minBytes > (SIZE_T)2048 * 1024 * 1024) minBytes = (SIZE_T)2048 * 1024 * 1024;
    SIZE_T maxBytes = minBytes * 4;
    bool locked = false;
    HANDLE h = ProcOpen(pid);
    if (h) {
        locked = SetProcessWorkingSetSizeEx(h, minBytes, maxBytes, QUOTA_LIMITS_HARDWS_MIN_ENABLE) != FALSE;
        if (!locked) locked = SetProcessWorkingSetSize(h, minBytes, maxBytes) != FALSE;
        CloseHandle(h);
    }
    int freedMB = RamAvailMB() - beforeMB;
    if (freedMB < 0) freedMB = 0;
    if (fa) {
        msg = WFormat(L"رم آزاد شد: %d مگ (%d پردازش سبک شد) | حداقل رم قفل‌شده برای %u: %d مگ %s",
            freedMB, trimmed, pid, (int)(minBytes / (1024 * 1024)), locked ? L"✔" : L"(نشد)");
    } else {
        msg = WFormat(L"Freed %d MB RAM (%d processes trimmed) | min working set locked for %u: %d MB %s",
            freedMB, trimmed, pid, (int)(minBytes / (1024 * 1024)), locked ? L"OK" : L"(failed)");
    }
    return locked || trimmed > 0;
}

bool Proc_Restore(DWORD pid) {
    HANDLE h = ProcOpen(pid);
    if (!h) return false;
    bool a = SetPriorityClass(h, NORMAL_PRIORITY_CLASS) != FALSE;
    bool b = SetProcessWorkingSetSize(h, (SIZE_T)-1, (SIZE_T)-1) != FALSE;
    CloseHandle(h);
    return a && b;
}

int Proc_TrimAll() {
    int trimmed = 0;
    DWORD self = GetCurrentProcessId();
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe)) {
        do {
            DWORD id = pe.th32ProcessID;
            if (id == 0 || id == 4 || id == self) continue;
            HANDLE h = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, id);
            if (!h) continue;
            if (EmptyWorkingSet(h)) trimmed++;
            CloseHandle(h);
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return trimmed;
}
// Lock a guaranteed minimum RAM amount for any running process.
// mb <= 0 means MAX: (available - 1GB reserve), at least 512MB.
bool Proc_RamLock(DWORD pid, int mb, std::wstring& msg) {
    bool fa = Strings_GetLang() == 1;
    EnablePrivilege(L"SeIncreaseQuotaPrivilege");
    int wantMB = mb;
    if (wantMB <= 0) {
        wantMB = RamAvailMB() - 1024;
        if (wantMB < 512) wantMB = 512;
    }
    if (wantMB > 65536) wantMB = 65536;
    HANDLE h = ProcOpen(pid);
    if (!h) {
        msg = fa ? L"خطا: دسترسی به پردازش ممکن نشد (خطای سیستمی)." : L"Error: cannot open process (access denied).";
        return false;
    }
    SIZE_T curMin = 0, curMax = 0;
    DWORD flags = 0;
    GetProcessWorkingSetSizeEx(h, &curMin, &curMax, &flags);
    SIZE_T newMin = (SIZE_T)wantMB * 1024 * 1024;
    SIZE_T newMax = curMax > newMin ? curMax : newMin * 2;
    bool ok = SetProcessWorkingSetSizeEx(h, newMin, newMax, QUOTA_LIMITS_HARDWS_MIN_ENABLE) != FALSE;
    if (!ok) ok = SetProcessWorkingSetSize(h, newMin, newMax) != FALSE;
    CloseHandle(h);
    if (fa) {
        msg = ok ? WFormat(L"%d مگابایت رم برای پردازش %u قفل شد (تضمینی، آزاد نمی‌شود)", wantMB, pid)
                 : WFormat(L"قفل رم برای پردازش %u ممکن نشد (دسترسی ناکافی)", pid);
    } else {
        msg = ok ? WFormat(L"Locked %d MB RAM for process %u (guaranteed, never paged out)", wantMB, pid)
                 : WFormat(L"Could not lock RAM for process %u (access denied)", pid);
    }
    return ok;
}

// Full image path of a running process (for "pin to library").
bool Proc_ImagePath(DWORD pid, std::wstring& path) {
    path.clear();
    HANDLE h = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (!h) h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return false;
    wchar_t buf[MAX_PATH * 2];
    DWORD n = GetModuleFileNameExW(h, NULL, buf, MAX_PATH * 2);
    CloseHandle(h);
    if (n == 0) return false;
    path = buf;
    return true;
}
