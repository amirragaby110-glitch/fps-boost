// FPS Booster Pro - offline AI advisor.
// A 100% offline expert system: understands game names (English + Persian),
// scores your CPU/GPU/RAM, compares against the embedded requirements
// database and answers "can I run it?" plus FPS questions.
#include "app.h"
#include <winhttp.h>
#include <wctype.h>

// ---------- small text helpers ----------
static std::wstring AiNorm(const std::wstring& s) {
    std::wstring o;
    for (size_t i = 0; i < s.size(); i++) {
        wchar_t c = towlower(s[i]);
        if ((c >= L'a' && c <= L'z') || (c >= L'0' && c <= L'9')) o += c;
    }
    return o;
}
static bool AiHas(const std::wstring& h, const wchar_t* n) { return h.find(n) != std::wstring::npos; }
static bool AiHasU8(const std::wstring& h, const char* n) { return AiHas(h, Utf8ToWide(n).c_str()); }
static std::wstring AiGhz(int mhz) {
    if (mhz <= 0) return L"";
    return WFormat(L"%d.%d GHz", mhz / 1000, (mhz % 1000) / 100);
}

// ---------- CPU scoring 1..5 ----------
static int AiCpuTier(const std::wstring& name, int cores, int mhz) {
    std::wstring n = ToLower(name);
    if (AiHas(n, L"threadripper") || AiHas(n, L"epyc")) return 5;
    size_t rz = n.find(L"ryzen");
    if (rz != std::wstring::npos) {
        int tier = 0;
        size_t p = rz + 5;
        while (p < n.size() && (n[p] < L'0' || n[p] > L'9')) p++;
        if (p < n.size()) tier = n[p] - L'0';
        int series = 0;
        for (size_t i = rz; i + 4 <= n.size(); i++) {
            if (n[i] >= L'1' && n[i] <= L'9' && iswdigit(n[i + 1]) && iswdigit(n[i + 2]) && iswdigit(n[i + 3])) {
                series = (n[i] - L'0') * 1000 + (n[i + 1] - L'0') * 100 + (n[i + 2] - L'0') * 10 + (n[i + 3] - L'0');
                break;
            }
        }
        bool modern = series >= 5000;
        if (tier == 9) return 5;
        if (tier == 7) return modern ? 5 : 4;
        if (tier == 5) return modern ? 4 : 3;
        if (tier == 3) return modern ? 3 : 2;
        return modern ? 3 : 2;
    }
    if (AiHas(n, L" fx-") || (n.size() > 3 && n.substr(0, 3) == L"fx-")) return 2;
    if (AiHas(n, L"athlon")) return cores >= 4 ? 2 : 1;
    if (AiHas(n, L" a12") || AiHas(n, L" a10") || AiHas(n, L" a8") || AiHas(n, L" a6") || AiHas(n, L" a4")) return 1;
    if (AiHas(n, L"xeon")) return cores >= 8 ? 4 : 3;
    if (AiHas(n, L"celeron") || AiHas(n, L"atom")) return 1;
    if (AiHas(n, L"pentium")) return cores >= 4 ? 2 : 1;
    if (AiHas(n, L"n100")) return 2;
    if (AiHas(n, L"n305")) return 3;
    if (AiHas(n, L"ultra 9") || AiHas(n, L"ultra9")) return 5;
    if (AiHas(n, L"ultra 7") || AiHas(n, L"ultra7")) return 4;
    if (AiHas(n, L"ultra 5") || AiHas(n, L"ultra5")) return 4;
    int ix = 0;
    if (AiHas(n, L"i9-") || AiHas(n, L"i9 ")) ix = 9;
    else if (AiHas(n, L"i7-") || AiHas(n, L"i7 ")) ix = 7;
    else if (AiHas(n, L"i5-") || AiHas(n, L"i5 ")) ix = 5;
    else if (AiHas(n, L"i3-") || AiHas(n, L"i3 ")) ix = 3;
    if (ix > 0) {
        int model = 0;
        wchar_t pat[8];
        StringCchPrintfW(pat, 8, L"i%d", ix);
        size_t f = n.find(pat);
        if (f != std::wstring::npos) {
            size_t q = f + 2;
            while (q < n.size() && (n[q] < L'0' || n[q] > L'9')) q++;
            while (q < n.size() && n[q] >= L'0' && n[q] <= L'9' && model < 100000) {
                model = model * 10 + (n[q] - L'0'); q++;
            }
        }
        int gen = (model >= 1000) ? model / 1000 : (model > 0 ? 1 : 0);
        if (gen >= 12) return ix == 9 ? 5 : ix == 7 ? 5 : ix == 5 ? 4 : 3;
        if (gen >= 8) return ix == 9 ? 5 : ix == 7 ? 4 : ix == 5 ? (gen >= 10 ? 4 : 3) : 2;
        if (gen >= 4) return ix >= 7 ? 3 : 2;
        if (gen >= 1) return ix >= 7 ? 2 : ix == 5 ? 2 : 1;
        return ix >= 7 ? 3 : 2;
    }
    int t = 1;
    if (cores >= 12) t = 5; else if (cores >= 8) t = 4; else if (cores >= 6) t = 3; else if (cores >= 4) t = 2;
    if (mhz >= 4200 && t < 5) t++;
    return t;
}

// ---------- GPU scoring 1..5 ----------
static int AiGpuTier(const std::wstring& name, int vramMB) {
    std::wstring n = ToLower(name);
    if (AiHas(n, L"basic display") || AiHas(n, L"basic render")) return 1;
    if (AiHas(n, L"rtx")) {
        if (AiHas(n, L"40")) return 5;
        if (AiHas(n, L"30")) return AiHas(n, L"3050") ? 4 : 5;
        if (AiHas(n, L"20")) return 4;
        return 4;
    }
    if (AiHas(n, L"gtx")) {
        if (AiHas(n, L"1660")) return 4;
        if (AiHas(n, L"1650") || AiHas(n, L"1630")) return 3;
        if (AiHas(n, L"1080") || AiHas(n, L"1070")) return 4;
        if (AiHas(n, L"1060")) return 3;
        if (AiHas(n, L"1050")) return 2;
        if (AiHas(n, L"980") || AiHas(n, L"970")) return 3;
        if (AiHas(n, L"960") || AiHas(n, L"950")) return 2;
        if (AiHas(n, L"750")) return 2;
        return 1;
    }
    if (AiHas(n, L" mx")) return AiHas(n, L"mx5") ? 2 : 1;
    if (AiHas(n, L"quadro")) return vramMB >= 4096 ? 3 : 2;
    if (AiHas(n, L"radeon")) {
        size_t f = n.find(L"rx");
        if (f != std::wstring::npos) {
            int model = 0;
            size_t q = f + 2;
            while (q < n.size() && (n[q] < L'0' || n[q] > L'9')) q++;
            while (q < n.size() && n[q] >= L'0' && n[q] <= L'9' && model < 100000) {
                model = model * 10 + (n[q] - L'0'); q++;
            }
            if (model >= 7900) return 5;
            if (model >= 7700) return 4;
            if (model >= 7600) return 4;
            if (model >= 6900) return 5;
            if (model >= 6800) return 5;
            if (model >= 6700) return 4;
            if (model >= 6600) return 4;
            if (model >= 6500) return 3;
            if (model >= 6400) return 3;
            if (model >= 6300) return 3;
            if (model >= 5700) return 4;
            if (model >= 5600) return 3;
            if (model >= 5500) return 2;
            if (model >= 5300) return 2;
            if (model >= 590) return 3;
            if (model >= 580) return 3;
            if (model >= 570) return 3;
            if (model >= 560) return 2;
            if (model >= 100) return 1;
        }
        if (AiHas(n, L"vega")) return 3;
        if (AiHas(n, L"r9")) return 2;
        if (AiHas(n, L"r7") || AiHas(n, L"r5")) return 1;
        return 2; // modern integrated "Radeon Graphics"
    }
    if (AiHas(n, L"arc")) {
        if (AiHas(n, L" a7")) return 4;
        if (AiHas(n, L" a5")) return 3;
        if (AiHas(n, L" a3")) return 2;
        return 3;
    }
    if (AiHas(n, L"iris xe") || AiHas(n, L"iris plus")) return 2;
    if (AiHas(n, L"iris")) return 1;
    if (AiHas(n, L"uhd") || AiHas(n, L"hd graphics")) return 1;
    if (vramMB >= 6144) return 4;
    if (vramMB >= 4096) return 3;
    if (vramMB >= 2048) return 2;
    return 1;
}

