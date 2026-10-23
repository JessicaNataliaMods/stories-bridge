"""Build anim/ped.ifp from the original LCS ped animations; never executes GTA.

The SA ped block (ANP3) keeps its animation names, because the SA walk, idle,
car and gesture code asks for them by name, but every animation that has an
original counterpart carries the original keys (tools/lcs_ifp.py documents the
retarget to the converted ped rig). Original-only animations are appended.
Animations without any original counterpart (climbing, swimming, melee combos,
damage reactions, ...) stay SA data: the original game has no equivalent.
"""
import argparse
import hashlib
import json
import math
import re
from pathlib import Path

import lcs_ifp
import weapon_animations

# SA animations with no original of the same name, mapped to the original
# animation that serves the same purpose (same gait/idle/reaction family).
ALIASES = {
    'Walk_Wuzi': 'WALK_civi', 'Run_Wuzi': 'run_civi', 'Sprint_Wuzi': 'sprint_civi',
    'run_fat': 'run_fatold', 'run_old': 'run_fatold', 'swat_run': 'run_armed',
    'Idlestance_fat': 'IDLE_stance', 'idlestance_old': 'IDLE_stance', 'Idle_Gang1': 'IDLE_stance',
    'JOG_femaleA': 'JOG_maleA', 'WALK_drunk': 'WALK_shuffle', 'cower': 'handscower',
    'WOMAN_runbusy': 'woman_run', 'WOMAN_runsexy': 'woman_run', 'WOMAN_runfatold': 'run_fatold',
    'WOMAN_walkfatold': 'WALK_fatold', 'WOMAN_walkpro': 'WOMAN_walksexy',
    'roadcross_old': 'roadcross', 'roadcross_female': 'roadcross', 'roadcross_gang': 'roadcross',
    'Turn_L': 'turn_180', 'Turn_R': 'turn_180', 'SHOT_partial_B': 'SHOT_partial', 'JUMP_launch_R': 'JUMP_launch',
    # The original has no driving skill: the skill and speed variants share one animation.
    'CAR_LB_pro': 'CAR_LB', 'CAR_LB_weak': 'CAR_LB', 'CAR_sit_pro': 'CAR_sit', 'CAR_sit_weak': 'CAR_sit',
    'Drive_L_pro': 'Drive_L', 'Drive_L_pro_slow': 'Drive_L', 'Drive_L_slow': 'Drive_L',
    'Drive_L_weak': 'Drive_L', 'Drive_L_weak_slow': 'Drive_L',
    'Drive_R_pro': 'Drive_R', 'Drive_R_pro_slow': 'Drive_R', 'Drive_R_slow': 'Drive_R',
    'Drive_R_weak': 'Drive_R', 'Drive_R_weak_slow': 'Drive_R',
}


# SA task code that opens doors, sits, jumps and lands waits for fixed times of
# these animations: they keep the SA length (see lcs_ifp.convert_block).
TIMED_BY_SA = re.compile(r'(car_|bike_|drive|kart_|seat_|train_|door_|jump_|fall_|ev_)', re.IGNORECASE)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def build(original_bytes, sa_bytes):
    """(converted ped block, report) from the original ANPK block and the SA ANP3 block."""
    block, sa_animations = weapon_animations.read_anp3(sa_bytes)
    if block != 'ped':
        raise ValueError('Expected the SA ped animation block')
    parsed = lcs_ifp.read_original_block(original_bytes)
    by_lower = {name.lower(): name for name in parsed}
    name_map = {}
    for name, _ in sa_animations:
        if name.lower() in by_lower:
            name_map[name] = ('ped', by_lower[name.lower()])
        elif name in ALIASES:
            if ALIASES[name].lower() not in by_lower:
                raise ValueError('Alias target missing from the original block: ' + ALIASES[name])
            name_map[name] = ('ped', by_lower[ALIASES[name].lower()])
    data, report = lcs_ifp.convert_block(sa_bytes, {'ped': original_bytes}, name_map, add_original_only=True,
                                          keep_sa_length=lambda name: bool(TIMED_BY_SA.match(name)))
    check_block, converted = weapon_animations.read_anp3(data)
    names = [name for name, _ in converted]
    if check_block != 'ped' or len(set(n.lower() for n in names)) != len(names):
        raise ValueError('Converted ped block is inconsistent')
    parsed_back = {name: lcs_ifp.read_sa_animation(raw)[0] for name, raw in converted}
    for name, kind in report.items():
        if kind == 'kept':
            continue
        source = parsed[name_map[name][1]] if name in name_map else parsed[name]
        tracks = parsed_back[name]
        duration = max(k[0] for t in source.values() for k in t)
        written = max(k[0] for t in tracks.values() for k in t)
        if (abs(duration - written) > 1 / 30 and not TIMED_BY_SA.match(name)) or not all(math.isfinite(c) for t in tracks.values() for k in t for c in k[1]):
            raise ValueError('Converted animation timing/values differ from the original: ' + name)
    summary = dict(format=1, gameExecuted=False, installed=False,
                   original=dict(sha256=digest(original_bytes), animations=len(parsed)),
                   saPed=dict(sha256=digest(sa_bytes), animations=len(sa_animations)),
                   output=dict(file='ped.ifp', sha256=digest(data), animations=len(names)),
                   converted=sum(kind == 'original' for kind in report.values()),
                   convertedByAlias={name: ALIASES[name] for name in sorted(ALIASES) if name in name_map},
                   appendedOriginalOnly=sorted(name for name, kind in report.items() if kind == 'added'),
                   keptSaData=sorted(name for name, kind in report.items() if kind == 'kept'),
                   rule='original keys; thighs relative to Pelvis; root translation kept; other translations dropped')
    return data, summary


def prepare(original, sa_ped, output):
    if output.exists():
        raise ValueError('Choose a new ped animation directory')
    data, summary = build(original.read_bytes(), sa_ped.read_bytes())
    output.mkdir(parents=True)
    (output / 'ped.ifp').write_bytes(data)
    (output / 'report.json').write_text(json.dumps(summary, indent=2) + '\n')
    return summary


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--original', type=Path, required=True, help='converted original ped.ifp (ANPK)')
    parser.add_argument('--sa-ped', type=Path, required=True, help="the game's anim/ped.ifp (ANP3)")
    parser.add_argument('--output', type=Path, required=True)
    arguments = parser.parse_args()
    result = prepare(arguments.original, arguments.sa_ped, arguments.output)
    print(json.dumps({key: (value if not isinstance(value, (list, dict)) else len(value)) for key, value in result.items()}, indent=2))
