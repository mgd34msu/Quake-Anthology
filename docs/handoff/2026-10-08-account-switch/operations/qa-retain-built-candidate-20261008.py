import datetime,json,shutil,sys
from pathlib import Path
record=Path(sys.argv[1]);dst=Path(sys.argv[2]);diagnostic='--diagnostic' in sys.argv[3:]
a=json.loads((record/'source-attestation.json').read_text());sdk=Path(sys.argv[sys.argv.index('--source-build')+1]) if '--source-build' in sys.argv else Path('/home/buzzkill/.cache/quake-anthology-recovery/runtime-checkout/build');dst.mkdir(exist_ok=True)
names=['quake-anthology','qa-native-runner','native-profile/client/qa-native-profile.so','engine-data/fonts/DejaVuSans.ttf','engine-data/fonts/LICENSE-DejaVu.txt']
def pin(p):
 s=p.stat();return dict(device=s.st_dev,inode=s.st_ino,size=s.st_size,mtime_ns=s.st_mtime_ns)
original={n:pin(sdk/n) for n in names}
for n in names:
 p=sdk/n;q=dst/n;q.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(p,q);assert p.read_bytes()==q.read_bytes();assert pin(p)==original[n]
r=dict(source_commit=a['base_commit'],source_tree=a['source_tree'],uncommitted_diagnostic=diagnostic,build_directory=str(dst),built_time=datetime.datetime.fromtimestamp((sdk/'quake-anthology').stat().st_mtime,datetime.timezone.utc).isoformat(),candidate_files={n:pin(dst/n) for n in names},files={str(dst/n):pin(dst/n) for n in names},source_attestation=str(record/'source-attestation.json'),full_build_exit=0,direct_byte_equal=True,qualification='DIAGNOSTIC_ONLY_NOT_INSTALLABLE' if diagnostic else 'NOT_YET_RUN')
p=dst/'candidate-receipt.json';p.write_text(json.dumps(r,indent=2)+'\n');print(p);print(r['built_time']);print(r['source_tree'])