// ---------- game name aliases (normalized latin -> db match key) ----------
struct AiAlias { const char* key; const char* match; };
static const AiAlias kAliases[] = {
{"gta5", "gta5.exe"}, {"gtav", "gta5.exe"}, {"gtaonline", "gta5.exe"}, {"gta", "gta5.exe"},
{"reddead", "rdr2.exe"}, {"rdr2", "rdr2.exe"}, {"rdr", "rdr2.exe"},
{"cyberpunk", "cyberpunk2077.exe"}, {"cyber", "cyberpunk2077.exe"}, {"2077", "cyberpunk2077.exe"},
{"eldenring", "eldenring.exe"}, {"elden", "eldenring.exe"},
{"minecraft", "minecraft.exe"}, {"mine", "minecraft.exe"},
{"battlegrounds", "pubg.exe"}, {"pubg", "pubg.exe"},
{"pubglite", "pubglite.exe"}, {"lite", "pubglite.exe"},
{"apex", "apex_legends.exe"}, {"apexlegends", "apex_legends.exe"},
{"warzone", "warzone.exe"}, {"cod", "modernwarfare.exe"}, {"callofduty", "modernwarfare.exe"},
{"modern", "modernwarfare.exe"}, {"warfare", "modernwarfare.exe"},
{"overwatch", "overwatch.exe"}, {"ow2", "overwatch.exe"},
{"leagueoflegends", "league of legends.exe"}, {"league", "league of legends.exe"}, {"lol", "league of legends.exe"},
{"dota1", "dota.exe"}, {"dota2", "dota2.exe"}, {"dota", "dota2.exe"},
{"rust", "rust.exe"}, {"deadlock", "deadlock.exe"},
{"rocketleague", "rocketleague.exe"}, {"rocket", "rocketleague.exe"},
{"fifa", "fc25.exe"}, {"eafc", "fc25.exe"}, {"fc24", "fifa24.exe"}, {"fc25", "fc25.exe"}, {"fc26", "fc25.exe"}, {"fc", "fc25.exe"},
{"witcher", "witcher3.exe"},
{"hogwarts", "hogwartslegacy.exe"}, {"harrypotter", "hogwartslegacy.exe"}, {"legacy", "hogwartslegacy.exe"},
{"starfield", "starfield.exe"}, {"baldur", "baldursgate3.exe"}, {"bg3", "baldursgate3.exe"},
{"palworld", "palworld.exe"}, {"helldivers", "helldivers2.exe"},
{"tekken", "tekken8.exe"}, {"street", "streetfighter6.exe"}, {"sf6", "streetfighter6.exe"}, {"fighter", "streetfighter6.exe"},
{"warthunder", "warthunder.exe"}, {"thunder", "warthunder.exe"},
{"worldoftanks", "worldoftanks.exe"}, {"tanks", "worldoftanks.exe"}, {"wot", "worldoftanks.exe"},
{"crossfire", "crossfire.exe"}, {"pointblank", "pointblank.exe"}, {"zula", "zula.exe"},
{"pes", "pes2021.exe"}, {"efootball", "pes2021.exe"},
{"eurotruck", "eurotrucks2.exe"}, {"ets2", "eurotrucks2.exe"}, {"ets", "eurotrucks2.exe"},
{"thefinals", "thefinals.exe"}, {"finals", "thefinals.exe"},
{"delta", "deltaforce.exe"}, {"deltaforce", "deltaforce.exe"},
{"marvel", "marvelrivals.exe"}, {"rivals", "marvelrivals.exe"},
{"wukong", "blackmythwukong.exe"}, {"blackmyth", "blackmythwukong.exe"}, {"myth", "blackmythwukong.exe"},
{"stalker", "stalker2.exe"}, {"fragpunk", "fragpunk.exe"},
{"doom", "doometernal.exe"}, {"eternal", "doometernal.exe"},
{"halo", "haloinfinite.exe"}, {"infinite", "haloinfinite.exe"}, {"destiny", "destiny2.exe"},
{"rainbow", "rainbow6.exe"}, {"siege", "rainbow6.exe"}, {"r6", "rainbow6.exe"},
{"tarkov", "tarkov.exe"}, {"escapefromtarkov", "tarkov.exe"}, {"eft", "tarkov.exe"},
{"dayz", "dayz.exe"}, {"arma", "arma3.exe"}, {"squad", "squad.exe"},
{"forzahorizon5", "forzahorizon5.exe"}, {"forzahorizon", "forzahorizon5.exe"}, {"forza", "forzahorizon5.exe"},
{"nfs", "nfsunbound.exe"}, {"unbound", "nfsunbound.exe"}, {"needforspeed", "nfsunbound.exe"},
{"assetto", "assettocorsa.exe"}, {"corsa", "assettocorsa.exe"}, {"iracing", "iracing.exe"},
{"worldofwarcraft", "wow.exe"}, {"wow", "wow.exe"}, {"warcraft", "warcraft3.exe"},
{"ffxiv", "ffxiv.exe"}, {"ff14", "ffxiv.exe"}, {"finalfantasy14", "ffxiv.exe"},
{"lostark", "lostark.exe"}, {"newworld", "newworld.exe"},
{"diablo4", "diablo4.exe"}, {"diablo", "diablo4.exe"},
{"pathofexile", "pathofexile.exe"}, {"poe", "pathofexile.exe"},
{"smite", "smite.exe"}, {"brawlhalla", "brawlhalla.exe"}, {"brawl", "brawlhalla.exe"},
{"fallguys", "fallguys.exe"}, {"amongus", "amongus.exe"}, {"among", "amongus.exe"},
{"terraria", "terraria.exe"}, {"stardew", "stardewvalley.exe"}, {"factorio", "factorio.exe"},
{"rimworld", "rimworld.exe"}, {"cities", "cities.exe"}, {"skylines", "cities.exe"},
{"europa", "eu4.exe"}, {"eu4", "eu4.exe"}, {"crusader", "ck3.exe"}, {"ck3", "ck3.exe"},
{"totalwar", "totalwar.exe"}, {"ageofempires", "aoe4.exe"}, {"aoe4", "aoe4.exe"}, {"aoe", "aoe4.exe"},
{"starcraft", "starcraft2.exe"}, {"sc2", "starcraft2.exe"},
{"overcooked", "overcooked.exe"}, {"cuphead", "cuphead.exe"},
{"hollowknight", "hollowknight.exe"}, {"hollow", "hollowknight.exe"}, {"celeste", "celeste.exe"},
{"sekiro", "sekiro.exe"}, {"darksouls", "ds3.exe"}, {"souls", "ds3.exe"}, {"ds3", "ds3.exe"},
{"godofwar", "godofwar.exe"}, {"gow", "godofwar.exe"},
{"spiderman", "spiderman.exe"}, {"spider", "spiderman.exe"},
{"tlou", "tlou.exe"}, {"lastofus", "tlou.exe"}, {"uncharted", "uncharted.exe"},
{"horizon", "horizon.exe"}, {"zerodawn", "horizon.exe"}, {"forbiddenwest", "horizon.exe"},
{"ghostoftsushima", "ghostoftsushima.exe"}, {"tsushima", "ghostoftsushima.exe"}, {"ghost", "ghostoftsushima.exe"},
{"finalfantasy7", "finalfantasy7.exe"}, {"finalfantasy", "finalfantasy7.exe"}, {"ff7", "finalfantasy7.exe"},
{"persona", "persona5.exe"}, {"yakuza", "yakuza.exe"},
{"resident", "residentevil4.exe"}, {"residentevil", "residentevil4.exe"}, {"re4", "residentevil4.exe"},
{"remake", "residentevil4.exe"}, {"biohazard", "residentevil4.exe"},
{"residentevilvillage", "re8.exe"}, {"residentevil8", "re8.exe"}, {"village", "re8.exe"}, {"re8", "re8.exe"},
{"silenthill", "silent hill.exe"}, {"alanwake", "alanwake2.exe"}, {"wake", "alanwake2.exe"},
{"control", "control.exe"}, {"deathstranding", "deathstranding.exe"}, {"stranding", "deathstranding.exe"},
{"metro", "metroexodus.exe"}, {"exodus", "metroexodus.exe"},
{"dyinglight", "dyinglight2.exe"}, {"dl2", "dyinglight2.exe"},
{"backforblood", "back4blood.exe"}, {"back4blood", "back4blood.exe"}, {"b4b", "back4blood.exe"},
{"gtfo", "gtfo.exe"}, {"readyornot", "readyornot.exe"},
{"insurgency", "insurgency.exe"}, {"sandstorm", "insurgency.exe"},
{"hellletloose", "hellletloose.exe"}, {"hll", "hellletloose.exe"},
{"postscriptum", "postscriptum.exe"}, {"scriptum", "postscriptum.exe"},
{"beyondthewire", "beyondwire.exe"}, {"beyondwire", "beyondwire.exe"},
{"mordhau", "mordhau.exe"}, {"chivalry", "chivalry2.exe"}, {"scum", "scum.exe"},
{"miscreated", "miscreated.exe"}, {"freefire", "freefire.exe"},
{"gameloop", "gameloop.exe"}, {"bluestacks", "bluestacks.exe"}, {"ldplayer", "ldplayer.exe"}, {"nox", "nox.exe"},
{"csgo", "csgo.exe"}, {"cs2", "cs2.exe"}, {"counter", "cs2.exe"}, {"counterstrike", "cs2.exe"},
{"valorant", "valorant.exe"}, {"val", "valorant.exe"},
{"fortnite", "fortniteclient-win64-shipping.exe"}, {"fort", "fortniteclient-win64-shipping.exe"},
{"roblox", "RobloxPlayerBeta.exe"}, {"battlefield", "bf2042.exe"}, {"bf2042", "bf2042.exe"},
{"genshin", "GenshinImpact.exe"}, {"genshinimpact", "GenshinImpact.exe"},
{NULL, NULL}
};
// Persian aliases (UTF-8) -> db match key
struct AiFaAlias { const char* fa; const char* match; };
static const AiFaAlias kFaAliases[] = {
{"جی تی ای وی", "gta5.exe"}, {"جی تی ای", "gta5.exe"}, {"جی‌تی‌ای", "gta5.exe"},
{"کانتر", "cs2.exe"}, {"ولورانت", "valorant.exe"}, {"ولورنت", "valorant.exe"},
{"فورتنایت", "fortniteclient-win64-shipping.exe"}, {"فورنایت", "fortniteclient-win64-shipping.exe"},
{"رد دد", "rdr2.exe"}, {"ردد", "rdr2.exe"}, {"راست", "rust.exe"},
{"سایبرپانک", "cyberpunk2077.exe"}, {"الدن", "eldenring.exe"},
{"ماینکرفت", "minecraft.exe"}, {"پابجی", "pubg.exe"}, {"اپکس", "apex_legends.exe"},
{"وارزون", "warzone.exe"}, {"کالاف", "modernwarfare.exe"}, {"کال آف", "modernwarfare.exe"},
{"اورواچ", "overwatch.exe"}, {"اورواچ", "overwatch.exe"}, {"لیگ", "league of legends.exe"},
{"دوتا", "dota2.exe"}, {"راکت لیگ", "rocketleague.exe"},
{"اف سی ۲۴", "fifa24.exe"}, {"اف سی ۲۵", "fc25.exe"}, {"اف سی", "fc25.exe"},
{"فیفا", "fc25.exe"}, {"فوتبال", "fc25.exe"}, {"ویچر", "witcher3.exe"},
{"هری پاتر", "hogwartslegacy.exe"}, {"هاگوارتز", "hogwartslegacy.exe"},
{"استارفیلد", "starfield.exe"}, {"بالدور", "baldursgate3.exe"}, {"پالورلد", "palworld.exe"},
{"هل دایور", "helldivers2.exe"}, {"تکن", "tekken8.exe"}, {"استریت فایتر", "streetfighter6.exe"},
{"رینبو", "rainbow6.exe"}, {"تارکوف", "tarkov.exe"}, {"تارکف", "tarkov.exe"},
{"دیزی", "dayz.exe"}, {"آرما", "arma3.exe"}, {"اسکواد", "squad.exe"},
{"فورتزا", "forzahorizon5.exe"}, {"هورایزن", "horizon.exe"},
{"نید فور اسپید", "nfsunbound.exe"}, {"ان اف اس", "nfsunbound.exe"},
{"واو", "wow.exe"}, {"دیابلو", "diablo4.exe"}, {"تراریا", "terraria.exe"},
{"استاردو", "stardewvalley.exe"}, {"فکتوریو", "factorio.exe"}, {"سیتیز", "cities.exe"},
{"ایج آف", "aoe4.exe"}, {"استارکرفت", "starcraft2.exe"}, {"وارکرفت", "warcraft3.exe"},
{"سکیرو", "sekiro.exe"}, {"دارک سولز", "ds3.exe"},
{"گاد آو وار", "godofwar.exe"}, {"گاد", "godofwar.exe"},
{"مرد عنکبوتی", "spiderman.exe"}, {"اسپایدر", "spiderman.exe"},
{"لست آف آس", "tlou.exe"}, {"آنچارتد", "uncharted.exe"},
{"سوشیما", "ghostoftsushima.exe"}, {"گوست", "ghostoftsushima.exe"},
{"فاینال", "finalfantasy7.exe"}, {"پرسونا", "persona5.exe"}, {"یاکوزا", "yakuza.exe"},
{"رزیدنت اویل", "residentevil4.exe"}, {"رزیدنت", "residentevil4.exe"},
{"ویلج", "re8.exe"}, {"سایلنت", "silent hill.exe"}, {"الن ویک", "alanwake2.exe"},
{"دث استرندینگ", "deathstranding.exe"}, {"مترو", "metroexodus.exe"}, {"دایینگ", "dyinglight2.exe"},
{"دووم", "doometernal.exe"}, {"دوم اترنال", "doometernal.exe"},
{"هیلو", "haloinfinite.exe"}, {"هالو", "haloinfinite.exe"},
{"دستینی", "destiny2.exe"}, {"دستنی", "destiny2.exe"},
{"فال گایز", "fallguys.exe"}, {"فالگایز", "fallguys.exe"},
{"امانگ آس", "amongus.exe"}, {"امانگ", "amongus.exe"},
{"کاپ هد", "cuphead.exe"}, {"اورکوکد", "overcooked.exe"}, {"سلست", "celeste.exe"},
{"ریموورلد", "rimworld.exe"}, {"کراس فایر", "crossfire.exe"}, {"زولا", "zula.exe"},
{"پوینت بلنک", "pointblank.exe"}, {"یوروتراک", "eurotrucks2.exe"}, {"فاینالز", "thefinals.exe"},
{"مارول", "marvelrivals.exe"}, {"ووکونگ", "blackmythwukong.exe"}, {"استاکر", "stalker2.exe"},
{"فرگپانک", "fragpunk.exe"}, {"روبلاکس", "RobloxPlayerBeta.exe"}, {"رابلاکس", "RobloxPlayerBeta.exe"},
{"بتلفیلد", "bf2042.exe"}, {"گنشین", "GenshinImpact.exe"}, {"گینشین", "GenshinImpact.exe"},
{"گیم لوپ", "gameloop.exe"}, {"بلواستکس", "bluestacks.exe"}, {"ناکس", "nox.exe"}, {"الدی", "ldplayer.exe"},
{"شبیه ساز", "gameloop.exe"}, {"شبیه‌ساز", "gameloop.exe"},
{NULL, NULL}
};

