"""Distinguish a real CPU-ID readback from the pinned emulator's missing SVC."""
import json
from pathlib import Path
from gameplay_test import TEST_ELF
def processor_evidence(state):
    assert state['mp_render_worker_active']==1,state
    observed=state['mp_render_worker_core']
    result=dict(requested_processor=2,processor_query_value=observed,processor_query_implemented=True,
                physical_core_access_verified=False)
    if observed!=2:
        process=json.loads((TEST_ELF.parent/'process.json').read_text())
        assert Path(process['elf']).resolve()==TEST_ELF.resolve()
        log=(Path(process['exe']).parent/'user/log/azahar_log.txt').read_text(encoding='utf-8',errors='replace')
        diagnostic='unimplemented SVC function 11 GetCurrentProcessorNumber(..)'
        assert observed==0 and diagnostic in log,('Unexpected worker CPU',state)
        result.update(processor_query_implemented=False,emulator_diagnostic=diagnostic)
    return result
