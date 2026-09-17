// FPS Booster Pro - Shared header
#pragma once

#define APP_VER L"3.2.0"
#define HOTKEY_BOOST_ID 1

#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0A00
#define NOMINMAX

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <strsafe.h>
#include <powrprof.h>
#include <tlhelp32.h>

#include <string>
#include <vector>
#include <map>

#include "strings.h"

// ---------- App constants ----------
#define APP_VERSION       L"3.2.0"
#define APP_MUTEX         L"Global\\FPSBoosterPro_Mutex_v1"
#define RES_ICON_APP      101
#define RES_PNG_LOGO      201
#define RES_PNG_BANNER    202
#define RES_PNG_BG        203
#define RES_DB_GAMES      301

// Custom window messages
#define WM_APP_LOG        (WM_APP + 10)
#define WM_APP_PROGRESS   (WM_APP + 11)
#define WM_APP_BOOSTDONE  (WM_APP + 12)
#define WM_APP_GAME       (WM_APP + 13)
#define WM_APP_TRAY       (WM_APP + 14)
#define WM_APP_REFRESH    (WM_APP + 15)
#define WM_APP_NET        (WM_APP + 16)
#define WM_APP_AUTO       (WM_APP + 17)
#define WM_APP_UPDATE     (WM_APP + 19)

// Pages
enum PageId { PAGE_DASH = 0, PAGE_GAMES, PAGE_PROC, PAGE_BOOST, PAGE_TWEAKS, PAGE_SYSTEM, PAGE_POWER, PAGE_NET, PAGE_HELP, PAGE_SETTINGS, PAGE_COUNT };

// Tweak categories
enum TweakCat { TCAT_GAMING = 0, TCAT_PERF, TCAT_VISUAL, TCAT_NET, TCAT_PRIV, TCAT_ADV, TCAT_ACT };

// Game phases (wParam of WM_APP_GAME)
enum GamePhase { GPH_PREP = 1, GPH_LAUNCHED, GPH_INGAME, GPH_DONE, GPH_ERROR };

// Control IDs
enum CtrlId {
    IDC_NAV_BASE = 100,
    IDC_LANG_BTN = 200, IDC_THEME_BTN,
    IDC_ADMIN_BADGE,
    // dashboard
    IDC_DASH_SCORE = 300, IDC_DASH_GRADE, IDC_DASH_SYS, IDC_DASH_STATUS,
    IDC_DASH_BOOST, IDC_DASH_TIP, IDC_DASH_GAMES, IDC_DASH_LAST,
    IDC_DASH_PIC,
    // games
    IDC_G_LIST = 400, IDC_G_ADD, IDC_G_SCAN, IDC_G_REMOVE,
    IDC_G_PRIO, IDC_G_AFF, IDC_G_GPUPREF, IDC_G_FSO,
    IDC_G_ARGS, IDC_G_TIMER, IDC_G_POWER, IDC_G_KILL,
    IDC_G_LAUNCH, IDC_G_STATUS, IDC_G_TIP,
    IDC_G_AUTO = 415, IDC_G_AUTOST, IDC_G_RESCHK = 417, IDC_G_RES,
    // boost
    IDC_B_START = 500, IDC_B_UNDO, IDC_B_PROG, IDC_B_LOG, IDC_B_MAX,
    // tweaks
    IDC_T_LIST = 600, IDC_T_APPLY, IDC_T_REVERT, IDC_T_ALL,
    IDC_T_UNDOALL, IDC_T_DETAIL,
    // system
    IDC_S_CPU = 700, IDC_S_RAM, IDC_S_UPTIME, IDC_S_TIMER,
    IDC_S_GRAPH, IDC_S_CLEANRAM, IDC_S_CLEANTEMP, IDC_S_REFRESH,
    IDC_S_RES = 708, IDC_S_RESAPPLY, IDC_S_RESNATIVE, IDC_S_COPY,
    // help
    IDC_H_TEXT = 800,
    // settings
    IDC_SET_LANG = 900, IDC_SET_TRAY, IDC_SET_STARTUP,
    IDC_SET_FOLDER, IDC_SET_RESET, IDC_SET_ABOUT,
    IDC_SET_STLIST, IDC_SET_STEN, IDC_SET_STDIS,
    IDC_SET_THEME, IDC_SET_ACCENT, IDC_SET_HOTKEY, IDC_SET_UPDATE,
    // power page
    IDC_P_PLAN = 950, IDC_P_REFRESH, IDC_P_BEAST, IDC_P_STATUS, IDC_P_LIST,
    IDC_P_APPLYALL, IDC_P_RESTORE, IDC_P_DELETE, IDC_P_NOTE,
    // internet page
    IDC_N_START = 1100, IDC_N_CANCEL, IDC_N_PROG, IDC_N_PING, IDC_N_DOWN, IDC_N_UP,
    IDC_N_IP, IDC_N_GRADE, IDC_N_CF, IDC_N_GOOG, IDC_N_AUTO, IDC_N_DNSST, IDC_N_STATUS,
    IDC_N_DNSLIST = 1113, IDC_N_DNSAPPLY, IDC_N_PINGALL, IDC_N_FASTEST,
    IDC_N_PINGRES, IDC_N_C1, IDC_N_C2, IDC_N_CSET, IDC_N_COPY,
    // live processes page
    IDC_R_LIST = 1300, IDC_R_BOOST, IDC_R_RAM, IDC_R_RESTORE, IDC_R_REFRESH, IDC_R_STATUS,
    IDC_R_LOCK, IDC_R_RAMMB, IDC_R_ADD, IDC_R_KILL,
    // tray menu
    IDM_TRAY_OPEN = 2000, IDM_TRAY_BOOST, IDM_TRAY_EXIT,
};

