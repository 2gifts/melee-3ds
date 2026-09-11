"""Extract the single audited visual archive from a locally patched Diet ISO."""
import argparse,hashlib
from assets import ROOT,hsd_inventory,parse_fst,read_exact,u32

def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('patched_iso');args=ap.parse_args()
    with open(args.patched_iso,'rb') as source:
        source.seek(0,2);size=source.tell()
        header=read_exact(source,0,0x430)
        fst=read_exact(source,u32(header,0x424),u32(header,0x428))
        entries=parse_fst(fst,size)
        matches=[x for x in entries if not x.directory and x.path=='GrIz.dat']
        assert len(matches)==1,'Expected one Fountain archive'
        entry=matches[0];data=read_exact(source,entry.offset,entry.size)
    digest=hashlib.sha256(data).hexdigest()
    assert digest=='913134d58c804f44b9fe3dc41b981e50a076e6b27c3c58583fa74095ce22f61d','This Diet Fountain revision has not been audited'
    hsd_inventory(data)
    output=ROOT/'references/diet-melee/files/GrIz.dat';output.parent.mkdir(parents=True,exist_ok=True)
    if output.exists():assert output.read_bytes()==data,'Refuse to overwrite a different reference'
    else:output.write_bytes(data)
    from audit_diet_fountain import main as audit
    audit();print(f'Prepared {output}: {len(data)} bytes, SHA256 {digest}')

if __name__=='__main__':main()
