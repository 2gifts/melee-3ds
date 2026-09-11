"""Inspect completed NDSP output in an explicitly enabled Azahar HLE build."""
import json,socket,struct,wave,time
import numpy as np
from gameplay_test import ROOT,symbols,packet,receive
def request(value=None):
    with socket.create_connection(('127.0.0.1',24689),3) as sock:
        sock.settimeout(5);packet(sock,'?');receive(sock)
        try:
            address=symbols['audio_snapshot_request']
            if value is not None:
                packet(sock,f'M{address:x},4:'+struct.pack('<I',value).hex());assert receive(sock)=='OK'
            packet(sock,f'm{address:x},4');return int.from_bytes(bytes.fromhex(receive(sock)),'little')
        finally:packet(sock,'c');packet(sock,'D');receive(sock)

def capture():
    # Allow one complete ring to recover from the request's debugger pause,
    # then capture a second fresh ring on its producer thread.
    request(400);deadline=time.monotonic()+90;pending=[]
    while True:
        # 400 DSP callbacks cover about two emulated seconds. Leave ample
        # wall time even when the emulator runs slowly: a debugger poll in
        # the middle of this window can itself starve the guest's mixer.
        time.sleep(15)
        remaining=request();pending.append(remaining)
        if not remaining:break
        if time.monotonic()>deadline:raise TimeoutError('NDSP did not produce a full capture')
    with socket.create_connection(('127.0.0.1',24689),3) as sock:
        sock.settimeout(5);packet(sock,'?');receive(sock)
        def read(a,n):packet(sock,f'm{a:x},{n:x}');return bytes.fromhex(receive(sock))
        def word(name):return int.from_bytes(read(symbols[name],4),'little')
        try:
            result={name:word(name) for name in ('ready','engine_failed')}
            assert result['ready']==1 and not result['engine_failed'],result
            names=('engine_frames','audio_queued','audio_dropped','audio_underruns','prebuffering','capture_window_underruns','prefill_blocks','max_mixer_gap_us','wave_buffer_samples','max_logging_us','max_disc_read_us','max_gpu_wait_us')
            result.update(zip(names,struct.unpack('<12I',read(symbols['audio_snapshot_stats'],48))))
            base=symbols['audio_snapshot_pcm'];raw=b''.join(read(base+i,min(512,128000-i)) for i in range(0,128000,512))
        finally:packet(sock,'c');packet(sock,'D');receive(sock)
    samples=np.frombuffer(raw,dtype='<i2').reshape(32000,2)
    result.update({'capture':'NDSP final stereo mix, Azahar HLE, atomic producer-thread snapshot',
        'pending_at_debugger_polls':pending,
        'peak':int(np.abs(samples.astype(np.int32)).max()),'rms':float(np.sqrt(np.mean(samples.astype(float)**2))),
        'nonzero_fraction':float(np.any(samples!=0,axis=1).mean())})
    silent=np.all(samples==0,axis=1);edges=np.diff(np.r_[False,silent,False].astype(np.int8))
    runs=np.flatnonzero(edges==-1)-np.flatnonzero(edges==1)
    result['longest_zero_run_ms']=float(runs.max()*1000/32728) if len(runs) else 0
    with wave.open(str(ROOT/'build/ndsp-output.wav'),'wb') as out:
        out.setnchannels(2);out.setsampwidth(2);out.setframerate(32728);out.writeframes(samples.tobytes())
    (ROOT/'build/audio-capture-test.json').write_text(json.dumps(result,indent=2));print(json.dumps(result,indent=2))
    assert result['peak']>0 and result['audio_queued']>16,result
    assert pending==[0], 'Debugger interrupted the capture window; repeat with a longer initial wait'
if __name__=='__main__':capture()
