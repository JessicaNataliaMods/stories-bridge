"""Prepare the original campaign fleet for SA, without touching the game.

Uses the original IDE, handling flags and wheel library, never a donor body.
The native model slots are reused to keep SA's vehicle-model pool bounded.
Unsupported classes/flags remain explicit blockers, not generic-car bindings.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
from ps2_world import Game
from vehicle_config_fields import convert_handling as adapt_handling, convert_ide as adapt_ide
from vehicle_config import convert_carcols, parse_carcols

ROOT = Path(__file__).resolve().parents[1]
TYPE_FLAGS = {0x10000: "bike", 0x20000: "heli", 0x40000: "plane", 0x80000: "boat"}
# Dedicated engine behavior depends on these SA IDs. Name matches are reserved
# first; ordinary unmatched cars use ordinary car slots, never special classes.
ALIASES = {"hotrod": "hotknife", "kuruma": "primo", "police": "copcarla", "fbicar": "fbiranch"}
RESERVED = set("firetruk trash ambulan rhino towtruck combine forklift vortex kart tractor caddy rcbandit rctiger securica bus coach mrwhoop barracks packer tanker cement sweeper dumper dozer utility flatbed firela hotknife copcarla copcarsf copcarvg copcarru swatvan fbiranch fbitruck enforcer".split())


def sha(path):
    with Path(path).open("rb") as f:
        return hashlib.file_digest(f, "sha256").hexdigest().upper()


def read_ide(path):
    section = ""
    rows = []
    for line in Path(path).read_text(encoding="ascii").splitlines():
        line = line.split("#", 1)[0].strip()
        if not line:
            continue
        if line.lower() == "end":
            section = ""
        elif re.fullmatch("[a-z]+", line):
            section = line
        elif section == "cars":
            fields = [x.strip() for x in line.split(",")]
            if len(fields) < 11:
                raise ValueError("Truncated vehicle IDE row")
            rows.append(fields)
    if len({int(r[0]) for r in rows}) != len(rows):
        raise ValueError("Duplicate vehicle model ID")
    return rows


def handling_rows(path, expected):
    rows = {}
    for line in Path(path).read_text(encoding="ascii").splitlines():
        line = line.strip()
        if not line or line[0] in ";!$%^":
            continue
        fields = line.split()
        if len(fields) == 1:
            continue
        if len(fields) != expected or fields[0] in rows:
            raise ValueError("Invalid/duplicate original handling row: " + fields[0])
        rows[fields[0]] = fields
    return rows


def convert_handling(fields, native_name, ide_fields):
    result = adapt_handling(fields, native_name, ide_fields)
    return result["fields"], result["unmapped_flags"]


def collision_models(path):
    data = Path(path).read_bytes()
    pos = 0
    models = {}
    while pos < len(data):
        if data[pos:pos + 4] != b"COLL" or pos + 32 > len(data):
            raise ValueError("Not an original legacy vehicle collision library")
        end = pos + 8 + struct.unpack_from("<I", data, pos + 4)[0]
        if end > len(data) or end < pos + 92:
            raise ValueError("Truncated vehicle collision")
        name = data[pos + 8:pos + 30].split(b"\0")[0].decode("ascii").lower()
        if not name or name in models:
            raise ValueError("Duplicate collision model")
        models[name] = data[pos:end]
        pos = end
    return models


def alias_collision(chunk, name):
    encoded = name.encode("ascii")
    if not encoded or len(encoded) > 21:
        raise ValueError("Vehicle collision name exceeds native field")
    return chunk[:8] + encoded.ljust(22, b"\0") + chunk[30:]


def run(converter, *args):
    r = subprocess.run([str(converter), *map(str, args)], capture_output=True, text=True, check=False)
    if r.returncode:
        raise ValueError(r.stdout.strip() + " " + r.stderr.strip())
    return r.stdout.strip()


def prepare_colour_loader(game, output):
    # Retail SA's sscanf does not accept the known "77.93,96" RGB typo.
    # Normalize RGB syntax before native loading so the verified common
    # prefix is deterministic; keep every native model row intact. The LCS
    # combinations are applied by the guarded runtime consumer afterwards.
    text = (game / "data/carcols.dat").read_text(encoding="cp1252")
    parsed = parse_carcols(text, allow_sa_typo=True, validate_color_indices=False)
    lines = text.splitlines(keepends=True)
    corrected = []
    for row in parsed.colors:
        content, separator, comment = lines[row.line_index].partition("#")
        if "." in content:
            lines[row.line_index] = ", ".join(map(str, row.rgb)) + "\t#" + comment if separator else ", ".join(map(str, row.rgb)) + "\n"
            corrected.append(row.index)
    loader_text = "".join(lines)
    if len(parse_carcols(loader_text, validate_color_indices=False).colors) > 128:
        raise ValueError("Native carcols loader file exceeds the unexpanded table capacity")
    (output / "carcols.dat").write_text(loader_text, encoding="cp1252")
    return {"PreparedPath": "carcols.dat", "TargetPath": "data/carcols.dat", "CorrectedRgbSyntax": corrected}


def validate(output, converter):
    manifest = json.loads((output / "manifest.json").read_text(encoding="utf-8"))
    if manifest["Installed"] or manifest["GameExecuted"] or manifest["CampaignPlayable"]:
        raise ValueError("Offline fleet manifest claims gameplay verification")
    for record in manifest["SourceFiles"]:
        if sha(record["Path"]) != record["Sha256"]:
            raise ValueError("Original fleet source changed: " + record["Path"])
    for record in manifest["OutputFiles"]:
        if sha(output / record["RelativePath"]) != record["Sha256"]:
            raise ValueError("Prepared fleet asset changed: " + record["RelativePath"])
    for record in manifest["Vehicles"]:
        if record["Prepared"]:
            name = record["NativeName"]
            run(converter, output / "models" / (name + ".dff"), "--validate-sa-car")
    run(converter, output / "models", "--validate-actors")
    run(converter, output / "vehicles.col", "--validate-col")
    print(f"PASS: {manifest['PreparedCount']} original cars; {len(manifest['OutputFiles'])} output hashes; gameExecuted=false")


def prepare(args):
    source = args.source.resolve()
    entities = args.entities.resolve()
    game = args.game.resolve()
    out = args.output.resolve()
    if out.exists():
        raise ValueError("Fleet staging already exists; validate it or select a new directory")
    original = read_ide(source / "DATA/DEFAULT.IDE")
    native = read_ide(game / "data/vehicles.ide")
    lcs_handling = handling_rows(source / "DATA/HANDLING.CFG", 34)
    sa_handling = handling_rows(game / "data/handling.cfg", 36)
    collisions = collision_models(source / "models/coll/vehicles.col")
    entity_manifest = json.loads((entities / "manifest.json").read_text(encoding="utf-8-sig"))
    if entity_manifest["Schema"] != 2 or entity_manifest["FailedConversionCount"]:
        raise ValueError("Original entity geometry stage is incomplete")
    resources = {r["Name"]: r for r in entity_manifest["Resources"]}
    wheel_names = {250: "wheel_sport", 251: "wheel_saloon", 252: "wheel_lightvan", 253: "wheel_classic", 254: "wheel_alloy", 255: "wheel_lighttruck", 256: "wheel_smallcar"}
    native_names = {r[1].lower(): r for r in native}
    slots = {}
    used = set()
    vehicles = []
    for row in original:
        name = row[1].lower()
        fields = lcs_handling.get(row[4])
        kind = row[3]
        if fields:
            flags = int(fields[31], 16)
            types = [t for bit, t in TYPE_FLAGS.items() if flags & bit]
            if types:
                kind = types[0]
        record = {"LcsId": int(row[0]), "Name": name, "OriginalClass": row[3], "PhysicalClass": kind,
                  "OriginalFields": row, "Prepared": False, "NativeId": None, "NativeName": None, "Reason": ""}
        vehicles.append(record)
        if not fields:
            record["Reason"] = "No handling row in the original retail library"
        elif kind != "car":
            record["Reason"] = "Requires its original " + kind + " component/physics adapter"
        else:
            _, unknown = convert_handling(fields, row[4], row)
            if unknown:
                record["Reason"] = f"LCS handling flags still need an adapter: 0x{unknown:X}"
            elif int(row[11]) not in wheel_names:
                record["Reason"] = "Original wheel model is not in the seven-wheel library"
            elif name not in collisions:
                record["Reason"] = "Original named vehicle collision missing"
            elif name not in resources or not resources[name]["GeometryValidated"]:
                record["Reason"] = "Original full vehicle geometry missing"
            else:
                desired = native_names.get(ALIASES.get(name, name))
                if desired and desired[3] == "car":
                    if int(desired[0]) in used:
                        raise ValueError("Two original vehicles claim the same native slot")
                    slots[name] = desired
                    used.add(int(desired[0]))
    for record in vehicles:
        if record["Reason"] or record["Name"] in slots:
            continue
        # Leave special engine IDs to their corresponding actual vehicles.
        # Generic fallback slots must not alter another native model's physics
        # through a shared handling name (e.g. damaged Glendale / Rancher lure).
        available = [r for r in native if r[3] == "car" and r[1].lower() not in RESERVED and int(r[0]) not in used
                     and sum(other[4] == r[4] for other in native) == 1]
        if not available:
            raise ValueError("SA's bounded native car slots are exhausted")
        slots[record["Name"]] = available[0]
        used.add(int(available[0][0]))
    (out / "models").mkdir(parents=True)
    (out / "wheels").mkdir()
    (out / "checks").mkdir()
    # SA's CVehicleModelInfo::FindTextureCB resolves vehicle.txd before the
    # individual model dictionary. Keep its native textures intact and append
    # the original LCS generic dictionary once, instead of 60 private copies.
    native_shared = game / "models/generic/vehicle.txd"
    original_shared = entities / "generic.txd"
    # The chrome environment map is stored with the original shotgun rather
    # than in generic. Export only that named raster through the existing PS2
    # writer, preserving its pixels instead of using a dummy white texture.
    game_source = Game(args.ps2_source, ROOT / "vendor/librwgta/librwgta-master/tools/storiesconv/hashes.inc")
    chrome = [t for t in game_source.named_textures()
              if t["dictionary"] == "chromegun" and t["name"] == "xv_chromenv"]
    if len(chrome) != 1:
        raise ValueError("Original chrome environment raster must occur exactly once")
    width, height, pixels = chrome[0]["image"]
    effect_inputs = out / "checks/shared-input"
    effect_inputs.mkdir()
    (effect_inputs / "effects.pst").write_bytes(struct.pack("<4sI32sII", b"PST1", 1,
        b"xv_chromenv", width, height) + pixels)
    run(args.writer, effect_inputs, out / "checks/shared-native")
    shared_with_effects = out / "checks/shared-with-effects.txd"
    run(args.converter, original_shared, "--merge-txd", out / "checks/shared-native/effects.txd", shared_with_effects)
    run(args.converter, native_shared, "--merge-txd", shared_with_effects, out / "models/vehicle.txd")
    sources = set()
    for p in [source / "DATA/DEFAULT.IDE", source / "DATA/HANDLING.CFG", source / "models/coll/vehicles.col",
              source / "models/generic/wheels.DFF", source / "DATA/MAIN.SCM", game / "data/vehicles.ide", game / "data/handling.cfg",
              entities / "manifest.json", original_shared, native_shared,
              args.ps2_source / "CHK/PS2/GAME.DTZ", args.ps2_source / "MODELS/GTA3PS2.IMG",
              source / "DATA/CARCOLS.DAT", game / "data/carcols.dat"]:
        sources.add(p)
    for wheel in wheel_names.values():
        run(args.converter, source / "models/generic/wheels.DFF", out / "wheels" / (wheel + ".dff"), "--atomic-name", wheel + "_l0")
    native_updates = {}
    handling_updates = {}
    col_chunks = []
    bindings = []
    for record in vehicles:
        if record["Reason"]:
            continue
        name = record["Name"]
        row = record["OriginalFields"]
        slot = slots[name]
        alias = slot[1]
        car_dir = entities / "resources" / name
        original_dff = car_dir / (name + ".dff")
        original_txd = car_dir / (row[2].lower() + ".txd")
        sources.update([original_dff, original_txd])
        record.update(NativeId=int(slot[0]), NativeName=alias, NativeHandling=slot[4], OriginalWheel=wheel_names[int(row[11])], WheelScale=float(row[12]))
        record["SharedNativeHandlingModels"] = [dict(NativeId=int(other[0]),NativeName=other[1])
            for other in native if other[4] == slot[4] and other[0] != slot[0]]
        try:
            # Vehicle clumps must carry their own native COLModel. An external
            # COL registration alone is insufficient for a standalone DFF override.
            legacy_col = out / "checks" / (name + "-source.col")
            mapped_col = out / "checks" / (name + "-native.col")
            legacy_col.write_bytes(alias_collision(collisions[name], alias))
            log = run(args.converter, legacy_col, "--map-col", mapped_col)
            log += "\n" + run(args.converter, original_dff, out / "models" / (alias + ".dff"), "--sa-car", out / "wheels" / (record["OriginalWheel"] + ".dff"), row[12], mapped_col)
            log += "\n" + run(args.converter, original_txd, out / "models" / (slot[2] + ".txd"))
            log += "\n" + run(args.converter, out / "models" / (alias + ".dff"), "--validate-sa-car")
            (out / "checks" / (name + ".txt")).write_text(log + "\n", encoding="utf-8")
            (out / "checks" / (name + "-hierarchy.txt")).write_text(run(args.converter, out / "models" / (alias + ".dff"), "--info") + "\n", encoding="utf-8")
        except ValueError as e:
            record["Reason"] = str(e)
            record["NativeId"] = None
            record["NativeName"] = None
            continue
        definition = adapt_ide(row, model_id=int(slot[0]), model_name=alias,
            texture_dictionary=slot[2], handling_name=slot[4], wheel_model_asset=record["OriginalWheel"])
        native_updates[int(slot[0])] = definition["fields"]
        physics = adapt_handling(lcs_handling[row[4]], slot[4], row)
        handling, unknown = physics["fields"], physics["unmapped_flags"]
        record["Configuration"] = {"Definition": definition, "Handling": physics}
        if unknown:
            raise ValueError("Unknown handling bits reached a native car binding")
        if slot[4] in handling_updates:
            raise ValueError("Prepared cars share a native handling slot")
        handling_updates[slot[4]] = handling
        col_chunks.append(alias_collision(collisions[name], alias))
        bindings.append("\t".join([row[0], slot[0], name, alias, "vehicle"]))
        record["EmbeddedCollision"] = True
        record["Prepared"] = True
    col_source = out / "original-renamed-vehicles.col"
    col_source.write_bytes(b"".join(col_chunks))
    run(args.converter, col_source, "--map-col", out / "vehicles.col")
    original_native_ide = (game / "data/vehicles.ide").read_text(encoding="ascii")
    def replace_ide(m):
        fields = native_updates.get(int(m.group(1)))
        return ", ".join(fields) if fields else m.group(0)
    ide = re.sub(r"^(\d+)\s*,[^\r\n]+", replace_ide, original_native_ide, flags=re.M)
    (out / "vehicles.ide").write_text(ide, encoding="ascii")
    native_handling = (game / "data/handling.cfg").read_text(encoding="ascii")
    def replace_handling(m):
        fields = handling_updates.get(m.group(1))
        return "\t".join(fields) if fields else m.group(0)
    native_handling = re.sub(r"^([A-Z0-9_]+)[ \t]+[^\r\n]+", replace_handling, native_handling, flags=re.M)
    (out / "handling.cfg").write_text(native_handling, encoding="ascii")
    (out / "vehicle-bindings.tsv").write_text("\n".join(bindings) + "\n", encoding="ascii")
    if len(read_ide(out / "vehicles.ide")) != len(native):
        raise ValueError("Prepared fleet changed SA's model-pool capacity")
    handling_rows(out / "handling.cfg", 36)
    colours = convert_carcols((source / "DATA/CARCOLS.DAT").read_text(encoding="cp1252"),
        (game / "data/carcols.dat").read_text(encoding="cp1252"), {"Vehicles": vehicles}, max_capacity=256)
    parsed_colours = parse_carcols(colours.carcols_text, allow_sa_typo=True)
    colour_lines = ["LCS_VEHICLE_COLOURS 1"]
    for colour in parsed_colours.colors:
        colour_lines.append("color " + " ".join(map(str, (colour.index, *colour.rgb))))
    for original_colour, mapped in sorted(colours.lcs_to_sa_colour_map.items()):
        colour_lines.append(f"map {original_colour} {mapped}")
    selected_names = {r["NativeName"]: r for r in vehicles if r["Prepared"]}
    for colour_row in parsed_colours.vehicle_rows:
        if colour_row.model in selected_names:
            if colour_row.section != "car":
                raise ValueError("Original LCS cars require exactly two colour channels")
            indices = colour_row.color_indices
            colour_lines.append("car " + " ".join(map(str,
                (selected_names[colour_row.model]["NativeId"], len(indices)//2, *indices))))
    (out / "vehicle-colours.dat").write_text("\n".join(colour_lines) + "\n", encoding="ascii")
    colour_loader = prepare_colour_loader(game, out)
    run(args.converter, out / "models", "--validate-actors")
    files = sorted(p for p in out.rglob("*") if p.is_file())
    manifest = {"Schema": 1, "Prepared": True, "Installed": False, "GameExecuted": False, "CampaignPlayable": False,
                "NativeBindingsVerifiedInGame": False, "StreamingInstallVerified": False, "SaModCompatibilityVerified": False,
                "VehicleDefinitions": len(original), "NativeModelCount": len(native), "AdditionalArchiveSlots": 0,
                "PreparedCount": sum(r["Prepared"] for r in vehicles), "Vehicles": vehicles,
                "VehicleColours": {"PreparedPath": "vehicle-colours.dat", "TargetPath": "data/lcs/vehicle-colours.dat",
                    "RequiredConsumer": "sa_load_vehicle_colours", "PaletteRows": len(parsed_colours.colors),
                    "OriginalIndexCount": len(colours.lcs_to_sa_colour_map), "Report": colours.report.as_dict()},
                "NativeColourLoader": colour_loader,
                "SharedTextures": {"SourceDictionary": "generic", "TargetPath": "models/generic/vehicle.txd",
                    "PreparedPath": "models/vehicle.txd", "NativeTexturesPreserved": True,
                    "OriginalChromeRasterSha256": hashlib.sha256(pixels).hexdigest(),
                    "EnvironmentAdapter": {"xv_vehicleenv128": "xvehicleenv128",
                        "reason": "Use the native SA vehicle environment map; the original named LCS raster is absent"},
                    "Resolution": "SA CVehicleModelInfo::FindTextureCB common vehicle dictionary"},
                "SourceFiles": [{"Path": str(p), "Sha256": sha(p)} for p in sorted(sources)],
                "OutputFiles": [{"RelativePath": str(p.relative_to(out)), "Sha256": sha(p)} for p in files]}
    (out / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"Prepared {manifest['PreparedCount']}/{len(original)} original cars in {out}; gameExecuted=false installed=false")
    for record in vehicles:
        if not record["Prepared"]:
            print(f"BLOCKED {record['LcsId']} {record['Name']}: {record['Reason']}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game", type=Path, required=True, help="read-only native SA reference directory")
    parser.add_argument("--source", type=Path, default=ROOT / "out/lcs-converted")
    parser.add_argument("--entities", type=Path, default=ROOT / "out/campaign-entities-v2")
    parser.add_argument("--output", type=Path, default=ROOT / "out/campaign-vehicles")
    parser.add_argument("--converter", type=Path, default=ROOT / "build/Release/lcs-convert-rw.exe")
    parser.add_argument("--writer", type=Path, default=ROOT / "build/Release/lcs-ps2-write.exe")
    parser.add_argument("--ps2-source", type=Path, default=ROOT / "local/originals/lcs-ps2", help="user's extracted original PS2 files")
    parser.add_argument("--validate-only", action="store_true")
    args = parser.parse_args()
    if args.validate_only:
        validate(args.output, args.converter)
    else:
        prepare(args)
