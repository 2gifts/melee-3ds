"""Read-only random access to a GameCube disc image, plain ISO/GCM or CISO.

CISO is a 32 KB header (magic, little-endian block size, one byte per block
saying whether it is stored) followed by only the stored blocks. Blocks that
are not stored read back as zeros, exactly as in the original ISO.
"""
import io
from pathlib import Path
import struct

GC_DISC_SIZE = 0x57058000
HEADER_SIZE = 0x8000


class CisoError(ValueError):
    pass


class CisoFile(io.RawIOBase):
    """File-like view of the decompressed disc (read, seek, tell)."""

    def __init__(self, path):
        super().__init__()
        self._f = open(path, "rb")
        try:
            header = self._f.read(HEADER_SIZE)
            if len(header) < 8 or header[:4] != b"CISO":
                raise CisoError("Not a CISO file")
            self._block = struct.unpack_from("<I", header, 4)[0]
            if self._block < 2048 or self._block > 64 * 1024 * 1024:
                raise CisoError("Implausible CISO block size")
            # Where each block lives in the file; None means all zeros.
            self._where = []
            stored = 0
            for flag in header[8:]:
                if flag == 1:
                    self._where.append(HEADER_SIZE + stored * self._block)
                    stored += 1
                elif flag == 0:
                    self._where.append(None)
                else:
                    raise CisoError("Corrupt CISO block map")
            last = max((i for i, w in enumerate(self._where) if w is not None), default=-1)
            self.size = max(GC_DISC_SIZE, (last + 1) * self._block)
            self._pos = 0
        except BaseException:
            self._f.close()
            raise

    def readable(self):
        return True

    def seekable(self):
        return True

    def tell(self):
        return self._pos

    def seek(self, offset, whence=io.SEEK_SET):
        base = {io.SEEK_SET: 0, io.SEEK_CUR: self._pos, io.SEEK_END: self.size}[whence]
        if base + offset < 0:
            raise ValueError("negative seek position")
        self._pos = base + offset
        return self._pos

    def read(self, size=-1):
        if size is None or size < 0:
            size = self.size - self._pos
        size = max(0, min(size, self.size - self._pos))
        out = bytearray()
        while size:
            index, inside = divmod(self._pos, self._block)
            take = min(size, self._block - inside)
            where = self._where[index] if index < len(self._where) else None
            if where is None:
                out += bytes(take)
            else:
                self._f.seek(where + inside)
                data = self._f.read(take)
                if len(data) != take:
                    break  # truncated file: caller reports it
                out += data
            self._pos += take
            size -= take
        return bytes(out)

    def close(self):
        if not self.closed:
            self._f.close()
        super().close()


def is_ciso(path):
    with open(path, "rb") as f:
        return f.read(4) == b"CISO"


def open_disc(path):
    """Return (binary file object, logical disc size) for an ISO/GCM or CISO."""
    path = Path(path)
    if is_ciso(path):
        disc = CisoFile(path)
        return disc, disc.size
    return path.open("rb"), path.stat().st_size
