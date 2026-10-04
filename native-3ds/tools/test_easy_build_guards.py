"""Check the one-click builder's plain-language guards (no network, no disc)."""
import ssl
import tempfile
import urllib.error
import urllib.request
from pathlib import Path

import easy_build as e
import fetch


def stop_message(path):
    try:
        e.check_iso(path)
    except e.Stop as stop:
        return str(stop)
    return None


def main():
    advice = {
        'urlopen error [SSL: CERTIFICATE_VERIFY_FAILED] certificate verify failed: certificate has expired': 'date',
        '<urlopen error [Errno 11001] getaddrinfo failed>': 'reach the internet',
        'ConnectionResetError: [WinError 10054] An existing connection was forcibly closed': 'dropped',
        'HTTP Error 429: Too Many Requests': 'busy',
        'OSError: [Errno 28] No space left on device': 'disk is full',
        "PermissionError: [WinError 5] Access is denied: 'C:\\MeleeBuild\\x'": 'antivirus',
    }
    for text, expected in advice.items():
        assert expected in (e.hint_for(text) or ''), (text, e.hint_for(text))
    assert e.hint_for('Compiled 1429 files') is None, 'Ordinary output must not look like an error'

    folder = Path(tempfile.mkdtemp())
    def disc(name, game=b'GALE01', revision=2, size=e.DISC_BYTES, magic=None, nkit=False):
        header = bytearray(0x440)
        header[:6] = game
        header[7] = revision
        if magic:
            header[:4] = magic
        if nkit:
            header[0x200:0x204] = b'NKIT'
        path = folder/name
        with path.open('wb') as f:
            f.write(header)
            f.truncate(size)
        return path
    cases = [
        (disc('good.iso'), None),
        (disc('pal.iso', b'GALP01'), 'European'),
        (disc('jp.iso', b'GALJ01'), 'Japanese'),
        (disc('v100.iso', revision=0), 'version 1.00'),
        (disc('other.iso', b'GMSE01'), 'does not look like'),
        (disc('packed.iso', magic=b'RVZ\x01'), 'compressed'),
        (disc('shrunk.iso', nkit=True), 'shrunk'),
        (disc('trimmed.iso', size=500 << 20), 'shrunk'),
        (disc('game.rvz'), 'compressed or packed'),
        (disc('game.zip'), 'extract it first'),
        (folder, 'is a folder'),
        (folder/'missing.iso', 'was not found'),
    ]
    tiny = folder/'tiny.iso'
    tiny.write_bytes(b'GALE01')
    cases.append((tiny, 'far too small'))
    for path, expected in cases:
        message = stop_message(path)
        assert (message is None) if expected is None else (expected in (message or '')), (path.name, message)

    # Downloads: a dropped connection is retried; a missing file is not.
    calls = []
    def flaky(request, timeout):
        calls.append(request.full_url)
        if len(calls) < 3:
            raise urllib.error.URLError(ConnectionResetError(10054, 'reset'))
        raise urllib.error.HTTPError(request.full_url, 404, 'Not Found', {}, None)
    real, sleep = urllib.request.urlopen, fetch.time.sleep
    urllib.request.urlopen, fetch.time.sleep = flaky, lambda s: None
    try:
        try:
            fetch.fetch('https://example.invalid/file')
        except urllib.error.HTTPError as error:
            assert error.code == 404 and len(calls) == 3, calls
        calls.clear()
        def expired(request, timeout):
            calls.append(1)
            raise urllib.error.URLError(ssl.SSLCertVerificationError(1, 'certificate has expired'))
        urllib.request.urlopen = expired
        fallback = []
        real_curl = fetch._curl
        fetch._curl = lambda curl, url, dest, headers, timeout: (fallback.append(url), Path(dest).write_bytes(b'ok'))
        try:
            if fetch._windows_curl():
                assert fetch.fetch('https://example.invalid/cert') == b'ok' and fallback
        finally:
            fetch._curl = real_curl
    finally:
        urllib.request.urlopen, fetch.time.sleep = real, sleep
    print(f'PASS: {len(advice)} error hints, {len(cases)} disc-image checks, download retry and certificate fallback')


if __name__ == '__main__':
    main()
