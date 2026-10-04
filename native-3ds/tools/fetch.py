"""HTTPS downloads that also work with an out-of-date Windows certificate list.

Python checks a site's certificate against the root certificates installed
in Windows. Windows itself fetches renewed roots when it needs them; Python
cannot. On many PCs a site such as Docker Hub (Let's Encrypt's newer chain)
then fails with "certificate has expired". In that case this retries with
Windows' own curl.exe, which verifies the site with the system's TLS stack.
Callers still check every download against its pinned SHA-256.
"""
import os
import shutil
import ssl
import subprocess
import tempfile
import time
import urllib.error
import urllib.request
from pathlib import Path


def _certificate_error(error):
    reason = getattr(error, 'reason', error)
    return isinstance(reason, ssl.SSLCertVerificationError) or isinstance(error, ssl.SSLCertVerificationError)


def _windows_curl():
    curl = Path(os.environ.get('SystemRoot', r'C:\Windows'))/'System32/curl.exe'
    return curl if os.name == 'nt' and curl.exists() else None


def _curl(curl, url, dest, headers, timeout):
    base = [str(curl), '-fsSL', '--retry', '3', '--connect-timeout', '30', '--max-time', str(max(timeout, 600)),
            '-o', str(dest)]
    for key, value in (headers or {}).items():
        base += ['-H', f'{key}: {value}']
    # Some proxies and antivirus block certificate revocation checks; newer
    # curl.exe can treat that as a warning (older Windows 10 curl lacks it).
    result = subprocess.run(base[:1]+['--ssl-revoke-best-effort']+base[1:]+[url], capture_output=True, text=True)
    if result.returncode == 2:
        result = subprocess.run(base+[url], capture_output=True, text=True)
    if result.returncode:
        raise OSError(f'curl.exe could not download {url} (exit {result.returncode}): {result.stderr.strip()}')


def fetch_to(url, dest, headers=None, timeout=120, attempts=3):
    """Download URL to the file DEST, retrying dropped connections."""
    for attempt in range(attempts):
        try:
            return _fetch_once(url, dest, headers, timeout)
        except urllib.error.HTTPError as error:
            # A missing file will not appear on retry; a busy server may.
            if error.code not in (429, 500, 502, 503, 504) or attempt == attempts-1:
                raise
        except OSError:
            if attempt == attempts-1:
                raise
        time.sleep(5*(attempt+1))


def _fetch_once(url, dest, headers, timeout):
    dest = Path(dest)
    request = urllib.request.Request(url, headers=headers or {})
    try:
        with urllib.request.urlopen(request, timeout=timeout) as response, dest.open('wb') as out:
            shutil.copyfileobj(response, out, 1 << 20)
    except (urllib.error.URLError, ssl.SSLError) as error:
        curl = _windows_curl()
        if not (_certificate_error(error) and curl):
            raise
        _curl(curl, url, dest, headers, timeout)


def fetch(url, headers=None, timeout=120):
    """The bytes at URL."""
    with tempfile.TemporaryDirectory() as folder:
        path = Path(folder)/'download'
        fetch_to(url, path, headers, timeout)
        return path.read_bytes()
