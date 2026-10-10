#!/bin/zsh
# ab.sh <setName> [dry]: convert, render WT-1 for ab_wt.txt list, compare against Serum set.
S=/private/tmp/claude-501/-Users-mg-dev-fablesynth/c87532b3-bd02-4f23-a1e0-41c78841fb3e/scratchpad
cd $S && rm -rf bank && python3 -I convert.py s1dump cap bank > conv.log 2>&1 || { tail -3 conv.log; exit 1; }
SET=$S/$1; mkdir -p $SET/wtjson; rm -rf $SET/wt1 $SET/wtjson/*
python3 -I - "$2" "$SET" <<'PY'
import json, os, sys
dry, out = sys.argv[1] == 'dry', sys.argv[2]
for line in open('ab_wt.txt').read().split('\n'):
    if not line: continue
    p = json.load(open(line))
    if dry:
        for k in list(p['params']):
            if (k.startswith('fx.') and k.endswith('.on')) or k in ('filter.on', 'filter2.on'): p['params'][k] = 0
    json.dump(p, open(os.path.join(out, 'wtjson', os.path.basename(line)), 'w'))
PY
ls $SET/wtjson/*.json > $SET/list.txt
if [ ! -d $SET/serum ]; then mkdir -p $SET/serum; while read f; do s1host/build/s1host_artefacts/Release/s1host render "$f" "$SET/serum/$(basename "$f" .fxp).wav" $2 2>/dev/null; done < ab_fxp.txt; fi
cd /Users/mg/dev/fablesynth && PRESETS=$SET/list.txt OUT=$SET/wt1 npx vitest run --config $S/wtr/vt.config.mjs > $S/wtr.log 2>&1 || { grep Error $S/wtr.log | head -3; exit 1; }
python3 -I $S/abcmp.py $SET
