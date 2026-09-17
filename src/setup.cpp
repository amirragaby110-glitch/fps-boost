// FPS Booster Pro - Setup installer (wizard + uninstaller + silent /S)
// Standalone source: Windows SDK only + generated setup_license.h
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <tlhelp32.h>
#include <objbase.h>
#include <stdio.h>
#include <string>
#include <vector>
#include "setup_license.h"

static const wchar_t* kAppName = L"FPS Booster Pro";
static const wchar_t* kVer = L"1.4.0";
static const wchar_t* kExeName = L"fpsbooster.exe";
static const wchar_t* kUnName = L"uninstall.exe";
static const wchar_t* kRegUn = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\FPSBooster";
static const char* kFooterMagic = "FPSBOOSTERSETUP1";

static std::wstring W(const char* s) {
    if (!s || !*s) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    if (n <= 0) return std::wstring();
    std::vector<wchar_t> b(n);
    MultiByteToWideChar(CP_UTF8, 0, s, -1, &b[0], n);
    return std::wstring(&b[0]);
}
static std::wstring Join(const std::wstring& a, const std::wstring& b) {
    if (!a.empty() && a[a.size() - 1] != L'\\') return a + L"\\" + b;
    return a + b;
}

// ---------- Payload footer (written by build.py, last 64 bytes) ----------
#pragma pack(push, 1)
struct SetupFooter {
    char magic[32];
    unsigned long long off;
    unsigned long long size;
    unsigned long long flags;
};
#pragma pack(pop)

struct PayloadInfo { unsigned long long off; unsigned long long size; };

static bool SelfPath(wchar_t* out) { return GetModuleFileNameW(NULL, out, MAX_PATH) > 0; }

