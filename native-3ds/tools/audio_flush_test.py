"""Compare audio cache-flush routes without changing PCM or NDSP buffering.

Requires the development HLE-audio build. Azahar stubs cache maintenance,
so this validates routing/error status and exposes IPC scheduling cost; it
does not prove cache visibility or timings on physical New 3DS.
"""
import json
import socket
import time
from gameplay_test import ROOT,symbols,packet,receive
from profile_switch import set_word


def snapshot():
    with socket.create_connection(('127.0.0.1',24689),3) as s:
        s.settimeout(5);packet(s,'?');receive(s)
        try:
            result={}
            for name,count in [('engine_frames',1),('engine_failed',1),
                ('audio_flush_calls',2),('audio_flush_ticks',2),
                ('audio_flush_last_result',1),('audio_flush_failures',1),
                ('audio_direct_flush_unavailable',1),('audio_dropped',1),
                ('audio_queued',1),('audio_underruns',1)]:
                packet(s,f'm{symbols[name]:x},{count*4:x}')
                raw=bytes.fromhex(receive(s))
                result[name]=[int.from_bytes(raw[i:i+4],'little') for i in range(0,len(raw),4)]
            return result
        finally:packet(s,'c');packet(s,'D');receive(s)


def main():
    out=ROOT/'build/update16-audio-qa';out.mkdir(exist_ok=True)
    target=out/'flush-ab.json';target.unlink(missing_ok=True)
    rows=[]
    try:
        for iteration in range(3):
            for service in (1,0):
                set_word('audio_service_flush',service)
                time.sleep(1)
                before=snapshot();time.sleep(4);after=snapshot()
                assert not after['engine_failed'][0] and not after['audio_flush_failures'][0],after
                delta={k:[(a-b)&0xffffffff for a,b in zip(after[k],before[k])] for k in before}
                assert delta['audio_flush_calls'][service]>100,delta
                assert not delta['audio_flush_calls'][1-service],delta
                assert not after['audio_direct_flush_unavailable'][0] and not after['audio_flush_last_result'][0],after
                row={'iteration':iteration,'route':'DSP service' if service else 'process SVC',
                    'calls':delta['audio_flush_calls'][service],
                    'mean_flush_us':delta['audio_flush_ticks'][service]*1000000/268123480/delta['audio_flush_calls'][service],
                    'delta':delta}
                rows.append(row);print(json.dumps(row),flush=True)
                (out/'flush-ab-progress.json').write_text(json.dumps(rows,indent=2)+'\n')
        result={'passed':True,'pairs':rows,'physical_fps_verified':False,
            'scope':'Both cache operations are stubbed in Azahar; host queue test verifies exact PCM and publication ordering'}
        target.write_text(json.dumps(result,indent=2)+'\n')
    finally:set_word('audio_service_flush',0)


if __name__=='__main__':main()
