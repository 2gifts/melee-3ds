"""Check the HSD keyframe interpreter, and the disc-made diorama if the disc is extracted."""
import struct
import numpy as np
import hsd_model as hsd


def track(data, kind=1):
    return hsd.Track(bytes(data), 0, kind, 0, 0)


def key(value, wait=None):
    """A float key; the last key of a track has no wait."""
    return struct.pack('<f', value)+(bytes([wait]) if wait is not None else b'')


def main():
    # Linear keys 0 -> 10 over ten frames.
    linear = track([0x12]+list(key(0, 10))+list(key(10)))
    values = [float(hsd.track_value(linear, f)) for f in range(11)]
    assert values == [float(f) for f in range(11)], values
    # Constant keys step at the next key.
    step = track([0x11]+list(key(2, 3))+list(key(5)))
    assert [float(hsd.track_value(step, f)) for f in range(4)] == [2, 2, 2, 5]
    # Spline keys with zero slopes ease in and out, symmetric about the middle.
    spline = track([0x13]+list(key(0, 8))+list(key(1)))
    eased = np.array([float(hsd.track_value(spline, f)) for f in range(9)])
    assert eased[0] == 0 and eased[8] == 1 and abs(eased[4]-.5) < 1e-6
    assert np.allclose(eased+eased[::-1], 1, atol=1e-6) and (np.diff(eased) > 0).all()
    # The engine's matrix concatenation and SRT construction.
    a = hsd.mtx_srt((1, 2, 3), (0, 0, 0), (4, 5, 6), None)
    assert (a == np.array([[1, 0, 0, 4], [0, 2, 0, 5], [0, 0, 3, 6]], dtype=np.float32)).all()
    b = hsd.mtx_srt((1, 1, 1), (0, np.float32(np.pi/2), 0), (0, 0, 0), None)
    assert np.allclose(hsd.mtx_concat(a, b)[:, :3], [[0, 0, 1], [0, 2, 0], [-3, 0, 0]], atol=1e-6)
    print('PASS: keyframes (linear, constant, spline) and joint matrices follow the engine')
    try:
        import disc_diorama
        assert (disc_diorama.FILES/'PlFxNr.dat').exists()
    except (ImportError, AssertionError):
        print('SKIP: disc diorama (no extracted disc)')
        return
    fighter_dat = hsd.Dat.load(disc_diorama.FILES/'PlFx.dat')
    animations = (disc_diorama.FILES/'PlFxAJ.dat').read_bytes()
    for spec in disc_diorama.FIGHTERS:
        parts = disc_diorama.fighter(spec, fighter_dat, animations)
        assert len(parts) == 31 and sum(len(p["indices"]) for p in parts)//3 == 467
        height = np.ptp(np.concatenate([p['positions'] for p in parts])[:, 1])
        assert 10 < height < 20, height
    parts, center, up, plane = disc_diorama.stage()
    assert sum(len(p['indices']) for p in parts)//3 < 700, 'Stage exceeds the captured stage budget'
    print('PASS: both Fox poses and the simplified stage come from the disc files')


if __name__ == '__main__':
    main()
