"""Force command-arena rollover and compare actual rendered reference pixels."""
import json, socket, time
from gameplay_test import ROOT, symbols, packet, receive
import point_gpu_test, cull_gpu_test, blend_gpu_test, efb_copy_test, raster_state_gpu_test


def inspect(**changes):
    with socket.create_connection(('127.0.0.1', 24689), 3) as sock:
        sock.settimeout(8); packet(sock, '?'); receive(sock)
        try:
            for name, value in changes.items():
                endian='big' if name=='mp_performance_no_shadows' else 'little'
                packet(sock, f'M{symbols[name]:x},4:' + value.to_bytes(4, endian).hex())
                assert receive(sock) == 'OK'
            result = {}
            for name in ('engine_frames', 'engine_failed', 'command_byte_budget',
                         'command_barriers', 'command_peak_bytes', 'stream_vertex_budget', 'stream_barriers',
                         'mp_performance_no_shadows'):
                packet(sock, f'm{symbols[name]:x},4')
                result[name] = int.from_bytes(bytes.fromhex(receive(sock)), 'big' if name=='mp_performance_no_shadows' else 'little')
            return result
        finally:
            packet(sock, 'c'); packet(sock, 'D'); receive(sock)


def main():
    initial = inspect(); records = [initial]
    try:
        # The I4 copy fixture needs an actual projected-shadow pass. The
        # performance build disables those passes by default on every stage.
        # Restore the user's performance setting even if any fixture fails.
        inspect(command_byte_budget=128*1024,mp_performance_no_shadows=0)
        for fixture in (point_gpu_test, cull_gpu_test, blend_gpu_test, efb_copy_test, raster_state_gpu_test):
            before = inspect(); fixture.main(); after = inspect()
            assert not after['engine_failed'], after
            records.append({'fixture': fixture.__name__, 'before': before, 'after': after})
        # Recycle both arenas together; completed draws must retain their pixels.
        inspect(stream_vertex_budget=512)
        raster_state_gpu_test.main(); efb_copy_test.main()
        deadline = time.monotonic() + 90
        while True:
            after = inspect()
            assert not after['engine_failed'], after
            if after['engine_frames'] >= initial['engine_frames'] + 60: break
            if time.monotonic() > deadline: raise TimeoutError('GPU arena stress stopped advancing')
            time.sleep(.5)
        assert after['command_barriers'] > initial['command_barriers'], after
        assert after['stream_barriers'] > initial['stream_barriers'], after
        records.append(after)
        print(json.dumps({'passed': True, 'before': initial, 'after': after}), flush=True)
    finally:
        records.append(inspect(command_byte_budget=initial['command_byte_budget'],
                               stream_vertex_budget=initial['stream_vertex_budget'],
                               mp_performance_no_shadows=initial['mp_performance_no_shadows']))
        (ROOT/'build/command-buffer-test.json').write_text(json.dumps(records, indent=2))


if __name__ == '__main__': main()