static bool ReadFooter(PayloadInfo& pi) {
    wchar_t self[MAX_PATH];
    if (!SelfPath(self)) return false;
    HANDLE h = CreateFileW(self, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER fs; fs.QuadPart = 0;
    GetFileSizeEx(h, &fs);
    bool ok = false;
    if (fs.QuadPart > 128) {
        LARGE_INTEGER pos; pos.QuadPart = fs.QuadPart - 64;
        if (SetFilePointerEx(h, pos, NULL, FILE_BEGIN)) {
            char buf[64]; DWORD rd = 0;
            if (ReadFile(h, buf, 64, &rd, NULL) && rd == 64) {
                SetupFooter* f = (SetupFooter*)buf;
                if (!memcmp(f->magic, kFooterMagic, 16)) {
                    if (f->size > 1024 && f->off > 1024 &&
                        f->off + f->size <= (unsigned long long)fs.QuadPart) {
                        pi.off = f->off; pi.size = f->size; ok = true;
                    }
                }
            }
        }
    }
    CloseHandle(h);
    return ok;
}

static bool ExtractPayload(const wchar_t* dst, void (*cb)(int)) {
    PayloadInfo pi;
    if (!ReadFooter(pi)) return false;
    wchar_t self[MAX_PATH]; SelfPath(self);
    HANDLE in = CreateFileW(self, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (in == INVALID_HANDLE_VALUE) return false;
    HANDLE out = CreateFileW(dst, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (out == INVALID_HANDLE_VALUE) { CloseHandle(in); return false; }
    LARGE_INTEGER pos; pos.QuadPart = (LONGLONG)pi.off;
    SetFilePointerEx(in, pos, NULL, FILE_BEGIN);
    const DWORD CH = 1 << 20;
    std::vector<char> b(CH);
    unsigned long long left = pi.size;
    bool ok = true;
    while (left > 0) {
        DWORD want = left > CH ? CH : (DWORD)left;
        DWORD rd = 0, wr = 0;
        if (!ReadFile(in, &b[0], want, &rd, NULL) || rd != want) { ok = false; break; }
        if (!WriteFile(out, &b[0], rd, &wr, NULL) || wr != rd) { ok = false; break; }
        left -= rd;
        if (cb) cb((int)((pi.size - left) * 100 / pi.size));
    }
    CloseHandle(in); CloseHandle(out);
    if (!ok) DeleteFileW(dst);
    return ok;
}

// ---------- Helpers ----------
static void KillApp() {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return;
    PROCESSENTRY32W pe; pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe)) {
        do {
            wchar_t low[MAX_PATH];
            wcscpy(low, pe.szExeFile);
            _wcslwr(low);
            if (!wcscmp(low, kExeName)) {
                HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, pe.th32ProcessID);
                if (h) { TerminateProcess(h, 1); CloseHandle(h); }
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    Sleep(500);
}

static bool MakeShortcut(const wchar_t* target, const wchar_t* workdir, const wchar_t* link, const wchar_t* desc) {
    CoInitialize(NULL);
    bool ok = false;
    IShellLinkW* sl = NULL;
    if (SUCCEEDED(CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, IID_IShellLinkW, (void**)&sl))) {
        sl->SetPath(target);
        sl->SetWorkingDirectory(workdir);
        sl->SetDescription(desc);
        sl->SetIconLocation(target, 0);
        IPersistFile* pf = NULL;
        if (SUCCEEDED(sl->QueryInterface(IID_IPersistFile, (void**)&pf))) {
            ok = SUCCEEDED(pf->Save(link, TRUE));
            pf->Release();
        }
        sl->Release();
    }
    CoUninitialize();
    return ok;
}

static void SelfDelete(const wchar_t* path) {
    wchar_t cmd[1024];
    _snwprintf(cmd, 1024, L"cmd.exe /c ping -n 3 127.0.0.1 >nul & del \"%s\" & exit", path);
    cmd[1023] = 0;
    STARTUPINFOW si; PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si)); si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW; si.wShowWindow = SW_HIDE;
    ZeroMemory(&pi, sizeof(pi));
    if (CreateProcessW(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    }
}

static bool WriteUninstallKey(const std::wstring& dir) {
    HKEY h = NULL;
    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, kRegUn, 0, NULL, 0, KEY_WRITE, NULL, &h, NULL) != ERROR_SUCCESS)
        return false;
    std::wstring exe = Join(dir, kExeName);
    std::wstring un = Join(dir, kUnName);
    std::wstring unq = L"\"" + un + L"\"";
    std::wstring icon = exe + L",0";
    DWORD dw1 = 1;
    RegSetValueExW(h, L"DisplayName", 0, REG_SZ, (const BYTE*)kAppName, (DWORD)((wcslen(kAppName) + 1) * 2));
    RegSetValueExW(h, L"DisplayVersion", 0, REG_SZ, (const BYTE*)kVer, (DWORD)((wcslen(kVer) + 1) * 2));
    RegSetValueExW(h, L"Publisher", 0, REG_SZ, (const BYTE*)L"FPS Booster", 24);
    RegSetValueExW(h, L"InstallLocation", 0, REG_SZ, (const BYTE*)dir.c_str(), (DWORD)((dir.size() + 1) * 2));
    RegSetValueExW(h, L"DisplayIcon", 0, REG_SZ, (const BYTE*)icon.c_str(), (DWORD)((icon.size() + 1) * 2));
    RegSetValueExW(h, L"UninstallString", 0, REG_SZ, (const BYTE*)unq.c_str(), (DWORD)((unq.size() + 1) * 2));
    RegSetValueExW(h, L"NoModify", 0, REG_DWORD, (const BYTE*)&dw1, 4);
    RegSetValueExW(h, L"NoRepair", 0, REG_DWORD, (const BYTE*)&dw1, 4);
    RegCloseKey(h);
    return true;
}

// ---------- Install / uninstall core ----------
static void (*g_cbProg)(int) = NULL;
static void (*g_cbStage)(int) = NULL;
static void ProgCb(int p) { if (g_cbProg) g_cbProg(p); }

