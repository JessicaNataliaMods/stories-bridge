"""Rewrite LCS control-icon tokens in a native SA GXT into SA control tokens.

LCS text writes a button as "~k~ ~VEACC~". SA's text engine expects
"~k~~VEHICLE_ACCELERATE~" and shows the player's real key; the LCS codes are
unknown to it and print empty squares. The same table lives in
src/text.cpp (translate_controls) so the ASI's mission-text check, which
compares the installed GXT with the original, applies the same rewrite.

usage: translate-gxt-controls.py INPUT.GXT OUTPUT.GXT   (INPUT == OUTPUT is refused)
"""
import struct
import sys

SINGLE = {
    'VEACC': 'VEHICLE_ACCELERATE', 'VEBRK': 'VEHICLE_BRAKE', 'VEHN': 'VEHICLE_HORN',
    'VEHB': 'VEHICLE_HANDBRAKE', 'VEEE': 'VEHICLE_ENTER_EXIT', 'VEWEP': 'VEHICLE_FIREWEAPON',
    'VELL': 'VEHICLE_LOOKLEFT', 'VELR': 'VEHICLE_LOOKRIGHT', 'VELB': 'VEHICLE_LOOKBEHIND',
    'PDFW': 'PED_FIREWEAPON', 'PDLT': 'PED_LOCK_TARGET', 'PDLOO': 'PED_LOOKBEHIND',
    'PDSPR': 'PED_SPRINT', 'PDCTL': 'PED_CYCLE_TARGET_RIGHT', 'SNZI': 'PED_SNIPER_ZOOM_IN',
    'SNZO': 'PED_SNIPER_ZOOM_OUT', 'FREE1': 'PED_LOCK_TARGET', 'ANS': 'PED_ANSWER_PHONE',
    'TGSUB': 'TOGGLE_SUBMISSIONS', 'CVEIW': 'CAMERA_CHANGE_VIEW_ALL_SITUATIONS',
    'TRSK': 'VEHICLE_ENTER_EXIT', 'PUCF': 'VEHICLE_ENTER_EXIT', 'AMBUY': 'PED_SPRINT',
    'AMEXI': 'VEHICLE_ENTER_EXIT',
}
PAIR = {
    'VESTR': ('VEHICLE_STEERLEFT', 'VEHICLE_STEERRIGHT'),
    'VEWEA': ('VEHICLE_TURRETLEFT', 'VEHICLE_TURRETRIGHT'),
    'VEWEI': ('VEHICLE_STEERUP', 'VEHICLE_STEERDOWN'),
    'VECRS': ('VEHICLE_RADIO_STATION_UP', 'VEHICLE_RADIO_STATION_DOWN'),
    'PDCWE': ('PED_CYCLE_WEAPON_LEFT', 'PED_CYCLE_WEAPON_RIGHT'),
    'AMMOV': ('GO_FORWARD', 'GO_BACK'),
}
PLAIN = {'FREE2': 'mouse'}


def translate(text):
    out = []
    at = 0
    while True:
        start = text.find('~k~ ~', at)
        if start < 0:
            out.append(text[at:])
            return ''.join(out)
        end = text.find('~', start + 5)
        code = text[start + 5:end] if end > 0 else ''
        if code in SINGLE:
            replacement = '~k~~%s~' % SINGLE[code]
        elif code in PAIR:
            replacement = '~k~~%s~/~k~~%s~' % PAIR[code]
        elif code in PLAIN:
            replacement = PLAIN[code]
        else:
            # Already an SA control name: only the stray space has to go.
            out.append(text[at:start] + '~k~~')
            at = start + 5
            continue
        out.append(text[at:start])
        out.append(replacement)
        at = end + 1


def parse(data):
    if data[:4] != bytes([4, 0, 8, 0]) or data[4:8] != b'TABL':
        raise SystemExit('Not a native SA GXT')
    table = struct.unpack_from('<I', data, 8)[0]
    banks = {}
    for entry in range(12, 12 + table, 12):
        name = data[entry:entry + 8].split(b'\0')[0].decode('latin1')
        cursor = struct.unpack_from('<I', data, entry + 8)[0]
        if name != 'MAIN':
            cursor += 8
        key_size = struct.unpack_from('<I', data, cursor + 4)[0]
        rows = [struct.unpack_from('<II', data, cursor + 8 + i) for i in range(0, key_size, 8)]
        cursor += 8 + key_size
        data_size = struct.unpack_from('<I', data, cursor + 4)[0]
        blob = data[cursor + 8:cursor + 8 + data_size]
        banks[name] = {key: blob[offset:blob.index(b'\0', offset)] for offset, key in rows}
    return banks


def build(banks):
    out = bytearray([4, 0, 8, 0]) + b'TABL' + struct.pack('<I', 12 * len(banks))
    table = len(out)
    out += bytes(12 * len(banks))
    order = ['MAIN'] + sorted(n for n in banks if n != 'MAIN')
    for n, name in enumerate(order):
        offset = len(out)
        out[table + 12 * n:table + 12 * n + len(name)] = name.encode('latin1')
        out[table + 12 * n + 8:table + 12 * n + 12] = struct.pack('<I', offset)
        if name != 'MAIN':
            out += name.encode('latin1').ljust(8, b'\0')
        bank = banks[name]
        out += b'TKEY' + struct.pack('<I', 8 * len(bank))
        blob = bytearray()
        for key in sorted(bank):
            out += struct.pack('<II', len(blob), key)
            blob += bank[key] + b'\0'
        out += b'TDAT' + struct.pack('<I', len(blob)) + blob
    return bytes(out)


def main(source, target):
    if source == target:
        raise SystemExit('Refusing to overwrite the input')
    data = open(source, 'rb').read()
    banks = parse(data)
    if build(banks) != data:
        raise SystemExit('Round trip differs; refusing to rewrite %s' % source)
    changed = 0
    for bank in banks.values():
        for key, value in bank.items():
            new = translate(value.decode('latin1')).encode('latin1')
            if new != value:
                bank[key] = new
                changed += 1
    open(target, 'wb').write(build(banks))
    print('%s: %d strings rewritten' % (source, changed))


if __name__ == '__main__':
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    main(sys.argv[1], sys.argv[2])
