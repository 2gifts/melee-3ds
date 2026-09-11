"""Prepare libctru's installed 3DSX layout for lld without a diagnostic build."""
from pathlib import Path


def prepare_layout(sdk: Path) -> str:
    layout = (sdk / 'devkitARM/arm-none-eabi/lib/3dsx.ld').read_text()
    layout = layout.replace('data   PT_LOAD FLAGS(6)',
                            'tls    PT_TLS FLAGS(4);\n\tdata   PT_LOAD FLAGS(6)')
    for start, end in (('\t.tdata :', '\t.tbss :'), ('\t.tbss :', '\t/*')):
        a = layout.index(start)
        b = layout.index(end, a + len(start))
        layout = layout[:a] + layout[a:b].replace(': data', ': data : tls') + layout[b:]
    layout = layout.replace('.bss ALIGN(4)', '.bss ALIGN(8)')
    # The CRT zeroes bss; initialized GOT entries must precede it.
    return layout.replace('*(.data.*)', '*(.data.*)\n\t\t*(.got .got.*)')