// Find game: returns db index or -1; fills alts with up to 3 alternatives.
static int AiFindGame(const std::wstring& q, const std::vector<GameSpec>& db, std::vector<int>& alts) {
    alts.clear();
    std::wstring low = ToLower(q);
    // 1. Persian aliases (longest wins)
    int faBest = -1; size_t faLen = 0;
    for (int i = 0; kFaAliases[i].fa; i++) {
        std::wstring k = Utf8ToWide(kFaAliases[i].fa);
        if (k.size() > faLen && low.find(ToLower(k)) != std::wstring::npos) {
            std::wstring want = ToLower(Utf8ToWide(kFaAliases[i].match));
            for (size_t d = 0; d < db.size(); d++)
                if (db[d].match == want) { faBest = (int)d; faLen = k.size(); break; }
        }
    }
    if (faBest >= 0) return faBest;
    // 2. latin aliases on normalized query (longest wins)
    std::wstring nq = AiNorm(q);
    int best = -1; size_t bestLen = 0;
    if (nq.size() >= 2) {
        for (int i = 0; kAliases[i].key; i++) {
            std::string k = kAliases[i].key;
            if (k.size() < bestLen || k.size() < 2) continue;
            std::wstring wk = Utf8ToWide(k);
            if (nq.find(wk) == std::wstring::npos) continue;
            if (k.size() == 2 && nq != wk) continue; // 2-letter keys must be the whole query
            std::wstring want = ToLower(Utf8ToWide(kAliases[i].match));
            for (size_t d = 0; d < db.size(); d++)
                if (db[d].match == want) {
                    if (k.size() > bestLen) { bestLen = k.size(); best = (int)d; alts.clear(); }
                    else if ((int)d != best && (int)alts.size() < 3) alts.push_back((int)d);
                    break;
                }
        }
    }
    if (best >= 0) return best;
    // 3. direct title/match word hit
    for (size_t d = 0; d < db.size() && alts.size() < 3; d++) {
        std::wstring base = db[d].match;
        size_t dot = base.find(L'.');
        if (dot != std::wstring::npos) base = base.substr(0, dot);
        std::wstring nb = AiNorm(base), nt = AiNorm(db[d].title);
        if (nb.size() >= 4 && (nq.find(nb) != std::wstring::npos || (nq.size() >= 4 && nb.find(nq) != std::wstring::npos))) {
            if (alts.empty() || alts[0] != (int)d) alts.push_back((int)d);
            continue;
        }
        if (nt.size() >= 4 && (nq.find(nt) != std::wstring::npos || (nq.size() >= 4 && nt.find(nq) != std::wstring::npos))) {
            if (alts.empty() || alts[0] != (int)d) alts.push_back((int)d);
        }
    }
    if (alts.size() == 1) { int r = alts[0]; alts.clear(); return r; }
    return -1;
}