// ---------- Data structures ----------
struct StartupItem {
    std::wstring name;
    std::wstring cmd;
    int  loc;      // 0 HKCU Run, 1 HKLM Run, 2 Startup folder
    bool enabled;
};
std::vector<StartupItem> StartupEnum();
bool StartupSetEnabled(const StartupItem& it, bool on);
struct GameProfile {
    std::wstring name;
    std::wstring path;
    std::wstring args;
    int  priority;    // 0 normal, 1 above, 2 high
    int  affinity;    // 0 all, 1 all except cpu0
    int  gpuPref;     // 0 default, 1 high performance
    bool disableFSO;
    bool timerBoost;
    bool powerBoost;
    std::wstring killList; // ; separated exe names
    int  playCount;
    std::wstring lastPlayed;
    int  resW, resh;      // per-game resolution (0 = keep current)
    GameProfile() : priority(2), affinity(0), gpuPref(1), disableFSO(false),
        timerBoost(true), powerBoost(true), playCount(0), resW(0), resh(0) {}
};

struct Tweak {
    const char* id;
    const char* nameEn; const char* nameFa;
    const char* descEn; const char* descFa;
    int  cat;          // TweakCat
    bool recommended;
    bool needsReboot;
    bool isAction;     // action tweaks can't be "reverted"
    int  (*check)();   // 1 applied, 0 not applied, -1 n/a
    bool (*apply)();
    bool (*revert)();
};

// ---------- Globals (main.cpp) ----------
extern HINSTANCE   g_hInst;
extern HWND        g_hMain;
extern HANDLE g_mutex;
extern HWND        g_hPages[PAGE_COUNT];
extern int         g_page;
extern std::wstring g_dataDir;
extern std::wstring g_exeDir;
extern bool        g_isAdmin;
extern HFONT       g_hFont;
extern HFONT       g_hFontBig;
extern HFONT       g_hFontHuge;
extern bool        g_closeToTray;
extern bool        g_boosting;
extern bool        g_inGame;
extern std::wstring g_activeGame;
extern int         g_lastScore;
extern std::wstring g_beastGuid;
extern std::wstring g_beastPrev;
extern bool        g_autoBoost;

// ---------- util.cpp ----------
std::wstring Utf8ToWide(const char* s);
std::wstring Utf8ToWide(const std::string& s);
std::string  WideToUtf8(const std::wstring& s);
std::wstring VFormat(const wchar_t* fmt, va_list ap);
std::wstring WFormat(const wchar_t* fmt, ...);
std::wstring NowStamp();
void  LogInit(const std::wstring& path);
void  LogW(const wchar_t* fmt, ...);
bool  EnsureDir(const std::wstring& path);
bool  FileExists(const std::wstring& path);
bool  ReadFileText(const std::wstring& path, std::string& out);
bool  WriteFileText(const std::wstring& path, const std::string& data);
std::wstring JoinPath(const std::wstring& a, const std::wstring& b);
std::wstring BaseName(const std::wstring& path);
std::wstring DirName(const std::wstring& path);
std::wstring ToLower(std::wstring s);
std::vector<std::wstring> SplitWs(const std::wstring& s, wchar_t delim);
std::string JsonEscape(const std::string& s);
std::string JsonEscapeW(const std::wstring& s);

// Registry helpers (backup-aware ones live in tweaks.cpp)
bool RegGetDword(HKEY hive, const wchar_t* sub, const wchar_t* name, DWORD& out);
bool RegSetDword(HKEY hive, const wchar_t* sub, const wchar_t* name, DWORD val);
bool RegGetString(HKEY hive, const wchar_t* sub, const wchar_t* name, std::wstring& out);
bool RegSetString(HKEY hive, const wchar_t* sub, const wchar_t* name, const std::wstring& val, bool expand = false);
bool RegDeleteValue(HKEY hive, const wchar_t* sub, const wchar_t* name);
bool RegValueExists(HKEY hive, const wchar_t* sub, const wchar_t* name);
bool RegDeleteKeyTree(HKEY hive, const wchar_t* sub);

