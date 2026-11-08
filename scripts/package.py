"""Package Windows plugins and their corresponding dependency-complete source."""
from pathlib import Path
import argparse, hashlib, json, re, subprocess, zipfile

root=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser()
p.add_argument('--stage',type=Path,default=root/'stage')
p.add_argument('--output',type=Path,default=root/'release')
p.add_argument('--revision',default='')
a=p.parse_args()
version=re.search(r'project\(ReaJSFX VERSION ([0-9.]+)',(root/'CMakeLists.txt').read_text()).group(1)
revision=a.revision
if not revision:
    revision=(root/'SOURCE_REVISION').read_text().strip() if (root/'SOURCE_REVISION').is_file() else subprocess.check_output(['git','-C',str(root),'rev-parse','HEAD'],text=True).strip()
a.output.mkdir(parents=True,exist_ok=True)
def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()
common=[x for x in a.stage.rglob('*') if x.is_file() and x.relative_to(a.stage).parts[0] not in ('VST2','VST3','CLAP')]
artifacts={}
for fmt,relative in [('VST2','VST2/reajsfx.dll'),('VST3','VST3/reajsfx.vst3/Contents/x86_64-win/reajsfx.vst3'),('CLAP','CLAP/reajsfx.clap')]:
    binary=a.stage/relative
    if not binary.is_file():raise SystemExit('Missing staged plugin: '+str(binary))
    archive=a.output/f'ReaJSFX-{version}-{fmt.lower()}-win-x64.zip'
    with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED,compresslevel=9) as z:
        for path in [binary,*common]:z.write(path,path.relative_to(a.stage).as_posix())
    artifacts[fmt]={'path':relative,'sha256':digest(binary)}
archive=a.output/f'ReaJSFX-{version}-source.zip'
with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
    for name in ('CMakeLists.txt','build.ps1','dependencies.json','.gitignore','.gitattributes','LICENSE','THIRD_PARTY_NOTICES.txt','README.md','README.zh-CN.md'):
        z.write(root/name,name)
    for folder in ('src','cmake','scripts','docs','dependencies','.github','upstream'):
        for path in sorted((root/folder).rglob('*')):
            rel=path.relative_to(root)
            if not path.is_file() or any(part in ('.git','__pycache__','CMakeFiles') for part in rel.parts):continue
            if rel.parts[:3] in (('upstream','vst3sdk','doc'),('upstream','vst3sdk','tutorials')):continue
            if path.suffix in ('.pyc','.pdb','.ilk'):continue
            z.write(path,rel.as_posix())
    z.writestr('SOURCE_REVISION',revision+'\n')
manifest={'name':'ReaJSFX','version':version,'platform':'windows-x64','revision':revision,
          'dependencies':json.loads((root/'dependencies.json').read_text()),'plugins':artifacts}
manifest_path=a.output/f'ReaJSFX-{version}-manifest.json'
manifest_path.write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')
archives=sorted(a.output.glob(f'ReaJSFX-{version}-*.zip'))
for path in archives:
    with zipfile.ZipFile(path) as z:
        bad=z.testzip()
        if bad:raise SystemExit(f'Invalid archive: {path}: {bad}')
checksums=a.output/f'ReaJSFX-{version}-SHA256SUMS.txt'
checksums.write_text(''.join(f'{digest(path)}  {path.name}\n' for path in [*archives,manifest_path]),encoding='ascii')
print(f'Packaged ReaJSFX {version}: VST2, VST3, CLAP and source')
