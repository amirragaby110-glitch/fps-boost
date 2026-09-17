// FPS Booster Pro - User interface (dark theme, sidebar, 9 pages)
#include "app.h"
#include <commdlg.h>
#include <winhttp.h>
#include <gdiplus.h>
#include <stdio.h>

using namespace Gdiplus;

// ---------- Theme engine (dark / light / auto + accent color) ----------
struct Theme {
    COLORREF bgTop, bgBottom;   // page background gradient
    COLORREF panel;             // control surface / list background
    COLORREF side, side2;       // sidebar gradient
    COLORREF card;              // cards, list header
    COLORREF edit;              // edit / listbox / combo fields
    COLORREF rowAlt, selBar;    // list rows + selection
    COLORREF accent, accent2;   // gradient pair (accent-overrideable)
    COLORREF text, muted;
    COLORREF green, red, yellow;
    COLORREF line;              // hairlines, grid
    bool dark;
};
static Theme g_theme;
#define COL_BG     (g_theme.bgTop)
#define COL_PANEL  (g_theme.panel)
#define COL_SIDE   (g_theme.side)
#define COL_CARD   (g_theme.card)
#define COL_ACCENT (g_theme.accent)
#define COL_ACCENT2 (g_theme.accent2)
#define COL_TEXT   (g_theme.text)
#define COL_MUTED  (g_theme.muted)
#define COL_GREEN  (g_theme.green)
#define COL_RED    (g_theme.red)
#define COL_YELLOW (g_theme.yellow)
#define COL_ROWALT (g_theme.rowAlt)
#define COL_SELBAR (g_theme.selBar)
#define COL_EDIT   (g_theme.edit)
#define COL_LINE   (g_theme.line)

static int g_themeMode = 2; // 0 dark, 1 light, 2 auto (follow Windows)
static int g_accent = 0;    // 0 neon, 1 emerald, 2 sunset, 3 violet
static bool g_hotkey = true;
static std::vector<HWND> g_lvWins; // listviews needing recolor on theme switch

static bool WinThemeIsLight() {
    HKEY k = NULL;
    DWORD v = 0, sz = sizeof(v), ty = 0;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", 0, KEY_READ, &k) == ERROR_SUCCESS) {
        RegQueryValueExW(k, L"AppsUseLightTheme", NULL, &ty, (BYTE*)&v, &sz);
        RegCloseKey(k);
    }
    return v != 0;
}
static void Theme_Resolve() {
    bool dark = (g_themeMode == 0) || (g_themeMode == 2 && !WinThemeIsLight());
    if (g_themeMode < 0 || g_themeMode > 2) g_themeMode = 2;
    if (g_accent < 0 || g_accent > 3) g_accent = 0;
    g_theme.dark = dark;
    if (dark) {
        g_theme.bgTop = RGB(11, 15, 31); g_theme.bgBottom = RGB(6, 9, 20);
        g_theme.panel = RGB(15, 21, 38);
        g_theme.side = RGB(6, 9, 20); g_theme.side2 = RGB(11, 17, 36);
        g_theme.card = RGB(26, 34, 56);
        g_theme.edit = RGB(10, 14, 26);
        g_theme.rowAlt = RGB(24, 32, 54); g_theme.selBar = RGB(30, 58, 80);
        g_theme.text = RGB(226, 232, 240); g_theme.muted = RGB(148, 163, 184);
        g_theme.green = RGB(52, 211, 153); g_theme.red = RGB(248, 113, 113);
        g_theme.yellow = RGB(251, 191, 36);
        g_theme.line = RGB(40, 60, 96);
    } else {
        g_theme.bgTop = RGB(242, 245, 251); g_theme.bgBottom = RGB(221, 229, 242);
        g_theme.panel = RGB(255, 255, 255);
        g_theme.side = RGB(255, 255, 255); g_theme.side2 = RGB(231, 237, 247);
        g_theme.card = RGB(236, 241, 248);
        g_theme.edit = RGB(255, 255, 255);
        g_theme.rowAlt = RGB(243, 246, 252); g_theme.selBar = RGB(186, 216, 245);
        g_theme.text = RGB(15, 23, 42); g_theme.muted = RGB(100, 116, 139);
        g_theme.green = RGB(4, 120, 87); g_theme.red = RGB(220, 38, 38);
        g_theme.yellow = RGB(161, 98, 7);
        g_theme.line = RGB(203, 213, 225);
    }
    static const COLORREF kAcc[4][2] = {
        { RGB(34, 211, 238), RGB(232, 121, 249) },
        { RGB(52, 211, 153), RGB(34, 197, 94) },
        { RGB(251, 146, 60), RGB(244, 114, 182) },
        { RGB(167, 139, 250), RGB(96, 165, 250) },
    };
    g_theme.accent = kAcc[g_accent][0];
    g_theme.accent2 = kAcc[g_accent][1];
}
static COLORREF Darken(COLORREF c, int pct) {
    return RGB(GetRValue(c) * pct / 100, GetGValue(c) * pct / 100, GetBValue(c) * pct / 100);
}

static int g_scale = 96;
static int S(int v) { return (v * g_scale + 48) / 96; }

static HBRUSH hBrBg = NULL, hBrPanel = NULL, hBrSide = NULL, hBrCard = NULL, hBrEdit = NULL;
static Bitmap* g_imgLogo = NULL;
static Bitmap* g_imgBanner = NULL;
static Bitmap* g_imgBg = NULL;

// Colored labels registry (role-based: resolves against live theme)
enum LRole { LR_TEXT, LR_MUTED, LR_ACCENT, LR_ACCENT2, LR_GREEN, LR_RED, LR_YELLOW, LR_WHITE };
struct ColorLabel { HWND h; int role; };
static std::vector<ColorLabel> g_colors;
static COLORREF RoleColor(int role) {
    switch (role) {
    case LR_MUTED: return g_theme.muted;
    case LR_ACCENT: return g_theme.dark ? g_theme.accent : Darken(g_theme.accent, 62);
    case LR_ACCENT2: return g_theme.dark ? g_theme.accent2 : Darken(g_theme.accent2, 62);
    case LR_GREEN: return g_theme.green;
    case LR_RED: return g_theme.red;
    case LR_YELLOW: return g_theme.yellow;
    case LR_WHITE: return g_theme.dark ? RGB(255, 255, 255) : RGB(15, 23, 42);
    default: return g_theme.text;
    }
}
static void LabelColor(HWND h, int role) {
    for (size_t i = 0; i < g_colors.size(); i++)
        if (g_colors[i].h == h) { g_colors[i].role = role; return; }
    ColorLabel e; e.h = h; e.role = role; g_colors.push_back(e);
}
static void SetLabel(HWND h, const std::wstring& s, int role) {
    SetWindowTextW(h, s.c_str());
    LabelColor(h, role);
    InvalidateRect(h, NULL, TRUE);
}
static void ProgSet(HWND h, int pct) {
    if (!h) return;
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    SetWindowLongPtrW(h, GWLP_USERDATA, pct + 100); // +100: never collides with checkbox tag (1)
    InvalidateRect(h, NULL, FALSE);
}

// ---------- Settings ----------
static bool g_aiOnline = true;
static std::wstring g_aiPending;
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
    g_beastGuid = root.wstr("beast");
    g_beastPrev = root.wstr("beastprev");
    g_autoBoost = root.num("autoboost", 0) != 0;
    g_aiOnline = root.num("aionline", 1) != 0;
    g_themeMode = root.num("theme", 2);
    g_accent = root.num("accent", 0);
    g_hotkey = root.num("hotkey", 1) != 0;
}
void Settings_Save() {
    char nb[128];
    snprintf(nb, 128, "{\"lang\":%d,\"tray\":%d,\"score\":%d,\"autoboost\":%d,\"aionline\":%d", Strings_GetLang(), g_closeToTray?1:0, g_lastScore, g_autoBoost?1:0, g_aiOnline?1:0);
    std::string j = nb;
    j += ",\"lastBoost\":\"" + JsonEscapeW(g_lastBoost) + "\"";
    j += ",\"beast\":\"" + JsonEscapeW(g_beastGuid) + "\",\"beastprev\":\"" + JsonEscapeW(g_beastPrev) + "\"";
    j += ",\"theme\":" + std::to_string(g_themeMode) + ",\"accent\":" + std::to_string(g_accent) + ",\"hotkey\":" + std::to_string(g_hotkey ? 1 : 0) + "}";
    WriteFileText(JoinPath(g_dataDir, L"settings.json"), j);
}

