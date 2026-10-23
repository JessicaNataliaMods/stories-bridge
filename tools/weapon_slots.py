"""Own SA weapon slots for every original LCS weapon, the minigun barrel and
SA weapon.dat values from the original WEAPON.DAT. Pure data transforms on the
user's own converted files; never executes GTA.

docs/weapons.md documents the slot choices and conversion rules.
"""
import math
import struct

# Original model name -> SA weapon model it replaces. Every LCS weapon type gets
# its own SA weapon type; only the laser scope shares the sniper (SA ties the
# scope camera to the sniper type) and fingers (no weapon type) share brass
# knuckles. Gift weapons and the free gun types take the remaining weapons.
WEAPON_TARGETS = {
    "cellphone": "cellphone", "brassknuckle": "brassknuckle", "fingers": "brassknuckle",
    "chisel": "gun_dildo2", "hockeystick": "golfclub", "nitestick": "nitestick", "knifecur": "knifecur",
    "bat": "bat", "handaxe": "gun_dildo1", "cleaver": "gun_vibe2", "machete": "gun_vibe1",
    "katana": "katana", "chnsaw": "chnsaw", "grenade": "grenade", "teargas": "teargas", "molotov": "molotov",
    "missile": "missile", "glock17": "colt45", "python": "desert_eagle", "ak47": "ak47",
    "chromegun": "chromegun", "shotgspa": "shotgspa", "buddyshot": "sawnoff", "m4": "m4", "tec9": "tec9",
    "uzi": "micro_uzi", "ingramsl": "silenced", "mp5k": "mp5lng", "sniper": "sniper", "laser": "sniper",
    "rocketla": "rocketla", "flame": "flame", "m60": "cuntgun", "minigun": "minigun", "minigun2": "minigun",
    "bomb": "satchel", "camera": "camera",
    # names used by older converted catalogues
    "screwdriver": "gun_dildo2", "golfclub": "golfclub", "nightstick": "nitestick", "baseball": "bat",
}
# Shared targets keep this original as the visible model.
CANONICAL = {"brassknuckle": "brassknuckle", "sniper": "sniper", "minigun": "minigun"}

# Original eWeaponType -> SA eWeaponType (src/native.cpp equivalent_weapon_counterparts).
WEAPON_TYPES = [0, 1, 11, 2, 3, 4, 5, 10, 13, 12, 8, 9, 16, 39, 17, 18, 35, 22, 24, 25, 27, 26, 32, 28,
                23, 29, 31, 30, 34, 34, 35, 37, 33, 38, 40, 38, 43]
SA_WEAPON_NAMES = ["UNARMED", "BRASSKNUCKLE", "GOLFCLUB", "NIGHTSTICK", "KNIFE", "BASEBALLBAT", "SHOVEL",
                   "POOLCUE", "KATANA", "CHAINSAW", "DILDO1", "DILDO2", "VIBE1", "VIBE2", "FLOWERS", "CANE",
                   "GRENADE", "TEARGAS", "MOLOTOV", "ROCKET", "ROCKET_HS", "FREEFALL_BOMB", "PISTOL",
                   "PISTOL_SILENCED", "DESERT_EAGLE", "SHOTGUN", "SAWNOFF", "SPAS12", "MICRO_UZI", "MP5", "AK47",
                   "M4", "TEC9", "COUNTRYRIFLE", "SNIPERRIFLE", "RLAUNCHER", "RLAUNCHER_HS", "FTHROWER", "MINIGUN",
                   "SATCHEL_CHARGE", "DETONATOR", "SPRAYCAN", "EXTINGUISHER", "CAMERA"]
