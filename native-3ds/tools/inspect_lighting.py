import socket,struct,time,json
from gameplay_test import symbols
from gdb_probe import packet,receive
for attempt in range(10):
    with socket.create_connection(('127.0.0.1',24689),3) as sock:
        sock.settimeout(5);packet(sock,'?');receive(sock)
        def read(name,size):
            packet(sock,f'm{symbols[name]:x},{size:x}');return bytes.fromhex(receive(sock))
        try:
            result={name:int.from_bytes(read(name,4),'big') for name in ('gpu_shading','flat_shading','num_stages')}
            result['channels']=struct.unpack('>36I',read('channel_configuration',144))
            result['primitive_vertices']=struct.unpack('>8I',read('primitive_vertices',32))
            result['tev']=[struct.unpack('>30I',read('tev_configuration',16*120)[i*120:(i+1)*120]) for i in range(result['num_stages'])]
            result['colors']=read('tev_color',16).hex()
            result['konst']=read('konst_color',16).hex()
            result['material']=read('material',8).hex()
            result['lights']=read('lights',512).hex()
            print(json.dumps(result),flush=True)
        finally:packet(sock,'c');packet(sock,'D');receive(sock)
    if not result['gpu_shading'] and not result['flat_shading']:break
    time.sleep(.08)
