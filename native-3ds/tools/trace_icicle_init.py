"""One-shot call-return trace for the current smoke ELF's stage initialization."""
import json,re,socket,struct,subprocess,time
from gameplay_test import ROOT,symbols,packet,receive
listing=subprocess.check_output([str(ROOT/'.toolchain/llvm-mingw-20260908-ucrt-x86_64/bin/llvm-objdump.exe'),'--disassemble-symbols=grIceMt_801F686C',str(ROOT/'build/game/melee.elf')],text=True)
points={symbols['grIceMt_801F686C']:'stage init'}
for line in listing.splitlines():
 m=re.match(r'\s*([0-9a-f]+):.*\bbl\s+0x[0-9a-f]+ <([^>]+)>',line)
 if m:points[int(m[1],16)+4]='returned '+m[2]
records=[]
with socket.create_connection(('127.0.0.1',24689),3) as s:
 s.settimeout(60);packet(s,'?');receive(s)
 def rpc(cmd):packet(s,cmd);return receive(s)
 try:
  for p in points:assert rpc(f'Z0,{p:x},4')=='OK'
  data=struct.pack('<IIIii',time.time_ns()&0xffffffff,0x100,4,0,0)
  assert rpc(f'M{symbols["mp_test_control"]:x},{len(data):x}:'+data.hex())=='OK'
  while points:
   event=rpc('c');r=bytes.fromhex(rpc('g'));v=struct.unpack('<16I',r[:64]);pc=v[15]
   row={'event':event,'pc':hex(pc),'point':points.get(pc),'registers':[hex(n) for n in v]};records.append(row)
   print(json.dumps(row),flush=True);(ROOT/'build/icicle-init-trace.json').write_text(json.dumps(records,indent=2))
   if pc not in points:break
   assert rpc(f'z0,{pc:x},4')=='OK';points.pop(pc)
 finally:
  for p in points:
   try:rpc(f'z0,{p:x},4')
   except Exception:break
  try:packet(s,'c');packet(s,'D');receive(s)
  except Exception:pass
