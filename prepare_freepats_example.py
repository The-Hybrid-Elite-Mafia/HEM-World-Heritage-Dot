"""Strict conversion of the verified FreePats guitar SFZ, not a general SFZ parser."""
from pathlib import Path
import re, json, hashlib, sys
pack=Path(sys.argv[1]).resolve()
sfz=pack/'SpanishClassicalGuitar-20190618.sfz'
original={p.relative_to(pack).as_posix():{'bytes':p.stat().st_size,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()} for p in pack.rglob('*') if p.is_file() and p.suffix in ('.wav','.sfz','.txt')}
body=re.sub(r'//[^\n]*','',sfz.read_text())
prefix,*regions=body.split('<region>')
assert 'ampeg_release=1.5' in prefix and 'loop_mode=no_loop' in prefix
zones=[]
for region in regions:
    ops=dict(re.findall(r'(\w+)=([^\s]+)',region))
    assert set(ops)<= {'sample','key','lokey','hikey','pitch_keycenter'},ops
    root=int(ops.get('key',ops.get('pitch_keycenter')))
    low=int(ops.get('key',ops.get('lokey'))); high=int(ops.get('key',ops.get('hikey')))
    path=ops['sample']; assert (pack/path).is_file()
    zones.append({'path':path,'root':root,'low':low,'high':high,'velocity_low':1,'velocity_high':127,'rr':0,'release_seconds':1.5})
assert len(zones)==48
for key in range(128):
    assert sum(z['low']<=key<=z['high'] for z in zones)==(1 if 29<=key<=88 else 0)
provenance='FreePats Spanish classical guitar 2019-06-18; recorded 2008 by roberto@zenvoid.org with AKG Perception120; CC0 1.0; https://freepats.zenvoid.org/Guitar/acoustic-guitar.html; original SFZ key zones and 1.5s release preserved'
lines=['WHIC_PRESET 2',json.dumps(provenance),str(len(zones))]
lines += [f'"{z["path"]}" {z["root"]} {z["low"]} {z["high"]} 1 127 0 1.5' for z in zones]
(pack/'SpanishClassicalGuitar.whic').write_text('\n'.join(lines)+'\n',encoding='utf8')
(pack/'SOURCE-MANIFEST.json').write_text(json.dumps({'official_page':'https://freepats.zenvoid.org/Guitar/acoustic-guitar.html','official_archive':'https://freepats.zenvoid.org/Guitar/SpanishClassicalGuitar/SpanishClassicalGuitar-SFZ-20190618.7z','license':'CC0 1.0','files':original,'zones':zones},indent=2))
print('48 verified SFZ zones; continuous MIDI29-88; original files hashed; portable WHIC preset v2 written.')
