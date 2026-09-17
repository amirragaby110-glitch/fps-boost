// FPS Booster Pro - User interface (dark theme, sidebar, 7 pages)
#include "app.h"
#include <commdlg.h>
#include <gdiplus.h>
#include <stdio.h>

using namespace Gdiplus;

// ---------- Theme ----------
#define COL_BG        RGB(13,18,32)
#define COL_PANEL     RGB(20,27,45)
#define COL_SIDE      RGB(9,13,25)
#define COL_CARD      RGB(26,34,56)
#define COL_ACCENT    RGB(34,211,238)
#define COL_ACCENT2   RGB(232,121,249)
#define COL_TEXT      RGB(226,232,240)
#define COL_MUTED     RGB(148,163,184)
#define COL_GREEN     RGB(52,211,153)
#define COL_RED       RGB(248,113,113)
#define COL_YELLOW    RGB(251,191,36)
#define COL_ROWALT    RGB(24,32,54)
#define COL_SELBAR    RGB(30,58,80)

static int g_scale = 96;
static int S(int v) { return (v * g_scale + 48) / 96; }

static HBRUSH hBrBg = NULL, hBrPanel = NULL, hBrSide = NULL, hBrCard = NULL, hBrEdit = NULL;
static Bitmap* g_imgLogo = NULL;
static Bitmap* g_imgBanner = NULL;

// Colored labels registry
struct ColorLabel { HWND h; COLORREF c; };
static std::vector<ColorLabel> g_colors;
static void LabelColor(HWND h, COLORREF c) {
    for (size_t i = 0; i < g_colors.size(); i++)
        if (g_colors[i].h == h) { g_colors[i].c = c; return; }
    ColorLabel e; e.h = h; e.c = c; g_colors.push_back(e);
}
static void SetLabel(HWND h, const std::wstring& s, COLORREF c) {
    SetWindowTextW(h, s.c_str());
    LabelColor(h, c);
    InvalidateRect(h, NULL, TRUE);
}

// ---------- Settings ----------
std::wstring g_lastBoost;
void Settings_Load() {
    g_lastBoost.clear();
    std::string text;
    if (!ReadFileText(JoinPath(g_dataDir, L"settings.json"), text)) return;
    JVal root;
    if (!ParseJson(text, root) || root.type != JVal::OBJ) return;
    Strings_SetLang(root.num("lang", 1));
    g_closeToTray = root.num("tray", 1) != 0;
    g_lastBoost = root.wstr("lastBoost");
    g_lastScore = root.num("score", 0);
}
void Settings_Save() {
    char nb[128];
    snprintf(nb, 128, "{\"lang\":%d,\"tray\":%d,\"score\":%d", Strings_GetLang(), g_closeToTray?1:0, g_lastScore);
    std::string j = nb;
    j += ",\"lastBoost\":\"" + JsonEscapeW(g_lastBoost) + "\"}";
    WriteFileText(JoinPath(g_dataDir, L"settings.json"), j);
}

// ---------- Control helpers ----------
static HWND hSide = NULL, hTitle = NULL, hLangBtn = NULL, hAdminBadge = NULL, hStatusBar = NULL;

static HWND Mk(HWND parent, const wchar_t* cls, const wchar_t* text, DWORD style, DWORD ex,
               int x, int y, int w, int h, int id, HFONT f) {
    HWND c = CreateWindowExW(ex, cls, text, style, S(x), S(y), S(w), S(h),
        parent, (HMENU)(INT_PTR)id, g_hInst, NULL);
    if (c && f) SendMessageW(c, WM_SETFONT, (WPARAM)f, 0);
    return c;
}
static HWND MkLabel(HWND p, int x, int y, int w, int h, HFONT f = NULL) {
    return Mk(p, WC_STATICW, L"", WS_CHILD | WS_VISIBLE | SS_LEFT, 0, x, y, w, h, 0, f ? f : g_hFont);
}
static HWND MkHeader(HWND p, int x, int y, int w, StrId t) {
    HWND h = MkLabel(p, x, y, w, 26, g_hFontBig);
    SetLabel(h, T(t), COL_ACCENT);
    return h;
}
static HWND MkButton(HWND p, int id, int x, int y, int w, int h, StrId t) {
    HWND b = Mk(p, WC_BUTTONW, T(t), WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0,
        x, y, w, h, id, g_hFont);
    return b;
}
static HWND MkCTA(HWND p, int id, int x, int y, int w, int h, StrId t) {
    HWND b = Mk(p, WC_BUTTONW, T(t), WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 0,
        x, y, w, h, id, g_hFontBig);
    return b;
}
static HWND MkCheck(HWND p, int id, int x, int y, int w, StrId t) {
    HWND b = Mk(p, WC_BUTTONW, T(t), WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 0,
        x, y, w, 26, id, g_hFont);
    SetWindowLongPtrW(b, GWLP_USERDATA, 1); // 1 = checkbox
    return b;
}
static bool IsChecked(HWND h) { return SendMessageW(h, BM_GETCHECK, 0, 0) == BST_CHECKED; }
static void SetChecked(HWND h, bool on) { SendMessageW(h, BM_SETCHECK, on ? BST_CHECKED : BST_UNCHECKED, 0); }
static HWND MkEdit(HWND p, int id, int x, int y, int w, int h, bool multi = false, bool ro = false) {
    DWORD st = WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | WS_BORDER;
    if (multi) st |= ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL | ES_WANTRETURN;
    if (ro) st |= ES_READONLY;
    HWND e = Mk(p, WC_EDITW, L"", st, WS_EX_CLIENTEDGE, x, y, w, h, id, g_hFont);
    if (e) SendMessageW(e, EM_SETLIMITTEXT, multi ? 100000 : 1024, 0);
    return e;
}
static HWND MkCombo(HWND p, int id, int x, int y, int w) {
    return Mk(p, WC_COMBOBOXW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
        0, x, y, w, 200, id, g_hFont);
}
static HWND MkList(HWND p, int id, int x, int y, int w, int h) {
    HWND l = Mk(p, WC_LISTVIEWW, L"", WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL |
        LVS_SHOWSELALWAYS | WS_BORDER | WS_TABSTOP, WS_EX_CLIENTEDGE, x, y, w, h, id, g_hFont);
    if (l) SendMessageW(l, LVM_SETEXTENDEDLISTVIEWSTYLE, 0,
        LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_GRIDLINES);
    return l;
}
static void LVCols(HWND l, const int* ids, const int* widths, int n) {
    for (int i = 0; i < n; i++) {
        LVCOLUMNW c; ZeroMemory(&c, sizeof(c));
        c.mask = LVCF_TEXT | LVCF_WIDTH;
        c.pszText = (LPWSTR)T((StrId)ids[i]);
        c.cx = S(widths[i]);
        ListView_InsertColumn(l, i, &c);
    }
}
static int LVAddRow(HWND l, int idx, const wchar_t* c0) {
    LVITEMW it; ZeroMemory(&it, sizeof(it));
    it.mask = LVIF_TEXT; it.iItem = idx; it.pszText = (LPWSTR)c0;
    return ListView_InsertItem(l, &it);
}
static void LVSet(HWND l, int row, int col, const std::wstring& s) {
    ListView_SetItemText(l, row, col, (LPWSTR)s.c_str());
}
static void LVSetData(HWND l, int row, LPARAM d) {
    LVITEMW it; ZeroMemory(&it, sizeof(it));
    it.mask = LVIF_PARAM; it.iItem = row; it.lParam = d;
    ListView_SetItem(l, &it);
}
static LPARAM LVGetData(HWND l, int row) {
    LVITEMW it; ZeroMemory(&it, sizeof(it));
    it.mask = LVIF_PARAM; it.iItem = row;
    ListView_GetItem(l, &it);
    return it.lParam;
}

// ---------- Page windows ----------
static LRESULT CALLBACK PageProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_ERASEBKGND: {
        HDC dc = (HDC)w;
        RECT r; GetClientRect(h, &r);
        FillRect(dc, &r, hBrPanel);
        return 1;
    }
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
    case WM_CTLCOLORBTN:
    case WM_COMMAND:
    case WM_NOTIFY:
    case WM_DRAWITEM:
        return SendMessageW(g_hMain, m, w, l);
    }
    return DefWindowProcW(h, m, w, l);
}
static const wchar_t* NavText(int page) {
    switch (page) {
    case PAGE_DASH: return T(SID_NAV_DASH);
    case PAGE_GAMES: return T(SID_NAV_GAMES);
    case PAGE_BOOST: return T(SID_NAV_BOOST);
    case PAGE_TWEAKS: return T(SID_NAV_TWEAKS);
    case PAGE_SYSTEM: return T(SID_NAV_SYSTEM);
    case PAGE_HELP: return T(SID_NAV_HELP);
    default: return T(SID_NAV_SETTINGS);
    }
}
static LRESULT CALLBACK SideProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        RECT r; GetClientRect(h, &r);
        HBRUSH bg = CreateSolidBrush(COL_SIDE);
        FillRect(dc, &r, bg);
        DeleteObject(bg);
        Graphics g(dc);
        g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
        if (g_imgLogo) {
            int sz = S(168);
            int x = (r.right - sz) / 2;
            g.DrawImage(g_imgLogo, x, S(16), sz, sz);
        }
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, COL_TEXT);
        HFONT old = (HFONT)SelectObject(dc, g_hFontBig);
        RECT tr = {0, S(192), r.right, S(220)};
        DrawTextW(dc, T(SID_APP_NAME), -1, &tr, DT_CENTER | DT_SINGLELINE);
        SelectObject(dc, g_hFont);
        SetTextColor(dc, COL_MUTED);
        RECT tr2 = {0, S(216), r.right, S(238)};
        DrawTextW(dc, T(SID_APP_TAG), -1, &tr2, DT_CENTER | DT_SINGLELINE);
        // divider
        HPEN pen = CreatePen(PS_SOLID, 1, RGB(40, 50, 80));
        HPEN op = (HPEN)SelectObject(dc, pen);
        MoveToEx(dc, S(20), S(250), NULL); LineTo(dc, r.right - S(20), S(250));
        SelectObject(dc, op); DeleteObject(pen);
        // version at bottom
        SetTextColor(dc, RGB(90, 100, 130));
        RECT vr = {0, r.bottom - S(34), r.right, r.bottom - S(10)};
        DrawTextW(dc, T(SID_VER), -1, &vr, DT_CENTER | DT_SINGLELINE);
        SelectObject(dc, old);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_COMMAND:
    case WM_NOTIFY:
    case WM_DRAWITEM:
        return SendMessageW(g_hMain, m, w, l);
    }
    return DefWindowProcW(h, m, w, l);
}

