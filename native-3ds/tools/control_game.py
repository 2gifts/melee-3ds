"""Send a bounded GameCube controller input to the smoke-test 3DS build."""
import argparse,socket,struct,subprocess,time
from gdb_probe import ROOT,packet,receive
ap=argparse.ArgumentParser();ap.add_argument('buttons',type=lambda s:int(s,0));ap.add_argument('--frames',type=int,default=3);ap.add_argument('--x',type=int,default=0);ap.add_argument('--y',type=int,default=0);args=ap.parse_args()
symbols=subprocess.check_output([str(ROOT/'.toolchain/llvm-mingw-20260908-ucrt-x86_64/bin/llvm-nm.exe'),str(ROOT/'build/game/melee.elf')],text=True)
address=next(int(line.split()[0],16) for line in symbols.splitlines() if line.endswith(' mp_test_control'))
values=struct.pack('<IIIii',time.time_ns()&0xffffffff,args.buttons,args.frames,args.x,args.y)
with socket.create_connection(('127.0.0.1',24689),3) as sock:
    sock.settimeout(5);packet(sock,'?');receive(sock)
    try:
        packet(sock,f'M{address:x},{len(values):x}:'+values.hex());result=receive(sock)
        if result!='OK':raise RuntimeError(result)
        print(f'Controller buttons {args.buttons:#x}, stick ({args.x},{args.y}) for {args.frames} rendered frames')
    finally:
        # Azahar resets its selected process while detaching. Resume it before
        # detach so the debugger's temporary scheduler pause is cleared.
        packet(sock,'c');packet(sock,'D');receive(sock)
