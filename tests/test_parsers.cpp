// Test harness: compiles the EXACT parser code from src/ on Linux.
// json.inc is extracted verbatim from src/util.cpp, vdf.inc from src/games.cpp.
#include <string>
#include <vector>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cassert>

// ---- minimal declarations copied from app.h ----
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
// simple UTF-8 -> wide (Linux wchar_t=32bit) for testing
std::wstring Utf8ToWide(const std::string& s) {
    std::wstring w; w.reserve(s.size());
    for (size_t i = 0; i < s.size();) {
        unsigned char c = s[i];
        if (c < 0x80) { w.push_back(c); i++; }
        else if ((c & 0xE0) == 0xC0 && i + 1 < s.size()) { w.push_back(((c & 0x1F) << 6) | (s[i+1] & 0x3F)); i += 2; }
        else if ((c & 0xF0) == 0xE0 && i + 2 < s.size()) { w.push_back(((c & 0x0F) << 12) | ((s[i+1] & 0x3F) << 6) | (s[i+2] & 0x3F)); i += 3; }
        else if ((c & 0xF8) == 0xF0 && i + 3 < s.size()) { w.push_back(((c & 0x07) << 18) | ((s[i+1] & 0x3F) << 12) | ((s[i+2] & 0x3F) << 6) | (s[i+3] & 0x3F)); i += 4; }
        else { i++; }
    }
    return w;
}
std::wstring Utf8ToWide(const char* s) { return Utf8ToWide(std::string(s ? s : "")); }

#include "json.inc"
#include "vdf.inc"

// ================= TESTS =================
static std::string slurp(const char* p) {
    FILE* f = fopen(p, "rb"); assert(f);
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    std::string s; s.resize(n);
    if (n > 0) fread(&s[0], 1, n, f);
    fclose(f); return s;
}
int main() {
    // 1. real game DB
    {
        JVal r; assert(ParseJson(slurp("data/games_db.json"), r));
        const JVal* db = r.find("db"); assert(db && db->type == JVal::ARR);
        printf("games_db: %d entries OK\n", (int)db->arr.size());
        assert(db->arr.size() > 50);
        assert(db->arr[0].str("match") == "cs2.exe");
    }
    // 2. games.json sample (as written by Games_Save: escapes, unicode)
    {
        const char* t = "{\"games\":[{\"prio\":2,\"aff\":0,\"gpu\":1,\"fso\":0,\"timer\":1,\"power\":1,\"plays\":7,\"name\":\"Test \\u0628\\u0627\\u0632\\u06cc\",\"path\":\"C:\\\\Games\\\\test.exe\",\"args\":\"-high\",\"kill\":\"\",\"last\":\"2026-01-01\"}]}";
        JVal r; assert(ParseJson(t, r));
        const JVal* a = r.find("games"); assert(a && a->arr.size() == 1);
        assert(a->arr[0].wstr("path") == L"C:\\Games\\test.exe");
        assert(a->arr[0].num("plays", 0) == 7);
        assert(a->arr[0].boolean("timer", false) == true);
        assert(a->arr[0].str("missing", "dflt") == "dflt");
        printf("games.json sample OK\n");
    }
    // 3. backup.json sample
    {
        const char* t = "{\"power\":\"e9a42b02-d5df-448d-aa00-03f14749eb61\",\"cphave\":1,\"cpscheme\":\"x\",\"cpac\":50,\"cpdc\":20,\"reg\":[{\"h\":0,\"t\":4,\"d\":1,\"ex\":1,\"sub\":\"A\\\\B\",\"name\":\"N\",\"s\":\"\"}],\"svc\":[{\"n\":\"SysMain\",\"start\":2}],\"bcd\":[]}";
        JVal r; assert(ParseJson(t, r));
        assert(r.wstr("power") == L"e9a42b02-d5df-448d-aa00-03f14749eb61");
        assert(r.num("cpac", 0) == 50);
        assert(r.find("reg")->arr.size() == 1);
        assert(r.find("bcd")->arr.size() == 0);
        printf("backup.json sample OK\n");
    }
    // 4. Epic-style manifest with BOM + escapes
    {
        std::string t = "\xEF\xBB\xBF{\"DisplayName\":\"Test \\\"Game\\\"\",\"InstallLocation\":\"D:/Epic/G\",\"LaunchExecutable\":\"Bin/game.exe\",\"num\":1.5,\"flag\":true,\"nil\":null}";
        JVal r; assert(ParseJson(t, r));
        assert(r.str("DisplayName") == "Test \"Game\"");
        assert(r.wstr("Missing", L"X") == L"X");
        assert(r.find("num")->n == 1.5);
        printf("epic manifest OK\n");
    }
    // 5. malformed must fail cleanly (no crash)
    {
        const char* bad[] = {"{", "{\"a\":}", "[1,]", "{\"a\":[1,}", "", "{,}", "{\"a\" 1}", NULL};
        for (int i = 0; bad[i]; i++) { JVal r; assert(!ParseJson(bad[i], r)); }
        printf("malformed rejection OK\n");
    }
    // 6. VDF tokenize + libraryfolders logic
    {
        const char* vdf = "\"libraryfolders\"\n{\n\t\"0\"\n\t{\n\t\t\"path\"\t\t\"C:\\\\Steam\"\n\t}\n\t\"1\"\n\t{\n\t\t\"path\"\t\t\"D:\\\\SteamLib\"\n\t}\n}";
        std::vector<std::string> toks = VdfTokens(vdf);
        int paths = 0;
        for (size_t i = 0; i + 1 < toks.size(); i++) if (toks[i] == "path") paths++;
        assert(paths == 2);
        const char* acf = "\"AppState\"\n{\n\t\"appid\"\t\t\"730\"\n\t\"name\"\t\t\"Counter-Strike 2\"\n\t\"installdir\"\t\t\"CSGO\"\n}";
        std::vector<std::string> t2 = VdfTokens(acf);
        std::string name, inst;
        for (size_t i = 0; i + 1 < t2.size(); i++) {
            if (t2[i] == "name") name = t2[i+1];
            if (t2[i] == "installdir") inst = t2[i+1];
        }
        assert(name == "Counter-Strike 2" && !inst.empty());
        printf("VDF parsing OK\n");
    }
    printf("ALL PARSER TESTS PASSED\n");
    return 0;
}