# SA slots of the reused types, from the original weapon slot (WEAPON.DAT
# column b: 1 melee, 5 submachine gun, 7 heavy). Other types keep SA's slot,
# which already matches (and SA's special slots such as the detonator's 12).
REUSED_SLOTS = {"DILDO1": 1, "DILDO2": 1, "VIBE1": 1, "VIBE2": 1, "PISTOL_SILENCED": 4, "COUNTRYRIFLE": 7}
# SA melee combo for gift types reused by original melee weapons, from the
# original animation group (knife/screwdrv swing, baseball swing).
MELEE_COMBOS = {"knife": "KNIFE", "screwdrv": "KNIFE", "baseball": "BBALLBAT", "golfclub": "BBALLBAT"}
# Gun types that were unused SA slots copy the animation data of the SA weapon
# they now behave like (Ingram: one-handed SMG; M60: two-handed rifle stance, since the flame group
# is the flamethrower's low hip pose and its own group has no SA counterpart).
BORROWED_GUN_ROWS = {"PISTOL_SILENCED": "MICRO_UZI", "COUNTRYRIFLE": "M4"}
# v95+: original animation group (WEAPON.DAT column P) -> SA group whose block
# carries the converted original animations (tools/weapon_animations.py), and
# the SA weapon that natively uses that group: a weapon moved to another group
# takes that weapon's standard-skill flags; others keep their own.
# Off until tools/weapon_animations.py has verified conversions (VERIFIED):
# the original loop frames only fit the original animations.
ANIMATIONS_ENABLED = False
# The player always walks while firing: SA grants MOVEFIRE (second flag digit, 0x20)
# only at the top skill level, so every skill row of every gun that can aim
# (CANAIM, 0x1) gets MOVEAIM and MOVEFIRE (0x30). (The original LCS only allowed it for the Colt45,
# Tec9 and silenced Ingram; this is a deliberate user-requested change.)
MIN_ACCURACY = 1.2
ANIMATED_GROUPS = {"colt45": "PISTOL", "python": "DESERT_EAGLE", "shotgun": "SHOTGUN", "buddy": "SPAS12",
                   "uzi": "MP5", "rifle": "M4", "flame": "FTHROWER", "grenade": "GRENADE"}


def original_weapons(text):
    """Rows of the original WEAPON.DAT in eWeaponType order."""
    rows = []
    for line in text.splitlines():
        line = line.split('#', 1)[0].strip()
        if not line:
            continue
        if line == 'ENDWEAPONDATA':
            break
        fields = line.split()
        if len(fields) != 26:
            raise ValueError('Unexpected original weapon row: ' + fields[0])
        rows.append(fields)
    if len(rows) != len(WEAPON_TYPES):
        raise ValueError('Original weapon table size changed')
    return rows


def primary_sources(rows, models, sa_type_models):
    """SA type -> original weapon row it is converted from: the original
    weapon whose own model is mounted on that SA type's model (so the rocket
    launcher, not its projectile, and the sniper, not the laser scope)."""
    result = {}
    for index, row in enumerate(rows):
        sa_type = WEAPON_TYPES[index]
        model = int(row[22])
        if model < 0:
            continue
        name = models[model]
        target = WEAPON_TARGETS[name]
        if CANONICAL.get(target, name) != name or sa_type_models.get(sa_type) != target:
            continue
        if sa_type in result:
            raise ValueError('Two original weapons claim SA type ' + SA_WEAPON_NAMES[sa_type])
        result[sa_type] = index
    return result


def number(value):
    text = ('%.4f' % value).rstrip('0').rstrip('.')
    return text if text not in ('-0', '') else '0'


def sa_type_models(sa_text, sa_model_names):
    """SA weapon type -> model name, from weapon.dat column E and default.ide."""
    result = {}
    for line in sa_text.splitlines():
        fields = line.split()
        if len(fields) > 5 and fields[0] in ('$', '£') and fields[1] in SA_WEAPON_NAMES:
            model = int(fields[5])
            if model >= 0:
                result.setdefault(SA_WEAPON_NAMES.index(fields[1]), sa_model_names[model])
    return result


