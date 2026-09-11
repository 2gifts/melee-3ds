"""Replace stage code's GameCube linker-adjacency views with named objects."""
import re


def adapt(name, text):
    if name != 'grvenom.c':
        return text
    old = '&((StageCallbacks*) ((char*) base + 0x44))[gobj_id]'
    assert text.count(old) == 1
    text = text.replace(old, '&grVe_StageCallbacks[gobj_id]')
    # The zero spawn vectors occupy words 12..47 of the existing data table.
    # Give them a typed view instead of manufacturing a pointer 0x218 bytes
    # before an unrelated allocation. Original DOL bytes are checked by tests.
    text = text.replace('    u8 pad[0x218];\n', '')
    text = text.replace('data_idx = new_var[spawn_idx + 11];',
                        'data_idx = grVe_803E5348.arwing.arwing_type[spawn_idx];')
    text = text.replace('(VenomSpawnData*) (new_var2 + data_idx * 12)',
                        '(VenomSpawnData*) &grVe_803E5530[12 + data_idx * 3]')
    text, count = re.subn(r'\(VenomSpawnData\*\) \(base \+\s*base\[(\w+)->u\.venom\.xC8 \+\s*11\] \*\s*3\)',
        r'(VenomSpawnData*) &grVe_803E5530[12 + grVe_803E5348.arwing.arwing_type[\1->u.venom.xC8] * 3]', text)
    assert count == 2
    for offset, table in [(8, 'grVe_803E5348.arwing.arwing_gobj'),
                          (11, 'grVe_803E5348.arwing.arwing_type'),
                          (14, 'grVe_803E5380')]:
        text = re.sub(r'base\[([^\[\]\n]+) \+ '+str(offset)+r'\]',
                      lambda m: table+'['+m[1]+']', text)
        for alias in ('ptr', 'entry'):
            text = text.replace(f'{alias}[{offset}]', table+'[gp->u.venom.xC8]')
    text = text.replace('base[grVe_803E5380[gp->u.venom.xC8] + 170]',
                        'grVe_803E5530[48 + grVe_803E5380[gp->u.venom.xC8]]')
    text = text.replace('base[grVe_803E5348.arwing.arwing_type[gp->u.venom.xC8] + 0x7A]',
                        'grVe_803E5530[grVe_803E5348.arwing.arwing_type[gp->u.venom.xC8]]')
    text = text.replace('base[idx0 + 0xD6]', 'grVe_803E56A0[idx0]')
    text = text.replace('anim_data->anim_ids[idx0]', 'grVe_803E56A0[idx0]')
    start = text.index('    anim_data = (grVe_AnimData*) ((s32*) anim_data + gp->u.venom.xF4 * 2);')
    end = text.index('\n}', start)
    text = text[:start] + '    return grVe_803E5644[gp->u.venom.xF4 * 2 + fire_kind];' + text[end:]
    # No residual lookup may depend on walking past the first global.
    assert not re.search(r'\b(?:base|ptr|entry|new_var)\[', text.replace('`base[state + 0x7A]`', ''))
    return text
