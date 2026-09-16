"""Collect selected downloaded generations and build their audition folders."""
from pathlib import Path
import json,re,shutil,subprocess,sys
base=Path('build/auditions');library=base/'kit-library';library.mkdir(exist_ok=True)
configs=json.loads(Path('scripts/drum-kit-prompts.json').read_text())
selected=sys.argv[1:] or list(configs)
if any(kit not in configs for kit in selected):raise ValueError('Unknown kit')
for kit in selected:
 source=base/('kie-stems-'+kit);state=json.loads((source/'workflow.json').read_text())
 if state.get('stems',{}).get('status')!='success':raise RuntimeError(f'{kit}: separation incomplete')
 dest=library/kit;raw=dest/'Sources';raw.mkdir(parents=True,exist_ok=True)
 shutil.copy2(source/'workflow.json',raw/'workflow.json');shutil.copy2(source/'original.audio',raw/'Original.mp3')
 for i,url in enumerate(state['stems']['result']['resultUrls']):
  m=re.search(r'_([A-Za-z_]+)\.mp3(?:\?|$)',url)
  if not m:raise ValueError('Unrecognized stem filename')
  shutil.copy2(source/f'stem_resultUrls_{i}.audio',raw/(m[1]+'.mp3'))
 subprocess.run([sys.executable,'scripts/drum-kit-library.py','cut',str(raw),str(dest/'Samples'),'--kit',kit,'--limit','20'],check=True)
subprocess.run([sys.executable,'scripts/drum-kit-library.py','index',str(library)],check=True)
