"""Directed checks for tools/convert-timecyc.py with a synthetic (authored) table."""
import importlib.util
import pathlib
import sys

root = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(root/'tools'))
spec = importlib.util.spec_from_file_location('convert_timecyc', root/'tools/convert-timecyc.py')
tc = importlib.util.module_from_spec(spec)
spec.loader.exec_module(tc)


def check(value, message):
    if not value:
        raise AssertionError(message)


def native_text(short_row=None):
    lines = []
    for w, name in enumerate(tc.SA_WEATHERS):
        lines.append('//////////// ' + name)
        for slot in range(8):
            values = [w * 1000 + slot * 100 + i for i in range(51)]
            if (w, slot) == short_row:
                values = values[:49]
            lines.append('//slot')
            lines.append('\t'.join(str(v) for v in values))
    return '\r\n'.join(lines) + '\r\n'


def source(field, index, hour, weather):
    if field in ('far_clip', 'fog_start'):
        return 500 + hour * 10 + weather
    if field == 'light_ground':
        return (hour + weather) / 10.0
    return (tc.ARRAYS[field] + index + hour + weather) % 256


text, report = tc.convert(source, native_text())
rows = [line for line in text.split('\r\n') if line and not line.startswith('//')]
check(len(rows) == 23 * 8 and all(len(r.split()) == 51 for r in rows), 'SA layout has 184 rows of 51 values')
native = tc.numeric_rows(native_text(), 184, 51)
for w in (20, 21, 22):
    for slot in range(8):
        check([float(v) for v in rows[w * 8 + slot].split()] == native[w * 8 + slot], 'Underwater/extra colours stay native')
noon_sunny = [float(v) for v in rows[1 * 8 + 4].split()]   # SUNNY_LA 12h <- LCS SUNNY (0)
check(noon_sunny[0:3] == native[12][0:3], 'Amb stays native: source world ambient is baked into the map prelight')
check(noon_sunny[3:6] == [source('amb_obj', i, 12, 0) for i in range(3)], 'Object ambient from the source')
check(noon_sunny[9:12] == [source('sky_top', i, 12, 0) for i in range(3)], 'Sky top from the source')
check(noon_sunny[21:24] == native[12][21:24], 'Sun/sprite size and brightness stay native')
check(noon_sunny[27] == 620 and noon_sunny[28] == 620 and noon_sunny[29] == 1.2, 'Far clip, fog start, lights on ground')
check(noon_sunny[36:40] == [source('water', i, 12, 0) for i in range(4)], 'Water RGBA from the source')
check(noon_sunny[40:51] == native[12][40:51], 'SA-only colour filter/cloud/fog fields stay native')
rain = [float(v) for v in rows[16 * 8 + 6].split()]        # RAINY_COUNTRYSIDE 20h <- HURRICANE (5)
check(rain[27] == 500 + 200 + 5, 'Forced hurricane uses SA RAINY_COUNTRYSIDE')
carried = tc.numeric_rows(native_text(short_row=(16, 0)), 184, 51)
check(carried[16 * 8][49:] == carried[16 * 8 - 1][49:], 'Short native row keeps the previous sscanf tail')
check(report['convertedWeathers'] == 20 and report['convertedValues'] + report['nativeValuesKept'] == 20 * 8 * 51,
      'Report counts every converted-weather value')
if len(sys.argv) == 3:
    # Optional: the user's own disc root and SA timecyc.dat.
    user_text, user = tc.convert_files(sys.argv[1], sys.argv[2])
    user_rows = [line for line in user_text.split('\r\n') if line and not line.startswith('//')]
    check(len(user_rows) == 184 and user['convertedValues'] == 5440, 'User timecycle converts completely')
print('PASS: SA 23x8 layout, role-mapped source fields, native Amb/sun/filter fields, untouched special weathers, short-row carry')