static bool InstallCore(const wchar_t* dir, bool desk, bool menu) {
    if (g_cbStage) g_cbStage(0);
    KillApp();
    LONG cr = SHCreateDirectoryExW(NULL, dir, NULL);
    if (cr != ERROR_SUCCESS && cr != ERROR_ALREADY_EXISTS) return false;
    if (g_cbStage) g_cbStage(1);
    std::wstring exe = Join(dir, kExeName);
    if (!ExtractPayload(exe.c_str(), ProgCb)) return false;
    if (g_cbStage) g_cbStage(2);
    wchar_t self[MAX_PATH]; SelfPath(self);
    CopyFileW(self, Join(dir, kUnName).c_str(), FALSE);
    wchar_t sm[MAX_PATH], dt[MAX_PATH];
    bool haveSM = SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_COMMON_PROGRAMS, NULL, 0, sm));
    bool haveDT = SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_COMMON_DESKTOPDIRECTORY, NULL, 0, dt));
    if (menu && haveSM) {
        std::wstring fld = Join(sm, kAppName);
        SHCreateDirectoryExW(NULL, fld.c_str(), NULL);
        MakeShortcut(exe.c_str(), dir, Join(fld, L"FPS Booster Pro.lnk").c_str(), kAppName);
        std::wstring un = Join(dir, kUnName);
        MakeShortcut(un.c_str(), dir, Join(fld, L"Uninstall.lnk").c_str(), L"Uninstall FPS Booster Pro");
    }
    if (desk && haveDT)
        MakeShortcut(exe.c_str(), dir, Join(dt, L"FPS Booster Pro.lnk").c_str(), kAppName);
    if (g_cbStage) g_cbStage(3);
    WriteUninstallKey(dir);
    if (g_cbStage) g_cbStage(4);
    return true;
}

static bool FindInstallDir(bool viaCopy, std::wstring& out) {
    if (viaCopy) {
        wchar_t self[MAX_PATH];
        if (!SelfPath(self)) return false;
        std::wstring s = self;
        size_t p = s.find_last_of(L"\\/");
        if (p == std::wstring::npos) return false;
        out = s.substr(0, p);
        return true;
    }
    HKEY h = NULL;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, kRegUn, 0, KEY_READ, &h) != ERROR_SUCCESS) return false;
    wchar_t v[MAX_PATH]; DWORD dl = sizeof(v), tp = 0;
    LONG r = RegQueryValueExW(h, L"InstallLocation", NULL, &tp, (BYTE*)v, &dl);
    RegCloseKey(h);
    if (r != ERROR_SUCCESS || !v[0]) return false;
    out = v;
    return true;
}

static bool UninstallCore(const std::wstring& dir) {
    if (g_cbStage) g_cbStage(10);
    KillApp();
    // safety: only touch dirs that contain our marker
    if (GetFileAttributesW(Join(dir, kExeName).c_str()) == INVALID_FILE_ATTRIBUTES &&
        GetFileAttributesW(Join(dir, kUnName).c_str()) == INVALID_FILE_ATTRIBUTES)
        return false;
    if (g_cbStage) g_cbStage(11);
    wchar_t sm[MAX_PATH], dt[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_COMMON_PROGRAMS, NULL, 0, sm))) {
        std::wstring fld = Join(sm, kAppName);
        DeleteFileW(Join(fld, L"FPS Booster Pro.lnk").c_str());
        DeleteFileW(Join(fld, L"Uninstall.lnk").c_str());
        RemoveDirectoryW(fld.c_str());
    }
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_COMMON_DESKTOPDIRECTORY, NULL, 0, dt)))
        DeleteFileW(Join(dt, L"FPS Booster Pro.lnk").c_str());
    DeleteFileW(Join(dir, kExeName).c_str());
    if (g_cbStage) g_cbStage(4);
    RegDeleteTreeW(HKEY_LOCAL_MACHINE, kRegUn);
    RemoveDirectoryW(dir.c_str()); // only if empty
    return true;
}

// ---------- Wizard UI ----------
#define IDC_BACK 101
#define IDC_NEXT 102
#define IDC_CANCEL 103
#define IDC_BROWSE 104
#define WM_APP_PROG (WM_APP + 1)
#define WM_APP_END (WM_APP + 2)

enum { PG_WELCOME, PG_LICENSE, PG_DIR, PG_OPTS, PG_PROG, PG_DONE, PG_UCONFIRM, PG_UPROG, PG_UDONE, PG_COUNT };