// Process helpers
bool RunHidden(const wchar_t* exe, const wchar_t* args, DWORD* exitCode = NULL);
bool RunCapture(const wchar_t* exe, const wchar_t* args, std::wstring& output);
bool EnablePrivilege(const wchar_t* name);
bool IsUserAdmin();
bool RelaunchAsAdmin();
bool SetStartupRun(bool on);

// Mini JSON parser (subset: object/array/string/number/true/false/null)
struct JVal {
    enum Type { NUL, BOOL, NUM, STR, ARR, OBJ } type;
    bool b; double n; std::string s;
    std::vector<JVal> arr;
    std::vector<std::pair<std::string,JVal>> obj;
    JVal() : type(NUL), b(false), n(0) {}
    const JVal* find(const char* key) const;
    std::wstring wstr(const char* key, const wchar_t* def = L"") const;
    std::string str(const char* key, const char* def = "") const;
    int num(const char* key, int def = 0) const;
    bool boolean(const char* key, bool def = false) const;
};
bool ParseJson(const std::string& text, JVal& out);

// ---------- sysinfo.cpp ----------
std::wstring SysCpuName();
std::wstring SysGpuName();
std::wstring SysSummary();
bool CopyTextToClipboard(const std::wstring& s);
std::wstring SysRamString();
std::wstring SysOsString();
std::wstring SysUptimeString();
std::wstring SysDisplayString();
int  SysCpuCount();
int  SysCpuMHz();      // ~MHz of CPU0, 0 if unknown
int  SysGpuVramMB();   // dedicated VRAM of best GPU, 0 if unknown
class CpuMeter {
    ULARGE_INTEGER prevIdle, prevKernel, prevUser;
    bool first;
public:
    CpuMeter();
    int sample(); // 0..100
};
int  RamUsagePercent();
int  RamAvailMB();
int  RamTotalMB();
// Power
bool     PowerGetActiveGuid(GUID& g);
std::wstring PowerGetName(const GUID& g);
std::wstring PowerGetActiveName();
bool     PowerSetActive(const GUID& g);
bool     PowerEnsureUltimate(GUID& g);
bool     PowerReadSetting(const GUID& scheme, const GUID& sub, const GUID& setting, DWORD& ac, DWORD& dc);
bool     PowerWriteSetting(const GUID& scheme, const GUID& sub, const GUID& setting, DWORD ac, DWORD dc);
std::wstring GuidToWString(const GUID& g);
bool     WStringToGuid(const std::wstring& s, GUID& g);
// Display / memory / timer
bool SetDisplayResolution(int w, int h);
bool GetNativeResolution(int& w, int& h);
bool GetCurrentResolution(int& w, int& h);
int  SetMaxRefreshRate(); // returns new Hz, 0 if unchanged/failed
bool PurgeStandbyList();
bool TimerSetMin(ULONG* prev);
void TimerRestore(ULONG prev);
bool TimerQuery(ULONG& cur, ULONG& min_, ULONG& max_);
bool CreateRestorePoint(const std::wstring& name);
// Processes
bool FindPidsByName(const std::wstring& exe, std::vector<DWORD>& pids);
int  KillProcessesByNames(const std::vector<std::wstring>& names);
bool SetProcessGameOpts(DWORD pid, int priority, int affinityMode);
DWORD_PTR SystemAffinityMask();
// Cleaner
unsigned long long CleanJunkFiles();
// Services
bool ServiceGetStart(const wchar_t* svc, DWORD& start);
bool ServiceSetStart(const wchar_t* svc, DWORD start);
// BCD (bcdedit based)
bool BcdQuery(const wchar_t* token, std::wstring& value, bool& present);
bool BcdSet(const wchar_t* token, const wchar_t* value);
bool BcdDelete(const wchar_t* token);

// ---------- tweaks.cpp ----------
const std::vector<Tweak>& Tweaks_All();
const Tweak* TweakById(const char* id);
int  TweakCheck(const Tweak& t);
bool TweakApply(const Tweak& t);
bool TweakRevert(const Tweak& t);
void TweakDisplayName(const Tweak& t, std::wstring& out);
void TweakDisplayDesc(const Tweak& t, std::wstring& out);
int  Tweaks_Score(int* appliedOut = NULL, int* totalOut = NULL);
bool BackupInit();
bool BackupSave();
bool BackupSetPowerGuid(const std::wstring& g);
bool BackupGetPowerGuid(std::wstring& g);
bool BackupReg(HKEY hive, const wchar_t* sub, const wchar_t* name);
bool BackupService(const wchar_t* svc);
bool BackupBcd(const wchar_t* token);
void BoostRun(HWND notifyWnd);   // worker thread entry (tweaks.cpp)
void BoostUndo(HWND notifyWnd);  // worker thread entry