// ---------- answer builders ----------
struct AiHw { std::wstring cpu; int cores, mhz, cpuTier; std::wstring gpu; int vramMB, gpuTier; int ramMB; };
static AiHw AiHardware() {
    AiHw h;
    h.cpu = SysCpuName(); h.cores = SysCpuCount(); h.mhz = SysCpuMHz();
    h.cpuTier = AiCpuTier(h.cpu, h.cores, h.mhz);
    h.gpu = SysGpuName(); h.vramMB = SysGpuVramMB();
    h.gpuTier = AiGpuTier(h.gpu, h.vramMB);
    h.ramMB = RamTotalMB();
    return h;
}
static const wchar_t* AiMark(int v) { return v >= 2 ? L"\x2705" : v == 1 ? L"\x26A0\xFE0F" : L"\x274C"; }

static std::wstring AiVerdict(const GameSpec& g, const AiHw& h, bool fa) {
    int ramGB = (h.ramMB + 512) / 1024;
    int pc = h.cpuTier >= g.recCpu ? 2 : h.cpuTier >= g.minCpu ? 1 : 0;
    int pg = h.gpuTier >= g.recGpu ? 2 : h.gpuTier >= g.minGpu ? 1 : 0;
    int pr = ramGB >= g.recRam ? 2 : ramGB >= g.minRam ? 1 : 0;
    int total = pc + pg + pr;
    int wantVram = g.recGpu >= 5 ? 8 : g.recGpu == 4 ? 6 : g.recGpu == 3 ? 4 : 2;
    bool vramShort = h.vramMB > 0 && h.vramMB < wantVram * 1024;
    std::wstring o, ghz = AiGhz(h.mhz);
    if (!fa) {
        o += WFormat(L"\U0001F3AE %s \u2014 Can you run it?\r\n", g.title.c_str());
        o += L"------------------------------\r\n";
        o += WFormat(L"CPU: %s%s (%d cores, Tier %d/5) %s\r\n", h.cpu.c_str(),
            ghz.empty() ? L"" : WFormat(L" @ %s", ghz.c_str()).c_str(), h.cores, h.cpuTier, AiMark(pc));
        o += WFormat(L"     needs Tier %d min / %d rec\r\n", g.minCpu, g.recCpu);
        o += WFormat(L"GPU: %s (Tier %d/5) %s\r\n", h.gpu.c_str(), h.gpuTier, AiMark(pg));
        o += WFormat(L"     needs Tier %d min / %d rec\r\n", g.minGpu, g.recGpu);
        o += WFormat(L"RAM: %d GB (needs %d min / %d rec) %s\r\n", ramGB, g.minRam, g.recRam, AiMark(pr));
        if (h.vramMB > 0)
            o += WFormat(L"VRAM: %d GB (this game likes %d+ GB) %s\r\n",
                (h.vramMB + 512) / 1024, wantVram, vramShort ? L"\x26A0\xFE0F" : L"\x2705");
        else
            o += WFormat(L"VRAM: unknown (this game likes %d+ GB)\r\n", wantVram);
        o += L"\r\n";
        if (total >= 6) o += L"\x2705 VERDICT: Runs great! Expect 80\u2013144+ FPS on High/Ultra at 1080p.\r\n";
        else if (total >= 4) o += L"\x2705 VERDICT: Runs well! Expect 60+ FPS on Medium\u2013High.\r\n";
        else if (total >= 2) o += L"\x26A0\xFE0F VERDICT: Playable on Low. Expect 30\u201360 FPS \u2014 use Performance mode and 720p\u20131080p.\r\n";
        else o += L"\x274C VERDICT: Below minimum. It will stutter badly (\u201330 FPS). Upgrade needed \u2014 see weakest part below.\r\n";
        if (total < 6) {
            o += L"\r\nWeakest part: ";
            if (pc <= pg && pc <= pr) o += L"CPU \u2014 a newer processor would help most.";
            else if (pg <= pr) o += L"GPU \u2014 a stronger graphics card would help most.";
            else o += WFormat(L"RAM \u2014 consider %d GB.", g.recRam);
            o += L"\r\n";
        }
        if (!g.tip.empty()) o += WFormat(L"\r\n\U0001F4A1 Tip: %s\r\n", g.tip.c_str());
        if (!g.hasSpecs) o += L"\r\nNote: exact requirements unknown \u2014 estimate for a typical 3D game.\r\n";
        o += L"\r\nNext: add it in My Games, then Boost & Launch for max FPS.";
    } else {
        o += WFormat(L"\U0001F3AE %s \u2014 آیا اجرا می‌شود؟\r\n", g.title.c_str());
        o += L"------------------------------\r\n";
        o += WFormat(L"پردازنده: %s%s (%d هسته، سطح %d از ۵) %s\r\n", h.cpu.c_str(),
            ghz.empty() ? L"" : WFormat(L" @ %s", ghz.c_str()).c_str(), h.cores, h.cpuTier, AiMark(pc));
        o += WFormat(L"     نیاز بازی: حداقل سطح %d / پیشنهادی %d\r\n", g.minCpu, g.recCpu);
        o += WFormat(L"گرافیک: %s (سطح %d از ۵) %s\r\n", h.gpu.c_str(), h.gpuTier, AiMark(pg));
        o += WFormat(L"     نیاز بازی: حداقل سطح %d / پیشنهادی %d\r\n", g.minGpu, g.recGpu);
        o += WFormat(L"رم: %d گیگ (نیاز: حداقل %d / پیشنهادی %d) %s\r\n", ramGB, g.minRam, g.recRam, AiMark(pr));
        if (h.vramMB > 0)
            o += WFormat(L"حافظه گرافیک: %d گیگ (این بازی %d+ گیگ می‌خواهد) %s\r\n",
                (h.vramMB + 512) / 1024, wantVram, vramShort ? L"\x26A0\xFE0F" : L"\x2705");
        else
            o += WFormat(L"حافظه گرافیک: نامشخص (این بازی %d+ گیگ می‌خواهد)\r\n", wantVram);
        o += L"\r\n";
        if (total >= 6) o += L"\x2705 نتیجه: عالی اجرا می‌شود! انتظار ۸۰ تا ۱۴۴+ فریم روی High/Ultra در 1080p.\r\n";
        else if (total >= 4) o += L"\x2705 نتیجه: خوب اجرا می‌شود! انتظار ۶۰+ فریم روی Medium-High.\r\n";
        else if (total >= 2) o += L"\x26A0\xFE0F نتیجه: فقط روی Low قابل بازی است. انتظار ۳۰ تا ۶۰ فریم \u2014 از Performance mode استفاده کن.\r\n";
        else o += L"\x274C نتیجه: سیستم ضعیف‌تر از حداقل است. بازی شدیداً لگ می‌زند (زیر ۳۰ فریم). ارتقا لازم است.\r\n";
        if (total < 6) {
            o += L"\r\nضعیف‌ترین قطعه: ";
            if (pc <= pg && pc <= pr) o += L"پردازنده \u2014 ارتقای آن بیشترین کمک را می‌کند.";
            else if (pg <= pr) o += L"کارت گرافیک \u2014 ارتقای آن بیشترین کمک را می‌کند.";
            else o += WFormat(L"رم \u2014 ارتقا به %d گیگ پیشنهاد می‌شود.", g.recRam);
            o += L"\r\n";
        }
        if (!g.tip.empty()) o += WFormat(L"\r\n\U0001F4A1 نکته: %s\r\n", g.tip.c_str());
        if (!g.hasSpecs) o += L"\r\nتوجه: نیاز دقیق این بازی ثبت نشده \u2014 تخمین برای یک بازی سه‌بعدی معمولی است.\r\n";
        o += L"\r\nقدم بعد: در «بازی‌های من» اضافه‌اش کن و با Boost & Launch اجرایش کن تا بیشترین فریم را بگیری.";
    }
    return o;
}

static std::wstring AiGameTips(const GameSpec& g, const AiHw& h, bool fa) {
    std::wstring o;
    if (!fa) {
        o += WFormat(L"\U0001F3AE FPS tips for %s:\r\n", g.title.c_str());
        if (!g.tip.empty()) o += WFormat(L"\u2022 %s\r\n", g.tip.c_str());
        o += L"\u2022 Lower resolution scale / 3D resolution first \u2014 biggest FPS gain.\r\n";
        o += L"\u2022 Shadows, anti-aliasing and grass/draw-distance eat the most FPS.\r\n";
        o += L"\u2022 Use exclusive Fullscreen + High GPU preference (My Games \u2192 Boost & Launch does it).\r\n";
        o += WFormat(L"\u2022 Your GPU tier is %d/5 with %d GB RAM \u2014 ", h.gpuTier, (h.ramMB + 512) / 1024);
        o += (h.gpuTier >= 4 ? L"you can push High settings." : h.gpuTier == 3 ? L"Medium is the sweet spot." : L"stick to Low + Performance mode.");
    } else {
        o += WFormat(L"\U0001F3AE نکات افزایش فریم %s:\r\n", g.title.c_str());
        if (!g.tip.empty()) o += WFormat(L"\u2022 %s\r\n", g.tip.c_str());
        o += L"\u2022 اول رزولوشن/کیفیت سه‌بعدی را کم کن \u2014 بیشترین تأثیر را دارد.\r\n";
        o += L"\u2022 سایه، آنتی‌الیاسینگ و فاصله دید بیشترین فریم را می‌خورند.\r\n";
        o += L"\u2022 از Fullscreen اختصاصی استفاده کن (Boost & Launch خودش این‌ها را ست می‌کند).\r\n";
        o += WFormat(L"\u2022 سطح گرافیک تو %d از ۵ است \u2014 ", h.gpuTier);
        o += (h.gpuTier >= 4 ? L"می‌توانی High بازی کنی." : h.gpuTier == 3 ? L"بهترین حالت Medium است." : L"روی Low + حالت Performance بمان.");
    }
    return o;
}

