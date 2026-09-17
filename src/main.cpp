// FPS Booster Pro - Entry point
#include "app.h"
#include <gdiplus.h>

// ---------- Globals ----------
HINSTANCE   g_hInst = NULL;
HWND        g_hMain = NULL;
HANDLE      g_mutex = NULL;
HWND        g_hPages[PAGE_COUNT] = {0};
int         g_page = 0;
std::wstring g_dataDir;
std::wstring g_exeDir;
bool        g_isAdmin = false;
HFONT       g_hFont = NULL;
HFONT       g_hFontBig = NULL;
HFONT       g_hFontHuge = NULL;
bool        g_closeToTray = true;
bool        g_boosting = false;
bool        g_inGame = false;
std::wstring g_activeGame;
int         g_lastScore = 0;
std::wstring g_beastGuid;
std::wstring g_beastPrev;
bool        g_autoBoost = false;

static HFONT MakeFont(int pt, bool bold) {
    HDC dc = GetDC(NULL);
    int dpi = dc ? GetDeviceCaps(dc, LOGPIXELSY) : 96;
    if (dc) ReleaseDC(NULL, dc);
    if (dpi < 96) dpi = 96;
    int h = -MulDiv(pt, dpi, 72);
    return CreateFontW(h, 0, 0, 0, bold ? FW_BOLD : FW_NORMAL,
        FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
}

static void EnableDpiAwareness() {
    HMODULE u = GetModuleHandleW(L"user32.dll");
    if (u) {
        typedef BOOL (WINAPI *SPDAC)(HANDLE);
        SPDAC fn = (SPDAC)GetProcAddress(u, "SetProcessDpiAwarenessContext");
        if (fn && fn((HANDLE)-4)) return; // PER_MONITOR_AWARE_V2
        typedef BOOL (WINAPI *SPDA)(void);
        SPDA f2 = (SPDA)GetProcAddress(u, "SetProcessDPIAware");
        if (f2) f2();
    }
}

int main() {
    g_hInst = GetModuleHandleW(NULL);
    EnableDpiAwareness();

    // Single instance
    g_mutex = CreateMutexW(NULL, TRUE, APP_MUTEX);
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND old = FindWindowW(L"FPSBoosterMain", NULL);
        if (old) {
            ShowWindow(old, SW_RESTORE);
            SetForegroundWindow(old);
        }
        if (g_mutex) { CloseHandle(g_mutex); g_mutex = NULL; }
        return 0;
    }

    g_isAdmin = IsUserAdmin();

    INITCOMMONCONTROLSEX icc;
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_LISTVIEW_CLASSES | ICC_PROGRESS_CLASS | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icc);

    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    ULONG_PTR gdipTok = 0;
    Gdiplus::GdiplusStartupInput gsi;
    Gdiplus::GdiplusStartup(&gdipTok, &gsi, NULL);

    // Paths
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(NULL, exe, MAX_PATH);
    g_exeDir = DirName(exe);
    wchar_t common[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_COMMON_APPDATA, NULL, 0, common)))
        g_dataDir = JoinPath(common, L"FPSBooster");
    else
        g_dataDir = JoinPath(g_exeDir, L"data");
    EnsureDir(g_dataDir);
    LogInit(JoinPath(g_dataDir, L"fpsbooster.log"));

    Strings_Init();
    Settings_Load();

    g_hFont = MakeFont(10, false);
    g_hFontBig = MakeFont(12, true);
    g_hFontHuge = MakeFont(44, true);

    BackupInit();
    Games_Load();
    int ap = 0, tt = 0;
    g_lastScore = Tweaks_Score(&ap, &tt);
    LogW(L"Initial score: %d/100 (%d/%d), admin=%d", g_lastScore, ap, tt, g_isAdmin ? 1 : 0);

    if (!UI_Create(g_hInst)) {
        LogW(L"UI_Create failed");
        return 1;
    }
    UI_TrayInit();
    if (g_autoBoost) AutoBoostStart(g_hMain);

    if (!g_isAdmin) {
        // Still allow to run (games page works), but warn
        LogW(L"Running without admin rights");
    }

    bool startMin = wcsstr(GetCommandLineW(), L"/min") != NULL;
    if (startMin && g_closeToTray) ShowWindow(g_hMain, SW_HIDE);
    else { ShowWindow(g_hMain, SW_SHOW); UpdateWindow(g_hMain); }

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    // Shutdown
    NetTestCancel();
    DnsPingCancel();
    AutoBoostStop();
    Games_RestoreAfterSession();
    Settings_Save();
    Games_Save();
    BackupSave();
    UI_TrayRemove();
    if (gdipTok) Gdiplus::GdiplusShutdown(gdipTok);
    CoUninitialize();
    if (g_mutex) { CloseHandle(g_mutex); g_mutex = NULL; }
    LogW(L"===== FPS Booster Pro exited =====");
    return (int)msg.wParam;
}