// ---------- Resource images ----------
static Bitmap* LoadPng(int id) {
    HRSRC r = FindResourceW(g_hInst, MAKEINTRESOURCEW(id), L"PNG");
    if (!r) return NULL;
    DWORD sz = SizeofResource(g_hInst, r);
    HGLOBAL hg = LoadResource(g_hInst, r);
    if (!hg || !sz) return NULL;
    const void* p = LockResource(hg);
    if (!p) return NULL;
    HGLOBAL hm = GlobalAlloc(GMEM_MOVEABLE, sz);
    if (!hm) return NULL;
    memcpy(GlobalLock(hm), p, sz);
    GlobalUnlock(hm);
    IStream* st = NULL;
    Bitmap* bmp = NULL;
    if (CreateStreamOnHGlobal(hm, TRUE, &st) == S_OK && st) {
        bmp = new Bitmap(st, FALSE);
        st->Release();
    } else GlobalFree(hm);
    return bmp;
}

// ---------- Page controls ----------
static HWND hDashVals[6], hDashNames[6];
static HWND hDashSys[6];
static HWND hDashInfo1, hDashInfo2, hDashTip;
static HWND hDashPic;
static HWND hGamesList, hGamesPrio, hGamesAff, hGamesGpu, hGamesFso, hGamesArgs,
            hGamesTimer, hGamesPower, hGamesKill, hGamesStatus, hGamesTip, hGamesLaunch;
static int g_gameSel = -1;
static bool g_gamesLoading = false;
static HWND hBoostLog, hBoostProg, hBoostStart, hBoostUndo;
static HWND hTweakList, hTweakDetail, hTweakApply, hTweakRevert, hTweakAll, hTweakUndoAll;
static HWND hSysCpu, hSysRam, hSysUp, hSysTimer, hSysGraph;
static HWND hHelpText;
static HWND hSetLang, hSetTray, hSetStartup;
static int g_cpuHist[90], g_ramHist[90], g_histN = 0;
static CpuMeter* g_meter = NULL;

// ---------- Dashboard ----------
static const char* kDashTweaks[] = {"gamemode", "hags", "powerplan", "gamedvr", "visualfx", "mouseaccel"};
static void DashRefresh() {
    int ap = 0, tt = 0;
    g_lastScore = Tweaks_Score(&ap, &tt);
    InvalidateRect(hDashPic, NULL, TRUE);
    SetLabel(hDashSys[0], SysCpuName(), COL_TEXT);
    SetLabel(hDashSys[1], SysGpuName(), COL_TEXT);
    SetLabel(hDashSys[2], WFormat(L"%s (%d GB)", SysRamString().c_str(), RamTotalMB() / 1024), COL_TEXT);
    SetLabel(hDashSys[3], SysOsString(), COL_TEXT);
    SetLabel(hDashSys[4], PowerGetActiveName(), COL_TEXT);
    SetLabel(hDashSys[5], SysDisplayString(), COL_TEXT);
    for (int i = 0; i < 6; i++) {
        const Tweak* t = TweakById(kDashTweaks[i]);
        std::wstring nm, st; COLORREF c = COL_MUTED;
        if (t) {
            TweakDisplayName(*t, nm);
            int s = TweakCheck(*t);
            if (s == 1) { st = T(SID_APPLIED); c = COL_GREEN; }
            else if (s == 0) { st = T(SID_NOT_APPLIED); c = COL_RED; }
            else { st = T(SID_NA); c = COL_YELLOW; }
        }
        SetLabel(hDashNames[i], nm, COL_TEXT);
        SetLabel(hDashVals[i], st, c);
    }
    wchar_t nb[64];
    StringCchPrintfW(nb, 64, L"%s: %d", T(SID_DASH_GAMES), (int)Games_All().size());
    SetLabel(hDashInfo1, nb, COL_MUTED);
    SetLabel(hDashInfo2, WFormat(L"%s: %s", T(SID_DASH_LASTBOOST),
        g_lastBoost.empty() ? T(SID_DASH_NEVER) : g_lastBoost.c_str()), COL_MUTED);
    SYSTEMTIME st; GetLocalTime(&st);
    StrId tips[] = {SID_TIP1, SID_TIP2, SID_TIP3, SID_TIP4, SID_TIP5};
    SetWindowTextW(hDashTip, WFormat(L"%s: %s", T(SID_DASH_TIP), T(tips[st.wDay % 5])).c_str());
}
static void BuildDash(HWND p) {
    hDashPic = Mk(p, WC_STATICW, L"", WS_CHILD | WS_VISIBLE | SS_OWNERDRAW, 0, 0, 0, 892, 150, IDC_DASH_PIC, NULL);
    MkHeader(p, 0, 158, 430, SID_DASH_SYSTEM);
    for (int i = 0; i < 6; i++) {
        HWND n = MkLabel(p, 0, 188 + i * 30, 150, 24);
        SetLabel(n, L"", COL_MUTED);
        static StrId ids[] = {SID_DASH_CPU, SID_DASH_GPU, SID_DASH_RAM, SID_DASH_OS, SID_DASH_POWER, SID_DASH_DISPLAY};
        SetWindowTextW(n, WFormat(L"%s:", T(ids[i])).c_str());
        hDashSys[i] = MkLabel(p, 150, 188 + i * 30, 290, 24);
    }
    MkHeader(p, 452, 158, 440, SID_DASH_STATUS);
    for (int i = 0; i < 6; i++) {
        hDashNames[i] = MkLabel(p, 452, 188 + i * 30, 300, 24);
        hDashVals[i] = MkLabel(p, 752, 188 + i * 30, 140, 24);
    }
    MkCTA(p, IDC_DASH_BOOST, 0, 380, 300, 56, SID_DASH_BOOST);
    hDashInfo1 = MkLabel(p, 320, 388, 250, 24, g_hFontBig);
    hDashInfo2 = MkLabel(p, 320, 416, 560, 24);
    hDashTip = MkEdit(p, 0, 0, 448, 892, 120, true, true);
}

