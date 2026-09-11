"""Trace GObj callback register preservation around a stage transition."""
import json,re,socket,struct,subprocess,time
from gameplay_test import ROOT,symbols,packet,receive
text=subprocess.check_output([str(ROOT/'.toolchain/llvm-mingw-20260908-ucrt-x86_64/bin/llvm-objdump.exe'),'--disassemble-symbols=HSD_GObj_80390CFC',str(ROOT/'build/game/melee.elf')],text=True)
m=re.search(r'\s([0-9a-f]+):[^\n]+\bblx\s+r1',text);before=int(m[1],16);after=before+4
names=sorted((v,k) for k,v in symbols.items() if not k.startswith(('mp_be_fix_','$')))
def name(p):return next((f'{n}+{p-v:#x}' for v,n in reversed(names) if v<=p),'?')
rows=[];prior=None
with socket.create_connection(('127.0.0.1',24689),3) as s:
 s.settimeout(60);packet(s,'?');receive(s)
 def rpc(cmd):packet(s,cmd);return receive(s)
 try:
  assert rpc(f'Z0,{before:x},4')=='OK'
  data=struct.pack('<IIIii',time.time_ns()&0xffffffff,0x100,4,0,0)
  assert rpc(f'M{symbols["mp_test_control"]:x},{len(data):x}:'+data.hex())=='OK'
  for i in range(10000):
   event=rpc('c');v=struct.unpack('<16I',bytes.fromhex(rpc('g'))[:64]);pc=v[15]
   if pc==before:
    prior={'callee':name(v[1]),'function':hex(v[1]),'r4':v[4],'registers':[hex(n) for n in v]};rows.append(prior)
   elif pc==after:
    if not prior or v[4]!=prior['r4']:
     bad={'event':event,'last_calls':rows[-20:],'after':[hex(n) for n in v],'stack':rpc(f'm{v[13]:x},100')}
     (ROOT/'build/icicle-register-failure.json').write_text(json.dumps(bad,indent=2));print(json.dumps(bad),flush=True);break
   else:print('Unexpected stop',event,hex(pc),flush=True);break
   assert rpc(f'z0,{pc:x},4')=='OK'
   target=after if pc==before else before
   assert rpc(f'Z0,{target:x},4')=='OK'
   if i%200==0:print(json.dumps({'stops':i,'last':rows[-1] if rows else None}),flush=True)
 finally:
  (ROOT/'build/icicle-callback-trace.json').write_text(json.dumps(rows,indent=2))
  for p in (before,after):
   try:rpc(f'z0,{p:x},4')
   except Exception:break
  try:packet(s,'c');packet(s,'D');receive(s)
  except Exception:pass
