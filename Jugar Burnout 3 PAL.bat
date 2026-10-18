@echo off
setlocal
cd /d "%~dp0"
rem Manual PAL launcher: validated executable, own persistent card, unique logs.
rem --comprobar validates paths and build evidence without starting the game.
py -3.12 -B -c "import sys,os,json,uuid,subprocess; from pathlib import Path; sys.path.insert(0,'games/burnout3-pal/tools'); import autotest as a; s=a.select(); g=a.runner.load(str(a.GAME)); assert s['compatible'],'Los inputs actuales no coinciden con el build validado'; assert [0x242a50,0x242d50] in s['work'].get('completions',[]),'Falta la correccion del trabajador'; source=(a.GAME/'port/overrides.cpp').read_text(); assert all('Override(0x'+x+'u' in source for x in ['00244208','002445C0','00246578','00246778','00245FB0','002470C8','00245388']),'Faltan handlers mc2'; elf=Path(g.elf_path).resolve(); iso=Path(g.path(g.iso)).resolve(); assert elf.is_file() and iso.is_file(),'Falta ELF o ISO PAL'; print('Ejecutable validado:',s['executable']); sys.exit(0) if '--comprobar' in sys.argv else None; card=a.GAME/'memcards/manual-playtest'; card.mkdir(parents=True,exist_ok=True); d=a.BUILD/'manual-play'/('run-'+uuid.uuid4().hex[:12]); d.mkdir(parents=True); (d/'temp').mkdir(); env=dict(os.environ,TEMP=str(d/'temp'),TMP=str(d/'temp'),PAL_RELOCATION_AUDIT_DIR=str(d)); env.pop('PS2RT_EE_ROUND_NEAREST',None); env.pop('PAL_MC2_LOG',None); cmd=[str(s['executable']),str(elf),'--disc',str(iso),'--window','--gpu','--audio','--memcard',str(card),'--timeout','315360000']; manifest={'command':cmd,'build_manifest':str(s['manifest']),'executable_sha256':a.sha(s['executable']),'elf_sha256':a.sha(elf),'card':str(card)}; a.atomic_json(d/'launch.json',manifest); print('Partidas:',card); print('Logs:',d,flush=True); out=(d/'stdout.log').open('xb'); err=(d/'stderr.log').open('xb'); result=subprocess.run(cmd,cwd=a.ROOT,env=env,stdout=out,stderr=err); out.close(); err.close(); a.atomic_json(d/'exit.json',{'exit_code':result.returncode,'exit_hex':hex(result.returncode & 0xffffffff)}); print('Fin. Codigo:',result.returncode,'Logs:',d); sys.exit(result.returncode)" %*
set "pal_exit=%errorlevel%"
if not "%pal_exit%"=="0" (
 echo No se pudo iniciar o el juego termino con un error. Revisa los logs indicados.
 pause
)
endlocal & exit /b %pal_exit%
