"""Local PAL package using the USA dependency/license audit; never uploads."""
import argparse
import importlib.util
from pathlib import Path
import re
import shutil
import uuid
import zipfile

ROOT=Path(__file__).resolve().parents[3]
def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--exe',type=Path,required=True)
    p.add_argument('--dependency-cache',type=Path,required=True,help='Preserved CMake tree with _deps source licenses and ps2host/third_party')
    p.add_argument('--output-root',type=Path,required=True)
    p.add_argument('--forbid-path',type=Path,action='append',default=[],help='Additional private source roots to reject in PE strings')
    args=p.parse_args()
    exe=args.exe.resolve();cache=args.dependency_cache.resolve();out=args.output_root.resolve()
    out.mkdir(parents=True,exist_ok=True)
    stage=out/('package-'+uuid.uuid4().hex[:10]);stage.mkdir()
    source=ROOT/'games/burnout3/port/tools/package.py'
    spec=importlib.util.spec_from_file_location('usa_package',source)
    usa=importlib.util.module_from_spec(spec);spec.loader.exec_module(usa)
    usa.ROOT=str(ROOT);usa.BUILD=str(stage);usa.DEPS=str(cache/'_deps');usa.HEADERS=str(cache/'ps2host/third_party')
    dlls=usa.imports(str(exe))
    if not dlls:raise ValueError('Cannot determine PE dependencies')
    if any(x.startswith(('msvcp','vcruntime','ucrtbase','api-ms-win-crt')) for x in dlls):raise ValueError('Dynamic VC++ runtime is not allowed')
    data=exe.read_bytes()
    for path in [str(ROOT),str(Path.home()),*[str(x.resolve()) for x in args.forbid_path]]:
        for form in [path,path.replace('\\','/'),path.replace('/','\\')]:
            if form.encode() in data or form.encode('utf-16le') in data:raise ValueError('Executable contains private build path')
    if re.search(rb'[A-Za-z]:\\Users\\[^\\\x00]{1,64}\\',data):raise ValueError('Executable contains a user directory')
    notices=['Dependencies from the upstream USA package audit; no game assets.']
    for name,folder,names in usa.LICENCES:
        path=next((cache/'_deps'/folder/n for n in names if (cache/'_deps'/folder/n).is_file()),None)
        if path is None:raise ValueError('Missing license: '+name)
        notices+=['\n'+name+'\n',path.read_text(encoding='utf8',errors='replace')]
    for name,header,start in usa.HEADER_LICENCES:notices+=['\n'+name+'\n',usa.header_licence(header,start)]
    for name,relative in usa.FILE_LICENCES:notices+=['\n'+name+'\n',(ROOT/relative).read_text(encoding='utf8')]
    notices+=['\nps2-port-engine\n',(ROOT/'engine/LICENSE').read_text(encoding='utf8')]
    folder=stage/'Burnout3-PAL-Enhanced-0.1.0-alpha';folder.mkdir()
    shutil.copy2(exe,folder/'Burnout3-PAL-Enhanced.exe')
    shutil.copy2(ROOT/'LICENSE',folder/'LICENSE')
    shutil.copy2(ROOT/'games/burnout3-pal/port/release/README.txt',folder/'README.txt')
    (folder/'THIRD-PARTY-NOTICES.txt').write_text('\n'.join(notices),encoding='utf8')
    archive=out/(folder.name+'-windows-x64.zip')
    with zipfile.ZipFile(archive,'x',zipfile.ZIP_DEFLATED,compresslevel=9) as z:
        for f in sorted(folder.iterdir()):z.write(f,folder.name+'/'+f.name)
    print('Player folder:',folder);print('ZIP:',archive);print('Imports:',', '.join(dlls))
if __name__=='__main__':main()