static HINSTANCE g_hInst = NULL;
static HWND g_hWnd = NULL, g_hBack = NULL, g_hNext = NULL, g_hCancel = NULL;
static HWND g_pages[PG_COUNT] = {0};
static HWND g_hProg = NULL, g_hStatus = NULL, g_hDone = NULL;
static HWND g_hUProg = NULL, g_hUStatus = NULL, g_hUDone = NULL, g_hULabel = NULL;
static HWND g_hEdDir = NULL, g_hChkAccept = NULL, g_hChkDesk = NULL, g_hChkMenu = NULL, g_hChkLaunch = NULL;
static HFONT g_hFont = NULL;
static int g_page = 0;
static bool g_uninstall = false, g_working = false, g_ok = false, g_threadStarted = false;
static wchar_t g_dir[MAX_PATH] = {0};
static std::wstring g_unDir;

static HWND Mk(HWND p, const wchar_t* cls, const wchar_t* txt, DWORD st, int x, int y, int w, int h, int id) {
    HWND c = CreateWindowExW(0, cls, txt, st, x, y, w, h, p, (HMENU)(INT_PTR)id, g_hInst, NULL);
    if (c) SendMessageW(c, WM_SETFONT, (WPARAM)g_hFont, 0);
    return c;
}

static const char* StageText(int s) {
    switch (s) {
    case 0: return "Preparing...\nآماده‌سازی...";
    case 1: return "Extracting files...\nاستخراج فایل‌ها...";
    case 2: return "Creating shortcuts...\nساخت میانبرها...";
    case 3: return "Writing registry...\nثبت در رجیستری...";
    case 4: return "Done!\nتمام شد!";
    case 10: return "Stopping app...\nبستن برنامه...";
    case 11: return "Removing files...\nحذف فایل‌ها...";
    default: return "";
    }
}

static void ShowPage(int n) {
    g_page = n;
    for (int i = 0; i < PG_COUNT; i++)
        if (g_pages[i]) ShowWindow(g_pages[i], i == n ? SW_SHOW : SW_HIDE);
    bool last = (n == PG_DONE || n == PG_UDONE);
    bool prog = (n == PG_PROG || n == PG_UPROG);
    ShowWindow(g_hBack, (!last && !prog && n != PG_WELCOME && n != PG_UCONFIRM) ? SW_SHOW : SW_HIDE);
    ShowWindow(g_hNext, (!last && !prog) ? SW_SHOW : SW_HIDE);
    EnableWindow(g_hCancel, prog ? FALSE : TRUE);
    SetWindowTextW(g_hCancel, last ? L"Finish" : L"Cancel");
    if (n == PG_LICENSE)
        EnableWindow(g_hNext, Button_GetCheck(g_hChkAccept) == BST_CHECKED ? TRUE : FALSE);
    else if (!last && !prog)
        EnableWindow(g_hNext, TRUE);
    if (n == PG_UCONFIRM) SetWindowTextW(g_hNext, L"Uninstall");
    else SetWindowTextW(g_hNext, L"Next >");
    if ((n == PG_PROG || n == PG_UPROG) && !g_threadStarted) {
        g_threadStarted = true;
        g_working = true;
        DWORD tid = 0;
        HANDLE h = CreateThread(NULL, 0, [](LPVOID)->DWORD {
            bool ok = false;
            if (!g_uninstall) ok = InstallCore(g_dir,
                Button_GetCheck(g_hChkDesk) == BST_CHECKED,
                Button_GetCheck(g_hChkMenu) == BST_CHECKED);
            else ok = UninstallCore(g_unDir);
            PostMessageW(g_hWnd, WM_APP_END, ok ? 1 : 0, 0);
            return 0;
        }, NULL, 0, &tid);
        if (h) CloseHandle(h);
    }
}

static void UiProg(int p) { PostMessageW(g_hWnd, WM_APP_PROG, (WPARAM)p, 0); }
static void UiStage(int s) { PostMessageW(g_hWnd, WM_APP_PROG, (WPARAM)-1, (LPARAM)s); }

static INT_PTR BrowseDir(HWND owner, wchar_t* out) {
    BROWSEINFOW bi;
    ZeroMemory(&bi, sizeof(bi));
    bi.hwndOwner = owner;
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    LPITEMIDLIST pid = SHBrowseForFolderW(&bi);
    if (!pid) return 0;
    INT_PTR ok = SHGetPathFromIDListW(pid, out) ? 1 : 0;
    CoTaskMemFree(pid);
    return ok;
}