static std::wstring AiSpecs(const AiHw& h, bool fa) {
    std::wstring ghz = AiGhz(h.mhz);
    if (!fa) {
        std::wstring o = L"\U0001F4BB Your PC:\r\n";
        o += WFormat(L"CPU: %s%s (%d cores) \u2014 Tier %d/5\r\n", h.cpu.c_str(),
            ghz.empty() ? L"" : WFormat(L" @ %s", ghz.c_str()).c_str(), h.cores, h.cpuTier);
        o += WFormat(L"GPU: %s \u2014 Tier %d/5", h.gpu.c_str(), h.gpuTier);
        if (h.vramMB > 0) o += WFormat(L" (%d GB VRAM)", (h.vramMB + 512) / 1024);
        o += L"\r\n";
        o += WFormat(L"RAM: %d GB (%d MB free now)\r\n", (h.ramMB + 512) / 1024, RamAvailMB());
        o += WFormat(L"OS: %s\r\n", SysOsString().c_str());
        o += L"\r\nAsk me: \"Can I run GTA V?\" or \"How do I boost FPS?\"";
        return o;
    }
    std::wstring o = L"\U0001F4BB مشخصات سیستم تو:\r\n";
    o += WFormat(L"پردازنده: %s%s (%d هسته) \u2014 سطح %d از ۵\r\n", h.cpu.c_str(),
        ghz.empty() ? L"" : WFormat(L" @ %s", ghz.c_str()).c_str(), h.cores, h.cpuTier);
    o += WFormat(L"گرافیک: %s \u2014 سطح %d از ۵", h.gpu.c_str(), h.gpuTier);
    if (h.vramMB > 0) o += WFormat(L" (%d گیگ حافظه)", (h.vramMB + 512) / 1024);
    o += L"\r\n";
    o += WFormat(L"رم: %d گیگ (%d مگابایت الان خالی است)\r\n", (h.ramMB + 512) / 1024, RamAvailMB());
    o += WFormat(L"ویندوز: %s\r\n", SysOsString().c_str());
    o += L"\r\nاز من بپرس: «GTA V اجرا میشه؟» یا «چطور اف‌پی‌اس را بالا ببرم؟»";
    return o;
}

static std::wstring AiRam(const AiHw& h, bool fa) {
    int tot = (h.ramMB + 512) / 1024, use = RamUsagePercent(), av = RamAvailMB();
    if (!fa) {
        std::wstring o = WFormat(L"\U0001F9E0 RAM: %d GB total, %d%% in use, %d MB free.\r\n\r\n", tot, use, av);
        if (use >= 85) o += L"Memory is nearly full! Close Chrome/Discord, then use System \u2192 Clean RAM.\r\n";
        else if (use >= 65) o += L"Memory is getting tight. Close unused apps before gaming.\r\n";
        else o += L"Memory looks healthy for gaming.\r\n";
        o += L"\r\nTip: the Live Processes page can trim background apps and lock extra RAM for your game (RAM Focus).";
        if (tot < 16) o += L"\r\nFor modern AAA games, 16 GB is the sweet spot.";
        return o;
    }
    std::wstring o = WFormat(L"\U0001F9E0 رم: %d گیگ کل، %d%% در حال استفاده، %d مگ خالی.\r\n\r\n", tot, use, av);
    if (use >= 85) o += L"رم تقریباً پر است! کروم و دیسکورد را ببند، بعد از «سیستم» گزینه Clean RAM را بزن.\r\n";
    else if (use >= 65) o += L"رم دارد تنگ می‌شود. قبل از بازی برنامه‌های اضافه را ببند.\r\n";
    else o += L"وضع رم برای بازی خوب است.\r\n";
    o += L"\r\nنکته: در صفحه «پردازش‌های زنده» می‌توانی رم برنامه‌های پس‌زمینه را خالی و رم بیشتری به بازی اختصاص بدهی (RAM Focus).";
    if (tot < 16) o += L"\r\nبرای بازی‌های جدید، ۱۶ گیگ رم ایده‌آل است.";
    return o;
}

static std::wstring AiNet(bool fa) {
    if (!fa) return L"\U0001F310 Lower ping guide:\r\n"
        L"\u2022 Use a cable instead of Wi-Fi \u2014 biggest win.\r\n"
        L"\u2022 Close downloads, streams and cloud sync while playing.\r\n"
        L"\u2022 Internet page: test DNS servers and apply the fastest.\r\n"
        L"\u2022 Pick game servers closest to you (lowest ms).\r\n"
        L"\u2022 Tweaks Center: network throttling + Game GPU priority are already in One-Click Boost.";
    return L"\U0001F310 راهنمای کاهش پینگ:\r\n"
        L"\u2022 به‌جای وای‌فای از کابل استفاده کن \u2014 بیشترین تأثیر.\r\n"
        L"\u2022 موقع بازی دانلود، استریم و سینک ابری را ببند.\r\n"
        L"\u2022 در صفحه اینترنت، DNSها را تست کن و سریع‌ترین را اعمال کن.\r\n"
        L"\u2022 سرور بازی نزدیک به خودت را انتخاب کن (کمترین ms).\r\n"
        L"\u2022 توییک‌های شبکه با One-Click Boost اعمال می‌شوند.";
}

static std::wstring AiBoost(bool fa) {
    if (!fa) return L"\U0001F680 Boost FPS in 4 steps:\r\n"
        L"1. One-Click Boost page \u2192 Start (applies all recommended tweaks).\r\n"
        L"2. Add your game in My Games \u2192 Boost & Launch (priority, GPU, timer).\r\n"
        L"3. Close Chrome/Discord/launchers before playing.\r\n"
        L"4. Update your GPU driver; use exclusive Fullscreen in-game.\r\n"
        L"\r\nFor max gains also try MAX FPS mode on the Boost page.";
    return L"\U0001F680 افزایش فریم در ۴ قدم:\r\n"
        L"1. صفحه بوست تک‌کلیکی \u2192 دکمه Start (همه توییک‌های پیشنهادی).\r\n"
        L"2. بازی را در «بازی‌های من» اضافه کن \u2192 بعد Boost & Launch.\r\n"
        L"3. قبل از بازی کروم و دیسکورد و لانچرها را ببند.\r\n"
        L"4. درایور گرافیک را آپدیت کن؛ داخل بازی Fullscreen بگذار.\r\n"
        L"\r\nبرای حداکثر نتیجه، حالت MAX FPS را هم امتحان کن.";
}

static std::wstring AiAbout(bool fa) {
    if (!fa) return L"\U0001F916 I am the FPS Booster AI advisor. I live inside this app, work 100% offline, "
        L"and know 120+ games. Ask me \"Can I run GTA V?\" and I will compare it with your PC.";
    return L"\U0001F916 من مشاور هوشمند FPS Booster هستم. داخل همین برنامه زندگی می‌کنم، کاملاً آفلاین کار می‌کنم "
        L"و ۱۲۰+ بازی را می‌شناسم. بپرس «GTA V اجرا میشه؟» تا با سیستمت مقایسه‌اش کنم.";
}

static std::wstring AiHelp(bool fa) {
    if (!fa) return L"\U0001F916 I can help with:\r\n"
        L"\u2022 \"Can I run GTA V / Valorant / ...?\" \u2014 full verdict\r\n"
        L"\u2022 \"FPS tips for Rust\" \u2014 best settings per game\r\n"
        L"\u2022 \"My specs?\" / \"RAM?\" / \"Ping?\" \u2014 PC answers\r\n"
        L"\u2022 \"How do I boost FPS?\" \u2014 step-by-step\r\n"
        L"\u2022 Anything else \u2014 online AI answers in seconds\r\n"
        L"\r\nYou can also ask about: resolution, RAM lock, processes, hotkey, themes, updates.\r\n\r\nI speak English and Persian. I work offline, inside this app.";
    return L"\U0001F916 من این کارها را بلدم:\r\n"
        L"\u2022 «GTA V اجرا میشه؟» \u2014 بررسی کامل سیستم\r\n"
        L"\u2022 «نکات فریم Rust» \u2014 بهترین تنظیمات هر بازی\r\n"
        L"\u2022 «مشخصات سیستم؟» / «رم؟» / «پینگ؟»\r\n"
        L"\u2022 «چطور اف‌پی‌اس را بالا ببرم؟» \u2014 قدم‌به‌قدم\r\n"
        L"\u2022 هر سوال دیگری \u2014 هوش آنلاین در چند ثانیه جواب می‌دهد\r\n"
        L"\r\nدرباره رزولوشن، قفل رم، پردازش‌ها، هات‌کی، تم و آپدیت هم بپرس.\r\n\r\nفارسی و انگلیسی می‌فهمم و کاملاً آفلاین داخل همین برنامه کار می‌کنم.";
}

