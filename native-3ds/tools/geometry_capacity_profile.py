"""Compare bounded decoded-geometry capacities in the same live match."""
import json, time
from gameplay_test import ROOT
from profile_switch import set_word
from profile_render_detail import snapshot, summarize


def main():
    results = []
    try:
        for mib in (6,8,12,6,12):
            set_word('geometry_budget', mib*1024*1024, 'big')
            time.sleep(1)
            start = snapshot(True); time.sleep(6); end = snapshot(False)
            result = {'budget_mib': mib, **summarize(start,end)}
            result['within_budget'] = end['memory']['geometry_bytes'] <= mib*1024*1024
            results.append(result); print(json.dumps(result), flush=True)
            (ROOT/'build/geometry-capacity-profile.json').write_text(json.dumps(results,indent=2))
    finally:
        set_word('geometry_budget', 6*1024*1024, 'big')
        (ROOT/'build/geometry-capacity-profile.json').write_text(json.dumps(results,indent=2))


if __name__ == '__main__': main()
