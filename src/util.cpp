// FPS Booster Pro - Utilities
#include "app.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <wctype.h>

// ---------- Strings ----------
std::wstring Utf8ToWide(const char* s) {
    if (!s || !*s) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    if (n <= 0) return std::wstring();
    std::wstring w; w.resize((size_t)n);
    MultiByteToWideChar(CP_UTF8, 0, s, -1, &w[0], n);
    if (!w.empty() && w.back() == 0) w.pop_back();
    return w;
}
std::wstring Utf8ToWide(const std::string& s) { return Utf8ToWide(s.c_str()); }

std::string WideToUtf8(const std::wstring& s) {
    if (s.empty()) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1, NULL, 0, NULL, NULL);
    if (n <= 0) return std::string();
    std::string u; u.resize((size_t)n);
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1, &u[0], n, NULL, NULL);
    if (!u.empty() && u.back() == 0) u.pop_back();
    return u;
}

std::wstring VFormat(const wchar_t* fmt, va_list ap) {
    wchar_t buf[4096];
    StringCchVPrintfW(buf, 4096, fmt, ap);
    return std::wstring(buf);
}
std::wstring WFormat(const wchar_t* fmt, ...) {
    va_list ap; va_start(ap, fmt);
    std::wstring r = VFormat(fmt, ap);
    va_end(ap);
    return r;
}

std::wstring NowStamp() {
    SYSTEMTIME st; GetLocalTime(&st);
    return WFormat(L"%04d-%02d-%02d %02d:%02d:%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
}

// ---------- Log ----------
static std::wstring g_logPath;
static CRITICAL_SECTION g_logCs;
static bool g_logInit = false;

void LogInit(const std::wstring& path) {
    g_logPath = path;
    if (!g_logInit) { InitializeCriticalSection(&g_logCs); g_logInit = true; }
    LogW(L"===== FPS Booster Pro started =====");
}
void LogW(const wchar_t* fmt, ...) {
    if (!g_logInit || g_logPath.empty()) return;
    va_list ap; va_start(ap, fmt);
    std::wstring msg = VFormat(fmt, ap);
    va_end(ap);
    std::wstring line = NowStamp() + L"  " + msg + L"\r\n";
    EnterCriticalSection(&g_logCs);
    HANDLE h = CreateFileW(g_logPath.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h != INVALID_HANDLE_VALUE) {
        std::string u = WideToUtf8(line);
        DWORD wr = 0; WriteFile(h, u.c_str(), (DWORD)u.size(), &wr, NULL);
        CloseHandle(h);
    }
    LeaveCriticalSection(&g_logCs);
}

// ---------- Files ----------
bool EnsureDir(const std::wstring& path) {
    if (path.empty()) return false;
    DWORD a = GetFileAttributesW(path.c_str());
    if (a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY)) return true;
    // create parents first
    size_t p = path.find_last_of(L"\\/");
    if (p != std::wstring::npos && p > 2) {
        std::wstring parent = path.substr(0, p);
        if (!EnsureDir(parent)) return false;
    }
    return CreateDirectoryW(path.c_str(), NULL) || GetLastError() == ERROR_ALREADY_EXISTS;
}
bool FileExists(const std::wstring& path) {
    DWORD a = GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}
