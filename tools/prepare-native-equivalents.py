"""Mission-first native SA entity profile, as explicitly requested by the user."""
import argparse
import hashlib
import json
import re
from pathlib import Path


def ide(path):
    sections = {}
    section = None
    for line in path.read_text(encoding="ascii").splitlines():
        line = line.split("#", 1)[0].strip()
        if not line:
            continue
        if line.lower() == "end":
            section = None
        elif "," not in line:
            section = line.lower()
        elif section:
            sections.setdefault(section, []).append(re.split(r"[,\s]+", line))
    return sections


VEHICLES = {
    "spider": "bravura", "idaho": "greenwoo", "stinger": "zr350", "blista": "blistac", "kuruma": "primo",
    "police": "copcarla", "fbicar": "fbiranch", "hearse": "romero", "dodo": "dodo", "bellyup": "mule", "mrwongs": "mule", "mafia": "sentinel", "yardie": "savanna",
    "yakuza": "zr350", "diablos": "blade", "columb": "patriot", "hoods": "burrito", "panlant": "burrito",
    "borgnine": "cabbie", "toyz": "pony", "campvan": "camper", "ballot": "boxville", "shelby": "sabre",
    "pontiac": "phoenix", "esprit": "supergt", "ammotruk": "barracks", "hotrod": "hotknife",
    "sindacco_car": "sentinel", "forelli_car": "sentinel", "ferry": "tropic", "ghost": "predator",
    "train": "streak", "escape": "maverick", "chopper": "maverick", "airtrain": "at400", "deaddodo": "dodo",
    "angel": "freeway", "angel2": "freeway", "noodleboy": "pizzaboy", "sanchez2": "sanchez",
    "rcgoblin": "rcgoblin", "rcraider": "rcraider", "vcnmav": "maverick",
}
WEAPONS = {
    "cellphone": "cellphone", "chisel": "knifecur", "hockeystick": "bat", "nitestick": "nitestick", "bat": "bat",
    "brassknuckle": "brassknuckle", "screwdriver": "knifecur", "golfclub": "golfclub", "nightstick": "nitestick",
    "knifecur": "knifecur", "baseball": "bat", "handaxe": "bat", "cleaver": "knifecur", "machete": "katana",
    "katana": "katana", "chnsaw": "chnsaw", "grenade": "grenade", "teargas": "teargas", "molotov": "molotov",
    "missile": "missile", "glock17": "colt45", "python": "desert_eagle", "ak47": "ak47", "chromegun": "chromegun",
    "shotgspa": "shotgspa", "buddyshot": "sawnoff", "m4": "m4", "tec9": "tec9", "uzi": "micro_uzi",
    "ingramsl": "micro_uzi", "mp5k": "mp5lng", "sniper": "sniper", "laser": "sniper",
    "rocketla": "rocketla", "flame": "flame", "m60": "m4", "minigun": "minigun", "minigun2": "minigun",
    "bomb": "satchel", "camera": "camera", "fingers": "brassknuckle",
}


def prepare(game, source, output):
    original = ide(source / "DATA/DEFAULT.IDE")
    cars = {r[1].lower(): r for r in ide(game / "data/vehicles.ide")["cars"]}
    peds = {r[1].lower(): r for r in ide(game / "data/peds.ide")["peds"] if int(r[0]) < 290}
    weapons = {r[1].lower(): r for r in ide(game / "data/default.ide")["weap"]}
    rows = []
    for r in original["cars"]:
        name = r[1].lower()
        target = cars[VEHICLES.get(name, name)]
        rows.append([r[0], target[0], name, target[1].lower(), "vehicle"])
    gang_peds = {"GANG1": ["vmaff1", "vmaff2"], "GANG2": ["ballas1", "ballas2"], "GANG3": ["lsv1", "lsv2"],
                 "GANG4": ["triada", "triadb"], "GANG5": ["swat", "swat"], "GANG6": ["vla1", "vla2"],
                 "GANG7": ["fam1", "fam2"], "GANG8": ["vmaff1", "vmaff2"], "GANG9": ["hmyri", "omyst"]}
    special = {"cop": "lapd1", "swat": "swat", "fbi": "fbi", "army": "army", "medic": "laemt1",
               "fireman": "lafd1", "special01": "vmaff1"}
    for r in original["peds"]:
        name = r[1].lower()
        if name in peds:
            target_name = name
        elif name in special:
            target_name = special[name]
        elif r[3] in gang_peds:
            target_name = gang_peds[r[3]][int(r[0]) % 2]
        elif name.startswith("special"):
            target_name = "vmaff1"
        elif r[3] == "CIVFEMALE" or "woman" in r[5].lower():
            target_name = "wfyri"
        elif r[3] == "COP":
            target_name = "lapd1"
        elif r[3] == "EMERGENCY":
            target_name = "laemt1"
        elif r[3] == "FIREMAN":
            target_name = "lafd1"
        else:
            target_name = "male01"
        if name == "null":
            target_name = "null"
        target = peds[target_name]
        rows.append([r[0], target[0], name, target[1].lower(), "ped"])
    for r in original["weap"]:
        name = r[1].lower()
        target = weapons[WEAPONS[name]]
        rows.append([r[0], target[0], name, target[1].lower(), "object"])
    if len({r[0] for r in rows}) != len(rows):
        raise ValueError("Original entity IDs overlap")
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text("\n".join("\t".join(r) for r in rows) + "\n", encoding="ascii")
    report = {"OriginalScmSha256": hashlib.sha256((source / "DATA/MAIN.SCM").read_bytes()).hexdigest().upper(),
              "ProfileSha256": hashlib.sha256(output.read_bytes()).hexdigest().upper(),
              "Vehicles": sum(r[-1] == "vehicle" for r in rows), "Peds": sum(r[-1] == "ped" for r in rows),
              "WeaponModels": sum(r[-1] == "object" for r in rows), "UsesNativeSaEntities": True, "GameExecuted": False}
    output.with_suffix(".json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report))


if __name__ == "__main__":
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game", type=Path, required=True)
    parser.add_argument("--source", type=Path, default=root / "out/lcs-converted")
    parser.add_argument("--output", type=Path, default=root / "out/native-equivalents.tsv")
    args = parser.parse_args()
    prepare(args.game, args.source, args.output)