def convert_weapon_dat(sa_text, original_text, models, sa_model_names, animations=False):
    """SA weapon.dat with the values of each mapped original weapon:
    weapon range, target range (capped by it), clip, damage, fire offset,
    projectile speed/radius/lifespan/spread, slot; gift melee types get the
    melee slot and the original swing combo. Unmapped SA types are unchanged."""
    animations = animations and ANIMATIONS_ENABLED
    rows = original_weapons(original_text)
    sources = primary_sources(rows, models, sa_type_models(sa_text, sa_model_names))
    by_name = {SA_WEAPON_NAMES[t]: rows[i] for t, i in sources.items()}
    borrowed, standard = {}, {}
    for line in sa_text.splitlines():
        fields = line.split()
        if fields and fields[0] == '$' and fields[1] in BORROWED_GUN_ROWS.values():
            borrowed.setdefault(fields[1], []).append(fields)
        if fields and fields[0] == '$' and len(fields) > 25 and fields[14] == '1':
            standard.setdefault(fields[1], fields)
    output = []
    seen = {name: 0 for name in BORROWED_GUN_ROWS}
    for line in sa_text.splitlines():
        fields = line.split()
        if len(fields) < 2 or fields[1] not in by_name or fields[0] not in ('$', '\xa3'):
            output.append(line)
            continue
        original = by_name[fields[1]]
        weapon_range = float(original[2])
        slot = str(REUSED_SLOTS.get(fields[1], fields[7]))
        if fields[0] == '\xa3':
            if len(fields) != 12:
                raise ValueError('Unexpected SA melee row: ' + fields[1])
            fields[4] = number(weapon_range)
            fields[7] = slot
            if fields[1] in ('DILDO1', 'DILDO2', 'VIBE1', 'VIBE2'):
                fields[8] = MELEE_COMBOS[original[14].lower()]
            output.append('\t'.join(fields))
            continue
        if len(fields) not in (26, 30):
            raise ValueError('Unexpected SA gun row: ' + fields[1])
        if fields[1] in BORROWED_GUN_ROWS:
            template = borrowed[BORROWED_GUN_ROWS[fields[1]]]
            source = template[min(seen[fields[1]], len(template) - 1)]
            seen[fields[1]] += 1
            # Animation group, skill accuracy/move speed, loops, breakout, flags.
            for index in (8, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25):
                fields[index] = source[index]
        fields[4] = number(weapon_range)
        fields[3] = number(min(float(fields[3]), weapon_range))
        fields[7] = slot
        if original[1] != 'PROJECTILE':
            fields[9] = original[5]
        fields[10] = original[6]
        # Fire offset along the barrel; the sniper/laser/camera rows of the
        # original hold no barrel offset (x = 0) and keep SA's.
        if float(original[11]) > 0:
            fields[11:14] = original[11:14]
        if len(fields) == 30 and original[1] in ('PROJECTILE', 'AREA_EFFECT'):
            fields[26:30] = original[7:11]
        group = original[14].lower()
        loops = original[15:22]
        if group == 'm60':
            # The original M60 group has no SA block; it plays the rifle group
            # with the Ruger's loops (an SA type that natively uses it).
            group, loops = 'rifle', by_name['AK47'][15:22]
        if animations and group in ANIMATED_GROUPS:
            # Every skill row plays the converted original group (LCS has no
            # weapon skill, low-skill or twin-pistol variants), with that
            # group's native flags and the original animation loops.
            own = standard.get(fields[1])
            template = own if own and own[8] == group else standard[ANIMATED_GROUPS[group]]
            fields[8] = group
            fields[25] = template[25]
            fields[18:25] = loops
        if len(fields) in (26, 30) and fields[0] == '$' and fields[2] == 'INSTANT_HIT' and int(fields[25], 16) & 0x1:
            # ONLYFREEAIM (0x8) instead of CANAIM, like the minigun: shots follow the crosshair, not the arm;
            # the skill-less LCS has no 'bad' animation group variants either.
            fields[25] = '%X' % ((int(fields[25], 16) & ~0x1) | 0x38)
            if fields[8].endswith('bad'):
                fields[8] = fields[8][:-3]
            # LCS has no weapon skill: accuracy (column R, which also sizes SA's crosshair
            # ring) is never worse than the best SA skill level.
            fields[16] = number(max(float(fields[16]), MIN_ACCURACY))
        output.append('\t'.join(fields))
    return '\r\n'.join(output) + '\r\n'


# ---- RenderWare minigun assembly -------------------------------------------

def chunk(kind, payload, version):
    return struct.pack('<III', kind, len(payload), version) + payload


def children(data, start, end):
    result, cursor = [], start
    while cursor + 12 <= end:
        kind, size, version = struct.unpack_from('<III', data, cursor)
        result.append((kind, cursor + 12, cursor + 12 + size, version))
        cursor += 12 + size
    if cursor != end:
        raise ValueError('Truncated RenderWare chunk')
    return result


def clump_parts(data):
    (kind, start, end, version), = children(data, 0, len(data))
    if kind != 0x10:
        raise ValueError('Not a clump')
    parts = children(data, start, end)
    kinds = [p[0] for p in parts]
    if kinds != [1, 0x0E, 0x1A, 0x14, 3]:
        raise ValueError('Unexpected weapon clump layout')
    return version, parts


def barrel_matrix():
    """LCS CPed render: barrel = hand * T(0.829,-0.001,0.226) * Rotate(-4.477,
    29.731, 1.064 deg times vecTestTemp(-1,-1,-1)) * RotateX(spin). SA spins
    frame minigun2 about its local X (pre-concatenated), so the frame matrix is
    the fixed part. CMatrix::Rotate columns; CMatrix forward/up are RW up/at."""
    x, y, z = (math.radians(a) for a in (4.477, -29.731, -1.064))
    cx, sx, cy, sy, cz, sz = math.cos(x), math.sin(x), math.cos(y), math.sin(y), math.cos(z), math.sin(z)
    right = (cz * cy - sz * sx * sy, cz * sx * sy + sz * cy, -cx * sy)
    forward = (-sz * cx, cz * cx, sx)
    up = (sz * sx * cy + cz * sy, sz * sy - cz * sx * cy, cx * cy)
    return right, forward, up, (0.829, -0.001, 0.226)