bool ReadFileText(const std::wstring& path, std::string& out) {
    out.clear();
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD sz = GetFileSize(h, NULL);
    if (sz != INVALID_FILE_SIZE && sz > 0 && sz < 64*1024*1024) {
        out.resize(sz);
        DWORD rd = 0; ReadFile(h, &out[0], sz, &rd, NULL);
        out.resize(rd);
    }
    CloseHandle(h);
    return true;
}
bool WriteFileText(const std::wstring& path, const std::string& data) {
    std::wstring tmp = path + L".tmp";
    HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD wr = 0;
    if (!data.empty()) WriteFile(h, data.c_str(), (DWORD)data.size(), &wr, NULL);
    CloseHandle(h);
    if (wr != data.size()) { DeleteFileW(tmp.c_str()); return false; }
    if (!MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        DeleteFileW(tmp.c_str()); return false;
    }
    return true;
}
std::wstring JoinPath(const std::wstring& a, const std::wstring& b) {
    if (a.empty()) return b;
    if (a.back() == L'\\' || a.back() == L'/') return a + b;
    return a + L"\\" + b;
}
std::wstring BaseName(const std::wstring& path) {
    size_t p = path.find_last_of(L"\\/");
    std::wstring b = (p == std::wstring::npos) ? path : path.substr(p + 1);
    size_t d = b.rfind(L'.');
    if (d != std::wstring::npos) b = b.substr(0, d);
    return b;
}
std::wstring DirName(const std::wstring& path) {
    size_t p = path.find_last_of(L"\\/");
    if (p == std::wstring::npos) return L".";
    return path.substr(0, p);
}
std::wstring ToLower(std::wstring s) {
    for (size_t i = 0; i < s.size(); i++)
        if (s[i] >= L'A' && s[i] <= L'Z') s[i] += 32;
    return s;
}
std::vector<std::wstring> SplitWs(const std::wstring& s, wchar_t delim) {
    std::vector<std::wstring> v; std::wstring cur;
    for (size_t i = 0; i <= s.size(); i++) {
        if (i == s.size() || s[i] == delim) {
            // trim
            size_t a = 0; while (a < cur.size() && iswspace(cur[a])) a++;
            size_t b = cur.size(); while (b > a && iswspace(cur[b-1])) b--;
            if (b > a) v.push_back(cur.substr(a, b - a));
            cur.clear();
        } else cur.push_back(s[i]);
    }
    return v;
}
std::string JsonEscape(const std::string& s) {
    std::string o; o.reserve(s.size() + 8);
    for (size_t i = 0; i < s.size(); i++) {
        unsigned char c = (unsigned char)s[i];
        switch (c) {
        case '"': o += "\\\""; break;
        case '\\': o += "\\\\"; break;
        case '\n': o += "\\n"; break;
        case '\r': o += "\\r"; break;
        case '\t': o += "\\t"; break;
        default:
            if (c < 0x20) { char b[8]; snprintf(b, 8, "\\u%04x", c); o += b; }
            else o.push_back((char)c);
        }
    }
    return o;
}
std::string JsonEscapeW(const std::wstring& s) { return JsonEscape(WideToUtf8(s)); }

