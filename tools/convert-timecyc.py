"""Convert the retail LCS timecycle into a native SA data/timecyc.dat.

Source: the resident CTimeCycle table in the user's CHK/PS2/GAME.DTZ (the same
table tools/ps2_lighting.py reads), not the loose TIMECYC.DAT: the text file
matches it in most cells but differs in others (e.g. FOGGY emission at 1-2h).
Resident layout, verified cell by cell against the text columns: uint8[24][8]
arrays in text-column order (k 0..32), int16[24][8] far clip then fog start,
then uint8 arrays for lights-on-ground, clouds, blur and water (k 37..53).
Destination: SA's 23 weathers x 8 slots (0,5,6,7,12,19,20,22 h).

Fields mapped by consumer role (source Lights.cpp/Ped/Vehicle consumers):
object ambient (peds, cars, objects), directional, sky top/bottom, sun core and
corona colours, shadow/light/pole shadow, far clip, fog start, lights on ground,
low clouds, bottom (fluffy) clouds and water RGBA. Kept native, with reasons:
- Amb: the source world ambient multiplies the map prelight and is already baked
  into the converted map colours (ps2_lighting.py); copying it would apply it twice.
- Sun/sprite size and brightness: SA's DoSunAndMoon (0x6FC5A0) draws the sun at
  SunSize*2.7335 (core) and *6 (corona), the source at ~10x and 25x; no proven scale.
- Colour filter, cloud alpha, intensity limit, water fog alpha: SA-only renderer
  fields. The source "blur" colour is a motion-blur tint whose alpha is camera
  state, not a timecycle value. Source top fluffy clouds have no SA column.
- Underwater and the two extra-colour weathers (script extra colours use bank 1).
SA interpolates between its 8 slots, so the other 16 source hours are not
represented. No game data is bundled; the tool reads the user's own files.
"""
import argparse
import hashlib
import json
import pathlib
import struct

from ps2_world import Chunk

SOURCE_WEATHERS = ('SUNNY', 'CLOUDY', 'RAINY', 'FOGGY', 'EXTRASUNNY', 'HURRICANE', 'EXTRACOLOURS', 'SNOW')
SA_WEATHERS = ('EXTRASUNNY_LA', 'SUNNY_LA', 'EXTRASUNNY_SMOG_LA', 'SUNNY_SMOG_LA', 'CLOUDY_LA',
               'SUNNY_SF', 'EXTRASUNNY_SF', 'CLOUDY_SF', 'RAINY_SF', 'FOGGY_SF',
               'SUNNY_VEGAS', 'EXTRASUNNY_VEGAS', 'CLOUDY_VEGAS',
               'EXTRASUNNY_COUNTRYSIDE', 'SUNNY_COUNTRYSIDE', 'CLOUDY_COUNTRYSIDE', 'RAINY_COUNTRYSIDE',
               'EXTRASUNNY_DESERT', 'SUNNY_DESERT', 'SANDSTORM_DESERT', 'UNDERWATER', 'EXTRACOLOURS_1', 'EXTRACOLOURS_2')
SA_HOURS = (0, 5, 6, 7, 12, 19, 20, 22)
# SA weather -> source weather. The forced pairs are the inverse of the bridge's
# FORCE_WEATHER table (sunny 1, cloudy 4, rain 8, fog 9, extra sunny 0,
# hurricane 16); the remaining climates take the source weather of the same kind.
SA_FROM_SOURCE = {0: 4, 1: 0, 2: 4, 3: 0, 4: 1, 5: 0, 6: 4, 7: 1, 8: 2, 9: 3, 10: 0, 11: 4, 12: 1,
                  13: 4, 14: 0, 15: 1, 16: 5, 17: 4, 18: 0, 19: 3}
# Resident array index of each source field (uint8 unless listed in INT16).
ARRAYS = dict(amb_obj=3, dir=12, sky_top=15, sky_bot=18, sun_core=21, sun_corona=24, shadow=30,
              light_shadow=31, pole_shadow=32, light_ground=37, low_clouds=38, fluffy_bottom=44, water=50)
INT16 = {'far_clip': 0, 'fog_start': 1}
TABLE_OFFSET = 640          # after 16 CVuVectors and six float[16]
CELLS = 24 * 8
# SA row layout (51 values). A None field keeps the native SA value.
SA_LAYOUT = ([(None, 'amb')] * 3 + [('amb_obj', i) for i in range(3)] + [('dir', i) for i in range(3)] +
             [('sky_top', i) for i in range(3)] + [('sky_bot', i) for i in range(3)] +
             [('sun_core', i) for i in range(3)] + [('sun_corona', i) for i in range(3)] +
             [(None, 'sun_size'), (None, 'sprite_size'), (None, 'sprite_bright'), ('shadow', 0), ('light_shadow', 0),
              ('pole_shadow', 0), ('far_clip', 'far_clip'), ('fog_start', 'fog_start'), ('light_ground', 'light_ground')] +
             [('low_clouds', i) for i in range(3)] + [('fluffy_bottom', i) for i in range(3)] +
             [('water', i) for i in range(4)] + [(None, 'filter')] * 11)