// ---------- Games ----------
static void GamesFillList() {
    g_gamesLoading = true;
    Games_Lock();
    ListView_DeleteAllItems(hGamesList);
    const std::vector<GameProfile>& v = Games_All();
    for (size_t i = 0; i < v.size(); i++) {
        int r = LVAddRow(hGamesList, (int)i, v[i].name.c_str());
        LVSet(hGamesList, r, 1, v[i].path);
        wchar_t nb[32]; StringCchPrintfW(nb, 32, L"%d", v[i].playCount);
        LVSet(hGamesList, r, 2, nb);
        LVSet(hGamesList, r, 3, v[i].lastPlayed.empty() ? L"-" : v[i].lastPlayed);
    }
    if (g_gameSel >= (int)v.size()) g_gameSel = (int)v.size() - 1;
    if (g_gameSel >= 0) {
        ListView_SetItemState(hGamesList, g_gameSel, LVIS_SELECTED, LVIS_SELECTED);
        ListView_EnsureVisible(hGamesList, g_gameSel, FALSE);
    }
    Games_Unlock();
    g_gamesLoading = false;
    // load panel
    Games_Lock();
    if (g_gameSel >= 0 && g_gameSel < (int)v.size()) {
        GameProfile g = v[g_gameSel];
        Games_Unlock();
        g_gamesLoading = true;
        SendMessageW(hGamesPrio, CB_SETCURSEL, g.priority, 0);
        SendMessageW(hGamesAff, CB_SETCURSEL, g.affinity, 0);
        SetChecked(hGamesGpu, g.gpuPref == 1);
        SetChecked(hGamesFso, g.disableFSO);
        SetWindowTextW(hGamesArgs, g.args.c_str());
        SetChecked(hGamesTimer, g.timerBoost);
        SetChecked(hGamesPower, g.powerBoost);
        SetWindowTextW(hGamesKill, g.killList.c_str());
        std::wstring tip = Games_TipFor(g.path);
        if (!tip.empty()) SetLabel(hGamesTip, WFormat(L"%s %s", T(SID_G_TIP_TITLE), tip.c_str()), COL_YELLOW);
        else SetWindowTextW(hGamesTip, L"");
        g_gamesLoading = false;
        EnableWindow(hGamesLaunch, !Games_IsBusy());
    } else {
        Games_Unlock();
        SetWindowTextW(hGamesStatus, T(SID_G_IDLE));
        EnableWindow(hGamesLaunch, FALSE);
    }
}
static void GamesStorePanel() {
    if (g_gamesLoading) return;
    Games_Lock();
    if (g_gameSel < 0 || g_gameSel >= (int)Games_All().size()) { Games_Unlock(); return; }
    GameProfile& g = Games_All()[g_gameSel];
    g.priority = (int)SendMessageW(hGamesPrio, CB_GETCURSEL, 0, 0);
    g.affinity = (int)SendMessageW(hGamesAff, CB_GETCURSEL, 0, 0);
    g.gpuPref = IsChecked(hGamesGpu) ? 1 : 0;
    g.disableFSO = IsChecked(hGamesFso);
    wchar_t b[1024];
    GetWindowTextW(hGamesArgs, b, 1024); g.args = b;
    g.timerBoost = IsChecked(hGamesTimer);
    g.powerBoost = IsChecked(hGamesPower);
    GetWindowTextW(hGamesKill, b, 1024); g.killList = b;
    GameProfile snap = g;
    Games_Unlock();
    Games_Save();
    Games_ApplyPersistent(snap);
}
static void BuildGames(HWND p) {
    hGamesList = MkList(p, IDC_G_LIST, 0, 0, 892, 236);
    int ids[] = {SID_G_COL_NAME, SID_G_COL_PATH, SID_G_COL_PLAYS, SID_G_COL_LAST};
    int wd[] = {220, 430, 90, 130};
    LVCols(hGamesList, ids, wd, 4);
    MkButton(p, IDC_G_ADD, 0, 246, 130, 32, SID_G_ADD);
    MkButton(p, IDC_G_SCAN, 140, 246, 150, 32, SID_G_SCAN);
    MkButton(p, IDC_G_REMOVE, 300, 246, 120, 32, SID_G_REMOVE);
    MkHeader(p, 0, 288, 400, SID_G_SETTINGS);
    HWND l1 = MkLabel(p, 0, 318, 140, 24); SetWindowTextW(l1, T(SID_G_PRIORITY));
    hGamesPrio = MkCombo(p, IDC_G_PRIO, 140, 316, 220);
    SendMessageW(hGamesPrio, CB_ADDSTRING, 0, (LPARAM)T(SID_G_PRIO0));
    SendMessageW(hGamesPrio, CB_ADDSTRING, 0, (LPARAM)T(SID_G_PRIO1));
    SendMessageW(hGamesPrio, CB_ADDSTRING, 0, (LPARAM)T(SID_G_PRIO2));
    SendMessageW(hGamesPrio, CB_SETCURSEL, 2, 0);
    HWND l2 = MkLabel(p, 0, 350, 140, 24); SetWindowTextW(l2, T(SID_G_AFFINITY));
    hGamesAff = MkCombo(p, IDC_G_AFF, 140, 348, 220);
    SendMessageW(hGamesAff, CB_ADDSTRING, 0, (LPARAM)T(SID_G_AFF0));
    SendMessageW(hGamesAff, CB_ADDSTRING, 0, (LPARAM)T(SID_G_AFF1));
    SendMessageW(hGamesAff, CB_SETCURSEL, 0, 0);
    hGamesGpu = MkCheck(p, IDC_G_GPUPREF, 0, 382, 350, SID_G_GPUPREF);
    hGamesFso = MkCheck(p, IDC_G_FSO, 0, 410, 350, SID_G_FSO);
    hGamesTimer = MkCheck(p, IDC_G_TIMER, 0, 438, 350, SID_G_TIMER);
    hGamesPower = MkCheck(p, IDC_G_POWER, 0, 466, 380, SID_G_POWER);
    HWND l3 = MkLabel(p, 400, 316, 200, 24); SetWindowTextW(l3, T(SID_G_ARGS));
    hGamesArgs = MkEdit(p, IDC_G_ARGS, 400, 340, 492, 28);
    HWND l4 = MkLabel(p, 400, 376, 480, 24); SetWindowTextW(l4, T(SID_G_KILL));
    hGamesKill = MkEdit(p, IDC_G_KILL, 400, 400, 492, 28);
    HWND hint = MkLabel(p, 400, 430, 480, 24); SetLabel(hint, T(SID_G_KILL_HINT), COL_MUTED);
    hGamesLaunch = MkCTA(p, IDC_G_LAUNCH, 0, 506, 300, 54, SID_G_LAUNCH);
    hGamesStatus = MkLabel(p, 320, 512, 572, 26, g_hFontBig);
    hGamesTip = MkLabel(p, 320, 540, 572, 40);
}