static std::wstring AiThanks(bool fa) {
    if (!fa) return L"\U0001F60E Anytime! Good luck and high FPS. Ask me about any other game.";
    return L"\U0001F60E خواهش می‌کنم! موفق باشی و فریمت بالا. درباره هر بازی دیگری هم بپرس.";
}

static std::wstring AiUnknown(const std::vector<int>& alts, const std::vector<GameSpec>& db, bool fa) {
    std::wstring o;
    if (!alts.empty()) {
        o = fa ? L"\U0001F914 منظورت کدام است؟\r\n" : L"\U0001F914 Did you mean:\r\n";
        for (size_t i = 0; i < alts.size(); i++)
            o += WFormat(L"\u2022 %s\r\n", db[alts[i]].title.c_str());
        return o;
    }
    if (!fa) return L"\U0001F914 I don't know that game yet. I know 120+ popular titles \u2014 try GTA V, Valorant, "
        L"Fortnite, CS2, Minecraft, RDR2, Elden Ring, Rust, Apex, Warzone, FC 25, Dota 2...";
    return L"\U0001F914 این بازی را نمی‌شناسم. ۱۲۰+ بازی معروف را بلدم \u2014 امتحان کن: GTA V، ولورانت، فورتنایت، "
        L"کانتر، ماینکرفت، RDR2، الدن رینگ، راست، اپکس، وارزون، فیفا، دوتا...";
}

static std::wstring AiFallback(bool fa) {
    if (!fa) return L"\U0001F914 Hmm, I didn't get that. Try:\r\n"
        L"\u2022 \"Can I run GTA V?\"\r\n\u2022 \"FPS tips for Rust\"\r\n\u2022 \"My specs?\"\r\n\u2022 \"How do I boost FPS?\"";
    return L"\U0001F914 نفهمیدم چی گفتی. این‌ها را امتحان کن:\r\n"
        L"\u2022 «GTA V اجرا میشه؟»\r\n\u2022 «نکات فریم Rust»\r\n\u2022 «مشخصات سیستم؟»\r\n\u2022 «چطور اف‌پی‌اس را بالا ببرم؟»";
}

static std::wstring AiTheme(bool fa) {
    if (!fa) return L"\U0001F316 Theme: Settings page \u2192 Theme = Dark / Light / Auto (follows Windows).\r\n"
        L"\u2022 4 accent colors: Neon, Emerald, Sunset, Violet\r\n"
        L"\u2022 Quick button (moon/sun) at the top toggles instantly, no restart.";
    return L"\U0001F316 تم: صفحه تنظیمات \u2192 پوسته = تیره / روشن / خودکار (دنبال‌کننده ویندوز).\r\n"
        L"\u2022 چهار رنگ اصلی: نئون، زمردی، غروب، بنفش\r\n"
        L"\u2022 دکمه سریع ماه/خورشید بالا هم فوری عوض می‌کند، بدون ری‌استارت.";
}
static std::wstring AiHotkey(bool fa) {
    if (!fa) return L"\u2328\uFE0F Global hotkey Ctrl+Alt+B boosts from anywhere in Windows.\r\n"
        L"\u2022 It opens the app on the Boost page and starts the boost\r\n"
        L"\u2022 Toggle it in Settings if another app uses the same keys.";
    return L"\u2328\uFE0F هات‌کی سراسری Ctrl+Alt+B از هرجای ویندوز بوست می‌زند.\r\n"
        L"\u2022 برنامه را روی صفحه بوست باز می‌کند و بوست را شروع می‌کند\r\n"
        L"\u2022 اگر برنامه دیگری همین کلیدها را دارد، از تنظیمات خاموشش کن.";
}
static std::wstring AiUpdate(bool fa) {
    if (!fa) return L"\U0001F504 Updates: Settings page \u2192 Check for updates.\r\n"
        L"\u2022 Needs internet; compares your version with GitHub Releases\r\n"
        L"\u2022 If a new version exists, download it from the Releases page.";
    return L"\U0001F504 آپدیت: صفحه تنظیمات \u2192 بررسی آپدیت.\r\n"
        L"\u2022 اینترنت لازم دارد؛ نسخه تو را با گیت‌هاب مقایسه می‌کند\r\n"
        L"\u2022 اگر نسخه جدید باشد، از صفحه Releases دانلودش کن.";
}
static std::wstring AiRes(bool fa) {
    if (!fa) return L"\U0001F5A5\uFE0F Resolution = free FPS: My Games \u2192 tick \u201CLower resolution\u201D \u2192 pick 720p/900p/1080p.\r\n"
        L"\u2022 Game launches low (huge FPS gain), resolution auto-restores on exit\r\n"
        L"\u2022 Upscalers (NIS / RSR / FSR / DLSS) render low then upscale: enable them in NVIDIA/AMD panel or in-game for sharp + fast image.";
    return L"\U0001F5A5\uFE0F رزولوشن = اف‌پی‌اس مجانی: بازی‌های من \u2192 تیک \u201Cرزولوشن پایین‌تر\u201D \u2192 انتخاب 720p/900p/1080p.\r\n"
        L"\u2022 بازی پایین اجرا می‌شود (اف‌پی‌اس خیلی بیشتر) و بعد از خروج رزولوشن خودکار برمی‌گردد\r\n"
        L"\u2022 آپ‌اسکیلرها (NIS / RSR / FSR / DLSS) پایین رندر و بعد شارپ می‌کنند: از پنل انویدیا/ای‌ام‌دی یا داخل بازی روشنشان کن.";
}
static std::wstring AiProc(bool fa) {
    if (!fa) return L"\U0001F4CA Live Processes page controls ANY running program:\r\n"
        L"\u2022 Boost priority / RAM Focus / Lock RAM (up to MAX, guaranteed)\r\n"
        L"\u2022 + Library pins any process as a game profile; right-click menu, double-click = boost, Kill for frozen apps.";
    return L"\U0001F4CA صفحه پردازش‌های زنده، هر برنامه در حال اجرا را کنترل می‌کند:\r\n"
        L"\u2022 بوست اولویت / تمرکز رم / قفل رم (تا حداکثر، تضمینی)\r\n"
        L"\u2022 دکمه کتابخانه هر پردازش را پروفایل بازی می‌کند؛ راست‌کلیک منو دارد، دابل‌کلیک = بوست، بستن برای برنامه قفل‌کرده.";
}
static std::wstring AiLang(bool fa) {
    if (!fa) return L"\U0001F310 Language: Settings page, or the EN button at the top.\r\n"
        L"\u2022 The app relaunches itself in the new language (English / Persian)\r\n"
        L"\u2022 Everything is translated: pages, AI, messages, even the guide.";
    return L"\U0001F310 زبان: صفحه تنظیمات، یا دکمه EN/فا بالای پنجره.\r\n"
        L"\u2022 برنامه خودش را با زبان جدید اجرا می‌کند (انگلیسی / فارسی)\r\n"
        L"\u2022 همه‌چیز ترجمه شده: صفحه‌ها، هوش مصنوعی، پیام‌ها، حتی راهنما.";
}

