"""Request a screenshot without adding periodic disk writes to frame timings."""
import socket,time
from gameplay_test import symbols,packet,receive
with socket.create_connection(('127.0.0.1',24689),3) as sock:
    sock.settimeout(5);packet(sock,'?');receive(sock)
    try:
        packet(sock,f'm{symbols["engine_failed"]:x},4')
        if receive(sock)!='00000000':raise SystemExit('Engine already stopped; use its automatic panic capture')
        packet(sock,f'M{symbols["mp_test_capture"]:x},4:01000000');assert receive(sock)=='OK'
    finally:packet(sock,'c');packet(sock,'D');receive(sock)
deadline=time.monotonic()+60
while time.monotonic()<deadline:
    time.sleep(.25)
    with socket.create_connection(('127.0.0.1',24689),3) as sock:
        sock.settimeout(5);packet(sock,'?');receive(sock)
        try:packet(sock,f'm{symbols["mp_test_capture"]:x},4');pending=receive(sock)
        finally:packet(sock,'c');packet(sock,'D');receive(sock)
    if pending=='00000000':print('Captured original engine display');break
else:raise TimeoutError('Display capture did not finish')