// ---------- Control helpers ----------
static HWND hSide = NULL, hTitle = NULL, hLangBtn = NULL, hAdminBadge = NULL, hStatusBar = NULL, hThemeBtn = NULL;

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
    SetLabel(h, T(t), LR_ACCENT);
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
    DWORD ex = WS_EX_CLIENTEDGE;
    if (multi && ro && Strings_GetLang() == 1) ex |= WS_EX_RIGHT | WS_EX_RTLREADING;
    HWND e = Mk(p, WC_EDITW, L"", st, ex, x, y, w, h, id, g_hFont);
    if (e) SendMessageW(e, EM_SETLIMITTEXT, multi ? 100000 : 1024, 0);
    return e;
}
static HWND MkCombo(HWND p, int id, int x, int y, int w) {
    return Mk(p, WC_COMBOBOXW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_VSCROLL,
        0, x, y, w, 200, id, g_hFont);
}
static HWND MkList(HWND p, int id, int x, int y, int w, int h) {
    HWND l = Mk(p, WC_LISTVIEWW, L"", WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL |
        LVS_SHOWSELALWAYS | WS_BORDER | WS_TABSTOP, WS_EX_CLIENTEDGE, x, y, w, h, id, g_hFont);
    if (l) {
        SendMessageW(l, LVM_SETEXTENDEDLISTVIEWSTYLE, 0,
            LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_GRIDLINES);
        ListView_SetBkColor(l, COL_PANEL);
        ListView_SetTextColor(l, COL_TEXT);
        ListView_SetTextBkColor(l, COL_PANEL);
        g_lvWins.push_back(l);
    }
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
static Gdiplus::Color ThColor(BYTE a, COLORREF c) {
    return Gdiplus::Color(a, GetRValue(c), GetGValue(c), GetBValue(c));
}
// Modern procedural background: vertical gradient + two soft accent glows
static void PaintPageBg(HDC dc, int W, int H) {
    if (W < 1) W = 1; if (H < 1) H = 1;
    Graphics g(dc);
    LinearGradientBrush bg(Point(0, 0), Point(0, H),
        ThColor(255, g_theme.bgTop), ThColor(255, g_theme.bgBottom));
    g.FillRectangle(&bg, 0, 0, W, H);
    int R = W < H ? W : H;
    int rad = R * 3 / 4 + 40;
    BYTE ga = g_theme.dark ? (BYTE)30 : (BYTE)22;
    GraphicsPath p1;
    int cx1 = W * 17 / 20, cy1 = H / 8;
    p1.AddEllipse(cx1 - rad, cy1 - rad, rad * 2, rad * 2);
    PathGradientBrush g1(&p1);
    g1.SetCenterColor(ThColor(ga, g_theme.accent));
    Color s1[1]; s1[0] = ThColor(0, g_theme.accent);
    int n1 = 1; g1.SetSurroundColors(s1, &n1);
    g1.SetCenterPoint(Point(cx1, cy1));
    g.FillPath(&g1, &p1);
    GraphicsPath p2;
    int cx2 = W / 12, cy2 = H * 9 / 10;
    p2.AddEllipse(cx2 - rad, cy2 - rad, rad * 2, rad * 2);
    PathGradientBrush g2(&p2);
    g2.SetCenterColor(ThColor(ga, g_theme.accent2));
    Color s2[1]; s2[0] = ThColor(0, g_theme.accent2);
    int n2 = 1; g2.SetSurroundColors(s2, &n2);
    g2.SetCenterPoint(Point(cx2, cy2));
    g.FillPath(&g2, &p2);
}
static LRESULT CALLBACK PageProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_ERASEBKGND: {
        RECT r; GetClientRect(h, &r);
        PaintPageBg((HDC)w, r.right - r.left, r.bottom - r.top);
        return 1;
    }
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
    case WM_CTLCOLORBTN:
    case WM_COMMAND:
    case WM_NOTIFY:
    case WM_MEASUREITEM:
    case WM_DRAWITEM:
        return SendMessageW(g_hMain, m, w, l);
    }
    return DefWindowProcW(h, m, w, l);
}
static const wchar_t* NavText(int page) {
    switch (page) {
    case PAGE_DASH: return T(SID_NAV_DASH);
    case PAGE_AI: return T(SID_NAV_AI);
    case PAGE_GAMES: return T(SID_NAV_GAMES);
    case PAGE_PROC: return T(SID_NAV_PROC);
    case PAGE_BOOST: return T(SID_NAV_BOOST);
    case PAGE_TWEAKS: return T(SID_NAV_TWEAKS);
    case PAGE_SYSTEM: return T(SID_NAV_SYSTEM);
    case PAGE_HELP: return T(SID_NAV_HELP);
    case PAGE_POWER: return T(SID_NAV_POWER);
    case PAGE_NET: return T(SID_NAV_NET);
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
        Graphics g(dc);
        int gh = r.bottom > 1 ? r.bottom : 1;
        LinearGradientBrush bg(Point(0, 0), Point(0, gh), ThColor(255, g_theme.side), ThColor(255, g_theme.side2));
        g.FillRectangle(&bg, r.left, r.top, r.right - r.left, gh);
        SolidBrush glow(ThColor(70, g_theme.accent));
        g.FillRectangle(&glow, 0, 0, r.right, S(3));
        g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
        if (g_imgLogo) {
            int sz = S(148);
            int x = (r.right - sz) / 2;
            g.DrawImage(g_imgLogo, x, S(12), sz, sz);
        }
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, COL_TEXT);
        HFONT old = (HFONT)SelectObject(dc, g_hFontBig);
        RECT tr = {0, S(166), r.right, S(192)};
        DrawTextW(dc, T(SID_APP_NAME), -1, &tr, DT_CENTER | DT_SINGLELINE);
        SelectObject(dc, g_hFont);
        SetTextColor(dc, COL_MUTED);
        RECT tr2 = {0, S(190), r.right, S(210)};
        DrawTextW(dc, T(SID_APP_TAG), -1, &tr2, DT_CENTER | DT_SINGLELINE);
        // divider
        HPEN pen = CreatePen(PS_SOLID, 1, COL_LINE);
        HPEN op = (HPEN)SelectObject(dc, pen);
        MoveToEx(dc, S(20), S(222), NULL); LineTo(dc, r.right - S(20), S(222));
        SelectObject(dc, op); DeleteObject(pen);
        // version at bottom
        SetTextColor(dc, COL_MUTED);
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
static HWND hBoostLog, hBoostProg, hBoostStart, hBoostUndo, hBoostMax;
static HWND hTweakList, hTweakDetail, hTweakApply, hTweakRevert, hTweakAll, hTweakUndoAll;
static HWND hSysCpu, hSysRam, hSysUp, hSysTimer, hSysGraph;
static HWND hHelpText;
static HWND hAiInput, hAiAsk, hAiQ1, hAiQ2, hAiQ3, hAiHint, hAiOut, hAiStatus, hAiOnline;
static HWND hProcList, hProcHint, hProcStatus;
static HWND hSetLang, hSetTray, hSetStartup;
static HWND hSetStList = NULL;
static int g_cpuHist[90], g_ramHist[90], g_histN = 0;
static CpuMeter* g_meter = NULL;

// ---------- Dashboard ----------
static const char* kDashTweaks[] = {"gamemode", "hags", "powerplan", "gamedvr", "visualfx", "mouseaccel"};
static void DashRefresh() {
    int ap = 0, tt = 0;
    g_lastScore = Tweaks_Score(&ap, &tt);
    InvalidateRect(hDashPic, NULL, TRUE);
    SetLabel(hDashSys[0], SysCpuName(), LR_TEXT);
    SetLabel(hDashSys[1], SysGpuName(), LR_TEXT);
    SetLabel(hDashSys[2], WFormat(L"%s (%d GB)", SysRamString().c_str(), RamTotalMB() / 1024), LR_TEXT);
    SetLabel(hDashSys[3], SysOsString(), LR_TEXT);
    SetLabel(hDashSys[4], PowerGetActiveName(), LR_TEXT);
    SetLabel(hDashSys[5], SysDisplayString(), LR_TEXT);
    for (int i = 0; i < 6; i++) {
        const Tweak* t = TweakById(kDashTweaks[i]);
        std::wstring nm, st; int c = LR_MUTED;
        if (t) {
            TweakDisplayName(*t, nm);
            int s = TweakCheck(*t);
            if (s == 1) { st = T(SID_APPLIED); c = LR_GREEN; }
            else if (s == 0) { st = T(SID_NOT_APPLIED); c = LR_RED; }
            else { st = T(SID_NA); c = LR_YELLOW; }
        }
        SetLabel(hDashNames[i], nm, LR_TEXT);
        SetLabel(hDashVals[i], st, c);
    }
    wchar_t nb[64];
    StringCchPrintfW(nb, 64, L"%s: %d", T(SID_DASH_GAMES), (int)Games_All().size());
    SetLabel(hDashInfo1, nb, LR_MUTED);
    SetLabel(hDashInfo2, WFormat(L"%s: %s", T(SID_DASH_LASTBOOST),
        g_lastBoost.empty() ? T(SID_DASH_NEVER) : g_lastBoost.c_str()), LR_MUTED);
    SYSTEMTIME st; GetLocalTime(&st);
    StrId tips[] = {SID_TIP1, SID_TIP2, SID_TIP3, SID_TIP4, SID_TIP5};
    SetWindowTextW(hDashTip, WFormat(L"%s: %s", T(SID_DASH_TIP), T(tips[st.wDay % 5])).c_str());
}
static void BuildDash(HWND p) {
    hDashPic = Mk(p, WC_STATICW, L"", WS_CHILD | WS_VISIBLE | SS_OWNERDRAW, 0, 0, 0, 892, 150, IDC_DASH_PIC, NULL);
    MkHeader(p, 0, 158, 430, SID_DASH_SYSTEM);
    for (int i = 0; i < 6; i++) {
        HWND n = MkLabel(p, 0, 188 + i * 30, 150, 24);
        SetLabel(n, L"", LR_MUTED);
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
        if (!tip.empty()) SetLabel(hGamesTip, WFormat(L"%s %s", T(SID_G_TIP_TITLE), tip.c_str()), LR_YELLOW);
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
static HWND hGamesAuto = NULL, hGamesAutoSt = NULL;
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
    HWND hint = MkLabel(p, 400, 430, 480, 24); SetLabel(hint, T(SID_G_KILL_HINT), LR_MUTED);
    hGamesLaunch = MkCTA(p, IDC_G_LAUNCH, 0, 506, 300, 54, SID_G_LAUNCH);
    hGamesStatus = MkLabel(p, 320, 512, 572, 26, g_hFontBig);
    hGamesTip = MkLabel(p, 320, 540, 572, 40);
    hGamesAuto = MkCheck(p, IDC_G_AUTO, 0, 588, 560, SID_G_AUTO);
    SetChecked(hGamesAuto, g_autoBoost);
    hGamesAutoSt = MkLabel(p, 570, 588, 322, 26);
    if (g_autoBoost) SetLabel(hGamesAutoSt, T(SID_G_AUTO_WATCH), LR_MUTED);
}

// ---------- Boost ----------
static void BuildBoost(HWND p) {
    HWND d = MkLabel(p, 0, 0, 892, 40);
    SetLabel(d, T(SID_B_SUB), LR_MUTED);
    hBoostStart = MkCTA(p, IDC_B_START, 0, 44, 340, 56, SID_B_START);
    hBoostUndo = MkCTA(p, IDC_B_UNDO, 360, 44, 220, 56, SID_B_UNDO);
    hBoostMax = MkCTA(p, IDC_B_MAX, 600, 44, 292, 56, SID_B_MAXFPS);
    hBoostProg = Mk(p, WC_STATICW, L"", WS_CHILD | WS_VISIBLE | SS_OWNERDRAW, 0,
        0, 114, 892, 26, IDC_B_PROG, NULL);
    ProgSet(hBoostProg, 0);
    hBoostLog = Mk(p, WC_LISTBOXW, L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL |
        WS_BORDER | WS_TABSTOP, WS_EX_CLIENTEDGE, 0, 152, 892, 440, IDC_B_LOG, g_hFont);
}
void UI_LogBoost(const wchar_t* line) {
    if (!hBoostLog) return;
    int i = (int)SendMessageW(hBoostLog, LB_ADDSTRING, 0, (LPARAM)line);
    SendMessageW(hBoostLog, LB_SETTOPINDEX, i, 0);
}
void UI_BoostProgress(int pct) {
    ProgSet(hBoostProg, pct);
}
void UI_BoostDone(bool ok) {
    EnableWindow(hBoostStart, TRUE);
    EnableWindow(hBoostUndo, TRUE);
    EnableWindow(hBoostMax, TRUE);
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
static HWND hSysRes = NULL;
static HWND hSysGpu = NULL;
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
    SetLabel(hSysCpu, WFormat(L"%s: %d%%", T(SID_S_CPU), cpu), cpu > 85 ? LR_RED : LR_TEXT);
    SetLabel(hSysRam, WFormat(L"%s: %d%%  (%d MB %s)", T(SID_S_RAM), ram, RamAvailMB(),
        Strings_GetLang() == 1 ? L"آزاد" : L"free"), ram > 90 ? LR_RED : LR_TEXT);
    SetLabel(hSysUp, WFormat(L"%s: %s", T(SID_S_UPTIME), SysUptimeString().c_str()), LR_TEXT);
    ULONG cur = 0, mn = 0, mx = 0;
    if (TimerQuery(cur, mn, mx))
        SetLabel(hSysTimer, WFormat(L"%s: %.1f ms", T(SID_S_TIMER), cur / 10000.0), LR_TEXT);
    if (hSysGpu) SetLabel(hSysGpu, WFormat(L"%s: %s", T(SID_S_GPU), SysGpuName().c_str()), LR_TEXT);
    if (hSysGraph) InvalidateRect(hSysGraph, NULL, TRUE);
}
static void BuildSystem(HWND p) {
    hSysCpu = MkLabel(p, 0, 0, 440, 30, g_hFontBig);
    hSysRam = MkLabel(p, 0, 34, 440, 30, g_hFontBig);
    hSysUp = MkLabel(p, 452, 0, 440, 30);
    hSysTimer = MkLabel(p, 452, 34, 440, 30);
    hSysGpu = MkLabel(p, 0, 66, 892, 24);
    hSysGraph = Mk(p, WC_STATICW, L"", WS_CHILD | WS_VISIBLE | SS_OWNERDRAW | WS_BORDER, 0,
        0, 92, 892, 312, IDC_S_GRAPH, NULL);
    MkButton(p, IDC_S_CLEANRAM, 0, 416, 220, 38, SID_S_CLEAN_RAM);
    MkButton(p, IDC_S_CLEANTEMP, 232, 416, 220, 38, SID_S_CLEAN_TEMP);
    MkButton(p, IDC_S_REFRESH, 464, 416, 240, 38, SID_S_MAXREFRESH);
    MkButton(p, IDC_S_COPY, 716, 416, 176, 38, SID_S_COPY);
    HWND lr = MkLabel(p, 0, 466, 130, 28); SetWindowTextW(lr, T(SID_S_RES));
    hSysRes = MkCombo(p, IDC_S_RES, 140, 464, 200);
    const wchar_t* res[] = {L"1280x720", L"1366x768", L"1600x900", L"1920x1080", L"2560x1440", L"3840x2160"};
    int cw = 0, chh = 0; GetCurrentResolution(cw, chh);
    int sel = 3;
    for (int i = 0; i < 6; i++) {
        SendMessageW(hSysRes, CB_ADDSTRING, 0, (LPARAM)res[i]);
        int w2 = 0, h2 = 0;
        if (swscanf(res[i], L"%dx%d", &w2, &h2) == 2 && w2 == cw && h2 == chh) sel = i;
    }
    SendMessageW(hSysRes, CB_SETCURSEL, sel, 0);
    MkButton(p, IDC_S_RESAPPLY, 352, 464, 130, 32, SID_S_RES_APPLY);
    MkButton(p, IDC_S_RESNATIVE, 494, 464, 130, 32, SID_S_RES_NATIVE);
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
"- After big Windows updates, run the boost again.\r\n"
"\r\n6) POWER (BEAST MODE)\r\n"
"- The Power page creates a dedicated plan using 100% of your hardware:\r\n"
"  max CPU speed, aggressive turbo, no core parking, no USB/PCIe/Wi-Fi\r\n"
"  saving, display and sleep set to Never while plugged in.\r\n"
"- One click activates it, one click restores your previous plan.\r\n"
"- You can also apply all settings to your current plan instead.\r\n"
"\r\n7) INTERNET (SPEED TEST + DNS)\r\n"
"- Internet page: real ping, download and upload test with gaming grade.\r\n"
"- Public IP is shown automatically after the test.\r\n"
"- One-click gaming DNS: Cloudflare 1.1.1.1 or Google 8.8.8.8.\r\n"
"- Automatic (restore) brings back your original DNS.\r\n"
"\r\n8) AUTO-BOOST + DISPLAY\r\n"
"- My Games: enable Auto-boost to speed up library games on launch.\r\n"
"- System page: quick resolution switcher (Native = recommended).\r\n"
"\r\n9) MAXIMUM FPS MODE\r\n"
"- Boost page: MAXIMUM FPS button does EVERYTHING for the highest FPS possible.\r\n"
"- Applies all 27 tweaks, Beast Mode, stops background services, closes bloat,\r\n"
"  and even lowers resolution. Undo restores everything.\r\n"
"\r\n10) DNS PING + STARTUP\r\n"
"- Internet page: Ping all compares 5 DNS servers, Apply fastest uses the best.\r\n"
"- Custom DNS boxes accept any IPv4 pair.\r\n"
"- Settings page: Startup manager enables/disables auto-start programs.\r\n";
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
"- بعد از آپدیت بزرگ ویندوز، بوست را دوباره اجرا کنید.\r\n"
"\r\n۶) پاور (حالت حداکثر توان)\r\n"
"- صفحه پاور یک پلن اختصاصی می‌سازد که ۱۰۰٪ توان سخت‌افزار را آزاد می‌کند:\r\n"
"  حداکثر سرعت پردازنده، توربو تهاجمی، بدون پارک هسته، بدون صرفه‌جویی\r\n"
"  USB/PCIe/وای‌فای، نمایشگر و خواب روی Never.\r\n"
"- با یک کلیک فعال و با یک کلیک به پلن قبلی برمی‌گردد.\r\n"
"\r\n۷) اینترنت (تست سرعت + DNS)\r\n"
"- صفحه اینترنت: تست واقعی پینگ، دانلود و آپلود با رتبه گیمینگ.\r\n"
"- آی‌پی عمومی بعد از تست خودکار نمایش داده می‌شود.\r\n"
"- DNS گیمینگ با یک کلیک: کلادفلر 1.1.1.1 یا گوگل 8.8.8.8.\r\n"
"- گزینه خودکار DNS اصلی شما را برمی‌گرداند.\r\n"
"\r\n۸) بوست خودکار + نمایشگر\r\n"
"- در بازی‌ها: بوست خودکار بازی‌های کتابخانه هنگام اجرا.\r\n"
"- صفحه سیستم: تعویض سریع رزولوشن (Native = پیشنهادی).\r\n"
"\r\n۹) حالت بیشترین اف‌پی‌اس\r\n"
"- صفحه بوست: دکمه بیشترین اف پی اس همه کارها را برای بالاترین اف‌پی‌اس می‌کند.\r\n"
"- هر ۲۷ توییک، Beast Mode، توقف سرویس‌ها، بستن برنامه‌های اضافه و حتی\r\n"
"  پایین آوردن رزولوشن. Undo همه چیز را برمی‌گرداند.\r\n"
"\r\n۱۰) پینگ DNS + استارتاپ\r\n"
"- صفحه اینترنت: پینگ همه ۵ سرور DNS را مقایسه می‌کند و سریع‌ترین اعمال می‌شود.\r\n"
"- کادرهای DNS دلخواه هر IPv4 را قبول می‌کنند.\r\n"
"- صفحه تنظیمات: مدیریت استارتاپ برنامه‌های خوداجرا را فعال/غیرفعال می‌کند.\r\n";
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
static std::vector<StartupItem> g_stItems;
static void StartupFillList() {
    if (!hSetStList) return;
    g_stItems = StartupEnum();
    ListView_DeleteAllItems(hSetStList);
    for (size_t i = 0; i < g_stItems.size(); i++) {
        int r = LVAddRow(hSetStList, (int)i, g_stItems[i].name.c_str());
        LVSet(hSetStList, r, 1, T(g_stItems[i].enabled ? SID_ST_ON : SID_ST_OFF));
        LVSetData(hSetStList, r, (LPARAM)i);
    }
}
static HWND hSetTheme = NULL, hSetAccent = NULL, hSetHotkey = NULL;
static void BuildSettings(HWND p) {
    HWND l1 = MkLabel(p, 0, 8, 200, 26); SetWindowTextW(l1, T(SID_SET_LANG));
    hSetLang = MkCombo(p, IDC_SET_LANG, 210, 6, 240);
    SendMessageW(hSetLang, CB_ADDSTRING, 0, (LPARAM)T(SID_SET_LANG_EN));
    SendMessageW(hSetLang, CB_ADDSTRING, 0, (LPARAM)T(SID_SET_LANG_FA));
    SendMessageW(hSetLang, CB_SETCURSEL, Strings_GetLang(), 0);
    HWND lth = MkLabel(p, 0, 44, 200, 26); SetWindowTextW(lth, T(SID_SET_THEME));
    hSetTheme = MkCombo(p, IDC_SET_THEME, 210, 42, 240);
    SendMessageW(hSetTheme, CB_ADDSTRING, 0, (LPARAM)T(SID_SET_THEME_DARK));
    SendMessageW(hSetTheme, CB_ADDSTRING, 0, (LPARAM)T(SID_SET_THEME_LIGHT));
    SendMessageW(hSetTheme, CB_ADDSTRING, 0, (LPARAM)T(SID_SET_THEME_AUTO));
    SendMessageW(hSetTheme, CB_SETCURSEL, g_themeMode, 0);
    HWND lac = MkLabel(p, 0, 80, 200, 26); SetWindowTextW(lac, T(SID_SET_ACCENT));
    hSetAccent = MkCombo(p, IDC_SET_ACCENT, 210, 78, 240);
    SendMessageW(hSetAccent, CB_ADDSTRING, 0, (LPARAM)T(SID_SET_ACC0));
    SendMessageW(hSetAccent, CB_ADDSTRING, 0, (LPARAM)T(SID_SET_ACC1));
    SendMessageW(hSetAccent, CB_ADDSTRING, 0, (LPARAM)T(SID_SET_ACC2));
    SendMessageW(hSetAccent, CB_ADDSTRING, 0, (LPARAM)T(SID_SET_ACC3));
    SendMessageW(hSetAccent, CB_SETCURSEL, g_accent, 0);
    hSetTray = MkCheck(p, IDC_SET_TRAY, 0, 116, 420, SID_SET_TRAY);
    SetChecked(hSetTray, g_closeToTray);
    hSetStartup = MkCheck(p, IDC_SET_STARTUP, 0, 148, 420, SID_SET_STARTUP);
    SetChecked(hSetStartup, RegValueExists(HKEY_CURRENT_USER,
        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run", L"FPSBoosterPro"));
    hSetHotkey = MkCheck(p, IDC_SET_HOTKEY, 0, 180, 560, SID_SET_HOTKEY);
    SetChecked(hSetHotkey, g_hotkey);
    MkButton(p, IDC_SET_FOLDER, 0, 216, 240, 34, SID_SET_FOLDER);
    MkButton(p, IDC_SET_UPDATE, 250, 216, 260, 34, SID_SET_UPDATE);
    MkButton(p, IDC_SET_RESET, 520, 216, 240, 34, SID_SET_RESET);
    HWND ab = MkLabel(p, 0, 262, 892, 30);
    SetLabel(ab, T(SID_SET_ABOUT), LR_MUTED);
    MkHeader(p, 0, 300, 400, SID_SET_STARTUP_T);
    hSetStList = MkList(p, IDC_SET_STLIST, 0, 332, 892, 220);
    int sids[] = {SID_SET_ST_COL1, SID_SET_ST_COL2};
    int swd[] = {640, 220};
    LVCols(hSetStList, sids, swd, 2);
    MkButton(p, IDC_SET_STEN, 0, 562, 160, 34, SID_SET_ST_ENABLE);
    MkButton(p, IDC_SET_STDIS, 170, 562, 160, 34, SID_SET_ST_DISABLE);
    StartupFillList();
}
static void SettingsRefresh() {
    if (!hSetTheme) return;
    SendMessageW(hSetTheme, CB_SETCURSEL, g_themeMode, 0);
    SendMessageW(hSetAccent, CB_SETCURSEL, g_accent, 0);
    SetChecked(hSetTray, g_closeToTray);
    SetChecked(hSetHotkey, g_hotkey);
    SetChecked(hSetStartup, RegValueExists(HKEY_CURRENT_USER,
        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run", L"FPSBoosterPro"));
    InvalidateRect(g_hPages[PAGE_SETTINGS], NULL, TRUE);
}

// ---------- Power ----------
static HWND hPowPlan, hPowStatus, hPowBeast, hPowList, hPowNote;
static std::wstring PowFmtVal(const PowerSetting& p, DWORD v) {
    if (p.fmt == 1) return WFormat(L"%d%%", v);
    if (p.fmt == 2) {
        if (v == 0) return T(SID_P_NEVER);
        if (v >= 60 && v % 60 == 0)
            return WFormat(L"%d %s", v / 60, Strings_GetLang() == 1 ? L"دقیقه" : L"min");
        return WFormat(L"%d s", v);
    }
    return WFormat(L"%d", v);
}
static void PowRefresh() {
    SetLabel(hPowPlan, WFormat(L"%s %s", T(SID_P_CUR), PowerGetActiveName().c_str()), LR_TEXT);
    bool on = BeastIsActive();
    SetWindowTextW(hPowBeast, T(on ? SID_P_DEACTIVATE : SID_P_ACTIVATE));
    SetLabel(hPowStatus, WFormat(L"%s: %s", T(SID_P_BEAST), T(on ? SID_P_ACTIVE : SID_P_OFF)), on ? LR_GREEN : LR_MUTED);
    ListView_DeleteAllItems(hPowList);
    int n = 0;
    const PowerSetting* ps = PowerSettings(&n);
    for (int i = 0; i < n; i++) {
        std::wstring nm = Utf8ToWide(Strings_GetLang() == 1 ? ps[i].nameFa : ps[i].nameEn);
        int r = LVAddRow(hPowList, i, nm.c_str());
        LVSet(hPowList, r, 1, PowFmtVal(ps[i], ps[i].ac));
        DWORD ac = 0, dc = 0;
        LVSet(hPowList, r, 2, PowerReadActive(ps[i], ac, dc) ? PowFmtVal(ps[i], ac) : T(SID_NA));
    }
}
static void BuildPower(HWND p) {
    hPowPlan = MkLabel(p, 0, 4, 700, 26, g_hFontBig);
    MkButton(p, IDC_P_REFRESH, 712, 0, 180, 32, SID_BTN_REFRESH);
    hPowStatus = MkLabel(p, 0, 40, 560, 26, g_hFontBig);
    hPowNote = MkLabel(p, 560, 40, 332, 26);
    SetLabel(hPowNote, T(SID_P_NOTE), LR_MUTED);
    hPowBeast = MkCTA(p, IDC_P_BEAST, 0, 72, 340, 54, SID_P_ACTIVATE);
    hPowList = MkList(p, IDC_P_LIST, 0, 140, 892, 380);
    int ids[] = {SID_P_COL_SET, SID_P_COL_BEAST, SID_P_COL_CUR};
    int wd[] = {450, 200, 210};
    LVCols(hPowList, ids, wd, 3);
    MkButton(p, IDC_P_APPLYALL, 0, 532, 240, 36, SID_P_APPLYALL);
    MkButton(p, IDC_P_RESTORE, 250, 532, 200, 36, SID_P_RESTORE);
    MkButton(p, IDC_P_DELETE, 460, 532, 200, 36, SID_P_DELETE);
}

// ---------- Internet ----------
static HWND hNetStatus = NULL, hNetProg = NULL, hNetPing = NULL, hNetDown = NULL;
static HWND hNetUp = NULL, hNetIp = NULL, hNetGrade = NULL, hNetDnsSt = NULL;
static HWND hNetStart = NULL, hNetCancel = NULL;
static HWND hNetDns = NULL, hNetDnsApply = NULL, hNetPingAll = NULL, hNetFastest = NULL;
static HWND hNetPingRes = NULL, hNetC1 = NULL, hNetC2 = NULL;
static int g_dnsPingMs[5] = {-1, -1, -1, -1, -1};
static void NetRefresh() {
    if (!hNetDnsSt) return;
    SetLabel(hNetDnsSt, WFormat(L"%s %s", T(SID_N_DNS_CUR), DnsCurrent().c_str()), LR_TEXT);
    bool busy = NetTestBusy();
    EnableWindow(hNetStart, busy ? FALSE : TRUE);
    EnableWindow(hNetCancel, busy ? TRUE : FALSE);
}
static void BuildNet(HWND p) {
    hNetStatus = MkLabel(p, 0, 4, 700, 26, g_hFontBig);
    SetLabel(hNetStatus, T(SID_STATUS_READY), LR_TEXT);
    HWND srv = MkLabel(p, 0, 32, 400, 24); SetLabel(srv, T(SID_N_SERVER), LR_MUTED);
    hNetProg = Mk(p, WC_STATICW, L"", WS_CHILD | WS_VISIBLE | SS_OWNERDRAW, 0,
        0, 60, 892, 26, IDC_N_PROG, NULL);
    ProgSet(hNetProg, 0);
    hNetPing = MkLabel(p, 0, 96, 200, 34, g_hFontBig);
    hNetDown = MkLabel(p, 220, 96, 300, 34, g_hFontBig);
    hNetUp = MkLabel(p, 540, 96, 300, 34, g_hFontBig);
    SetLabel(hNetPing, WFormat(L"%s: --", T(SID_N_PING)), LR_TEXT);
    SetLabel(hNetDown, WFormat(L"%s: --", T(SID_N_DOWN)), LR_TEXT);
    SetLabel(hNetUp, WFormat(L"%s: --", T(SID_N_UP)), LR_TEXT);
    hNetIp = MkLabel(p, 0, 136, 440, 26);
    SetLabel(hNetIp, WFormat(L"%s: ...", T(SID_N_IP)), LR_MUTED);
    hNetGrade = MkLabel(p, 452, 132, 440, 34, g_hFontBig);
    SetLabel(hNetGrade, WFormat(L"%s: --", T(SID_N_GRADE)), LR_TEXT);
    hNetStart = MkCTA(p, IDC_N_START, 0, 176, 300, 54, SID_N_START);
    hNetCancel = MkButton(p, IDC_N_CANCEL, 320, 176, 200, 54, SID_N_CANCEL);
    EnableWindow(hNetCancel, FALSE);
    MkHeader(p, 0, 248, 400, SID_N_DNS_T);
    hNetDnsSt = MkLabel(p, 0, 278, 892, 26);
    hNetDns = MkCombo(p, IDC_N_DNSLIST, 0, 308, 300);
    SendMessageW(hNetDns, CB_ADDSTRING, 0, (LPARAM)T(SID_N_DNS_AUTO));
    SendMessageW(hNetDns, CB_ADDSTRING, 0, (LPARAM)T(SID_N_DNS_CF));
    SendMessageW(hNetDns, CB_ADDSTRING, 0, (LPARAM)T(SID_N_DNS_GOOG));
    SendMessageW(hNetDns, CB_ADDSTRING, 0, (LPARAM)T(SID_N_DNS_Q9));
    SendMessageW(hNetDns, CB_ADDSTRING, 0, (LPARAM)T(SID_N_DNS_ODNS));
    SendMessageW(hNetDns, CB_ADDSTRING, 0, (LPARAM)T(SID_N_DNS_SHECAN));
    SendMessageW(hNetDns, CB_ADDSTRING, 0, (LPARAM)T(SID_N_DNS_CUSTOMITEM));
    SendMessageW(hNetDns, CB_SETCURSEL, 0, 0);
    hNetDnsApply = MkButton(p, IDC_N_DNSAPPLY, 310, 306, 110, 34, SID_S_RES_APPLY);
    hNetPingAll = MkButton(p, IDC_N_PINGALL, 430, 306, 130, 34, SID_N_DNS_PING);
    hNetFastest = MkButton(p, IDC_N_FASTEST, 570, 306, 200, 34, SID_N_DNS_FASTEST);
    EnableWindow(hNetFastest, FALSE);
    hNetPingRes = MkEdit(p, IDC_N_PINGRES, 0, 348, 892, 96, true, true);
    HWND lc = MkLabel(p, 0, 452, 140, 28); SetWindowTextW(lc, T(SID_N_DNS_CUSTOM));
    hNetC1 = MkEdit(p, IDC_N_C1, 150, 450, 170, 28);
    hNetC2 = MkEdit(p, IDC_N_C2, 330, 450, 170, 28);
    HWND cset = MkButton(p, IDC_N_CSET, 510, 448, 130, 34, SID_N_DNS_SET);
    if (!g_isAdmin) {
        EnableWindow(hNetDnsApply, FALSE); EnableWindow(hNetFastest, FALSE);
        EnableWindow(cset, FALSE);
    }
    SetLabel(hNetDnsSt, WFormat(L"%s %s", T(SID_N_DNS_CUR), DnsCurrent().c_str()), LR_TEXT);
}

// ---------- AI advisor ----------
static void AiAppendBlock(const std::wstring& block) {
    int len = GetWindowTextLengthW(hAiOut);
    SendMessageW(hAiOut, EM_SETSEL, (WPARAM)len, (LPARAM)len);
    SendMessageW(hAiOut, EM_REPLACESEL, 0, (LPARAM)block.c_str());
    SendMessageW(hAiOut, EM_SETSEL, (WPARAM)-1, (LPARAM)-1);
    SendMessageW(hAiOut, EM_SCROLLCARET, 0, 0);
}
static void AiAsk() {
    wchar_t q[1024];
    GetWindowTextW(hAiInput, q, 1024);
    size_t a = 0, b = wcslen(q);
    while (a < b && iswspace(q[a])) a++;
    while (b > a && iswspace(q[b - 1])) b--;
    if (a >= b) { SetLabel(hAiStatus, T(SID_A_HINT), LR_YELLOW); return; }
    q[b] = 0;
    std::wstring qq = q + a;
    SetWindowTextW(hAiInput, L"");
    if (g_aiOnline && Ai_NeedsOnline(qq)) {
        AiAppendBlock(WFormat(L"%s: %s\r\n", T(SID_A_YOU), qq.c_str()));
        g_aiPending = qq;
        SetLabel(hAiStatus, T(SID_A_THINK), LR_ACCENT);
        AiOnline_AskAsync(g_hMain, qq);
    } else {
        std::wstring ans = Ai_Answer(qq);
        AiAppendBlock(WFormat(L"%s: %s\r\n%s\r\n\r\n", T(SID_A_YOU), qq.c_str(), ans.c_str()));
        SetLabel(hAiStatus, T(SID_STATUS_READY), LR_MUTED);
    }
}
static void AiOnlineDone(bool ok) {
    std::wstring a;
    if (ok && AiOnline_TakeResult(a)) {
        AiAppendBlock(WFormat(L"%s (%s):\r\n%s\r\n\r\n", T(SID_NAV_AI), T(SID_A_ONTAG), a.c_str()));
    } else {
        AiOnline_TakeResult(a);
        std::wstring fb = Ai_Answer(g_aiPending);
        AiAppendBlock(WFormat(L"%s:\r\n%s\r\n\r\n", T(SID_NAV_AI), fb.c_str()));
    }
    SetLabel(hAiStatus, T(SID_STATUS_READY), LR_MUTED);
}
static void BuildAI(HWND p) {
    hAiInput = MkEdit(p, IDC_A_INPUT, 0, 0, 700, 32);
    hAiAsk = MkCTA(p, IDC_A_ASK, 710, 0, 160, 32, SID_A_ASK);
    hAiQ1 = MkButton(p, IDC_A_Q1, 0, 40, 280, 30, SID_A_Q1);
    hAiQ2 = MkButton(p, IDC_A_Q2, 290, 40, 280, 30, SID_A_Q2);
    hAiQ3 = MkButton(p, IDC_A_Q3, 580, 40, 280, 30, SID_A_Q3);
    hAiHint = MkLabel(p, 0, 76, 860, 22);
    SetLabel(hAiHint, T(SID_A_HINT), LR_MUTED);
    hAiOut = MkEdit(p, IDC_A_OUT, 0, 100, 860, 440, true, true);
    hAiStatus = MkLabel(p, 0, 548, 860, 24);
    SetLabel(hAiStatus, T(SID_STATUS_READY), LR_MUTED);
    hAiOnline = MkCheck(p, IDC_A_ONLINE, 660, 76, 200, SID_A_ONLINE);
    SetChecked(hAiOnline, g_aiOnline);
    SetWindowTextW(hAiOut, Ai_Answer(L"").c_str());
}

// ---------- Live processes ----------
static DWORD ProcSelectedPid() {
    int sel = ListView_GetNextItem(hProcList, -1, LVNI_SELECTED);
    if (sel < 0) return 0;
    return (DWORD)LVGetData(hProcList, sel);
}
static void ProcFillList() {
    if (!hProcList) return;
    ListView_DeleteAllItems(hProcList);
    std::vector<ProcInfo> v;
    Proc_Enum(v);
    for (size_t i = 0; i < v.size(); i++) {
        std::wstring nm = v[i].name;
        if (v[i].isSelf) nm += WFormat(L" %s", T(SID_R_SELF));
        int row = LVAddRow(hProcList, (int)i, nm.c_str());
        LVSet(hProcList, row, 1, WFormat(L"%u", v[i].pid));
        LVSet(hProcList, row, 2, WFormat(L"%lu", (unsigned long)v[i].memMB));
        LVSet(hProcList, row, 3, v[i].isGame ? L"\U0001F3AE" : L"");
        LVSetData(hProcList, row, (LPARAM)v[i].pid);
    }
}
static void BuildProc(HWND p) {
    hProcList = MkList(p, IDC_R_LIST, 0, 0, 860, 440);
    int ids[] = {SID_R_COL_PROC, SID_R_COL_PID, SID_R_COL_RAM, SID_R_COL_GAME};
    int ww[] = {380, 90, 130, 120};
    LVCols(hProcList, ids, ww, 4);
    hProcHint = MkLabel(p, 0, 448, 860, 22);
    SetLabel(hProcHint, T(SID_R_HINT), LR_MUTED);
    MkButton(p, IDC_R_BOOST, 0, 476, 200, 34, SID_R_BOOST);
    MkButton(p, IDC_R_RAM, 210, 476, 200, 34, SID_R_RAMFOCUS);
    MkButton(p, IDC_R_RESTORE, 420, 476, 200, 34, SID_R_RESTORE);
    MkButton(p, IDC_R_REFRESH, 630, 476, 170, 34, SID_BTN_REFRESH);
    hProcStatus = MkLabel(p, 0, 518, 860, 24);
    SetLabel(hProcStatus, T(SID_STATUS_READY), LR_MUTED);
}

// ---------- Theme apply (live) ----------
static void Theme_ApplyLists() {
    for (size_t i = 0; i < g_lvWins.size(); i++) {
        HWND l = g_lvWins[i];
        if (!IsWindow(l)) continue;
        ListView_SetBkColor(l, COL_PANEL);
        ListView_SetTextColor(l, COL_TEXT);
        ListView_SetTextBkColor(l, COL_PANEL);
        InvalidateRect(l, NULL, TRUE);
    }
}
static void Theme_MakeBrushes() {
    if (hBrBg) DeleteObject(hBrBg);
    if (hBrPanel) DeleteObject(hBrPanel);
    if (hBrSide) DeleteObject(hBrSide);
    if (hBrCard) DeleteObject(hBrCard);
    if (hBrEdit) DeleteObject(hBrEdit);
    hBrBg = CreateSolidBrush(COL_BG);
    hBrPanel = CreateSolidBrush(COL_PANEL);
    hBrSide = CreateSolidBrush(COL_SIDE);
    hBrCard = CreateSolidBrush(COL_CARD);
    hBrEdit = CreateSolidBrush(COL_EDIT);
}
static void Theme_ApplyClasses() {
    if (g_hMain) SetClassLongPtrW(g_hMain, GCLP_HBRBACKGROUND, (LONG_PTR)hBrBg);
    if (hSide) SetClassLongPtrW(hSide, GCLP_HBRBACKGROUND, (LONG_PTR)hBrSide);
    for (int i = 0; i < PAGE_COUNT; i++)
        if (g_hPages[i]) SetClassLongPtrW(g_hPages[i], GCLP_HBRBACKGROUND, (LONG_PTR)hBrPanel);
}
static void Theme_UpdateBtnFace() {
    if (!hThemeBtn) return;
    SetWindowTextW(hThemeBtn, (g_themeMode == 2) ? L"\U0001F317" : (g_theme.dark ? L"\U0001F319" : L"\u2600\uFE0F"));
}
static void Theme_ApplyAll() {
    Theme_Resolve();
    Theme_MakeBrushes();
    Theme_ApplyClasses();
    Theme_ApplyLists();
    Theme_UpdateBtnFace();
    if (g_hMain) InvalidateRect(g_hMain, NULL, TRUE);
}
static void Hotkey_Apply() {
    if (!g_hMain) return;
    UnregisterHotKey(g_hMain, HOTKEY_BOOST_ID);
    if (g_hotkey) RegisterHotKey(g_hMain, HOTKEY_BOOST_ID, MOD_CONTROL | MOD_ALT, 'B');
}

// ---------- Page management ----------
static const int kPageOfNav[PAGE_COUNT] = {0, 1, 2, 3, 4, 5, 6};
void UI_ShowPage(int page) {
    if (page < 0 || page >= PAGE_COUNT) return;
    g_page = page;
    for (int i = 0; i < PAGE_COUNT; i++)
        ShowWindow(g_hPages[i], i == page ? SW_SHOW : SW_HIDE);
    static StrId titles[] = {SID_NAV_DASH, SID_NAV_AI, SID_NAV_GAMES, SID_NAV_PROC, SID_NAV_BOOST,
                             SID_NAV_TWEAKS, SID_NAV_SYSTEM, SID_NAV_POWER, SID_NAV_NET, SID_NAV_HELP, SID_NAV_SETTINGS};
    static StrId subs[] = {SID_DASH_SUB, SID_A_SUB, SID_G_SUB, SID_R_SUB, SID_B_SUB, SID_T_SUB, SID_S_SUB, SID_P_SUB, SID_N_SUB, SID_H_SUB, SID_SET_SUB};
    SetWindowTextW(hTitle, WFormat(L"%s   -   %s", T(titles[page]), T(subs[page])).c_str());
    InvalidateRect(hSide, NULL, TRUE);
    for (int i = 0; i < PAGE_COUNT; i++) {
        HWND b = GetDlgItem(hSide, IDC_NAV_BASE + i);
        if (b) InvalidateRect(b, NULL, TRUE);
    }
    switch (page) {
    case PAGE_DASH: DashRefresh(); break;
    case PAGE_GAMES: GamesFillList(); break;
    case PAGE_PROC: ProcFillList(); break;
    case PAGE_TWEAKS: TweaksFillList(); TweaksShowDetail(-1); break;
    case PAGE_SYSTEM: SysRefresh(); break;
    case PAGE_POWER: PowRefresh(); break;
    case PAGE_NET: NetRefresh(); break;
    case PAGE_SETTINGS: SettingsRefresh(); break;
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
        SetLabel(hGamesStatus, T(SID_G_PREP), LR_YELLOW);
        EnableWindow(hGamesLaunch, FALSE);
        break;
    case GPH_LAUNCHED:
    case GPH_INGAME:
        SetLabel(hGamesStatus, T(SID_G_RUNNING), LR_GREEN);
        GamesFillList();
        break;
    case GPH_DONE:
        SetLabel(hGamesStatus, T(SID_G_DONE), LR_TEXT);
        EnableWindow(hGamesLaunch, TRUE);
        GamesFillList();
        break;
    case GPH_ERROR:
        SetLabel(hGamesStatus, T(SID_MSG_FAIL), LR_RED);
        EnableWindow(hGamesLaunch, TRUE);
        break;
    }
}

// ---------- Owner drawing ----------
static void DrawProg(const DRAWITEMSTRUCT* d) {
    HDC dc = d->hDC;
    RECT r = d->rcItem;
    HBRUSH pb = CreateSolidBrush(COL_PANEL);
    FillRect(dc, &r, pb);
    DeleteObject(pb);
    int pct = (int)GetWindowLongPtrW(d->hwndItem, GWLP_USERDATA) - 100;
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    Graphics g(dc);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    int rad = (r.bottom - r.top) / 2 - S(2);
    if (rad < 2) rad = 2;
    GraphicsPath track;
    track.AddArc(r.left, r.top + S(2), rad * 2, rad * 2, 180, 90);
    track.AddArc(r.right - rad * 2 - 1, r.top + S(2), rad * 2, rad * 2, 270, 90);
    track.AddArc(r.right - rad * 2 - 1, r.bottom - S(2) - rad * 2, rad * 2, rad * 2, 0, 90);
    track.AddArc(r.left, r.bottom - S(2) - rad * 2, rad * 2, rad * 2, 90, 90);
    track.CloseFigure();
    COLORREF trackC = g_theme.dark ? COL_EDIT : RGB(216, 224, 234);
    SolidBrush tbr(ThColor(255, trackC));
    g.FillPath(&tbr, &track);
    Pen edge(Color(255, 60, 80, 120), 1);
    g.DrawPath(&edge, &track);
    if (pct > 0) {
        int fw = (r.right - r.left) * pct / 100;
        if (fw > 0) {
            g.SetClip(&track);
            LinearGradientBrush fbr(Point(r.left, r.top), Point(r.right, r.top),
                Color(255, 34, 211, 238), Color(255, 232, 121, 249));
            g.FillRectangle(&fbr, r.left, r.top, fw, r.bottom - r.top);
            g.ResetClip();
        }
    }
    wchar_t t[16];
    StringCchPrintfW(t, 16, L"%d%%", pct);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, COL_TEXT);
    HFONT old = (HFONT)SelectObject(dc, g_hFont);
    DrawTextW(dc, t, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(dc, old);
}
static void DrawCombo(const DRAWITEMSTRUCT* d) {
    HDC dc = d->hDC;
    RECT r = d->rcItem;
    bool edit = (d->itemID == (UINT)-1);
    bool sel = !edit && (d->itemState & ODS_SELECTED) != 0;
    bool cdis = (d->itemState & ODS_DISABLED) != 0;
    HBRUSH b = CreateSolidBrush(sel ? COL_SELBAR : COL_EDIT);
    FillRect(dc, &r, b);
    DeleteObject(b);
    if (edit) {
        HBRUSH fb = CreateSolidBrush(RGB(70, 90, 130));
        FrameRect(dc, &r, fb);
        DeleteObject(fb);
    }
    wchar_t txt[256]; txt[0] = 0;
    if (edit) GetWindowTextW(d->hwndItem, txt, 256);
    else if (d->itemID != (UINT)-1) SendMessageW(d->hwndItem, CB_GETLBTEXT, d->itemID, (LPARAM)txt);
    RECT tr = r; tr.left += S(6); tr.right -= S(4);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, cdis ? COL_MUTED : (sel ? (g_theme.dark ? RGB(255, 255, 255) : RGB(15, 23, 42)) : COL_TEXT));
    HFONT old = (HFONT)SelectObject(dc, g_hFont);
    DrawTextW(dc, txt, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    SelectObject(dc, old);
    if ((d->itemState & ODS_FOCUS) && !edit) DrawFocusRect(dc, &r);
}
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
    // blend with the sidebar gradient (parent has WS_CLIPCHILDREN, so paint our own bg)
    RECT pr; GetClientRect(GetParent(d->hwndItem), &pr);
    POINT pt = {0, 0};
    MapWindowPoints(d->hwndItem, GetParent(d->hwndItem), &pt, 1);
    int ph = pr.bottom > 1 ? pr.bottom : 1;
    int tt = (pt.y * 255) / ph;
    int br0 = GetRValue(g_theme.side), bg0 = GetGValue(g_theme.side), bb0 = GetBValue(g_theme.side);
    int br1 = GetRValue(g_theme.side2), bg1 = GetGValue(g_theme.side2), bb1 = GetBValue(g_theme.side2);
    HBRUSH bg = CreateSolidBrush(RGB(br0 + (br1 - br0) * tt / 255, bg0 + (bg1 - bg0) * tt / 255, bb0 + (bb1 - bb0) * tt / 255));
    FillRect(dc, &r, bg);
    DeleteObject(bg);
    if (sel) {
        Graphics g(dc);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        SolidBrush pill(ThColor(g_theme.dark ? (BYTE)56 : (BYTE)70, g_theme.accent));
        int L = r.left + S(2), T = r.top + S(3), R = r.right - S(2), B = r.bottom - S(3);
        int rad = (B - T) / 2 - S(2);
        if (rad < S(4)) rad = S(4);
        GraphicsPath path;
        path.AddArc(L, T, rad * 2, rad * 2, 180, 90);
        path.AddArc(R - rad * 2, T, rad * 2, rad * 2, 270, 90);
        path.AddArc(R - rad * 2, B - rad * 2, rad * 2, rad * 2, 0, 90);
        path.AddArc(L, B - rad * 2, rad * 2, rad * 2, 90, 90);
        path.CloseFigure();
        g.FillPath(&pill, &path);
        RECT bar = {r.left + S(8), r.top + S(8), r.left + S(12), r.bottom - S(8)};
        HBRUSH ab = CreateSolidBrush(COL_ACCENT);
        FillRect(dc, &bar, ab);
        DeleteObject(ab);
    }
    // dot
    RECT dot = {r.left + S(18), (r.top + r.bottom - S(8)) / 2, r.left + S(26), (r.top + r.bottom + S(8)) / 2};
    HBRUSH db = CreateSolidBrush(sel ? COL_ACCENT : COL_MUTED);
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
    bool cdis = (d->itemState & ODS_DISABLED) != 0;
    int bs = S(18);
    RECT box = {r.left, (r.top + r.bottom - bs) / 2, r.left + bs, (r.top + r.bottom + bs) / 2};
    HBRUSH bb = CreateSolidBrush(checked && !cdis ? COL_SELBAR : COL_EDIT);
    FillRect(dc, &box, bb);
    DeleteObject(bb);
    HBRUSH fb = CreateSolidBrush(cdis ? RGB(90, 100, 130) : (checked ? COL_ACCENT : RGB(120, 140, 175)));
    FrameRect(dc, &box, fb);
    DeleteObject(fb);
    if (checked) {
        int pw = S(2); if (pw < 2) pw = 2;
        HPEN cp = CreatePen(PS_SOLID, pw, cdis ? RGB(140, 150, 170) : COL_ACCENT);
        HPEN ocp = (HPEN)SelectObject(dc, cp);
        int qx = box.left + bs / 4, qy = box.top + bs / 2;
        MoveToEx(dc, qx, qy, NULL);
        LineTo(dc, qx + bs / 5, qy + bs / 5);
        LineTo(dc, box.right - bs / 5, box.top + bs / 4);
        SelectObject(dc, ocp);
        DeleteObject(cp);
    }
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
    COLORREF cpuC = g_theme.dark ? RGB(34, 211, 238) : RGB(2, 132, 199);
    COLORREF ramC = g_theme.dark ? RGB(232, 121, 249) : RGB(192, 38, 211);
    Pen grid(g_theme.dark ? Color(60, 50, 70, 110) : Color(60, 148, 163, 184), 1);
    for (int i = 1; i < 4; i++) {
        int y = r.top + H * i / 4;
        g.DrawLine(&grid, r.left, y, r.right, y);
    }
    if (g_histN > 1) {
        Pen pCpu(ThColor(255, cpuC), 2), pRam(ThColor(255, ramC), 2);
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
    SetTextColor(dc, cpuC);
    RECT l1 = {r.left + 8, r.top + 6, r.left + 200, r.top + 28};
    DrawTextW(dc, WFormat(L"CPU %d%%", g_histN ? g_cpuHist[g_histN-1] : 0).c_str(), -1, &l1, DT_LEFT);
    SetTextColor(dc, ramC);
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
    if (g_boosting || g_inGame || Games_IsBusy()) return;
    Strings_SetLang(lang);
    Settings_Save();
    Games_Save();
    BackupSave();
    // relaunch into the new language (release the mutex so the new instance starts)
    if (g_mutex) { ReleaseMutex(g_mutex); CloseHandle(g_mutex); g_mutex = NULL; }
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(NULL, exe, MAX_PATH);
    HINSTANCE rr = ShellExecuteW(NULL, L"open", exe, NULL, NULL, SW_SHOWNORMAL);
    if ((INT_PTR)rr <= 32) {
        g_mutex = CreateMutexW(NULL, TRUE, APP_MUTEX); // relaunch failed: stay running
    } else {
        DestroyWindow(g_hMain);
    }
}

// ---------- Update check (GitHub releases) ----------
static LONG g_updBusy = 0;
static bool VerNewer(const std::wstring& tag) {
    size_t i = 0;
    while (i < tag.size() && (tag[i] == L'v' || tag[i] == L'V' || tag[i] == L' ')) i++;
    int tn[3] = {0, 0, 0}, an[3] = {0, 0, 0};
    swscanf(tag.c_str() + i, L"%d.%d.%d", &tn[0], &tn[1], &tn[2]);
    swscanf(APP_VER, L"%d.%d.%d", &an[0], &an[1], &an[2]);
    for (int k = 0; k < 3; k++) {
        if (tn[k] != an[k]) return tn[k] > an[k];
    }
    return false;
}
static DWORD WINAPI UpdateThread(LPVOID arg) {
    HWND w = (HWND)arg;
    int code = 0; // 0 fail, 1 latest, 2 new version
    std::wstring ver;
    HINTERNET hs = WinHttpOpen(L"FPSBooster/2.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (hs) {
        WinHttpSetTimeouts(hs, 8000, 8000, 8000, 15000);
        HINTERNET hc = WinHttpConnect(hs, L"api.github.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
        if (hc) {
            HINTERNET hr = WinHttpOpenRequest(hc, L"GET",
                L"/repos/amirragaby110-glitch/fps-boost/releases/latest",
                NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
            if (hr) {
                WinHttpAddRequestHeaders(hr, L"User-Agent: FPSBooster", (ULONG)-1, WINHTTP_ADDREQ_FLAG_ADD);
                if (WinHttpSendRequest(hr, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
                    WinHttpReceiveResponse(hr, NULL)) {
                    std::string body;
                    DWORD av = 0, rd = 0;
                    char buf[4096];
                    while (WinHttpQueryDataAvailable(hr, &av) && av > 0) {
                        DWORD chunk = av > sizeof(buf) ? sizeof(buf) : av;
                        if (!WinHttpReadData(hr, buf, chunk, &rd) || rd == 0) break;
                        body.append(buf, rd);
                        if (body.size() > 65536) break;
                    }
                    std::wstring wb = Utf8ToWide(body.c_str());
                    size_t q = wb.find(L"\"tag_name\"");
                    if (q != std::wstring::npos) {
                        size_t q1 = wb.find(L'"', q + 10);
                        size_t q2 = q1 == std::wstring::npos ? q1 : wb.find(L'"', q1 + 1);
                        if (q1 != std::wstring::npos && q2 != std::wstring::npos) {
                            ver = wb.substr(q1 + 1, q2 - q1 - 1);
                            code = VerNewer(ver) ? 2 : 1;
                        }
                    }
                }
                WinHttpCloseHandle(hr);
            }
            WinHttpCloseHandle(hc);
        }
        WinHttpCloseHandle(hs);
    }
    wchar_t* out = NULL;
    if (!ver.empty()) {
        out = new wchar_t[ver.size() + 1];
        StringCchCopyW(out, ver.size() + 1, ver.c_str());
    }
    if (IsWindow(w)) PostMessageW(w, WM_APP_UPDATE, (WPARAM)code, (LPARAM)out);
    else if (out) delete[] out;
    InterlockedExchange(&g_updBusy, 0);
    return 0;
}
static void Update_CheckAsync(HWND w) {
    if (InterlockedCompareExchange(&g_updBusy, 1, 0) != 0) return;
    HANDLE th = CreateThread(NULL, 0, UpdateThread, w, 0, NULL);
    if (th) CloseHandle(th);
    else InterlockedExchange(&g_updBusy, 0);
}

LRESULT CALLBACK UI_MainProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_ERASEBKGND: {
        RECT r; GetClientRect(h, &r);
        PaintPageBg((HDC)w, r.right - r.left, r.bottom - r.top);
        return 1;
    }
    case WM_CTLCOLORSTATIC: {
        HDC dc = (HDC)w;
        HWND c = (HWND)l;
        SetBkMode(dc, TRANSPARENT);
        int role = LR_TEXT;
        for (size_t i = 0; i < g_colors.size(); i++)
            if (g_colors[i].h == c) { role = g_colors[i].role; break; }
        SetTextColor(dc, RoleColor(role));
        return (LRESULT)hBrPanel;
    }
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX: {
        HDC dc = (HDC)w;
        SetBkMode(dc, OPAQUE);
        SetTextColor(dc, COL_TEXT);
        SetBkColor(dc, COL_EDIT);
        return (LRESULT)hBrEdit;
    }
    case WM_MEASUREITEM: {
        LPMEASUREITEMSTRUCT m = (LPMEASUREITEMSTRUCT)l;
        if (m && m->CtlType == ODT_COMBOBOX) m->itemHeight = (UINT)S(24);
        return TRUE;
    }
    case WM_DRAWITEM: {
        const DRAWITEMSTRUCT* d = (const DRAWITEMSTRUCT*)l;
        if (!d) break;
        if (d->CtlType == ODT_COMBOBOX) { DrawCombo(d); return TRUE; }
        int id = (int)d->CtlID;
        if (id >= IDC_NAV_BASE && id < IDC_NAV_BASE + PAGE_COUNT) { DrawNav(d); return TRUE; }
        if (id == IDC_S_GRAPH) { DrawGraph(d); return TRUE; }
        if (id == IDC_DASH_PIC) { DrawDashPic(d); return TRUE; }
        if (id == IDC_B_PROG || id == IDC_N_PROG) { DrawProg(d); return TRUE; }
        if (GetWindowLongPtrW(d->hwndItem, GWLP_USERDATA) == 1) { DrawCheckBtn(d); return TRUE; }
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
        case IDC_THEME_BTN:
            g_themeMode = (g_themeMode == 0) ? 1 : 0;
            Settings_Save();
            Theme_ApplyAll();
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
        case IDC_G_AUTO:
            if (code == BN_CLICKED) {
                HWND b = (HWND)l;
                SetChecked(b, !IsChecked(b));
                InvalidateRect(b, NULL, TRUE);
                g_autoBoost = IsChecked(b);
                Settings_Save();
                if (g_autoBoost) AutoBoostStart(g_hMain);
                else AutoBoostStop();
                if (g_autoBoost) SetLabel(hGamesAutoSt, T(SID_G_AUTO_WATCH), LR_MUTED);
                else SetLabel(hGamesAutoSt, L"", LR_MUTED);
            }
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
        case IDC_A_ONLINE:
            if (code == BN_CLICKED) {
                SetChecked(hAiOnline, !IsChecked(hAiOnline));
                InvalidateRect(hAiOnline, NULL, TRUE);
                g_aiOnline = IsChecked(hAiOnline);
                Settings_Save();
            }
            break;
        case IDC_A_ASK: AiAsk(); break;
        case IDC_A_Q1: SetWindowTextW(hAiInput, T(SID_A_Q1)); AiAsk(); break;
        case IDC_A_Q2: SetWindowTextW(hAiInput, T(SID_A_Q2)); AiAsk(); break;
        case IDC_A_Q3: SetWindowTextW(hAiInput, T(SID_A_Q3)); AiAsk(); break;
        case IDC_R_REFRESH: ProcFillList(); break;
        case IDC_R_BOOST: {
            DWORD pid = ProcSelectedPid();
            if (!pid) { SetLabel(hProcStatus, T(SID_R_HINT), LR_YELLOW); break; }
            std::wstring m; bool ok = Proc_Boost(pid, m);
            SetLabel(hProcStatus, m, ok ? LR_GREEN : LR_RED);
            ProcFillList();
            break;
        }
        case IDC_R_RAM: {
            DWORD pid = ProcSelectedPid();
            if (!pid) { SetLabel(hProcStatus, T(SID_R_HINT), LR_YELLOW); break; }
            std::wstring m; bool ok = Proc_RamFocus(pid, m);
            SetLabel(hProcStatus, m, ok ? LR_GREEN : LR_RED);
            ProcFillList();
            break;
        }
        case IDC_R_RESTORE: {
            DWORD pid = ProcSelectedPid();
            if (!pid) { SetLabel(hProcStatus, T(SID_R_HINT), LR_YELLOW); break; }
            bool ok = Proc_Restore(pid);
            SetLabel(hProcStatus, WFormat(L"%u - %s", pid, T(SID_BTN_REVERT)), ok ? LR_GREEN : LR_RED);
            ProcFillList();
            break;
        }
        case IDC_B_START:
            if (!g_boosting && !g_inGame) {
                g_boosting = true;
                EnableWindow(hBoostStart, FALSE);
                EnableWindow(hBoostUndo, FALSE);
                EnableWindow(hBoostMax, FALSE);
                SendMessageW(hBoostLog, LB_RESETCONTENT, 0, 0);
                SetWindowTextW(hStatusBar, T(SID_B_WORKING));
                BoostRun(g_hMain);
            }
            break;
        case IDC_B_MAX:
            if (!g_boosting && !g_inGame) {
                if (MessageBoxW(h, T(SID_B_MAXFPS_WARN), T(SID_APP_NAME), MB_YESNO | MB_ICONWARNING) == IDYES) {
                    g_boosting = true;
                    EnableWindow(hBoostStart, FALSE);
                    EnableWindow(hBoostUndo, FALSE);
                    EnableWindow(hBoostMax, FALSE);
                    SendMessageW(hBoostLog, LB_RESETCONTENT, 0, 0);
                    SetWindowTextW(hStatusBar, T(SID_B_WORKING));
                    MaxFpsRun(g_hMain);
                }
            }
            break;
        case IDC_B_UNDO:
            if (!g_boosting && !g_inGame) {
                if (MessageBoxW(h, T(SID_B_CONFIRM_UNDO), T(SID_APP_NAME), MB_YESNO | MB_ICONQUESTION) == IDYES) {
                    g_boosting = true;
                    EnableWindow(hBoostStart, FALSE);
                    EnableWindow(hBoostUndo, FALSE);
                    SendMessageW(hBoostLog, LB_RESETCONTENT, 0, 0);
                    EnableWindow(hBoostMax, FALSE);
                    if (MaxFpsIsActive()) MaxFpsUndo(g_hMain);
                    else BoostUndo(g_hMain);
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
        case IDC_S_RESAPPLY: {
            int si = (int)SendMessageW(hSysRes, CB_GETCURSEL, 0, 0);
            wchar_t b[64]; b[0] = 0;
            SendMessageW(hSysRes, CB_GETLBTEXT, si, (LPARAM)b);
            int w2 = 0, h2 = 0;
            if (swscanf(b, L"%dx%d", &w2, &h2) == 2 && SetDisplayResolution(w2, h2))
                SetWindowTextW(hStatusBar, T(SID_B_DONE));
            else
                SetWindowTextW(hStatusBar, T(SID_B_FAIL));
            break;
        }
        case IDC_S_RESNATIVE: {
            int w2 = 0, h2 = 0;
            if (GetNativeResolution(w2, h2) && SetDisplayResolution(w2, h2)) {
                wchar_t b[64];
                swprintf(b, 64, L"%dx%d", w2, h2);
                int si = (int)SendMessageW(hSysRes, CB_FINDSTRINGEXACT, (WPARAM)-1, (LPARAM)b);
                if (si >= 0) SendMessageW(hSysRes, CB_SETCURSEL, si, 0);
                SetWindowTextW(hStatusBar, T(SID_B_DONE));
            } else SetWindowTextW(hStatusBar, T(SID_B_FAIL));
            break;
        }
        case IDC_S_COPY:
            if (CopyTextToClipboard(SysSummary())) SetWindowTextW(hStatusBar, T(SID_S_COPIED));
            else SetWindowTextW(hStatusBar, T(SID_B_FAIL));
            break;
        case IDC_SET_LANG:
            if (code == CBN_SELCHANGE)
                SetLangAndAsk((int)SendMessageW(hSetLang, CB_GETCURSEL, 0, 0));
            break;
        case IDC_SET_THEME:
            if (code == CBN_SELCHANGE) {
                g_themeMode = (int)SendMessageW(hSetTheme, CB_GETCURSEL, 0, 0);
                if (g_themeMode < 0 || g_themeMode > 2) g_themeMode = 2;
                Settings_Save();
                Theme_ApplyAll();
            }
            break;
        case IDC_SET_ACCENT:
            if (code == CBN_SELCHANGE) {
                g_accent = (int)SendMessageW(hSetAccent, CB_GETCURSEL, 0, 0);
                if (g_accent < 0 || g_accent > 3) g_accent = 0;
                Settings_Save();
                Theme_ApplyAll();
            }
            break;
        case IDC_SET_HOTKEY:
            if (code == BN_CLICKED) {
                SetChecked(hSetHotkey, !IsChecked(hSetHotkey));
                InvalidateRect(hSetHotkey, NULL, TRUE);
                g_hotkey = IsChecked(hSetHotkey);
                Settings_Save();
                Hotkey_Apply();
            }
            break;
        case IDC_SET_UPDATE:
            Update_CheckAsync(g_hMain);
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
        case IDC_SET_STEN:
        case IDC_SET_STDIS: {
            if (g_boosting) break;
            int sel = ListView_GetNextItem(hSetStList, -1, LVNI_SELECTED);
            if (sel < 0) break;
            size_t i = (size_t)LVGetData(hSetStList, sel);
            if (i >= g_stItems.size()) break;
            bool on = (id == IDC_SET_STEN);
            if (StartupSetEnabled(g_stItems[i], on)) StartupFillList();
            else SetWindowTextW(hStatusBar, T(SID_B_FAIL));
            break;
        }
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
        case IDC_P_REFRESH:
            PowRefresh();
            break;
        case IDC_P_BEAST:
            if (!g_boosting && !Games_IsBusy()) {
                if (BeastIsActive()) BeastDeactivate();
                else BeastActivate();
                int ap2 = 0, tt2 = 0; BeastLastOp(ap2, tt2);
                if (tt2 > 0) SetWindowTextW(hStatusBar, WFormat(L"Beast: %d/%d %s", ap2, tt2, T(SID_P_APPLIED)).c_str());
                PowRefresh();
            }
            break;
        case IDC_P_APPLYALL:
            if (!g_boosting) {
                int tot = 0; PowerSettings(&tot);
                int n = PowerApplyAllActive();
                SetWindowTextW(hStatusBar, WFormat(L"%d/%d %s", n, tot, T(SID_P_APPLIED)).c_str());
                PowRefresh();
            }
            break;
        case IDC_P_RESTORE:
            if (!g_boosting) { PowerRestoreAllActive(); PowRefresh(); SetWindowTextW(hStatusBar, T(SID_MSG_DONE)); }
            break;
        case IDC_P_DELETE:
            if (!g_boosting && !Games_IsBusy()) { BeastDelete(); PowRefresh(); }
            break;
        case IDC_N_START:
            if (!NetTestBusy()) {
                SetLabel(hNetStatus, T(SID_N_TESTING), LR_YELLOW);
                SetLabel(hNetPing, WFormat(L"%s: ...", T(SID_N_PING)), LR_TEXT);
                SetLabel(hNetDown, WFormat(L"%s: ...", T(SID_N_DOWN)), LR_TEXT);
                SetLabel(hNetUp, WFormat(L"%s: ...", T(SID_N_UP)), LR_TEXT);
                SetLabel(hNetGrade, WFormat(L"%s: --", T(SID_N_GRADE)), LR_TEXT);
                ProgSet(hNetProg, 0);
                EnableWindow(hNetStart, FALSE);
                EnableWindow(hNetCancel, TRUE);
                NetTestRun(g_hMain);
            }
            break;
        case IDC_N_CANCEL:
            NetTestCancel();
            break;
        case IDC_N_DNSAPPLY: {
            int si = (int)SendMessageW(hNetDns, CB_GETCURSEL, 0, 0);
            if (si >= 0 && si <= 5) {
                DnsPingCancel();
                if (DnsSetPreset(si)) SetLabel(hNetDnsSt, WFormat(L"%s %s", T(SID_N_DNS_CUR), DnsCurrent().c_str()), LR_GREEN);
            } else if (si == 6) {
                wchar_t d1[64], d2[64]; d1[0] = 0; d2[0] = 0;
                GetWindowTextW(hNetC1, d1, 64); GetWindowTextW(hNetC2, d2, 64);
                if (DnsSetCustom(d1, d2)) SetLabel(hNetDnsSt, WFormat(L"%s %s", T(SID_N_DNS_CUR), DnsCurrent().c_str()), LR_GREEN);
            }
            break;
        }
        case IDC_N_PINGALL:
            if (!DnsPingBusy()) {
                for (int i = 0; i < 5; i++) g_dnsPingMs[i] = -1;
                SetWindowTextW(hNetPingRes, T(SID_N_PINGING));
                EnableWindow(hNetFastest, FALSE);
                DnsPingAll(g_hMain);
            }
            break;
        case IDC_N_FASTEST: {
            int bi = -1;
            for (int i = 0; i < 5; i++)
                if (g_dnsPingMs[i] >= 0 && (bi < 0 || g_dnsPingMs[i] < g_dnsPingMs[bi])) bi = i;
            if (bi >= 0) {
                SendMessageW(hNetDns, CB_SETCURSEL, bi + 1, 0);
                if (DnsSetPreset(bi + 1)) SetLabel(hNetDnsSt, WFormat(L"%s %s", T(SID_N_DNS_CUR), DnsCurrent().c_str()), LR_GREEN);
            }
            break;
        }
        case IDC_N_CSET: {
            wchar_t d1[64], d2[64]; d1[0] = 0; d2[0] = 0;
            GetWindowTextW(hNetC1, d1, 64); GetWindowTextW(hNetC2, d2, 64);
            if (DnsSetCustom(d1, d2)) {
                SendMessageW(hNetDns, CB_SETCURSEL, 6, 0);
                SetLabel(hNetDnsSt, WFormat(L"%s %s", T(SID_N_DNS_CUR), DnsCurrent().c_str()), LR_GREEN);
            }
            break;
        }
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
                    if (sel) { cd->clrText = g_theme.dark ? RGB(255,255,255) : RGB(15,23,42); cd->clrTextBk = COL_SELBAR; }
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
                    SetTextColor(dc, RoleColor(LR_ACCENT));
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
    case WM_APP_NET: {
        int phase = (int)w;
        if (phase == 0) SetLabel(hNetPing, WFormat(L"%s: %d ms", T(SID_N_PING), (int)l), LR_GREEN);
        else if (phase == 1) {
            double* d = (double*)l;
            if (d) { SetLabel(hNetDown, WFormat(L"%s: %.1f Mbps", T(SID_N_DOWN), *d), LR_TEXT); delete d; }
            ProgSet(hNetProg, 30);
        }
        else if (phase == 2) ProgSet(hNetProg, 55);
        else if (phase == 3) {
            double* d = (double*)l;
            if (d) { SetLabel(hNetUp, WFormat(L"%s: %.1f Mbps", T(SID_N_UP), *d), LR_TEXT); delete d; }
            ProgSet(hNetProg, 80);
        }
        else if (phase == 4) ProgSet(hNetProg, 90);
        else if (phase == 5) {
            wchar_t* ip = (wchar_t*)l;
            if (ip) { SetLabel(hNetIp, WFormat(L"%s: %s", T(SID_N_IP), ip), LR_TEXT); delete[] ip; }
        }
        else if (phase == 6) {
            static StrId g[] = {SID_N_GR0, SID_N_GR1, SID_N_GR2, SID_N_GR3};
            static int c[] = {LR_GREEN, LR_GREEN, LR_YELLOW, LR_RED};
            int gi = (int)l;
            if (gi < 0) gi = 0; if (gi > 3) gi = 3;
            SetLabel(hNetGrade, WFormat(L"%s: %s", T(SID_N_GRADE), T(g[gi])), c[gi]);
            SetLabel(hNetStatus, T(SID_B_DONE), LR_GREEN);
            ProgSet(hNetProg, 100);
            EnableWindow(hNetStart, TRUE);
            EnableWindow(hNetCancel, FALSE);
        }
        else if (phase == 7) {
            SetLabel(hNetStatus, T(SID_N_ERR), LR_RED);
            EnableWindow(hNetStart, TRUE);
            EnableWindow(hNetCancel, FALSE);
        }
        else if (phase >= 10 && phase <= 14) {
            int idx = phase - 10;
            int ms = (int)l;
            g_dnsPingMs[idx] = ms;
            static StrId dn[] = {SID_N_DNS_CF, SID_N_DNS_GOOG, SID_N_DNS_Q9, SID_N_DNS_ODNS, SID_N_DNS_SHECAN};
            wchar_t cur[1024]; cur[0] = 0;
            GetWindowTextW(hNetPingRes, cur, 1024);
            std::wstring s = cur;
            if (s == T(SID_N_PINGING)) s.clear();
            if (!s.empty()) s += L"\r\n";
            s += WFormat(L"%s: %s", T(dn[idx]), ms >= 0 ? WFormat(L"%d ms", ms).c_str() : L"--");
            SetWindowTextW(hNetPingRes, s.c_str());
        }
        else if (phase == 15) {
            int bi = -1;
            for (int i = 0; i < 5; i++)
                if (g_dnsPingMs[i] >= 0 && (bi < 0 || g_dnsPingMs[i] < g_dnsPingMs[bi])) bi = i;
            if (bi >= 0) {
                wchar_t cur[1024]; cur[0] = 0;
                GetWindowTextW(hNetPingRes, cur, 1024);
                std::wstring s = cur;
                s += WFormat(L"\r\n* %s", T(SID_N_DNS_FASTEST));
                SetWindowTextW(hNetPingRes, s.c_str());
                if (g_isAdmin) EnableWindow(hNetFastest, TRUE);
            }
        }
        break;
    }
    case WM_APP_AUTO: {
        wchar_t* names = (wchar_t*)l;
        int count = (int)w;
        if (count <= 0) SetLabel(hGamesAutoSt, T(SID_G_AUTO_WATCH), LR_MUTED);
        else SetLabel(hGamesAutoSt, WFormat(L"%s %s", T(SID_G_AUTO_BOOST), names ? names : L""), LR_GREEN);
        if (names) delete[] names;
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
    case WM_APP_AI:
        AiOnlineDone(w == 1);
        break;
    case WM_APP_UPDATE: {
        wchar_t* v = (wchar_t*)l;
        int ucode = (int)w;
        if (ucode == 2 && v) MessageBoxW(h, WFormat(T(SID_UPD_NEW), v).c_str(), T(SID_APP_NAME), MB_OK | MB_ICONINFORMATION);
        else if (ucode == 1) MessageBoxW(h, WFormat(T(SID_UPD_LATEST), T(SID_VER)).c_str(), T(SID_APP_NAME), MB_OK | MB_ICONINFORMATION);
        else MessageBoxW(h, T(SID_UPD_FAIL), T(SID_APP_NAME), MB_OK | MB_ICONWARNING);
        if (v) delete[] v;
        break;
    }
    case WM_HOTKEY:
        if (w == HOTKEY_BOOST_ID) {
            UI_TrayShow(true);
            UI_ShowPage(PAGE_BOOST);
            SendMessageW(h, WM_COMMAND, MAKEWPARAM(IDC_B_START, BN_CLICKED), 0);
        }
        break;
    case WM_SETTINGCHANGE:
        if (g_themeMode == 2 && l && !wcscmp((LPCWSTR)l, L"ImmersiveColorSet"))
            Theme_ApplyAll();
        break;
    case WM_TIMER:
        if (w == 1 && g_page == PAGE_SYSTEM) SysRefresh();
        break;
    case WM_SIZE: {
        int W = LOWORD(l), H = HIWORD(l);
        if (!hSide) break;
        MoveWindow(hSide, 0, 0, S(240), H, TRUE);
        MoveWindow(hTitle, S(264), S(14), W - S(264) - S(410), S(34), TRUE);
        MoveWindow(hThemeBtn, W - S(386), S(12), S(110), S(32), TRUE);
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
        MoveWindow(hAiInput, 0, 0, pw - S(170), S(32), TRUE);
        MoveWindow(hAiAsk, pw - S(160), 0, S(160), S(32), TRUE);
        { int qw = (pw - S(20)) / 3;
        MoveWindow(hAiQ1, 0, S(40), qw, S(30), TRUE);
        MoveWindow(hAiQ2, qw + S(10), S(40), qw, S(30), TRUE);
        MoveWindow(hAiQ3, qw * 2 + S(20), S(40), pw - qw * 2 - S(20), S(30), TRUE); }
        MoveWindow(hAiHint, 0, S(76), pw - S(220), S(22), TRUE);
        MoveWindow(hAiOnline, pw - S(210), S(74), S(210), S(26), TRUE);
        MoveWindow(hAiOut, 0, S(100), pw, ph - S(100) - S(32), TRUE);
        MoveWindow(hAiStatus, 0, ph - S(26), pw, S(24), TRUE);
        MoveWindow(hProcList, 0, 0, pw, ph - S(150), TRUE);
        MoveWindow(hProcHint, 0, ph - S(142), pw, S(22), TRUE);
        MoveWindow(GetDlgItem(g_hPages[PAGE_PROC], IDC_R_BOOST), 0, ph - S(112), S(200), S(34), TRUE);
        MoveWindow(GetDlgItem(g_hPages[PAGE_PROC], IDC_R_RAM), S(210), ph - S(112), S(200), S(34), TRUE);
        MoveWindow(GetDlgItem(g_hPages[PAGE_PROC], IDC_R_RESTORE), S(420), ph - S(112), S(200), S(34), TRUE);
        MoveWindow(GetDlgItem(g_hPages[PAGE_PROC], IDC_R_REFRESH), S(630), ph - S(112), S(170), S(34), TRUE);
        MoveWindow(hProcStatus, 0, ph - S(70), pw, S(24), TRUE);
        MoveWindow(hBoostProg, 0, S(114), pw, S(26), TRUE);
        MoveWindow(hBoostLog, 0, S(152), pw, ph - S(152) - S(8), TRUE);
        MoveWindow(hTweakList, 0, 0, pw, ph - S(230), TRUE);
        MoveWindow(hTweakDetail, 0, ph - S(220), pw, S(110), TRUE);
        int by = ph - S(100);
        MoveWindow(hTweakApply, 0, by, S(160), S(34), TRUE);
        MoveWindow(hTweakRevert, S(170), by, S(160), S(34), TRUE);
        MoveWindow(hTweakAll, S(340), by, S(230), S(34), TRUE);
        MoveWindow(hTweakUndoAll, S(580), by, S(160), S(34), TRUE);
        MoveWindow(hSysGraph, 0, S(92), pw, ph - S(92) - S(120), TRUE);
        MoveWindow(GetDlgItem(g_hPages[PAGE_SYSTEM], IDC_S_CLEANRAM), 0, ph - S(100), S(220), S(38), TRUE);
        MoveWindow(GetDlgItem(g_hPages[PAGE_SYSTEM], IDC_S_CLEANTEMP), S(232), ph - S(100), S(220), S(38), TRUE);
        MoveWindow(GetDlgItem(g_hPages[PAGE_SYSTEM], IDC_S_REFRESH), S(464), ph - S(100), S(240), S(38), TRUE);
        MoveWindow(GetDlgItem(g_hPages[PAGE_SYSTEM], IDC_S_COPY), S(716), ph - S(100), S(176), S(38), TRUE);
        MoveWindow(hHelpText, 0, 0, pw, ph - S(8), TRUE);
        MoveWindow(hNetProg, 0, S(60), pw, S(26), TRUE);
        MoveWindow(hPowList, 0, S(140), pw, ph - S(140) - S(60), TRUE);
        MoveWindow(GetDlgItem(g_hPages[PAGE_POWER], IDC_P_APPLYALL), 0, ph - S(48), S(240), S(36), TRUE);
        MoveWindow(GetDlgItem(g_hPages[PAGE_POWER], IDC_P_RESTORE), S(250), ph - S(48), S(200), S(36), TRUE);
        MoveWindow(GetDlgItem(g_hPages[PAGE_POWER], IDC_P_DELETE), S(460), ph - S(48), S(200), S(36), TRUE);
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

    Theme_Resolve();
    Theme_MakeBrushes();

    g_imgLogo = LoadPng(RES_PNG_LOGO);
    g_imgBanner = LoadPng(RES_PNG_BANNER);
    g_imgBg = LoadPng(RES_PNG_BG);

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

    int W = S(1180), H = S(800);
    g_hMain = CreateWindowExW(0, L"FPSBoosterMain", T(SID_APP_NAME),
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, W, H,
        NULL, NULL, hInst, NULL);
    if (!g_hMain) return false;

    hSide = CreateWindowExW(0, L"FPSBoosterSide", L"", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
        0, 0, S(240), H, g_hMain, NULL, hInst, NULL);
    for (int i = 0; i < PAGE_COUNT; i++) {
        HWND b = CreateWindowExW(0, WC_BUTTONW, NavText(i), WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
            S(14), S(258) + i * S(49), S(212), S(43), hSide, (HMENU)(INT_PTR)(IDC_NAV_BASE + i), hInst, NULL);
        SendMessageW(b, WM_SETFONT, (WPARAM)g_hFont, 0);
    }

    hTitle = Mk(g_hMain, WC_STATICW, L"", WS_CHILD | WS_VISIBLE | SS_LEFT, 0,
        264, 14, 600, 34, 0, g_hFontBig);
    hLangBtn = Mk(g_hMain, WC_BUTTONW, Strings_GetLang() == 1 ? L"فا" : L"EN",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0, 900, 12, 120, 32, IDC_LANG_BTN, g_hFont);
    hThemeBtn = Mk(g_hMain, WC_BUTTONW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 0,
        780, 12, 110, 32, IDC_THEME_BTN, g_hFontBig);
    hAdminBadge = Mk(g_hMain, WC_STATICW, L"", WS_CHILD | WS_VISIBLE | SS_RIGHT, 0,
        1030, 12, 128, 32, 0, g_hFont);

    for (int i = 0; i < PAGE_COUNT; i++) {
        g_hPages[i] = CreateWindowExW(0, L"FPSBoosterPage", L"", WS_CHILD | WS_CLIPCHILDREN,
            S(264), S(62), S(892), S(624), g_hMain, NULL, hInst, NULL);
    }
    BuildDash(g_hPages[PAGE_DASH]);
    BuildAI(g_hPages[PAGE_AI]);
    BuildGames(g_hPages[PAGE_GAMES]);
    BuildProc(g_hPages[PAGE_PROC]);
    BuildBoost(g_hPages[PAGE_BOOST]);
    BuildTweaks(g_hPages[PAGE_TWEAKS]);
    BuildSystem(g_hPages[PAGE_SYSTEM]);
    BuildHelp(g_hPages[PAGE_HELP]);
    BuildSettings(g_hPages[PAGE_SETTINGS]);
    BuildPower(g_hPages[PAGE_POWER]);
    BuildNet(g_hPages[PAGE_NET]);

    hStatusBar = Mk(g_hMain, WC_STATICW, T(SID_STATUS_READY), WS_CHILD | WS_VISIBLE | SS_LEFT, 0,
        240, 730, 900, 30, 0, g_hFont);
    LabelColor(hTitle, LR_TEXT);
    SetLabel(hAdminBadge, WFormat(L"[%s]", g_isAdmin ? T(SID_ADMIN_OK) : T(SID_ADMIN_NO)),
        g_isAdmin ? LR_GREEN : LR_RED);

    SetTimer(g_hMain, 1, 1000, NULL);
    Hotkey_Apply();
    Theme_UpdateBtnFace();
    UI_ShowPage(PAGE_DASH);

    // initial window position/size fix
    RECT r; GetClientRect(g_hMain, &r);
    SendMessageW(g_hMain, WM_SIZE, 0, MAKELPARAM(r.right, r.bottom));
    return true;
}