def assemble_minigun(body, barrel):
    """One SA minigun clump: the body atomic as converted, plus frame
    'minigun2' (child of the root) carrying the original barrel geometry."""
    version, parts = clump_parts(body)
    barrel_version, barrel_parts = clump_parts(barrel)
    if barrel_version != version:
        raise ValueError('Minigun parts use different RenderWare versions')
    frames = parts[1]
    frame_children = children(body, frames[1], frames[2])
    struct_kind, s0, s1, _ = frame_children[0]
    count = struct.unpack_from('<I', body, s0)[0]
    if struct_kind != 1 or count != 2 or len(frame_children) != 1 + count:
        raise ValueError('Unexpected minigun body frames')
    frame_struct = bytearray(body[s0:s1])
    right, forward, up, position = barrel_matrix()
    frame_struct[0:4] = struct.pack('<I', count + 1)
    frame_struct += struct.pack('<12f', *right, *forward, *up, *position) + struct.pack('<iI', 0, 0)
    name = chunk(0x253F2FE, b'minigun2', version)
    frame_list = chunk(1, bytes(frame_struct), version)
    for kind, start, end, chunk_version in frame_children[1:]:
        frame_list += chunk(kind, body[start:end], chunk_version)
    frame_list += chunk(3, name, version)
    # Geometry list: body geometry then the barrel geometry.
    geometry_body = children(body, parts[2][1], parts[2][2])
    geometry_barrel = children(barrel, barrel_parts[2][1], barrel_parts[2][2])
    if struct.unpack_from('<I', body, geometry_body[0][1])[0] != 1 or struct.unpack_from('<I', barrel, geometry_barrel[0][1])[0] != 1:
        raise ValueError('Expected one geometry per minigun part')
    geometries = chunk(1, struct.pack('<I', 2), version)
    for data, entries in ((body, geometry_body), (barrel, geometry_barrel)):
        kind, start, end, chunk_version = entries[1]
        geometries += chunk(kind, data[start:end], chunk_version)
    # Atomics: the body atomic unchanged; the barrel atomic on frame 2, geometry 1.
    atomic_body = body[parts[3][1] - 12:parts[3][2]]
    barrel_atomic = children(barrel, barrel_parts[3][1], barrel_parts[3][2])
    atomic_struct = bytearray(barrel[barrel_atomic[0][1]:barrel_atomic[0][2]])
    atomic_struct[0:8] = struct.pack('<II', count, 1)
    atomic = chunk(1, bytes(atomic_struct), version)
    for kind, start, end, chunk_version in barrel_atomic[1:]:
        atomic += chunk(kind, barrel[start:end], chunk_version)
    clump_struct = bytearray(body[parts[0][1]:parts[0][2]])
    clump_struct[0:4] = struct.pack('<I', 2)
    payload = (chunk(1, bytes(clump_struct), version) + chunk(0x0E, frame_list, frames[3]) +
               chunk(0x1A, geometries, parts[2][3]) + atomic_body + chunk(0x14, atomic, barrel_parts[3][3]) +
               body[parts[4][1] - 12:parts[4][2]])
    return chunk(0x10, payload, version)


def texture_names(data):
    (kind, start, end, version), = children(data, 0, len(data))
    names = []
    for kind, s, e, _ in children(data, start, end):
        if kind == 0x15:
            raster = children(data, s, e)[0]
            names.append(data[raster[1] + 8:raster[1] + 40].split(b'\0')[0].decode('latin1').lower())
    return names


def merge_textures(body_txd, barrel_txd):
    """Body dictionary plus any barrel texture it does not already hold."""
    (kind, start, end, version), = children(body_txd, 0, len(body_txd))
    (bkind, bstart, bend, _), = children(barrel_txd, 0, len(barrel_txd))
    have = set(texture_names(body_txd))
    body_parts = children(body_txd, start, end)
    added = [p for p in children(barrel_txd, bstart, bend) if p[0] == 0x15 and
             barrel_txd[p[1] + 8 + 12:p[1] + 40 + 12].split(b'\0')[0].decode('latin1').lower() not in have]
    if not added:
        return bytes(body_txd)
    count_device = bytearray(body_txd[body_parts[0][1]:body_parts[0][2]])
    count_device[0:2] = struct.pack('<H', struct.unpack_from('<H', count_device, 0)[0] + len(added))
    payload = chunk(1, bytes(count_device), body_parts[0][3])
    for kind, s, e, v in body_parts[1:-1]:
        payload += chunk(kind, body_txd[s:e], v)
    for kind, s, e, v in added:
        payload += chunk(kind, barrel_txd[s:e], v)
    last = body_parts[-1]
    payload += chunk(last[0], body_txd[last[1]:last[2]], last[3])
    return chunk(0x16, payload, version)
