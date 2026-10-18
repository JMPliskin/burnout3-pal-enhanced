"""Exercise the compiled startup contracts using tiny synthetic ISO images.
Usage: py -3.12 -B test_player_startup.py PATH_TO_NATIVE_TEST_EXE
"""
import hashlib
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

def synthetic_iso(boot):
    data=bytearray(24*2048)
    def record(name,sector,size):
        raw=name.encode('ascii');n=33+len(raw);n+=n%2
        entry=bytearray(n);entry[0]=n;struct.pack_into('<I',entry,2,sector);struct.pack_into('<I',entry,10,size);entry[32]=len(raw);entry[33:33+len(raw)]=raw
        return entry
    start=16*2048;data[start:start+7]=b'\x01CD001\x01';struct.pack_into('<I',data,start+158,20);struct.pack_into('<I',data,start+166,2048)
    text=('BOOT2 = cdrom0:\\'+boot+';1\r\n').encode('ascii');entry=record('SYSTEM.CNF;1',21,len(text));data[20*2048:20*2048+len(entry)]=entry;data[21*2048:21*2048+len(text)]=text
    return data

if __name__=='__main__':
    exe=Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix='PAL startup ') as folder:
        root=Path(folder)
        (root/'PAL espa\u00f1ol \u65e5.iso').write_bytes(synthetic_iso('SLES_525.84'))
        (root/'USA.iso').write_bytes(synthetic_iso('SLUS_210.50'))
        (root/'invalid.iso').write_bytes(b'synthetic invalid image')
        result=subprocess.run([str(exe),str(root)],text=True,capture_output=True)
        print(result.stdout,end='');print(result.stderr,end='',file=sys.stderr)
        assert result.returncode==0 and '11 passed' in result.stdout
