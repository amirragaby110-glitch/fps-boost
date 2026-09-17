#!/bin/bash
# Extract verbatim parser code from src/ and run the Linux test harness.
set -e
cd "$(dirname "$0")/.."
python3 - <<'EOF'
util = open('src/util.cpp', encoding='utf-8').read()
marker = '// ---------- Mini JSON ----------'
assert util.count(marker) == 1
open('tests/json.inc', 'w', encoding='utf-8').write(util.split(marker)[1])

games = open('src/games.cpp', encoding='utf-8').read()
vdf_start = games.index('static std::vector<std::string> VdfTokens')
vdf_end = games.index('static std::wstring SlashFix')
open('tests/vdf.inc', 'w', encoding='utf-8').write(games[vdf_start:vdf_end])
print('inc files extracted')
EOF
g++ -O2 -Wall -o /tmp/fb/ptest tests/test_parsers.cpp
/tmp/fb/ptest