// ---------- games.cpp ----------
void Games_Load();
void Games_Save();
void Games_Lock();
void Games_Unlock();
std::vector<GameProfile>& Games_All();
bool Games_Add(const std::wstring& exePath, const std::wstring& niceName = L"");
bool Games_Remove(int idx);
struct GameCandidate { std::wstring name; std::wstring exe; std::wstring dir; };
struct GameSpec {
    std::wstring match;  // lower-case exe key, e.g. L"gta5.exe"
    std::wstring title;  // display name
    std::wstring tip;    // localized tip (may be empty)
    int minCpu, minGpu, minRam;
    int recCpu, recGpu, recRam;
    bool hasSpecs;       // false = generic estimate, not curated data
    GameSpec() : minCpu(2), minGpu(2), minRam(8), recCpu(3), recGpu(4), recRam(16), hasSpecs(false) {}
};
void Games_Specs(std::vector<GameSpec>& out);
void Games_Scan(std::vector<GameCandidate>& out);
bool Games_ApplyPersistent(const GameProfile& g);
std::wstring Games_TipFor(const std::wstring& exePath); // from embedded DB
void Games_BoostLaunch(int idx, HWND notifyWnd);
void Games_RestoreAfterSession();
bool Games_IsBusy();

// ---------- settings (ui.cpp) ----------
void Settings_Load();
void Settings_Save();
extern std::wstring g_lastBoost;

// ---------- power.cpp ----------
struct PowerSetting {
    const char* key;
    const char* nameEn; const char* nameFa;
    GUID sub; GUID set;
    DWORD ac; DWORD dc;
    int fmt;
};
const PowerSetting* PowerSettings(int* count);
bool PowerReadActive(const PowerSetting& ps, DWORD& ac, DWORD& dc);
int  PowerApplyAllActive();
int  PowerRestoreAllActive();
bool BeastEnsure(GUID& g);
bool BeastActivate();
bool BeastDeactivate();
bool BeastIsActive();
bool BeastDelete();
void BeastLastOp(int& applied, int& total);

// ---------- net.cpp ----------
void NetTestRun(HWND notifyWnd);
void NetTestCancel();
bool NetTestBusy();
bool NetGetIP(std::wstring& ip);
// preset: 0 restore/auto, 1 cloudflare, 2 google, 3 quad9, 4 opendns, 5 shecan
bool DnsSetPreset(int preset);
bool DnsSetCustom(const wchar_t* d1, const wchar_t* d2);
void DnsPingAll(HWND notifyWnd);
void DnsPingCancel();
bool DnsPingBusy();
std::wstring DnsCurrent();

// ---------- watch.cpp ----------
// ---------- maxfps.cpp ----------
void MaxFpsRun(HWND notifyWnd);
void MaxFpsUndo(HWND notifyWnd);
bool MaxFpsIsActive();

void AutoBoostStart(HWND notifyWnd);
void AutoBoostStop();
bool AutoBoostWatching();


// ---------- procboost.cpp ----------
struct ProcInfo {
    DWORD pid;
    std::wstring name;
    unsigned long long memMB;
    bool isGame;
    bool isSelf;
    ProcInfo() : pid(0), memMB(0), isGame(false), isSelf(false) {}
};
void Proc_Enum(std::vector<ProcInfo>& out);
bool Proc_Boost(DWORD pid, std::wstring& msg);
bool Proc_RamFocus(DWORD pid, std::wstring& msg);
bool Proc_Restore(DWORD pid);
bool Proc_RamLock(DWORD pid, int mb, std::wstring& msg); // mb<=0 = MAX
bool Proc_ImagePath(DWORD pid, std::wstring& path);
int  Proc_TrimAll(); // EmptyWorkingSet on all user processes, returns count

// ---------- ui.cpp ----------
bool UI_Create(HINSTANCE hInst);
void UI_ShowPage(int page);
void UI_RefreshAll();
void UI_TrayInit();
void UI_TrayRemove();
void UI_TrayShow(bool show);
void UI_LogBoost(const wchar_t* line);
void UI_BoostProgress(int pct);
void UI_BoostDone(bool ok);
void UI_GameEvent(int phase, const wchar_t* info);
void UI_ApplyLanguage();
LRESULT CALLBACK UI_MainProc(HWND h, UINT m, WPARAM w, LPARAM l);
