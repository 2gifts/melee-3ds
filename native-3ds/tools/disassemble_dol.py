"""Disassemble a verified original DOL range (workspace Capstone 5.0.6)."""
import argparse,sys
from pathlib import Path
from assets import dol_region,validate_dol
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'.toolchain/disassembly-python'))
from capstone import Cs,CS_ARCH_PPC,CS_MODE_32,CS_MODE_BIG_ENDIAN
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('address',type=lambda x:int(x,0));parser.add_argument('size',type=lambda x:int(x,0))
args=parser.parse_args()
dol=(ROOT/'assets/GALE01/sys/main.dol').read_bytes();validate_dol(dol)
code=dol_region(dol,args.address,args.size)
decoder=Cs(CS_ARCH_PPC,CS_MODE_32|CS_MODE_BIG_ENDIAN)
for instruction in decoder.disasm(code,args.address):
    print(f'{instruction.address:08x}: {instruction.mnemonic} {instruction.op_str}')