// ---------- main entry ----------
std::wstring Ai_Answer(const std::wstring& q) {
    bool fa = Strings_GetLang() == 1;
    if (q.empty()) return AiHelp(fa);
    std::wstring low = ToLower(q);
    std::vector<GameSpec> db;
    Games_Specs(db);
    std::vector<int> alts;
    int gi = AiFindGame(q, db, alts);

    static const char* thanksK[] = {"thanks", "thank you", "مرسی", "ممنون", "تشکر", "دمت", NULL};
    for (int i = 0; thanksK[i]; i++)
        if (AiHasU8(low, thanksK[i]) && low.size() < 40) return AiThanks(fa);

    static const char* greetK[] = {"hello", "hi", "hey", "salam", "dorood", "سلام", "درود", "صبح بخیر", "عصر بخیر", NULL};
    bool greet = false;
    for (int i = 0; greetK[i]; i++)
        if (AiHasU8(low, greetK[i])) { greet = true; break; }
    if (greet && gi < 0 && low.size() < 30) return AiHelp(fa);

    static const char* helpK[] = {"help", "guide", "what can", "abilities", "who are", "کمک", "راهنما", "چی کار", "چکار", "توانایی", "کی هستی", "تو کی", NULL};
    for (int i = 0; helpK[i]; i++)
        if (AiHasU8(low, helpK[i]) && gi < 0) return AiHelp(fa);

    // game questions
    bool canRun = AiHas(low, L"run") || AiHas(low, L"play") || AiHas(low, L"can ") || AiHas(low, L"will ") ||
        AiHas(low, L"work") || AiHas(low, L"handle") || AiHas(low, L"enough") || AiHas(low, L"support");
    static const char* canRunFa[] = {"اجرا", "میاد", "می‌آید", "میکشه", "می‌کشه", "سیستم", "کامپیوتر", "لپتاپ", "لبتاب", "کافی", "جواب", "ساپورت", "پشتیبانی", "می‌تونه", "میتونه", "آیا", "برام", "میاره", "میکشه؟", "میاد؟", NULL};
    for (int i = 0; canRunFa[i]; i++)
        if (AiHasU8(low, canRunFa[i])) { canRun = true; break; }
    bool fpsQ = AiHas(low, L"fps") || AiHas(low, L"frame") || AiHas(low, L"lag") || AiHas(low, L"stutter") ||
        AiHas(low, L"boost") || AiHas(low, L"setting") || AiHas(low, L"performance") || AiHas(low, L"smooth");
    static const char* fpsFa[] = {"اف‌پی‌اس", "اف پی اس", "فریم", "لگ", "بوست", "بهینه", "تنظیمات", "گرافیک", "روان", "افت", "گیر", "تاخیر", "تأخیر", "فپس", NULL};
    for (int i = 0; fpsFa[i]; i++)
        if (AiHasU8(low, fpsFa[i])) { fpsQ = true; break; }

    AiHw h = AiHardware();
    if (gi >= 0) {
        if (fpsQ && !canRun) return AiGameTips(db[gi], h, fa);
        return AiVerdict(db[gi], h, fa);
    }
    if (!alts.empty() && (canRun || fpsQ || low.size() < 40)) return AiUnknown(alts, db, fa);

    // hardware questions (no game)
    static const char* hwK[] = {"spec", "my pc", "my system", "my laptop", "cpu", "gpu", "processor", "مشخصات", "سیستم من", "پردازنده", "کارت", NULL};
    for (int i = 0; hwK[i]; i++)
        if (AiHasU8(low, hwK[i])) return AiSpecs(h, fa);
    if (AiHasU8(low, "lock ram") || AiHasU8(low, "ram lock") || AiHasU8(low, "قفل رم") || AiHasU8(low, "اختصاص رم"))
        return AiProc(fa);
    if (AiHasU8(low, "ram") || AiHasU8(low, "رم") || AiHasU8(low, "memory") || AiHasU8(low, "حافظه") || AiHasU8(low, "مموری"))
        return AiRam(h, fa);
    if (AiHasU8(low, "ping") || AiHasU8(low, "پینگ") || AiHasU8(low, "dns") || AiHasU8(low, "internet") ||
        AiHasU8(low, "اینترنت") || AiHasU8(low, "packet") || AiHasU8(low, "آنلاین"))
        return AiNet(fa);
    if (AiHasU8(low, "boost") || AiHasU8(low, "بوست") || AiHasU8(low, "optimize") || AiHasU8(low, "بهینه") ||
        AiHasU8(low, "more fps") || AiHasU8(low, "فریم بیشتر"))
        return AiBoost(fa);
    static const char* themeK[] = {"theme", "dark mode", "light mode", "accent", "پوسته", "حالت شب", "تم", "تیره", "روشن", "دارک", "لایت", NULL};
    for (int i = 0; themeK[i]; i++)
        if (AiHasU8(low, themeK[i])) return AiTheme(fa);
    static const char* hotK[] = {"hotkey", "hot key", "ctrl", "میانبر", "هات", NULL};
    for (int i = 0; hotK[i]; i++)
        if (AiHasU8(low, hotK[i])) return AiHotkey(fa);
    static const char* updK[] = {"update", "upgrade", "version", "آپدیت", "نسخه", NULL};
    for (int i = 0; updK[i]; i++)
        if (AiHasU8(low, updK[i])) return AiUpdate(fa);
    static const char* resK[] = {"resolution", "resol", "720", "1080", "1440", "upscale", "dlss", "fsr", "nis", "rsr", "رزولوشن", "رزولیشن", "اسکیل", "کیفیت تصویر", NULL};
    for (int i = 0; resK[i]; i++)
        if (AiHasU8(low, resK[i])) return AiRes(fa);
    static const char* procK[] = {"process", "lock ram", "ram lock", "قفل رم", "اختصاص رم", "پردازش", "task manager", "تسک", "kill", NULL};
    for (int i = 0; procK[i]; i++)
        if (AiHasU8(low, procK[i])) return AiProc(fa);
    static const char* langK[] = {"language", "lang", "زبان", "فارسی", "english", "انگلیسی", NULL};
    for (int i = 0; langK[i]; i++)
        if (AiHasU8(low, langK[i])) return AiLang(fa);
    if (canRun || AiHas(low, L"?") || AiHas(low, L"game") || AiHasU8(low, "بازی"))
        return AiUnknown(alts, db, fa);
    return AiFallback(fa);
}

