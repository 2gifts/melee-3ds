"""Reading the character select's state out of an emulated console's memory (Azahar's GDB stub, through emu_group's
Stub): the four doors (mncharsel.c's CSSDoor), whether READY shows, and where a slider's knob sits. Engine data is
big-endian. Used by the group Azahar tests to check what the CSS shows, not only what the log says."""
import struct

from gdb_probe import packet, receive

DOOR_SIZE = 0x24
DOOR_FIELDS = ('emblem_joint costume_joint team_joint door_joint bg_joint player_indicator_joint slidername_joint '
               'cpuslider_joint cpuslider2_joint selected_since_load team p_kind p_kind_prev costume sel_icon '
               'sel_icon_prev dooranim_timer slideranim_timer is_hold_cpu_slider is_hold_handicap_slider').split()
JOBJ_INSTANCE = 1 << 12
HUMAN, CPU, CLOSED = 0, 1, 3


def _read(sock, addr, n):
    packet(sock, f'm{addr:x},{n:x}')
    return bytes.fromhex(receive(sock))


class Css:
    def __init__(self, stub):
        self.stub = stub

    def doors(self):
        """The four CSSDoor records, as dicts of their byte fields (mnCharSel_803F0DFC)."""
        b = self.stub.peek('mnCharSel_803F0DFC', 4 * DOOR_SIZE)
        return [dict(zip(DOOR_FIELDS, b[i * DOOR_SIZE:i * DOOR_SIZE + len(DOOR_FIELDS)])) for i in range(4)]

    def cursor(self, port):
        """A port's cursor (CSSCursorData, mnCharSel_804A0BC0): (state x5, x, y); y grows upwards."""
        ptr = struct.unpack('>I', self.stub.peek('mnCharSel_804A0BC0', 16)[4 * port:4 * port + 4])[0]
        b = self.stub._session(lambda sock: _read(sock, ptr, 0x14))
        x, y = struct.unpack('>ff', b[0xC:0x14])
        return b[5], x, y

    def ready(self):
        """READY's counter (mnCharSel_804D6CF7): 0 while the overlay is hidden, counting up while it shows."""
        return self.stub.peek('mnCharSel_804D6CF7', 1)[0]

    def joint_translate_x(self, index):
        """TranslateX of the CSS joint `index` (as lb_80011E24 numbers them: depth first from the root,
        mnCharSel_804D6CC0, not into an instance's children), read in one stub session."""
        root_ptr = self.stub.sym['mnCharSel_804D6CC0']

        def walk(sock):
            root = struct.unpack('>I', _read(sock, root_ptr, 4))[0]
            node, cur = root, 0
            while node:
                b = _read(sock, node, 0x3C)
                nxt, parent, child, flags = struct.unpack('>IIII', b[8:24])
                if cur == index:
                    return struct.unpack('>f', b[0x38:0x3C])[0]
                if not (flags & JOBJ_INSTANCE) and child:
                    node = child
                elif nxt:
                    node = nxt
                else:
                    saved, node = node, 0
                    while True:   # up to the first parent with a next joint (lb_80011E24 does the same)
                        p = struct.unpack('>I', _read(sock, saved, 0x10)[0xC:0x10])[0]
                        if not p:
                            break
                        pn = struct.unpack('>I', _read(sock, p, 0xC)[8:0xC])[0]
                        if pn:
                            node = pn
                            break
                        saved = p
                cur += 1
            return None
        return self.stub._session(walk)

    def knob(self, door, which):
        """Where a door's slider knob sits (TranslateX; value v sits at 1.25 * (v - 1)). which: 'cpuslider_joint'
        (the CPU level with handicap off, the handicap with it on) or 'cpuslider2_joint' (the CPU level with handicap
        on)."""
        return self.joint_translate_x(self.doors()[door][which])


def knob_x(value):
    return 1.25 * (value - 1)
