"""Compare native texture uploads with the original conversion in a live scene."""
import json, socket, struct, time
from gameplay_test import ROOT, symbols, packet, receive, exchange
from profile_switch import set_word


def counters():
    with socket.create_connection(('127.0.0.1', 24689), 3) as sock:
        sock.settimeout(5); packet(sock, '?'); receive(sock)
        try:
            result = {}
            for name in ('texture_repack_checks', 'texture_uploads', 'texture_bytes',
                         'texture_evictions', 'command_barriers', 'engine_failed'):
                packet(sock, f'm{symbols[name]:x},4')
                result[name] = int.from_bytes(bytes.fromhex(receive(sock)), 'little')
            for name in ('texture_repack_format_checks', 'texture_repack_fast_ticks', 'texture_repack_reference_ticks'):
                packet(sock, f'm{symbols[name]:x},40')
                result[name] = struct.unpack('<16I', bytes.fromhex(receive(sock)))
            return result
        finally:
            packet(sock, 'c'); packet(sock, 'D'); receive(sock)


def main():
    initial = exchange(); before = counters()
    try:
        set_word('texture_repack_validate', 1)
        time.sleep(4)
        after = counters(); final = exchange()
        assert after['texture_repack_checks'] > before['texture_repack_checks'], after
        assert not after['engine_failed'] and final['simulation'] > initial['simulation'], final
        assert final['stage_kind'] == initial['stage_kind'], final
    finally:
        set_word('texture_repack_validate', 0)
    result = {'initial': initial, 'final': final, 'before': before, 'after': after,
              'verified_uploads': after['texture_repack_checks']-before['texture_repack_checks']}
    (ROOT/'build/texture-repack-live-test.json').write_text(json.dumps(result, indent=2))
    print(json.dumps(result, indent=2))


if __name__ == '__main__': main()