static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_COMMAND: {
        int id = LOWORD(w), code = HIWORD(w);
        if (id == IDC_CANCEL) {
            if (!g_working) DestroyWindow(h);
            return 0;
        }
        if (id == IDC_BACK && !g_working) {
            if (g_page > 0 && g_page < PG_PROG) ShowPage(g_page - 1);
            return 0;
        }
        if (id == IDC_BROWSE) {
            wchar_t d[MAX_PATH];
            wcscpy(d, g_dir);
            GetWindowTextW(g_hEdDir, d, MAX_PATH);
            if (BrowseDir(h, d)) SetWindowTextW(g_hEdDir, d);
            return 0;
        }
        if (id == IDC_NEXT && !g_working) {
            if (g_page == PG_LICENSE && Button_GetCheck(g_hChkAccept) != BST_CHECKED) return 0;
            if (g_page == PG_DIR) {
                GetWindowTextW(g_hEdDir, g_dir, MAX_PATH);
                if (!g_dir[0] || !g_dir[1]) {
                    MessageBoxW(h, W("Please choose an install folder.\nلطفا پوشه نصب را انتخاب کنید.").c_str(), kAppName, MB_OK | MB_ICONWARNING);
                    return 0;
                }
            }
            if (g_page == PG_PROG - 1 || (g_uninstall && g_page == PG_UCONFIRM)) {
                if (g_uninstall) { ShowPage(PG_UPROG); return 0; }
                ShowPage(PG_PROG); return 0;
            }
            if (g_page == PG_UCONFIRM) { ShowPage(PG_UPROG); return 0; }
            ShowPage(g_page + 1); return 0;
        }
        if (code == BN_CLICKED && (HWND)l == g_hChkAccept)
            EnableWindow(g_hNext, Button_GetCheck(g_hChkAccept) == BST_CHECKED ? TRUE : FALSE);
        return 0;
    }
    case WM_APP_PROG:
        if ((int)w >= 0) {
            if (g_page == PG_PROG && g_hProg) SendMessageW(g_hProg, PBM_SETPOS, w, 0);
            if (g_page == PG_UPROG && g_hUProg) SendMessageW(g_hUProg, PBM_SETPOS, w, 0);
        } else {
            if (g_page == PG_PROG && g_hStatus) SetWindowTextW(g_hStatus, W(StageText((int)l)).c_str());
            if (g_page == PG_UPROG && g_hUStatus) SetWindowTextW(g_hUStatus, W(StageText((int)l)).c_str());
        }
        return 0;
    case WM_APP_END: {
        g_working = false;
        g_ok = (w == 1);
        if (!g_uninstall) {
            SendMessageW(g_hProg, PBM_SETPOS, g_ok ? 100 : 0, 0);
            SetWindowTextW(g_hDone, g_ok
                ? W("Installation completed successfully!\nنصب با موفقیت انجام شد!").c_str()
                : W("Installation FAILED. Try running setup as administrator.\nنصب ناموفق بود. setup را با دسترسی ادمین اجرا کنید.").c_str());
            ShowPage(PG_DONE);
        } else {
            SetWindowTextW(g_hUDone, g_ok
                ? W("Uninstalled successfully.\nحذف با موفقیت انجام شد.").c_str()
                : W("Uninstall FAILED or nothing found.\nحذف ناموفق بود یا چیزی پیدا نشد.").c_str());
            if (g_ok) {
                wchar_t self[MAX_PATH]; SelfPath(self);
                wchar_t low[MAX_PATH]; wcscpy(low, self); _wcslwr(low);
                if (wcsstr(low, kUnName)) SelfDelete(self);
            }
            ShowPage(PG_UDONE);
        }
        return 0;
    }
    case WM_CLOSE:
        if (!g_working) DestroyWindow(h);
        return 0;
    case WM_DESTROY:
        if (g_page == PG_DONE && g_ok && Button_GetCheck(g_hChkLaunch) == BST_CHECKED) {
            std::wstring exe = Join(g_dir, kExeName);
            ShellExecuteW(NULL, L"open", exe.c_str(), NULL, g_dir, SW_SHOWNORMAL);
        }
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

