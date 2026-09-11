"""Debugger reply framing must survive split reads and coalesced replies."""
import sys,unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from gdb_probe import receive

class FakeSocket:
    def __init__(self,chunks):self.chunks=iter(chunks);self.sent=[]
    def recv(self,_):return next(self.chunks,b'')
    def sendall(self,data):self.sent.append(data)

class TransportTest(unittest.TestCase):
    def test_every_split(self):
        stream=b'+$OK#9a+$001122ff#f2'
        for i in range(1,len(stream)):
            sock=FakeSocket([stream[:i],stream[i:]])
            self.assertEqual(receive(sock),'OK')
            self.assertEqual(receive(sock),'001122ff')
            self.assertEqual(sock.sent,[b'+',b'+'])
    def test_bad_checksum(self):
        with self.assertRaises(ValueError):receive(FakeSocket([b'$OK#00']))
    def test_truncated(self):
        with self.assertRaises(ConnectionError):receive(FakeSocket([b'$OK#9']))

if __name__=='__main__':unittest.main()
