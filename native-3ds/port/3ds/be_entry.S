.syntax unified
.arm
.text
.global mp_be_engine_probe
.type mp_be_engine_probe,%function
mp_be_engine_probe:
    push {r4,lr}
    setend be
    bl mp_be_probe_impl
    setend le
    pop {r4,pc}
.size mp_be_engine_probe,.-mp_be_engine_probe