static HWND NewPage(HWND parent, int idx) {
    HWND p = CreateWindowExW(0, WC_STATICW, L"", WS_CHILD | WS_CLIPSIBLINGS, 12, 12, 560, 340, parent, NULL, g_hInst, NULL);
    g_pages[idx] = p;
    return p;
}

static bool BuildUI(bool uninstall) {
    g_hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    WNDCLASSW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.lpfnWndProc = WndProc;
    wc.hInstance = g_hInst;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"FPSBoosterSetup";
    wc.hIcon = LoadIconW(g_hInst, MAKEINTRESOURCEW(1));
    if (!RegisterClassW(&wc)) return false;

    int WW = 600, WH = 460;
    int x = (GetSystemMetrics(SM_CXSCREEN) - WW) / 2;
    int y = (GetSystemMetrics(SM_CYSCREEN) - WH) / 2;
    g_hWnd = CreateWindowExW(0, L"FPSBoosterSetup",
        uninstall ? L"FPS Booster Pro - Uninstall" : L"FPS Booster Pro - Setup",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        x, y, WW, WH, NULL, NULL, g_hInst, NULL);
    if (!g_hWnd) return false;

    // pages
    HWND p0 = NewPage(g_hWnd, PG_WELCOME);
    std::wstring wel = W("Welcome to FPS Booster Pro Setup ") + kVer +
        W("\n\nThis wizard will install FPS Booster Pro on your computer.\nClick Next to continue.\n\nبه برنامه نصب خوش آمدید. برای ادامه Next را بزنید.");
    Mk(p0, WC_STATICW, wel.c_str(), WS_CHILD | WS_VISIBLE, 8, 8, 544, 200, 0);

    HWND p1 = NewPage(g_hWnd, PG_LICENSE);
    HWND lic = Mk(p1, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY | WS_VSCROLL | ES_AUTOVSCROLL, 8, 8, 544, 260, 0);
    SetWindowTextW(lic, W(kSetupLicenseUtf8).c_str());
    g_hChkAccept = Mk(p1, L"BUTTON", W("I accept the license (می‌پذیرم)").c_str(), WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 8, 280, 400, 24, 0);

    HWND p2 = NewPage(g_hWnd, PG_DIR);
    Mk(p2, WC_STATICW, W("Install folder (پوشه نصب):").c_str(), WS_CHILD | WS_VISIBLE, 8, 8, 544, 24, 0);
    g_hEdDir = Mk(p2, L"EDIT", g_dir, WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, 8, 36, 440, 26, 0);
    Mk(p2, L"BUTTON", L"Browse...", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 456, 34, 96, 28, IDC_BROWSE);

    HWND p3 = NewPage(g_hWnd, PG_OPTS);
    g_hChkDesk = Mk(p3, L"BUTTON", W("Desktop shortcut (میانبر دسکتاپ)").c_str(), WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 8, 8, 400, 24, 0);
    g_hChkMenu = Mk(p3, L"BUTTON", W("Start Menu shortcut (میانبر استارت)").c_str(), WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 8, 40, 400, 24, 0);
    g_hChkLaunch = Mk(p3, L"BUTTON", W("Launch when finished (اجرا پس از نصب)").c_str(), WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 8, 72, 400, 24, 0);
    Button_SetCheck(g_hChkDesk, BST_CHECKED);
    Button_SetCheck(g_hChkMenu, BST_CHECKED);
    Button_SetCheck(g_hChkLaunch, BST_CHECKED);

    HWND p4 = NewPage(g_hWnd, PG_PROG);
    g_hStatus = Mk(p4, WC_STATICW, L"", WS_CHILD | WS_VISIBLE, 8, 8, 544, 48, 0);
    g_hProg = Mk(p4, PROGRESS_CLASSW, L"", WS_CHILD | WS_VISIBLE | PBS_SMOOTH, 8, 64, 544, 26, 0);
    SendMessageW(g_hProg, PBM_SETRANGE, 0, MAKELPARAM(0, 100));

    HWND p5 = NewPage(g_hWnd, PG_DONE);
    g_hDone = Mk(p5, WC_STATICW, L"", WS_CHILD | WS_VISIBLE, 8, 8, 544, 120, 0);

    HWND p6 = NewPage(g_hWnd, PG_UCONFIRM);
    std::wstring ul = W("This will remove FPS Booster Pro from:\n\n") + g_unDir +
        W("\n\nClick Uninstall to continue.\nبرای حذف، Uninstall را بزنید.");
    g_hULabel = Mk(p6, WC_STATICW, ul.c_str(), WS_CHILD | WS_VISIBLE, 8, 8, 544, 200, 0);

    HWND p7 = NewPage(g_hWnd, PG_UPROG);
    g_hUStatus = Mk(p7, WC_STATICW, L"", WS_CHILD | WS_VISIBLE, 8, 8, 544, 48, 0);
    g_hUProg = Mk(p7, PROGRESS_CLASSW, L"", WS_CHILD | WS_VISIBLE | PBS_SMOOTH | PBS_MARQUEE, 8, 64, 544, 26, 0);
    SendMessageW(g_hUProg, PBM_SETMARQUEE, TRUE, 50);

    HWND p8 = NewPage(g_hWnd, PG_UDONE);
    g_hUDone = Mk(p8, WC_STATICW, L"", WS_CHILD | WS_VISIBLE, 8, 8, 544, 120, 0);

    // bottom buttons
    Mk(g_hWnd, WC_STATICW, L"", WS_CHILD | WS_VISIBLE | SS_ETCHEDHORZ, 12, 362, 560, 2, 0);
    g_hBack = Mk(g_hWnd, L"BUTTON", L"< Back", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 240, 376, 100, 30, IDC_BACK);
    g_hNext = Mk(g_hWnd, L"BUTTON", L"Next >", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_DEFPUSHBUTTON, 348, 376, 100, 30, IDC_NEXT);
    g_hCancel = Mk(g_hWnd, L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 456, 376, 100, 30, IDC_CANCEL);

    g_cbProg = UiProg; g_cbStage = UiStage;
    ShowPage(uninstall ? PG_UCONFIRM : PG_WELCOME);
    ShowWindow(g_hWnd, SW_SHOW);
    UpdateWindow(g_hWnd);
    return true;
}