// ---------- Registry ----------
static REGSAM HiveSam(HKEY hive) {
    if (hive == HKEY_LOCAL_MACHINE || hive == HKEY_CLASSES_ROOT) return KEY_WOW64_64KEY;
    return 0;
}
bool RegGetDword(HKEY hive, const wchar_t* sub, const wchar_t* name, DWORD& out) {
    HKEY h = NULL;
    if (RegOpenKeyExW(hive, sub, 0, KEY_QUERY_VALUE | HiveSam(hive), &h) != ERROR_SUCCESS) return false;
    DWORD type = 0, data = 0, cb = sizeof(data);
    LONG r = RegQueryValueExW(h, name, NULL, &type, (BYTE*)&data, &cb);
    RegCloseKey(h);
    if (r != ERROR_SUCCESS || type != REG_DWORD) return false;
    out = data; return true;
}
bool RegSetDword(HKEY hive, const wchar_t* sub, const wchar_t* name, DWORD val) {
    HKEY h = NULL;
    DWORD disp = 0;
    if (RegCreateKeyExW(hive, sub, 0, NULL, 0, KEY_SET_VALUE | HiveSam(hive), NULL, &h, &disp) != ERROR_SUCCESS) return false;
    LONG r = RegSetValueExW(h, name, 0, REG_DWORD, (const BYTE*)&val, sizeof(val));
    RegCloseKey(h);
    return r == ERROR_SUCCESS;
}
bool RegGetString(HKEY hive, const wchar_t* sub, const wchar_t* name, std::wstring& out) {
    HKEY h = NULL;
    if (RegOpenKeyExW(hive, sub, 0, KEY_QUERY_VALUE | HiveSam(hive), &h) != ERROR_SUCCESS) return false;
    DWORD type = 0, cb = 0;
    LONG r = RegQueryValueExW(h, name, NULL, &type, NULL, &cb);
    bool ok = false;
    if (r == ERROR_SUCCESS && (type == REG_SZ || type == REG_EXPAND_SZ) && cb > 0 && cb < 1024*1024) {
        std::wstring buf; buf.resize(cb / sizeof(wchar_t) + 1);
        r = RegQueryValueExW(h, name, NULL, NULL, (BYTE*)&buf[0], &cb);
        if (r == ERROR_SUCCESS) {
            buf.resize(wcslen(buf.c_str()));
            out = buf; ok = true;
        }
    }
    RegCloseKey(h);
    return ok;
}
bool RegSetString(HKEY hive, const wchar_t* sub, const wchar_t* name, const std::wstring& val, bool expand) {
    HKEY h = NULL; DWORD disp = 0;
    if (RegCreateKeyExW(hive, sub, 0, NULL, 0, KEY_SET_VALUE | HiveSam(hive), NULL, &h, &disp) != ERROR_SUCCESS) return false;
    LONG r = RegSetValueExW(h, name, 0, expand ? REG_EXPAND_SZ : REG_SZ,
        (const BYTE*)val.c_str(), (DWORD)((val.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(h);
    return r == ERROR_SUCCESS;
}
bool RegDeleteValue(HKEY hive, const wchar_t* sub, const wchar_t* name) {
    HKEY h = NULL;
    if (RegOpenKeyExW(hive, sub, 0, KEY_SET_VALUE | HiveSam(hive), &h) != ERROR_SUCCESS) return false;
    LONG r = RegDeleteValueW(h, name);
    RegCloseKey(h);
    return r == ERROR_SUCCESS || r == ERROR_FILE_NOT_FOUND;
}
bool RegValueExists(HKEY hive, const wchar_t* sub, const wchar_t* name) {
    HKEY h = NULL;
    if (RegOpenKeyExW(hive, sub, 0, KEY_QUERY_VALUE | HiveSam(hive), &h) != ERROR_SUCCESS) return false;
    LONG r = RegQueryValueExW(h, name, NULL, NULL, NULL, NULL);
    RegCloseKey(h);
    return r == ERROR_SUCCESS;
}
bool RegDeleteKeyTree(HKEY hive, const wchar_t* sub) {
    // manual recursive delete (RegDeleteTree may be missing on old headers; implement)
    HKEY h = NULL;
    if (RegOpenKeyExW(hive, sub, 0, KEY_READ | KEY_WRITE | HiveSam(hive), &h) != ERROR_SUCCESS) return true;
    for (;;) {
        wchar_t name[256]; DWORD len = 255;
        if (RegEnumKeyExW(h, 0, name, &len, NULL, NULL, NULL, NULL) != ERROR_SUCCESS) break;
        std::wstring child = std::wstring(sub) + L"\\" + name;
        RegDeleteKeyTree(hive, child.c_str());
    }
    RegCloseKey(h);
    RegDeleteKeyW(hive, sub);
    return true;
}

// ---------- Processes ----------
bool RunHidden(const wchar_t* exe, const wchar_t* args, DWORD* exitCode) {
    std::wstring cmd = std::wstring(L"\"") + exe + L"\" " + (args ? args : L"");
    STARTUPINFOW si; ZeroMemory(&si, sizeof(si)); si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW; si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi; ZeroMemory(&pi, sizeof(pi));
    std::vector<wchar_t> buf(cmd.begin(), cmd.end()); buf.push_back(0);
    if (!CreateProcessW(NULL, buf.data(), NULL, NULL, FALSE, CREATE_NO_WINDOW | BELOW_NORMAL_PRIORITY_CLASS,
        NULL, NULL, &si, &pi)) return false;
    WaitForSingleObject(pi.hProcess, 120000);
    if (exitCode) GetExitCodeProcess(pi.hProcess, exitCode);
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    return true;
}
bool RunCapture(const wchar_t* exe, const wchar_t* args, std::wstring& output) {
    output.clear();
    SECURITY_ATTRIBUTES sa; ZeroMemory(&sa, sizeof(sa)); sa.nLength = sizeof(sa); sa.bInheritHandle = TRUE;
    HANDLE rd = NULL, wr = NULL;
    if (!CreatePipe(&rd, &wr, &sa, 0)) return false;
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    std::wstring cmd = std::wstring(L"\"") + exe + L"\" " + (args ? args : L"");
    STARTUPINFOW si; ZeroMemory(&si, sizeof(si)); si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
    si.wShowWindow = SW_HIDE; si.hStdOutput = wr; si.hStdError = wr; si.hStdInput = NULL;
    PROCESS_INFORMATION pi; ZeroMemory(&pi, sizeof(pi));
    std::vector<wchar_t> buf(cmd.begin(), cmd.end()); buf.push_back(0);
    BOOL ok = CreateProcessW(NULL, buf.data(), NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
    CloseHandle(wr);
    if (!ok) { CloseHandle(rd); return false; }
    char tmp[4096]; DWORD got = 0; std::string acc;
    for (;;) {
        BOOL r = ReadFile(rd, tmp, sizeof(tmp), &got, NULL);
        if (!r || got == 0) break;
        acc.append(tmp, got);
        if (acc.size() > 1024*1024) break;
    }
    WaitForSingleObject(pi.hProcess, 60000);
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess); CloseHandle(rd);
    // console output is in OEM/ANSI codepage; try OEM then ANSI
    int n = MultiByteToWideChar(CP_OEMCP, 0, acc.c_str(), (int)acc.size(), NULL, 0);
    if (n > 0) {
        output.resize((size_t)n);
        MultiByteToWideChar(CP_OEMCP, 0, acc.c_str(), (int)acc.size(), &output[0], n);
    }
    return true;
}
bool EnablePrivilege(const wchar_t* name) {
    HANDLE tok = NULL;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &tok)) return false;
    TOKEN_PRIVILEGES tp; tp.PrivilegeCount = 1;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    BOOL ok = LookupPrivilegeValueW(NULL, name, &tp.Privileges[0].Luid) &&
        AdjustTokenPrivileges(tok, FALSE, &tp, 0, NULL, NULL) && GetLastError() == ERROR_SUCCESS;
    CloseHandle(tok);
    return ok != FALSE;
}
bool IsUserAdmin() {
    SID_IDENTIFIER_AUTHORITY sia = SECURITY_NT_AUTHORITY;
    PSID admin = NULL;
    if (!AllocateAndInitializeSid(&sia, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS, 0,0,0,0,0,0, &admin))
        return false;
    BOOL member = FALSE;
    CheckTokenMembership(NULL, admin, &member);
    FreeSid(admin);
    return member != FALSE;
}
bool RelaunchAsAdmin() {
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(NULL, exe, MAX_PATH);
    SHELLEXECUTEINFOW sei; ZeroMemory(&sei, sizeof(sei));
    sei.cbSize = sizeof(sei);
    sei.lpVerb = L"runas";
    sei.lpFile = exe;
    sei.nShow = SW_NORMAL;
    return ShellExecuteExW(&sei) != FALSE;
}
bool SetStartupRun(bool on) {
    const wchar_t* sub = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run";
    if (!on) return RegDeleteValue(HKEY_CURRENT_USER, sub, L"FPSBoosterPro");
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(NULL, exe, MAX_PATH);
    std::wstring v = std::wstring(L"\"") + exe + L"\" /min";
    return RegSetString(HKEY_CURRENT_USER, sub, L"FPSBoosterPro", v);
}

// ---------- Mini JSON ----------
struct JParser {
    const char* p; const char* end;
    JParser(const std::string& t) : p(t.c_str()), end(t.c_str() + t.size()) {}
    void skip() { while (p < end && (*p==' '||*p=='\t'||*p=='\r'||*p=='\n')) p++; }
    bool parse(JVal& v) {
        skip(); if (p >= end) return false;
        if (*p == '{') return parseObj(v);
        if (*p == '[') return parseArr(v);
        if (*p == '"') { v.type = JVal::STR; return parseStr(v.s); }
        if (*p == 't' && end-p>=4 && !strncmp(p,"true",4)) { p+=4; v.type=JVal::BOOL; v.b=true; return true; }
        if (*p == 'f' && end-p>=5 && !strncmp(p,"false",5)) { p+=5; v.type=JVal::BOOL; v.b=false; return true; }
        if (*p == 'n' && end-p>=4 && !strncmp(p,"null",4)) { p+=4; v.type=JVal::NUL; return true; }
        if (*p=='-' || (*p>='0'&&*p<='9')) {
            char* e = NULL; v.n = strtod(p, &e);
            if (e == p) return false;
            p = e; v.type = JVal::NUM; return true;
        }
        return false;
    }
    bool parseStr(std::string& s) {
        s.clear(); p++; // "
        while (p < end && *p != '"') {
            if (*p == '\\' && p+1 < end) {
                p++;
                switch (*p) {
                case 'n': s.push_back('\n'); break;
                case 'r': s.push_back('\r'); break;
                case 't': s.push_back('\t'); break;
                case 'u': {
                    // \uXXXX -> UTF-8
                    if (p + 4 < end) {
                        char hb[5]; hb[0]=p[1];hb[1]=p[2];hb[2]=p[3];hb[3]=p[4];hb[4]=0;
                        unsigned cp = (unsigned)strtoul(hb, NULL, 16);
                        p += 4;
                        if (cp < 0x80) s.push_back((char)cp);
                        else if (cp < 0x800) { s.push_back((char)(0xC0|(cp>>6))); s.push_back((char)(0x80|(cp&0x3F))); }
                        else { s.push_back((char)(0xE0|(cp>>12))); s.push_back((char)(0x80|((cp>>6)&0x3F))); s.push_back((char)(0x80|(cp&0x3F))); }
                    }
                    break;
                }
                default: s.push_back(*p); break;
                }
                p++;
            } else { s.push_back(*p); p++; }
        }
        if (p >= end) return false;
        p++; return true;
    }
    bool parseObj(JVal& v) {
        v.type = JVal::OBJ; p++; skip();
        if (p < end && *p == '}') { p++; return true; }
        while (p < end) {
            skip(); if (p>=end || *p!='"') return false;
            std::string k; if (!parseStr(k)) return false;
            skip(); if (p>=end || *p!=':') return false;
            p++;
            JVal val; if (!parse(val)) return false;
            v.obj.push_back(std::make_pair(k, val));
            skip(); if (p>=end) return false;
            if (*p == ',') { p++; continue; }
            if (*p == '}') { p++; return true; }
            return false;
        }
        return false;
    }
    bool parseArr(JVal& v) {
        v.type = JVal::ARR; p++; skip();
        if (p < end && *p == ']') { p++; return true; }
        while (p < end) {
            JVal val; if (!parse(val)) return false;
            v.arr.push_back(val);
            skip(); if (p>=end) return false;
            if (*p == ',') { p++; continue; }
            if (*p == ']') { p++; return true; }
            return false;
        }
        return false;
    }
};
bool ParseJson(const std::string& text, JVal& out) {
    // skip UTF-8 BOM
    std::string t = text;
    if (t.size() >= 3 && (unsigned char)t[0]==0xEF && (unsigned char)t[1]==0xBB && (unsigned char)t[2]==0xBF)
        t = t.substr(3);
    JParser pr(t);
    return pr.parse(out);
}
const JVal* JVal::find(const char* key) const {
    if (type != OBJ) return NULL;
    for (size_t i = 0; i < obj.size(); i++)
        if (obj[i].first == key) return &obj[i].second;
    return NULL;
}
std::string JVal::str(const char* key, const char* def) const {
    const JVal* v = find(key);
    if (!v) return std::string(def);
    if (v->type == STR) return v->s;
    if (v->type == NUM) { char b[32]; snprintf(b, 32, "%g", v->n); return b; }
    if (v->type == BOOL) return v->b ? "true" : "false";
    return std::string(def);
}
std::wstring JVal::wstr(const char* key, const wchar_t* def) const {
    const JVal* v = find(key);
    if (!v || v->type != STR) return std::wstring(def);
    return Utf8ToWide(v->s);
}
int JVal::num(const char* key, int def) const {
    const JVal* v = find(key);
    if (!v) return def;
    if (v->type == NUM) return (int)v->n;
    if (v->type == BOOL) return v->b ? 1 : 0;
    return def;
}
bool JVal::boolean(const char* key, bool def) const {
    const JVal* v = find(key);
    if (!v) return def;
    if (v->type == BOOL) return v->b;
    if (v->type == NUM) return v->n != 0;
    return def;
}
