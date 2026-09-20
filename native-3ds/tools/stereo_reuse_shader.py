"""Short geometry shader for batches proven inside both stereo viewports."""
def source():
    s=['; Experimental two-eye emission after one Melee vertex shader.',
       '.gsh point c0','.fvec stereo_params','.constf constants(0.5,0.0,1.0,-1.0)',
       '.out position position','.out color color','.out uv texcoord0',
       '.entry stereo_main','.proc stereo_main','    mov r2, stereo_params','    mov r3, constants']
    for eye in range(2):
        for vertex in range(3):
            n=vertex*3
            s += [f'    mov r0, v{n}',
                  f'    mad r0.y, {"-" if eye else ""}r2.x, r0.w, r0.y',
                  f'    add r0.y, r0.y, {"-" if eye else ""}r2.y',
                  '    mul r1.y, r2.w, r0.w',
                  f'    mad r0.y, r3.x, r0.y, {"-" if eye else ""}r1.y',
                  f'    setemit {vertex}'+(', prim' if vertex==2 else ''),
                  '    mov position, r0',f'    mov color, v{n+1}',f'    mov uv, v{n+2}','    emit']
    return '\n'.join(s+['    end','.end',''])