static bool ParseArgs(bool& silent, bool& uninstall) {
    silent = false; uninstall = false;
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return true;
    for (int i = 1; i < argc; i++) {
        wchar_t a[64];
        wcsncpy(a, argv[i], 63); a[63] = 0;
        _wcslwr(a);
        if (!wcscmp(a, L"/s") || !wcscmp(a, L"/silent")) silent = true;
        if (!wcscmp(a, L"/uninstall") || !wcscmp(a, L"/u")) uninstall = true;
    }
    LocalFree(argv);
    // copied as uninstall.exe => uninstall mode
    wchar_t self[MAX_PATH]; SelfPath(self);
    _wcslwr(self);
    if (wcsstr(self, kUnName)) uninstall = true;
    return true;
}

static void DefaultDir() {
    wchar_t pf[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_PROGRAM_FILES, NULL, 0, pf)))
        _snwprintf(g_dir, MAX_PATH, L"%s\\FPSBooster", pf);
    else
        wcscpy(g_dir, L"C:\\FPSBooster");
    g_dir[MAX_PATH - 1] = 0;
}

int WINAPI wWinMain(HINSTANCE h, HINSTANCE, LPWSTR, int) {
    g_hInst = h;
    InitCommonControls();
    bool silent = false, uninstall = false;
    ParseArgs(silent, uninstall);
    g_uninstall = uninstall;
    DefaultDir();
    if (uninstall) {
        wchar_t self[MAX_PATH]; SelfPath(self);
        wchar_t low[MAX_PATH]; wcscpy(low, self); _wcslwr(low);
        bool viaCopy = wcsstr(low, kUnName) != NULL;
        if (!FindInstallDir(viaCopy, g_unDir)) {
            if (!silent) MessageBoxW(NULL,
                W("Installation not found. Nothing to remove.\nنصب پیدا نشد.").c_str(),
                kAppName, MB_OK | MB_ICONINFORMATION);
            return 1;
        }
    }
    if (silent) {
        if (uninstall) return UninstallCore(g_unDir) ? 0 : 1;
        return InstallCore(g_dir, true, true) ? 0 : 1;
    }
    if (!BuildUI(uninstall)) return 1;
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}