// ---------- online AI (keyless HTTPS, async) ----------
static std::string AiUrlEncode(const std::string& in) {
    static const char* hex = "0123456789ABCDEF";
    std::string o;
    for (size_t i = 0; i < in.size(); i++) {
        unsigned char c = (unsigned char)in[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~') o += (char)c;
        else { o += '%'; o += hex[c >> 4]; o += hex[c & 15]; }
    }
    return o;
}
static std::wstring g_aiLastQ, g_aiLastA; // short conversation memory (online)
// Shared WinHTTP plumbing. Returns 1 = got HTTP response (code set), 0 = network failure.
static int AiHttp(const wchar_t* host, const wchar_t* method, const wchar_t* wpath,
                  const char* body, DWORD bodyLen, std::string& out, DWORD& code) {
    out.clear(); code = 0;
    HINTERNET ses = WinHttpOpen(L"FPSBooster/2.1.1", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!ses) return 0;
    WinHttpSetTimeouts(ses, 5000, 8000, 10000, 45000);
    int ret = 0;
    HINTERNET con = WinHttpConnect(ses, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (con) {
        HINTERNET req = WinHttpOpenRequest(con, method, wpath, NULL,
            WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
        if (req) {
            BOOL sent;
            if (body && bodyLen > 0) {
                WinHttpAddRequestHeaders(req, L"Content-Type: application/json", (ULONG)-1,
                    WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
                sent = WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                    (LPVOID)body, bodyLen, bodyLen, 0);
            } else {
                sent = WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                    WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
            }
            if (sent && WinHttpReceiveResponse(req, NULL)) {
                DWORD clen = sizeof(code);
                WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                    NULL, &code, &clen, NULL);
                char buf[4096];
                DWORD got = 0;
                while (out.size() < 12000 && WinHttpReadData(req, buf, sizeof(buf), &got) && got > 0)
                    out.append(buf, got);
                ret = 1;
            }
            WinHttpCloseHandle(req);
        }
        WinHttpCloseHandle(con);
    }
    WinHttpCloseHandle(ses);
    return ret;
}
// Drop \uD800-\uDFFF escapes (emoji) - the JSON parser has no surrogate-pair support.
static void AiStripSurrogates(std::string& s) {
    std::string o;
    o.reserve(s.size());
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '\\' && i + 5 < s.size() && s[i+1] == 'u' &&
            (s[i+2] == 'd' || s[i+2] == 'D')) {
            char h = s[i+3];
            bool hi = (h >= '8' && h <= '9') || (h >= 'a' && h <= 'f') || (h >= 'A' && h <= 'F');
            bool rest = true;
            for (int k = 4; k <= 5 && rest; k++) {
                char c = s[i+k];
                if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) rest = false;
            }
            if (hi && rest) { i += 5; continue; }
        }
        o += s[i];
    }
    s.swap(o);
}
static bool AiCleanAnswer(const std::string& in, std::wstring& out) {
    size_t st = 0, en = in.size();
    while (en > st && (in[en-1] == '\n' || in[en-1] == '\r' || in[en-1] == ' ' || in[en-1] == '\t')) en--;
    while (st < en && (in[st] == '\n' || in[st] == '\r' || in[st] == ' ')) st++;
    if (st >= en) return false;
    std::wstring w = Utf8ToWide(in.substr(st, en - st));
    out.clear();
    for (size_t i = 0; i < w.size(); i++) {
        if (w[i] == L'\r') continue;
        if (w[i] == L'\n') out += L"\r\n";
        else out += w[i];
    }
    return !out.empty();
}
static std::wstring AiSysPrompt(bool fa) {
    std::wstring specs = WFormat(fa ? L"مشخصات کاربر: %s؛ %s؛ %s؛ %s. " : L"User PC: %s; %s; %s; %s. ",
        SysCpuName().c_str(), SysGpuName().c_str(), SysRamString().c_str(), SysOsString().c_str());
    return (fa ? L"تو دستیار فارسی FPS Booster هستی. خیلی کوتاه (زیر ۱۲۰ کلمه) و کاربردی جواب بده. "
               : L"You are the FPS Booster assistant. Answer briefly (under 120 words), practical. ") + specs;
}
// Attempt codes: 1 ok, 0 network failure (do not retry), 2 http error (try next), 3 rate limited.
static int AiPostOpenAI(const std::wstring& q, bool fa, std::wstring& answer) {
    std::string jb = "{\"model\":\"openai\",\"messages\":[";
    jb += "{\"role\":\"system\",\"content\":\"" + JsonEscapeW(AiSysPrompt(fa)) + "\"}";
    if (!g_aiLastQ.empty()) {
        jb += ",{\"role\":\"user\",\"content\":\"" + JsonEscapeW(g_aiLastQ) + "\"}";
        jb += ",{\"role\":\"assistant\",\"content\":\"" + JsonEscapeW(g_aiLastA) + "\"}";
    }
    jb += ",{\"role\":\"user\",\"content\":\"" + JsonEscapeW(q) + "\"}]}";
    std::string out; DWORD code = 0;
    if (!AiHttp(L"text.pollinations.ai", L"POST", L"/openai", jb.c_str(), (DWORD)jb.size(), out, code))
        return 0;
    if (code == 429) return 3;
    if (code != 200 || out.empty()) return 2;
    AiStripSurrogates(out);
    JVal root;
    if (!ParseJson(out, root) || root.type != JVal::OBJ) return 2;
    const JVal* ch = root.find("choices");
    if (!ch || ch->type != JVal::ARR || ch->arr.empty()) return 2;
    const JVal* msg = ch->arr[0].find("message");
    if (!msg) return 2;
    return AiCleanAnswer(msg->str("content"), answer) ? 1 : 2;
}
static int AiGetPrompt(const std::wstring& q, bool fa, std::wstring& answer) {
    std::wstring mem;
    if (!g_aiLastQ.empty())
        mem = WFormat(fa ? L"گفتگوی قبلی: سوال «%s» / جواب «%s». " : L"Previous chat: Q: %s / A: %s. ",
            g_aiLastQ.c_str(), g_aiLastA.c_str());
    std::wstring full = AiSysPrompt(fa) + mem + (fa ? L"سوال: " : L"Question: ") + q.substr(0, 160);
    std::string path = "/" + AiUrlEncode(WideToUtf8(full)) + "?model=openai";
    if (path.size() > 3800 && !mem.empty()) {
        full = AiSysPrompt(fa) + (fa ? L"سوال: " : L"Question: ") + q.substr(0, 160);
        path = "/" + AiUrlEncode(WideToUtf8(full)) + "?model=openai";
    }
    if (path.size() > 4000) return 2;
    std::string out; DWORD code = 0;
    if (!AiHttp(L"text.pollinations.ai", L"GET", Utf8ToWide(path).c_str(), NULL, 0, out, code))
        return 0;
    if (code == 429) return 3;
    if (code != 200 || out.empty()) return 2;
    return AiCleanAnswer(out, answer) ? 1 : 2;
}
struct AiOnlineCtx { HWND w; std::wstring q; bool fa; };
static CRITICAL_SECTION g_aiCs;
static bool g_aiCsInit = false;
static std::wstring g_aiRes;
static bool g_aiOk = false;
static LONG g_aiBusy = 0;
static DWORD WINAPI AiOnlineThread(LPVOID arg) {
    AiOnlineCtx* c = (AiOnlineCtx*)arg;
    std::wstring a;
    int r = AiPostOpenAI(c->q, c->fa, a);
    if (r == 2 || r == 3) {
        int r2 = AiGetPrompt(c->q, c->fa, a);
        if (r2 == 1) r = 1;
        else if (r2 == 3) r = 3;
        else if (r == 2) r = r2;
    }
    if (r == 3) {
        PostMessageW(c->w, WM_APP_AI, 2, 0); // rate limited: wait out the window, retry once
        Sleep(16000);
        r = AiPostOpenAI(c->q, c->fa, a);
        if (r == 2) r = AiGetPrompt(c->q, c->fa, a);
    }
    bool ok = (r == 1);
    if (!g_aiCsInit) { InitializeCriticalSection(&g_aiCs); g_aiCsInit = true; }
    EnterCriticalSection(&g_aiCs);
    g_aiRes = a; g_aiOk = ok;
    LeaveCriticalSection(&g_aiCs);
    HWND w = c->w;
    delete c;
    PostMessageW(w, WM_APP_AI, ok ? 1 : 0, 0);
    InterlockedExchange(&g_aiBusy, 0);
    return 0;
}
bool AiOnline_AskAsync(HWND w, const std::wstring& q) {
    if (InterlockedCompareExchange(&g_aiBusy, 1, 0) != 0) return false;
    g_aiLastQ = q.substr(0, 80);
    AiOnlineCtx* c = new AiOnlineCtx;
    c->w = w; c->q = q; c->fa = Strings_GetLang() == 1;
    HANDLE h = CreateThread(NULL, 0, AiOnlineThread, c, 0, NULL);
    if (h) { CloseHandle(h); return true; }
    delete c;
    InterlockedExchange(&g_aiBusy, 0);
    return false;
}
bool AiOnline_TakeResult(std::wstring& a) {
    if (!g_aiCsInit) return false;
    EnterCriticalSection(&g_aiCs);
    a = g_aiRes;
    bool ok = g_aiOk;
    if (ok) g_aiLastA = a.substr(0, 80);
    g_aiRes.clear();
    LeaveCriticalSection(&g_aiCs);
    return ok;
}
// True when the online model would answer better than the offline engine:
// unknown/new games, open questions. Local data (specs/RAM/net) and known
// games stay offline: instant and more accurate.
bool Ai_NeedsOnline(const std::wstring& q) {
    if (q.empty()) return false;
    std::wstring low = ToLower(q);
    std::vector<GameSpec> db;
    Games_Specs(db);
    std::vector<int> alts;
    if (AiFindGame(q, db, alts) >= 0) return false;
    static const char* featK[] = {"dark mode", "light mode", "theme", "accent", "پوسته", "حالت شب",
        "hotkey", "hot key", "ctrl+alt", "میانبر", "هات‌کی", "هاتکی", "هات کی",
        "update", "upgrade", "version", "آپدیت", "نسخه",
        "resolution", "resol", "720", "1080", "1440", "upscale", "dlss", "fsr", "nis", "rsr",
        "رزولوشن", "رزولیشن", "اسکیل",
        "processes", "process ", "lock ram", "قفل رم", "اختصاص رم", "پردازش", "task manager", "kill ",
        "language", "lang", "زبان", "فارسی", "english", NULL};
    for (int i = 0; featK[i]; i++)
        if (AiHasU8(low, featK[i])) return false;
    bool canRun = AiHas(low, L"run") || AiHas(low, L"play") || AiHas(low, L"can ") || AiHas(low, L"will ") ||
        AiHas(low, L"work") || AiHas(low, L"handle");
    static const char* canRunFa[] = {"اجرا", "میاد", "می‌آید", "میکشه", "می‌کشه", "سیستم", "کامپیوتر",
        "لپتاپ", "لبتاب", "کافی", "جواب", "ساپورت", "پشتیبانی", "می‌تونه", "میتونه", "آیا", "برام", "میاره", NULL};
    for (int i = 0; canRunFa[i]; i++)
        if (AiHasU8(low, canRunFa[i])) { canRun = true; break; }
    if (canRun) return true;
    bool fpsQ = AiHas(low, L"fps") || AiHas(low, L"frame") || AiHas(low, L"lag") || AiHas(low, L"stutter") ||
        AiHas(low, L"boost") || AiHas(low, L"setting") || AiHas(low, L"performance") || AiHas(low, L"smooth");
    static const char* fpsFa[] = {"اف‌پی‌اس", "اف پی اس", "فریم", "لگ", "بوست", "بهینه", "تنظیمات",
        "گرافیک", "روان", "افت", "گیر", "تاخیر", "تأخیر", "فپس", NULL};
    for (int i = 0; fpsFa[i]; i++)
        if (AiHasU8(low, fpsFa[i])) { fpsQ = true; break; }
    if (fpsQ) return true;
    static const char* localK[] = {"spec", "my pc", "my system", "my laptop", "cpu", "gpu", "processor",
        "ram", "memory", "ping", "dns", "internet", "packet", "مشخصات", "سیستم من", "پردازنده", "کارت",
        "رم", "حافظه", "مموری", "پینگ", "اینترنت", "آنلاین", NULL};
    for (int i = 0; localK[i]; i++)
        if (AiHasU8(low, localK[i])) return false;
    if (low.size() < 40) {
        static const char* chat0[] = {"help", "guide", "what can", "abilities", "who are", "thanks", "thank you",
            "hello", "hey", "salam", "dorood", "کمک", "راهنما", "چی کار", "چکار", "توانایی", "کی هستی",
            "مرسی", "ممنون", "تشکر", "سلام", "درود", NULL};
        for (int i = 0; chat0[i]; i++)
            if (AiHasU8(low, chat0[i])) return false;
    }
    return true;
}