// ---------- Boost ----------
static void BuildBoost(HWND p) {
    HWND d = MkLabel(p, 0, 0, 892, 40);
    SetLabel(d, T(SID_B_SUB), COL_MUTED);
    hBoostStart = MkCTA(p, IDC_B_START, 0, 44, 340, 56, SID_B_START);
    hBoostUndo = MkButton(p, IDC_B_UNDO, 360, 44, 220, 56, SID_B_UNDO);
    hBoostProg = Mk(p, PROGRESS_CLASSW, L"", WS_CHILD | WS_VISIBLE | PBS_SMOOTH, 0,
        0, 114, 892, 26, IDC_B_PROG, NULL);
    SendMessageW(hBoostProg, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
    hBoostLog = Mk(p, WC_LISTBOXW, L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL |
        WS_BORDER | WS_TABSTOP, WS_EX_CLIENTEDGE, 0, 152, 892, 440, IDC_B_LOG, g_hFont);
}
void UI_LogBoost(const wchar_t* line) {
    if (!hBoostLog) return;
    int i = (int)SendMessageW(hBoostLog, LB_ADDSTRING, 0, (LPARAM)line);
    SendMessageW(hBoostLog, LB_SETTOPINDEX, i, 0);
}
void UI_BoostProgress(int pct) {
    if (hBoostProg) SendMessageW(hBoostProg, PBM_SETPOS, pct, 0);
}
void UI_BoostDone(bool ok) {
    EnableWindow(hBoostStart, TRUE);
    EnableWindow(hBoostUndo, TRUE);
    wchar_t nb[128];
    StringCchPrintfW(nb, 128, L"%s", ok ? T(SID_B_DONE) : T(SID_B_FAIL));
    SetWindowTextW(hStatusBar, nb);
    UI_RefreshAll();
}

// ---------- Tweaks ----------
static void TweaksFillList() {
    ListView_DeleteAllItems(hTweakList);
    const std::vector<Tweak>& v = Tweaks_All();
    static StrId cats[] = {SID_T_CAT_GAMING, SID_T_CAT_PERF, SID_T_CAT_VISUAL, SID_T_CAT_NET,
                           SID_T_CAT_PRIV, SID_T_CAT_ADV, SID_T_CAT_ACT};
    for (size_t i = 0; i < v.size(); i++) {
        std::wstring nm; TweakDisplayName(v[i], nm);
        if (v[i].recommended) nm += WFormat(L"  [%s]", T(SID_RECOMMENDED));
        if (v[i].cat == TCAT_ADV) nm += WFormat(L"  [%s]", T(SID_ADVANCED));
        int r = LVAddRow(hTweakList, (int)i, nm.c_str());
        int s = TweakCheck(v[i]);
        LVSet(hTweakList, r, 1, v[i].isAction ? T(SID_ACTION_NEEDED) :
            (s == 1 ? T(SID_APPLIED) : (s == 0 ? T(SID_NOT_APPLIED) : T(SID_NA))));
        LVSet(hTweakList, r, 2, T(cats[v[i].cat]));
        LVSetData(hTweakList, r, (LPARAM)i);
    }
}
static void TweaksShowDetail(int row) {
    if (row < 0) { SetWindowTextW(hTweakDetail, T(SID_T_DETAIL)); return; }
    size_t i = (size_t)LVGetData(hTweakList, row);
    const std::vector<Tweak>& v = Tweaks_All();
    if (i >= v.size()) return;
    std::wstring nm, ds;
    TweakDisplayName(v[i], nm); TweakDisplayDesc(v[i], ds);
    std::wstring t = nm + L"\r\n" + ds;
    if (v[i].needsReboot) t += WFormat(L"\r\n[%s]", T(SID_NEEDS_REBOOT));
    if (v[i].cat == TCAT_ADV) t += WFormat(L"\r\n%s", T(SID_T_WARN));
    SetWindowTextW(hTweakDetail, t.c_str());
}
static void BuildTweaks(HWND p) {
    hTweakList = MkList(p, IDC_T_LIST, 0, 0, 892, 372);
    int ids[] = {SID_T_COL_NAME, SID_T_COL_STATUS, SID_T_COL_CAT};
    int wd[] = {480, 180, 200};
    LVCols(hTweakList, ids, wd, 3);
    hTweakDetail = MkEdit(p, IDC_T_DETAIL, 0, 382, 892, 110, true, true);
    hTweakApply = MkButton(p, IDC_T_APPLY, 0, 502, 160, 34, SID_T_APPLY);
    hTweakRevert = MkButton(p, IDC_T_REVERT, 170, 502, 160, 34, SID_T_REVERT);
    hTweakAll = MkButton(p, IDC_T_ALL, 340, 502, 230, 34, SID_T_ALL);
    hTweakUndoAll = MkButton(p, IDC_T_UNDOALL, 580, 502, 160, 34, SID_T_UNDO_ALL);
}

// ---------- System ----------
static void SysRefresh() {
    if (!g_meter) g_meter = new CpuMeter();
    int cpu = g_meter->sample();
    int ram = RamUsagePercent();
    if (g_histN < 90) { g_cpuHist[g_histN] = cpu; g_ramHist[g_histN] = ram; g_histN++; }
    else {
        memmove(g_cpuHist, g_cpuHist + 1, sizeof(int) * 89);
        memmove(g_ramHist, g_ramHist + 1, sizeof(int) * 89);
        g_cpuHist[89] = cpu; g_ramHist[89] = ram;
    }
    SetLabel(hSysCpu, WFormat(L"%s: %d%%", T(SID_S_CPU), cpu), cpu > 85 ? COL_RED : COL_TEXT);
    SetLabel(hSysRam, WFormat(L"%s: %d%%  (%d MB %s)", T(SID_S_RAM), ram, RamAvailMB(),
        Strings_GetLang() == 1 ? L"آزاد" : L"free"), ram > 90 ? COL_RED : COL_TEXT);
    SetLabel(hSysUp, WFormat(L"%s: %s", T(SID_S_UPTIME), SysUptimeString().c_str()), COL_TEXT);
    ULONG cur = 0, mn = 0, mx = 0;
    if (TimerQuery(cur, mn, mx))
        SetLabel(hSysTimer, WFormat(L"%s: %.1f ms", T(SID_S_TIMER), cur / 10000.0), COL_TEXT);
    if (hSysGraph) InvalidateRect(hSysGraph, NULL, TRUE);
}
static void BuildSystem(HWND p) {
    hSysCpu = MkLabel(p, 0, 0, 440, 30, g_hFontBig);
    hSysRam = MkLabel(p, 0, 34, 440, 30, g_hFontBig);
    hSysUp = MkLabel(p, 452, 0, 440, 30);
    hSysTimer = MkLabel(p, 452, 34, 440, 30);
    hSysGraph = Mk(p, WC_STATICW, L"", WS_CHILD | WS_VISIBLE | SS_OWNERDRAW | WS_BORDER, 0,
        0, 74, 892, 330, IDC_S_GRAPH, NULL);
    MkButton(p, IDC_S_CLEANRAM, 0, 416, 220, 38, SID_S_CLEAN_RAM);
    MkButton(p, IDC_S_CLEANTEMP, 232, 416, 220, 38, SID_S_CLEAN_TEMP);
    MkButton(p, IDC_S_REFRESH, 464, 416, 240, 38, SID_S_MAXREFRESH);
}

// ---------- Help ----------
static const char* kHelpEN =
"FPS BOOSTER PRO - USER GUIDE\r\n"
"================================\r\n\r\n"
"1) QUICK START\r\n"
"- Open One-Click Boost and press START ULTIMATE BOOST.\r\n"
"- It creates a restore point, backs up your settings, then applies\r\n"
"  every recommended optimization: Game Mode, Game DVR off, Ultimate\r\n"
"  Performance power plan, no core parking, HAGS, visual effects,\r\n"
"  network tuning, junk cleanup, memory free and max refresh rate.\r\n"
"- Everything is reversible: press Undo everything.\r\n\r\n"
"2) MY GAMES (MOST FPS GAIN)\r\n"
"- Add your games manually or with Auto-scan (Steam + Epic).\r\n"
"- Select a game, set CPU priority High, then BOOST & LAUNCH.\r\n"
"- While you play: high-priority process, 0.5ms timer, free RAM,\r\n"
"  Ultimate Performance plan, auto-closed background apps.\r\n"
"- When you quit, the system is restored automatically.\r\n\r\n"
"3) NVIDIA / AMD PANEL (do once per game)\r\n"
"- NVIDIA: Power Management = Prefer Maximum Performance,\r\n"
"  Texture Filtering Quality = High Performance, Low Latency = On,\r\n"
"  Max Frame Rate = your monitor Hz, Shader Cache = 10GB.\r\n"
"- AMD: Graphics Profile = eSports, Anti-Lag On, Chill Off,\r\n"
"  Texture Filtering = Performance, Tessellation = Override 8x/16x.\r\n\r\n"
"4) IN-GAME SETTINGS\r\n"
"- Use exclusive Fullscreen (not borderless) for lowest latency.\r\n"
"- Cap FPS to monitor Hz if you get stutter; enable VSync only if tearing.\r\n"
"- Lower shadows, volumetrics and anti-aliasing first - biggest FPS cost.\r\n\r\n"
"5) FAQ\r\n"
"- Is it safe? Yes: restore point + full backup before changes.\r\n"
"- SmartScreen warning? Normal for new unsigned apps - click More info / Run.\r\n"
"- Admin rights? Required for system-level optimizations.\r\n"
"- After big Windows updates, run the boost again.\r\n";
static const char* kHelpFA =
"راهنمای FPS BOOSTER PRO\r\n"
"================================\r\n\r\n"
"۱) شروع سریع\r\n"
"- وارد «بوست تک‌کلیکی» شوید و دکمه شروع بوست نهایی را بزنید.\r\n"
"- برنامه نقطه بازیابی می‌سازد، از تنظیمات بکاپ می‌گیرد و همه\r\n"
"  بهینه‌سازی‌ها را اعمال می‌کند: حالت بازی، خاموشی ضبط بازی،\r\n"
"  پلن Ultimate Performance، بیداری هسته‌ها، HAGS، جلوه‌های بصری،\r\n"
"  تنظیم شبکه، پاک‌سازی فایل‌ها، آزادسازی رم و حداکثر هرتز.\r\n"
"- همه‌چیز قابل بازگشت است: دکمه «بازگردانی همه».\r\n\r\n"
"۲) بازی‌های من (بیشترین افزایش اف‌پی‌اس)\r\n"
"- بازی‌ها را دستی اضافه کنید یا با اسکن خودکار (استیم و اپیک).\r\n"
"- بازی را انتخاب کنید، اولویت را High بگذارید و «بوست و اجرا» بزنید.\r\n"
"- هنگام بازی: اولویت بالای پردازش، تایمر 0.5 میلی‌ثانیه، رم آزاد،\r\n"
"  پلن Ultimate Performance و بستن خودکار برنامه‌های اضافی.\r\n"
"- با خروج از بازی، سیستم خودکار به حالت عادی برمی‌گردد.\r\n\r\n"
"۳) پنل انویدیا / ای‌ام‌دی (یک‌بار برای هر بازی)\r\n"
"- انویدیا: Power Management روی Prefer Maximum Performance،\r\n"
"  کیفیت فیلتر بافت روی High Performance، Low Latency روشن،\r\n"
"  سقف فریم برابر هرتز مانیتور، کش شیدر ۱۰ گیگ.\r\n"
"- ای‌ام‌دی: پروفایل eSports، آنتی‌لگ روشن، چیل خاموش،\r\n"
"  فیلتر بافت Performance، تسلیشن 8x.\r\n\r\n"
"۴) تنظیمات داخل بازی\r\n"
"- از Fullscreen اختصاصی استفاده کنید نه Borderless.\r\n"
"- اگر لگ دارید سقف فریم را برابر هرتز مانیتور بگذارید.\r\n"
"- اول سایه‌ها، افکت‌های حجمی و آنتی‌الیزینگ را کم کنید.\r\n\r\n"
"۵) سؤالات رایج\r\n"
"- امن است؟ بله: قبل از تغییر، نقطه بازیابی و بکاپ کامل گرفته می‌شود.\r\n"
"- هشدار SmartScreen؟ برای برنامه‌های جدید طبیعی است.\r\n"
"- دسترسی مدیر؟ برای بهینه‌سازی‌های سیستمی لازم است.\r\n"
"- بعد از آپدیت بزرگ ویندوز، بوست را دوباره اجرا کنید.\r\n";
static void BuildHelp(HWND p) {
    hHelpText = MkEdit(p, IDC_H_TEXT, 0, 0, 892, 590, true, true);
    SetWindowTextW(hHelpText, Utf8ToWide(Strings_GetLang() == 1 ? kHelpFA : kHelpEN).c_str());
}

// ---------- Settings ----------
static void UI_SetLanguage(int lang) {
    if (lang != 0 && lang != 1) return;
    if (lang == Strings_GetLang()) return;
    Strings_SetLang(lang);
    Settings_Save();
    SetStartupRun(IsChecked(hSetStartup));
    MessageBoxW(g_hMain, T(SID_SET_RESTART_LANG), T(SID_APP_NAME), MB_OK | MB_ICONINFORMATION);
}
static void BuildSettings(HWND p) {
    HWND l1 = MkLabel(p, 0, 8, 200, 26); SetWindowTextW(l1, T(SID_SET_LANG));
    hSetLang = MkCombo(p, IDC_SET_LANG, 210, 6, 240);
    SendMessageW(hSetLang, CB_ADDSTRING, 0, (LPARAM)T(SID_SET_LANG_EN));
    SendMessageW(hSetLang, CB_ADDSTRING, 0, (LPARAM)T(SID_SET_LANG_FA));
    SendMessageW(hSetLang, CB_SETCURSEL, Strings_GetLang(), 0);
    hSetTray = MkCheck(p, IDC_SET_TRAY, 0, 52, 420, SID_SET_TRAY);
    SetChecked(hSetTray, g_closeToTray);
    hSetStartup = MkCheck(p, IDC_SET_STARTUP, 0, 84, 420, SID_SET_STARTUP);
    SetChecked(hSetStartup, RegValueExists(HKEY_CURRENT_USER,
        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run", L"FPSBoosterPro"));
    MkButton(p, IDC_SET_FOLDER, 0, 130, 240, 34, SID_SET_FOLDER);
    MkButton(p, IDC_SET_RESET, 0, 174, 240, 34, SID_SET_RESET);
    HWND ab = MkLabel(p, 0, 230, 892, 30);
    SetLabel(ab, T(SID_SET_ABOUT), COL_MUTED);
}

// ---------- Page management ----------
static const int kPageOfNav[PAGE_COUNT] = {0, 1, 2, 3, 4, 5, 6};
void UI_ShowPage(int page) {
    if (page < 0 || page >= PAGE_COUNT) return;
    g_page = page;
    for (int i = 0; i < PAGE_COUNT; i++)
        ShowWindow(g_hPages[i], i == page ? SW_SHOW : SW_HIDE);
    static StrId titles[] = {SID_NAV_DASH, SID_NAV_GAMES, SID_NAV_BOOST, SID_NAV_TWEAKS,
                             SID_NAV_SYSTEM, SID_NAV_HELP, SID_NAV_SETTINGS};
    static StrId subs[] = {SID_DASH_SUB, SID_G_SUB, SID_B_SUB, SID_T_SUB, SID_S_SUB, SID_H_SUB, SID_SET_SUB};
    SetWindowTextW(hTitle, WFormat(L"%s   -   %s", T(titles[page]), T(subs[page])).c_str());
    InvalidateRect(hSide, NULL, TRUE);
    for (int i = 0; i < PAGE_COUNT; i++) {
        HWND b = GetDlgItem(hSide, IDC_NAV_BASE + i);
        if (b) InvalidateRect(b, NULL, TRUE);
    }
    switch (page) {
    case PAGE_DASH: DashRefresh(); break;
    case PAGE_GAMES: GamesFillList(); break;
    case PAGE_TWEAKS: TweaksFillList(); TweaksShowDetail(-1); break;
    case PAGE_SYSTEM: SysRefresh(); break;
    }
}
void UI_RefreshAll() {
    DashRefresh();
    if (g_page == PAGE_GAMES) GamesFillList();
    if (g_page == PAGE_TWEAKS) { TweaksFillList(); TweaksShowDetail(-1); }
}
void UI_ApplyLanguage() { UI_ShowPage(g_page); }

// ---------- Tray ----------
static NOTIFYICONDATAW g_nid;
void UI_TrayInit() {
    ZeroMemory(&g_nid, sizeof(g_nid));
    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = g_hMain;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    g_nid.uCallbackMessage = WM_APP_TRAY;
    g_nid.hIcon = LoadIconW(g_hInst, MAKEINTRESOURCEW(RES_ICON_APP));
    StringCchCopyW(g_nid.szTip, 128, T(SID_TRAY_TIP));
    Shell_NotifyIconW(NIM_ADD, &g_nid);
    g_nid.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &g_nid);
}
void UI_TrayRemove() { Shell_NotifyIconW(NIM_DELETE, &g_nid); }
void UI_TrayShow(bool show) {
    if (show) { ShowWindow(g_hMain, SW_RESTORE); SetForegroundWindow(g_hMain); }
    else ShowWindow(g_hMain, SW_HIDE);
}

// ---------- Game events ----------
void UI_GameEvent(int phase, const wchar_t* info) {
    switch (phase) {
    case GPH_PREP:
        SetLabel(hGamesStatus, T(SID_G_PREP), COL_YELLOW);
        EnableWindow(hGamesLaunch, FALSE);
        break;
    case GPH_LAUNCHED:
    case GPH_INGAME:
        SetLabel(hGamesStatus, T(SID_G_RUNNING), COL_GREEN);
        GamesFillList();
        break;
    case GPH_DONE:
        SetLabel(hGamesStatus, T(SID_G_DONE), COL_TEXT);
        EnableWindow(hGamesLaunch, TRUE);
        GamesFillList();
        break;
    case GPH_ERROR:
        SetLabel(hGamesStatus, T(SID_MSG_FAIL), COL_RED);
        EnableWindow(hGamesLaunch, TRUE);
        break;
    }
}

// ---------- Owner drawing ----------
static void DrawCTA(const DRAWITEMSTRUCT* d) {
    HDC dc = d->hDC;
    RECT r = d->rcItem;
    bool pressed = (d->itemState & ODS_SELECTED) != 0;
    bool disabled = (d->itemState & ODS_DISABLED) != 0;
    Graphics g(dc);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    Color c1 = disabled ? Color(90, 100, 120) : (pressed ? Color(14, 150, 180) : Color(34, 211, 238));
    Color c2 = disabled ? Color(70, 80, 100) : (pressed ? Color(160, 60, 190) : Color(232, 121, 249));
    LinearGradientBrush br(Point(r.left, r.top), Point(r.right, r.bottom), c1, c2);
    GraphicsPath path;
    int rad = 10;
    path.AddArc(r.left, r.top, rad * 2, rad * 2, 180, 90);
    path.AddArc(r.right - rad * 2, r.top, rad * 2, rad * 2, 270, 90);
    path.AddArc(r.right - rad * 2, r.bottom - rad * 2, rad * 2, rad * 2, 0, 90);
    path.AddArc(r.left, r.bottom - rad * 2, rad * 2, rad * 2, 90, 90);
    path.CloseFigure();
    // clip background
    HRGN bg = CreateRectRgn(r.left, r.top, r.right, r.bottom);
    HBRUSH pb = CreateSolidBrush(COL_PANEL);
    FillRgn(dc, bg, pb);
    DeleteObject(pb); DeleteObject(bg);
    g.FillPath(&br, &path);
    // text
    wchar_t txt[128];
    GetWindowTextW(d->hwndItem, txt, 128);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, disabled ? RGB(200, 200, 200) : RGB(10, 12, 20));
    HFONT old = (HFONT)SelectObject(dc, g_hFontBig);
    DrawTextW(dc, txt, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(dc, old);
    if ((d->itemState & ODS_FOCUS) && !disabled) DrawFocusRect(dc, &r);
}
static void DrawNav(const DRAWITEMSTRUCT* d) {
    HDC dc = d->hDC;
    RECT r = d->rcItem;
    int id = (int)d->CtlID - IDC_NAV_BASE;
    bool sel = (id == g_page);
    HBRUSH bg = CreateSolidBrush(sel ? COL_SELBAR : COL_SIDE);
    FillRect(dc, &r, bg);
    DeleteObject(bg);
    if (sel) {
        RECT bar = {r.left, r.top, r.left + S(4), r.bottom};
        HBRUSH ab = CreateSolidBrush(COL_ACCENT);
        FillRect(dc, &bar, ab);
        DeleteObject(ab);
    }
    // dot
    RECT dot = {r.left + S(18), (r.top + r.bottom - S(8)) / 2, r.left + S(26), (r.top + r.bottom + S(8)) / 2};
    HBRUSH db = CreateSolidBrush(sel ? COL_ACCENT : RGB(80, 90, 120));
    HBRUSH ob = (HBRUSH)SelectObject(dc, db);
    HPEN op = (HPEN)SelectObject(dc, GetStockObject(NULL_PEN));
    Ellipse(dc, dot.left, dot.top, dot.right, dot.bottom);
    SelectObject(dc, op);
    SelectObject(dc, ob);
    DeleteObject(db);
    RECT tr = {r.left + S(36), r.top, r.right - S(8), r.bottom};
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, sel ? COL_TEXT : COL_MUTED);
    HFONT old = (HFONT)SelectObject(dc, g_hFont);
    DrawTextW(dc, NavText(id), -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    SelectObject(dc, old);
}
static void DrawCheckBtn(const DRAWITEMSTRUCT* d) {
    HDC dc = d->hDC;
    RECT r = d->rcItem;
    HBRUSH bg = CreateSolidBrush(COL_PANEL);
    FillRect(dc, &r, bg);
    DeleteObject(bg);
    bool checked = SendMessageW(d->hwndItem, BM_GETCHECK, 0, 0) == BST_CHECKED;
    int bs = S(18);
    RECT box = {r.left, (r.top + r.bottom - bs) / 2, r.left + bs, (r.top + r.bottom + bs) / 2};
    UINT st = DFCS_BUTTONCHECK | (checked ? DFCS_CHECKED : 0);
    if (d->itemState & ODS_DISABLED) st |= DFCS_INACTIVE;
    DrawFrameControl(dc, &box, DFC_BUTTON, st);
    wchar_t txt[256];
    GetWindowTextW(d->hwndItem, txt, 256);
    RECT tr = {box.right + S(8), r.top, r.right, r.bottom};
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, (d->itemState & ODS_DISABLED) ? COL_MUTED : COL_TEXT);
    HFONT old = (HFONT)SelectObject(dc, g_hFont);
    DrawTextW(dc, txt, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    SelectObject(dc, old);
}
static void DrawGraph(const DRAWITEMSTRUCT* d) {
    HDC dc = d->hDC;
    RECT r = d->rcItem;
    HBRUSH bg = CreateSolidBrush(COL_CARD);
    FillRect(dc, &r, bg);
    DeleteObject(bg);
    Graphics g(dc);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    int W = r.right - r.left, H = r.bottom - r.top;
    Pen grid(Color(60, 50, 70, 110), 1);
    for (int i = 1; i < 4; i++) {
        int y = r.top + H * i / 4;
        g.DrawLine(&grid, r.left, y, r.right, y);
    }
    if (g_histN > 1) {
        Pen pCpu(Color(255, 34, 211, 238), 2), pRam(Color(255, 232, 121, 249), 2);
        for (int s = 0; s < 2; s++) {
            const int* h = s ? g_ramHist : g_cpuHist;
            Point* pts = new Point[g_histN];
            for (int i = 0; i < g_histN; i++) {
                int x = r.left + (W - 8) * i / 89;
                int y = r.bottom - 6 - (H - 12) * h[i] / 100;
                pts[i] = Point(x, y);
            }
            // shift: show last N aligned right
            int off = r.right - 4 - (r.left + (W - 8) * (g_histN - 1) / 89);
            for (int i = 0; i < g_histN; i++) pts[i].X += off;
            g.DrawLines(s ? &pRam : &pCpu, pts, g_histN);
            delete[] pts;
        }
    }
    SetBkMode(dc, TRANSPARENT);
    HFONT old = (HFONT)SelectObject(dc, g_hFont);
    SetTextColor(dc, COL_ACCENT);
    RECT l1 = {r.left + 8, r.top + 6, r.left + 200, r.top + 28};
    DrawTextW(dc, WFormat(L"CPU %d%%", g_histN ? g_cpuHist[g_histN-1] : 0).c_str(), -1, &l1, DT_LEFT);
    SetTextColor(dc, COL_ACCENT2);
    RECT l2 = {r.left + 8, r.top + 28, r.left + 200, r.top + 50};
    DrawTextW(dc, WFormat(L"RAM %d%%", g_histN ? g_ramHist[g_histN-1] : 0).c_str(), -1, &l2, DT_LEFT);
    SelectObject(dc, old);
}
static void DrawDashPic(const DRAWITEMSTRUCT* d) {
    HDC dc = d->hDC;
    RECT r = d->rcItem;
    Graphics g(dc);
    g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
    if (g_imgBanner) g.DrawImage(g_imgBanner, (INT)r.left, (INT)r.top, (INT)(r.right - r.left), (INT)(r.bottom - r.top));
    else {
        HBRUSH b = CreateSolidBrush(COL_CARD);
        FillRect(dc, &r, b); DeleteObject(b);
    }
    // score text overlay
    SetBkMode(dc, TRANSPARENT);
    wchar_t sc[16]; StringCchPrintfW(sc, 16, L"%d", g_lastScore);
    const wchar_t* grade = L"D";
    COLORREF gc = COL_RED;
    if (g_lastScore >= 90) { grade = L"S"; gc = COL_ACCENT; }
    else if (g_lastScore >= 75) { grade = L"A"; gc = COL_GREEN; }
    else if (g_lastScore >= 60) { grade = L"B"; gc = COL_GREEN; }
    else if (g_lastScore >= 40) { grade = L"C"; gc = COL_YELLOW; }
    SetTextColor(dc, RGB(255, 255, 255));
    HFONT old = (HFONT)SelectObject(dc, g_hFontHuge);
    RECT sr = {r.left + S(24), r.top + S(10), r.left + S(200), r.bottom};
    DrawTextW(dc, sc, -1, &sr, DT_LEFT | DT_TOP | DT_SINGLELINE);
    SelectObject(dc, g_hFontBig);
    RECT lr = {r.left + S(24), r.top + S(86), r.left + S(400), r.bottom};
    DrawTextW(dc, WFormat(L"/ 100   %s", T(SID_DASH_SCORE)).c_str(), -1, &lr, DT_LEFT | DT_TOP | DT_SINGLELINE);
    SetTextColor(dc, gc);
    SelectObject(dc, g_hFontHuge);
    RECT gr = {r.right - S(160), r.top + S(10), r.right - S(24), r.bottom};
    DrawTextW(dc, WFormat(L"%s: %s", T(SID_DASH_GRADE), grade).c_str(), -1, &gr, DT_RIGHT | DT_TOP | DT_SINGLELINE);
    SelectObject(dc, old);
}

// ---------- Main window ----------
static void DoScan(HWND list) {
    HCURSOR old = SetCursor(LoadCursorW(NULL, IDC_WAIT));
    SetWindowTextW(hStatusBar, T(SID_G_SCAN));
    std::vector<GameCandidate> out;
    Games_Scan(out);
    int added = 0;
    for (size_t i = 0; i < out.size(); i++)
        if (Games_Add(out[i].exe, out[i].name)) added++;
    GamesFillList();
    SetWindowTextW(hStatusBar, added > 0 ?
        WFormat(L"%s: %d", T(SID_G_SCAN_DONE), added).c_str() : T(SID_G_NO_NEW));
    SetCursor(old);
}
static void DoAddGame() {
    wchar_t file[MAX_PATH * 2]; file[0] = 0;
    OPENFILENAMEW of; ZeroMemory(&of, sizeof(of));
    of.lStructSize = sizeof(of);
    of.hwndOwner = g_hMain;
    std::wstring filt = T(SID_G_EXE_FILTER);
    filt.push_back(0); filt += L"*.exe"; filt.push_back(0); filt.push_back(0);
    std::vector<wchar_t> fb(filt.begin(), filt.end());
    of.lpstrFilter = fb.data();
    of.lpstrFile = file;
    of.nMaxFile = MAX_PATH * 2;
    of.lpstrTitle = T(SID_G_PICK_EXE);
    of.Flags = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
    if (GetOpenFileNameW(&of)) {
        if (Games_Add(file)) {
            g_gameSel = (int)Games_All().size() - 1;
            GamesFillList();
            SetWindowTextW(hStatusBar, T(SID_G_ADDED));
        }
    }
}
static void SetLangAndAsk(int lang) {
    if (lang == Strings_GetLang()) return;
    Strings_SetLang(lang);
    Settings_Save();
    MessageBoxW(g_hMain, T(SID_SET_RESTART_LANG), T(SID_APP_NAME), MB_OK | MB_ICONINFORMATION);
}

LRESULT CALLBACK UI_MainProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_CTLCOLORSTATIC: {
        HDC dc = (HDC)w;
        HWND c = (HWND)l;
        SetBkMode(dc, TRANSPARENT);
        COLORREF col = COL_TEXT;
        for (size_t i = 0; i < g_colors.size(); i++)
            if (g_colors[i].h == c) { col = g_colors[i].c; break; }
        SetTextColor(dc, col);
        return (LRESULT)hBrPanel;
    }
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX: {
        HDC dc = (HDC)w;
        SetBkMode(dc, OPAQUE);
        SetTextColor(dc, COL_TEXT);
        SetBkColor(dc, RGB(10, 14, 26));
        return (LRESULT)hBrEdit;
    }
    case WM_DRAWITEM: {
        const DRAWITEMSTRUCT* d = (const DRAWITEMSTRUCT*)l;
        if (!d) break;
        int id = (int)d->CtlID;
        if (id >= IDC_NAV_BASE && id < IDC_NAV_BASE + PAGE_COUNT) { DrawNav(d); return TRUE; }
        if (GetWindowLongPtrW(d->hwndItem, GWLP_USERDATA) == 1) { DrawCheckBtn(d); return TRUE; }
        if (id == IDC_S_GRAPH) { DrawGraph(d); return TRUE; }
        if (id == IDC_DASH_PIC) { DrawDashPic(d); return TRUE; }
        DrawCTA(d);
        return TRUE;
    }
    case WM_COMMAND: {
        int id = LOWORD(w);
        int code = HIWORD(w);
        if (id >= IDC_NAV_BASE && id < IDC_NAV_BASE + PAGE_COUNT) { UI_ShowPage(id - IDC_NAV_BASE); break; }
        switch (id) {
        case IDC_LANG_BTN:
            SetLangAndAsk(Strings_GetLang() == 1 ? 0 : 1);
            break;
        case IDC_DASH_BOOST:
            UI_ShowPage(PAGE_BOOST);
            break;
        case IDC_G_ADD: if (!Games_IsBusy() && !g_boosting) DoAddGame(); break;
        case IDC_G_SCAN: if (!Games_IsBusy() && !g_boosting) DoScan(hGamesList); break;
        case IDC_G_REMOVE:
            if (g_gameSel >= 0 && !Games_IsBusy() && !g_boosting) { Games_Remove(g_gameSel); g_gameSel = -1; GamesFillList(); }
            break;
        case IDC_G_PRIO:
        case IDC_G_AFF:
            if (code == CBN_SELCHANGE) GamesStorePanel();
            break;
        case IDC_G_ARGS:
        case IDC_G_KILL:
            if (code == EN_CHANGE) GamesStorePanel();
            break;
        case IDC_G_GPUPREF: case IDC_G_FSO: case IDC_G_TIMER: case IDC_G_POWER:
            if (code == BN_CLICKED) {
                HWND b = (HWND)l;
                SetChecked(b, !IsChecked(b)); // manual toggle (owner-draw)
                InvalidateRect(b, NULL, TRUE);
                GamesStorePanel();
            }
            break;
        case IDC_G_LAUNCH:
            if (g_gameSel >= 0 && !g_boosting) Games_BoostLaunch(g_gameSel, g_hMain);
            break;
        case IDC_B_START:
            if (!g_boosting && !g_inGame) {
                g_boosting = true;
                EnableWindow(hBoostStart, FALSE);
                EnableWindow(hBoostUndo, FALSE);
                SendMessageW(hBoostLog, LB_RESETCONTENT, 0, 0);
                SetWindowTextW(hStatusBar, T(SID_B_WORKING));
                BoostRun(g_hMain);
            }
            break;
        case IDC_B_UNDO:
            if (!g_boosting && !g_inGame) {
                if (MessageBoxW(h, T(SID_B_CONFIRM_UNDO), T(SID_APP_NAME), MB_YESNO | MB_ICONQUESTION) == IDYES) {
                    g_boosting = true;
                    EnableWindow(hBoostStart, FALSE);
                    EnableWindow(hBoostUndo, FALSE);
                    SendMessageW(hBoostLog, LB_RESETCONTENT, 0, 0);
                    BoostUndo(g_hMain);
                }
            }
            break;
        case IDC_T_APPLY:
        case IDC_T_REVERT: {
            if (g_boosting) break;
            int sel = ListView_GetNextItem(hTweakList, -1, LVNI_SELECTED);
            if (sel < 0) break;
            size_t i = (size_t)LVGetData(hTweakList, sel);
            const std::vector<Tweak>& v = Tweaks_All();
            if (i >= v.size()) break;
            if (id == IDC_T_APPLY) TweakApply(v[i]); else TweakRevert(v[i]);
            TweaksFillList();
            ListView_SetItemState(hTweakList, sel, LVIS_SELECTED, LVIS_SELECTED);
            TweaksShowDetail(sel);
            DashRefresh();
            break;
        }
        case IDC_T_ALL: {
            if (g_boosting) break;
            const std::vector<Tweak>& v = Tweaks_All();
            HCURSOR old = SetCursor(LoadCursorW(NULL, IDC_WAIT));
            BackupInit();
            for (size_t i = 0; i < v.size(); i++)
                if (v[i].recommended && !v[i].isAction && TweakCheck(v[i]) != 1) TweakApply(v[i]);
            BackupSave();
            SetCursor(old);
            TweaksFillList(); DashRefresh();
            SetWindowTextW(hStatusBar, T(SID_MSG_DONE));
            break;
        }
        case IDC_T_UNDOALL: {
            if (g_boosting) break;
            if (MessageBoxW(h, T(SID_B_CONFIRM_UNDO), T(SID_APP_NAME), MB_YESNO | MB_ICONQUESTION) != IDYES) break;
            const std::vector<Tweak>& v = Tweaks_All();
            HCURSOR old = SetCursor(LoadCursorW(NULL, IDC_WAIT));
            for (int i = (int)v.size() - 1; i >= 0; i--) TweakRevert(v[i]);
            SetCursor(old);
            TweaksFillList(); DashRefresh();
            SetWindowTextW(hStatusBar, T(SID_MSG_DONE));
            break;
        }
        case IDC_S_CLEANRAM:
            PurgeStandbyList();
            SysRefresh();
            SetWindowTextW(hStatusBar, T(SID_MSG_DONE));
            break;
        case IDC_S_CLEANTEMP: {
            HCURSOR old = SetCursor(LoadCursorW(NULL, IDC_WAIT));
            unsigned long long f = CleanJunkFiles();
            SetCursor(old);
            SetWindowTextW(hStatusBar, WFormat(L"%s: %.1f MB", T(SID_S_FREED), f / (1024.0*1024.0)).c_str());
            break;
        }
        case IDC_S_REFRESH: {
            int hz = SetMaxRefreshRate();
            SetWindowTextW(hStatusBar, hz > 0 ?
                WFormat(L"%s %dHz", T(SID_S_REFRESH_DONE), hz).c_str() : T(SID_S_REFRESH_NONE));
            break;
        }
        case IDC_SET_LANG:
            if (code == CBN_SELCHANGE)
                SetLangAndAsk((int)SendMessageW(hSetLang, CB_GETCURSEL, 0, 0));
            break;
        case IDC_SET_TRAY:
            if (code == BN_CLICKED) {
                SetChecked(hSetTray, !IsChecked(hSetTray));
                InvalidateRect(hSetTray, NULL, TRUE);
                g_closeToTray = IsChecked(hSetTray);
                Settings_Save();
            }
            break;
        case IDC_SET_STARTUP:
            if (code == BN_CLICKED) {
                SetChecked(hSetStartup, !IsChecked(hSetStartup));
                InvalidateRect(hSetStartup, NULL, TRUE);
                SetStartupRun(IsChecked(hSetStartup));
            }
            break;
        case IDC_SET_FOLDER:
            ShellExecuteW(NULL, L"open", g_dataDir.c_str(), NULL, NULL, SW_SHOWNORMAL);
            break;
        case IDC_SET_RESET:
            if (!g_boosting && !Games_IsBusy() &&
                MessageBoxW(h, T(SID_SET_CONFIRM_RESET), T(SID_APP_NAME), MB_YESNO | MB_ICONQUESTION) == IDYES) {
                const std::vector<Tweak>& v = Tweaks_All();
                for (int i = (int)v.size() - 1; i >= 0; i--) TweakRevert(v[i]);
                g_lastBoost.clear();
                Settings_Save();
                UI_RefreshAll();
            }
            break;
        case IDM_TRAY_OPEN:
            UI_TrayShow(true);
            break;
        case IDM_TRAY_BOOST:
            UI_TrayShow(true);
            UI_ShowPage(PAGE_BOOST);
            break;
        case IDM_TRAY_EXIT:
            DestroyWindow(g_hMain);
            break;
        }
        break;
    }
    case WM_NOTIFY: {
        LPNMHDR hdr = (LPNMHDR)l;
        if (!hdr) break;
        if (hdr->code == LVN_ITEMCHANGED) {
            LPNMLISTVIEW lv = (LPNMLISTVIEW)l;
            if (hdr->idFrom == IDC_G_LIST && lv->uNewState & LVIS_SELECTED) {
                if (!g_gamesLoading) {
                    g_gameSel = lv->iItem;
                    GamesFillList();
                }
            } else if (hdr->idFrom == IDC_T_LIST && lv->uNewState & LVIS_SELECTED) {
                TweaksShowDetail(lv->iItem);
            }
        } else if (hdr->code == NM_CUSTOMDRAW) {
            // ListView or its header?
            wchar_t cls[32]; cls[0] = 0;
            GetClassNameW(hdr->hwndFrom, cls, 32);
            LPNMLVCUSTOMDRAW cd = (LPNMLVCUSTOMDRAW)l;
            if (!wcscmp(cls, WC_LISTVIEWW)) {
                switch (cd->nmcd.dwDrawStage) {
                case CDDS_PREPAINT: return CDRF_NOTIFYITEMDRAW;
                case CDDS_ITEMPREPAINT: {
                    bool sel = (cd->nmcd.uItemState & CDIS_SELECTED) != 0;
                    if (sel) { cd->clrText = RGB(255,255,255); cd->clrTextBk = COL_SELBAR; }
                    else {
                        cd->clrText = COL_TEXT;
                        cd->clrTextBk = (cd->nmcd.dwItemSpec % 2) ? COL_ROWALT : COL_PANEL;
                    }
                    // status column coloring for tweaks list
                    return CDRF_NEWFONT;
                }
                }
                return CDRF_DODEFAULT;
            } else if (!wcscmp(cls, WC_HEADERW)) {
                switch (cd->nmcd.dwDrawStage) {
                case CDDS_PREPAINT: return CDRF_NOTIFYITEMDRAW;
                case CDDS_ITEMPREPAINT: {
                    HDC dc = cd->nmcd.hdc;
                    RECT r = cd->nmcd.rc;
                    HBRUSH b = CreateSolidBrush(COL_CARD);
                    FillRect(dc, &r, b); DeleteObject(b);
                    wchar_t txt[128]; txt[0] = 0;
                    HWND lv = GetParent(hdr->hwndFrom);
                    LVCOLUMNW c; ZeroMemory(&c, sizeof(c));
                    c.mask = LVCF_TEXT; c.pszText = txt; c.cchTextMax = 128;
                    ListView_GetColumn(lv, (int)cd->nmcd.dwItemSpec, &c);
                    SetBkMode(dc, TRANSPARENT);
                    SetTextColor(dc, COL_ACCENT);
                    HFONT old = (HFONT)SelectObject(dc, g_hFont);
                    r.left += 6;
                    DrawTextW(dc, txt, -1, &r, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
                    SelectObject(dc, old);
                    return CDRF_SKIPDEFAULT;
                }
                }
                return CDRF_DODEFAULT;
            }
        }
        break;
    }
    case WM_APP_LOG: {
        wchar_t* p = (wchar_t*)l;
        if (p) { UI_LogBoost(p); delete[] p; }
        break;
    }
    case WM_APP_PROGRESS:
        UI_BoostProgress((int)w);
        break;
    case WM_APP_BOOSTDONE: {
        int fails = (int)w & 0x7FFF;
        bool undo = ((int)w & 0x8000) != 0;
        g_boosting = false;
        if (!undo && fails == 0) {
            g_lastBoost = NowStamp();
            Settings_Save();
        }
        UI_LogBoost(fails == 0 ? T(SID_B_DONE) : T(SID_B_FAIL));
        UI_BoostProgress(100);
        UI_BoostDone(fails == 0);
        break;
    }
    case WM_APP_GAME: {
        wchar_t* info = (wchar_t*)l;
        UI_GameEvent((int)w, info);
        if (info) delete[] info;
        break;
    }
    case WM_APP_TRAY:
        if (l == WM_LBUTTONDBLCLK) UI_TrayShow(true);
        else if (l == WM_RBUTTONUP) {
            POINT pt; GetCursorPos(&pt);
            HMENU m = CreatePopupMenu();
            AppendMenuW(m, MF_STRING, IDM_TRAY_OPEN, T(SID_TRAY_OPEN));
            AppendMenuW(m, MF_STRING, IDM_TRAY_BOOST, T(SID_TRAY_BOOST));
            AppendMenuW(m, MF_SEPARATOR, 0, NULL);
            AppendMenuW(m, MF_STRING, IDM_TRAY_EXIT, T(SID_TRAY_EXIT));
            SetForegroundWindow(h);
            TrackPopupMenu(m, TPM_BOTTOMALIGN | TPM_LEFTALIGN, pt.x, pt.y, 0, h, NULL);
            DestroyMenu(m);
        }
        break;
    case WM_TIMER:
        if (w == 1 && g_page == PAGE_SYSTEM) SysRefresh();
        break;
    case WM_SIZE: {
        int W = LOWORD(l), H = HIWORD(l);
        if (!hSide) break;
        MoveWindow(hSide, 0, 0, S(240), H, TRUE);
        MoveWindow(hTitle, S(264), S(14), W - S(264) - S(300), S(34), TRUE);
        MoveWindow(hLangBtn, W - S(268), S(12), S(120), S(32), TRUE);
        MoveWindow(hAdminBadge, W - S(140), S(12), S(128), S(32), TRUE);
        MoveWindow(hStatusBar, S(240), H - S(30), W - S(240), S(30), TRUE);
        for (int i = 0; i < PAGE_COUNT; i++)
            MoveWindow(g_hPages[i], S(264), S(62), W - S(264) - S(24), H - S(62) - S(44), TRUE);
        // stretch key controls
        int pw = W - S(264) - S(24), ph = H - S(62) - S(44);
        if (pw < 100 || ph < 100) break;
        MoveWindow(hDashPic, 0, 0, pw, S(150), TRUE);
        MoveWindow(hDashTip, 0, S(448), pw, ph - S(448) - S(8), TRUE);
        MoveWindow(hGamesList, 0, 0, pw, S(236), TRUE);
        MoveWindow(hBoostProg, 0, S(114), pw, S(26), TRUE);
        MoveWindow(hBoostLog, 0, S(152), pw, ph - S(152) - S(8), TRUE);
        MoveWindow(hTweakList, 0, 0, pw, ph - S(230), TRUE);
        MoveWindow(hTweakDetail, 0, ph - S(220), pw, S(110), TRUE);
        int by = ph - S(100);
        MoveWindow(hTweakApply, 0, by, S(160), S(34), TRUE);
        MoveWindow(hTweakRevert, S(170), by, S(160), S(34), TRUE);
        MoveWindow(hTweakAll, S(340), by, S(230), S(34), TRUE);
        MoveWindow(hTweakUndoAll, S(580), by, S(160), S(34), TRUE);
        MoveWindow(hSysGraph, 0, S(74), pw, ph - S(74) - S(120), TRUE);
        MoveWindow(GetDlgItem(g_hPages[PAGE_SYSTEM], IDC_S_CLEANRAM), 0, ph - S(100), S(220), S(38), TRUE);
        MoveWindow(GetDlgItem(g_hPages[PAGE_SYSTEM], IDC_S_CLEANTEMP), S(232), ph - S(100), S(220), S(38), TRUE);
        MoveWindow(GetDlgItem(g_hPages[PAGE_SYSTEM], IDC_S_REFRESH), S(464), ph - S(100), S(240), S(38), TRUE);
        MoveWindow(hHelpText, 0, 0, pw, ph - S(8), TRUE);
        InvalidateRect(h, NULL, TRUE);
        break;
    }
    case WM_GETMINMAXINFO: {
        MINMAXINFO* mi = (MINMAXINFO*)l;
        mi->ptMinTrackSize.x = S(1020);
        mi->ptMinTrackSize.y = S(680);
        break;
    }
    case WM_SYSCOMMAND:
        if ((w & 0xFFF0) == SC_MINIMIZE && g_closeToTray) {
            UI_TrayShow(false);
            return 0;
        }
        return DefWindowProcW(h, m, w, l);
    case WM_CLOSE:
        if (g_closeToTray && !g_boosting && !g_inGame) { UI_TrayShow(false); return 0; }
        return DefWindowProcW(h, m, w, l);
    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProcW(h, m, w, l);
    }
    return 0;
}

// ---------- Create ----------
bool UI_Create(HINSTANCE hInst) {
    // DPI scale from primary monitor
    HDC sdc = GetDC(NULL);
    if (sdc) { g_scale = GetDeviceCaps(sdc, LOGPIXELSX); ReleaseDC(NULL, sdc); }
    if (g_scale < 96) g_scale = 96;
    if (g_scale > 288) g_scale = 288;

    hBrBg = CreateSolidBrush(COL_BG);
    hBrPanel = CreateSolidBrush(COL_PANEL);
    hBrSide = CreateSolidBrush(COL_SIDE);
    hBrCard = CreateSolidBrush(COL_CARD);
    hBrEdit = CreateSolidBrush(RGB(10, 14, 26));

    g_imgLogo = LoadPng(RES_PNG_LOGO);
    g_imgBanner = LoadPng(RES_PNG_BANNER);

    WNDCLASSW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.lpfnWndProc = UI_MainProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = hBrBg;
    wc.lpszClassName = L"FPSBoosterMain";
    wc.hIcon = LoadIconW(hInst, MAKEINTRESOURCEW(RES_ICON_APP));
    if (!RegisterClassW(&wc)) return false;

    ZeroMemory(&wc, sizeof(wc));
    wc.lpfnWndProc = PageProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = hBrPanel;
    wc.lpszClassName = L"FPSBoosterPage";
    if (!RegisterClassW(&wc)) return false;

    ZeroMemory(&wc, sizeof(wc));
    wc.lpfnWndProc = SideProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = hBrSide;
    wc.lpszClassName = L"FPSBoosterSide";
    if (!RegisterClassW(&wc)) return false;

    int W = S(1180), H = S(760);
    g_hMain = CreateWindowExW(0, L"FPSBoosterMain", T(SID_APP_NAME),
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, W, H,
        NULL, NULL, hInst, NULL);
    if (!g_hMain) return false;

    hSide = CreateWindowExW(0, L"FPSBoosterSide", L"", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
        0, 0, S(240), H, g_hMain, NULL, hInst, NULL);
    for (int i = 0; i < PAGE_COUNT; i++) {
        HWND b = CreateWindowExW(0, WC_BUTTONW, NavText(i), WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
            S(14), S(266) + i * S(52), S(212), S(46), hSide, (HMENU)(INT_PTR)(IDC_NAV_BASE + i), hInst, NULL);
        SendMessageW(b, WM_SETFONT, (WPARAM)g_hFont, 0);
    }

    hTitle = Mk(g_hMain, WC_STATICW, L"", WS_CHILD | WS_VISIBLE | SS_LEFT, 0,
        264, 14, 600, 34, 0, g_hFontBig);
    hLangBtn = Mk(g_hMain, WC_BUTTONW, Strings_GetLang() == 1 ? L"فا" : L"EN",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0, 900, 12, 120, 32, IDC_LANG_BTN, g_hFont);
    hAdminBadge = Mk(g_hMain, WC_STATICW, L"", WS_CHILD | WS_VISIBLE | SS_RIGHT, 0,
        1030, 12, 128, 32, 0, g_hFont);

    for (int i = 0; i < PAGE_COUNT; i++) {
        g_hPages[i] = CreateWindowExW(0, L"FPSBoosterPage", L"", WS_CHILD | WS_CLIPCHILDREN,
            S(264), S(62), S(892), S(624), g_hMain, NULL, hInst, NULL);
    }
    BuildDash(g_hPages[PAGE_DASH]);
    BuildGames(g_hPages[PAGE_GAMES]);
    BuildBoost(g_hPages[PAGE_BOOST]);
    BuildTweaks(g_hPages[PAGE_TWEAKS]);
    BuildSystem(g_hPages[PAGE_SYSTEM]);
    BuildHelp(g_hPages[PAGE_HELP]);
    BuildSettings(g_hPages[PAGE_SETTINGS]);

    hStatusBar = Mk(g_hMain, WC_STATICW, T(SID_STATUS_READY), WS_CHILD | WS_VISIBLE | SS_LEFT, 0,
        240, 730, 900, 30, 0, g_hFont);
    LabelColor(hTitle, COL_TEXT);
    SetLabel(hAdminBadge, WFormat(L"[%s]", g_isAdmin ? T(SID_ADMIN_OK) : T(SID_ADMIN_NO)),
        g_isAdmin ? COL_GREEN : COL_RED);

    SetTimer(g_hMain, 1, 1000, NULL);
    UI_ShowPage(PAGE_DASH);

    // initial window position/size fix
    RECT r; GetClientRect(g_hMain, &r);
    SendMessageW(g_hMain, WM_SIZE, 0, MAKELPARAM(r.right, r.bottom));
    return true;
}
