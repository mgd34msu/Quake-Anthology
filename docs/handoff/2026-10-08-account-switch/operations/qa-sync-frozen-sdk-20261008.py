import io,json,subprocess,sys,tarfile
from pathlib import Path
repo=Path('/home/buzzkill/Projects/quake-anthology')
sdk=Path('/home/buzzkill/.cache/quake-anthology-recovery/runtime-checkout')
record=Path(sys.argv[1]); record.mkdir(parents=True,exist_ok=True)
tree=subprocess.check_output(['git','write-tree'],cwd=repo,text=True).strip()
base=subprocess.check_output(['git','rev-parse','HEAD'],cwd=repo,text=True).strip()
archive=subprocess.check_output(['git','archive',tree,'include','src','CMakeLists.txt','cmake','assets/fonts','tests'],cwd=repo)
updated=[]; managed=[]
with tarfile.open(fileobj=io.BytesIO(archive)) as a:
 for i in a:
  if not i.isfile(): continue
  data=a.extractfile(i).read(); target=sdk/i.name; managed.append(i.name)
  if not target.exists() or target.read_bytes()!=data:
   target.parent.mkdir(parents=True,exist_ok=True); target.write_bytes(data); updated.append(i.name)
  assert target.read_bytes()==data
state=Path('/tmp/qa-frozen-sdk-managed-20261008.json')
removed=[]
if state.exists():
 for name in set(json.loads(state.read_text())['files'])-set(managed):
  target=sdk/name
  if target.is_file():target.unlink();removed.append(name)
state.write_text(json.dumps({'source_tree':tree,'files':managed})+'\n')
r={'source_tree':tree,'base_commit':base,'matched':True,'file_count':len(managed),'updated_files':updated,'removed_files':removed,'mismatches':[],'method':'Direct bytes against frozen staged build inputs; no unstaged files included'}
(record/'source-attestation.json').write_text(json.dumps(r,indent=2)+'\n'); print(json.dumps(r))