FLOATS = {'sun_size', 'sprite_size', 'sprite_bright', 'far_clip', 'fog_start', 'light_ground'}


def resident_table(chunk):
    """Read the resident CTimeCycle arrays (pointer at ResourceImage+144)."""
    start = chunk.u32(32 + 144) + TABLE_OFFSET
    blob = chunk.span(start, CELLS * 54)

    def value(field, index, hour, weather):
        cell = hour * 8 + weather
        if field in INT16:
            return struct.unpack_from('<h', blob, 33 * CELLS + INT16[field] * 2 * CELLS + 2 * cell)[0]
        raw = blob[(ARRAYS[field] + (0 if field == 'light_ground' else index)) * CELLS + cell]
        return raw / 10.0 if field == 'light_ground' else raw  # stored x10
    return value


def numeric_rows(text, count, width):
    rows = []
    for raw in text.splitlines():
        line = raw.strip()
        if not line or line.startswith('/'):
            continue
        values = [float(v) for v in line.split()]
        if rows and width - 2 <= len(values) < width:
            # SA's sscanf reuses the same locals for every row: a short row
            # (retail RAINY_COUNTRYSIDE has 49 values) keeps the previous tail.
            values += rows[-1][len(values):]
        if len(values) != width:
            raise ValueError(f'Expected {width} SA timecycle values, got {len(values)}')
        rows.append(values)
    if len(rows) != count:
        raise ValueError(f'Expected {count} SA timecycle rows, got {len(rows)}')
    return rows


def format_row(values):
    return ' '.join(f'{v:.2f}' if key in FLOATS else str(int(round(v)))
                    for (field, key), v in zip(SA_LAYOUT, values))


def convert(value, native_text):
    native = numeric_rows(native_text, len(SA_WEATHERS) * 8, 51)
    lines = ['// Converted from the retail LCS timecycle (GAME.DTZ) by tools/convert-timecyc.py.',
             '// Amb, sun/sprite size and the SA-only colour filter/cloud/fog fields are native SA values.']
    converted = kept = 0
    for w, name in enumerate(SA_WEATHERS):
        source = SA_FROM_SOURCE.get(w)
        lines.append('//////////// ' + name + ('' if source is None else ' <- LCS ' + SOURCE_WEATHERS[source]))
        for slot, hour in enumerate(SA_HOURS):
            row = list(native[w * 8 + slot])
            if source is not None:
                for i, (field, key) in enumerate(SA_LAYOUT):
                    if field is None:
                        kept += 1
                    else:
                        row[i] = value(field, key, hour, source)
                        converted += 1
            lines.append(f'//{hour}h')
            lines.append(format_row(row))
    report = dict(format=2, source='GAME.DTZ resident CTimeCycle', sourceWeathers=len(SOURCE_WEATHERS),
                  nativeWeathers=len(SA_WEATHERS), convertedWeathers=len(SA_FROM_SOURCE), nativeSlots=list(SA_HOURS),
                  convertedValues=converted, nativeValuesKept=kept,
                  mapping={SA_WEATHERS[k]: SOURCE_WEATHERS[v] for k, v in SA_FROM_SOURCE.items()}, gameExecuted=False)
    return '\r\n'.join(lines) + '\r\n', report


def convert_files(rom, native):
    chunk = Chunk.compressed(pathlib.Path(rom) / 'CHK/PS2/GAME.DTZ')
    native_bytes = pathlib.Path(native).read_bytes()
    text, report = convert(resident_table(chunk), native_bytes.decode('latin-1'))
    report.update(sourceChunkSha256=hashlib.sha256(chunk.data).hexdigest(),
                  nativeSha256=hashlib.sha256(native_bytes).hexdigest())
    return text, report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('rom', type=pathlib.Path, help='LCS PS2 disc root of the user copy')
    parser.add_argument('native', type=pathlib.Path, help='SA data/timecyc.dat from the user game')
    parser.add_argument('output', type=pathlib.Path)
    args = parser.parse_args()
    text, report = convert_files(args.rom, args.native)
    args.output.write_bytes(text.encode('ascii'))
    report['outputSha256'] = hashlib.sha256(text.encode('ascii')).hexdigest()
    print(json.dumps(report, indent=2))
