"""Mount the prepared LCS car fleet into a new SA campaign stage, offline."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import uuid


CONFIG_PATHS = (
    "data/vehicles.ide",
    "data/handling.cfg",
    "data/lcs/native-equivalents.tsv",
)
PROFILE_PATH = "data/gta.dat"
REPORT_PATH = "metadata/campaign-vehicles.json"


def digest_bytes(data):
    return hashlib.sha256(data).hexdigest()


def digest(path):
    return digest_bytes(Path(path).read_bytes())


def inside(root, path):
    return Path(path).resolve(strict=False).is_relative_to(Path(root).resolve())


def read_json(path):
    return json.loads(Path(path).read_text(encoding="utf-8-sig"))


def read_ide(text):
    """Return cars IDE rows with their source line index and parsed fields."""
    section = ""
    rows = {}
    for index, original in enumerate(text.splitlines(keepends=True)):
        line = original.split("#", 1)[0].strip()
        if not line:
            continue
        if line.lower() == "end":
            section = ""
        elif re.fullmatch(r"[a-z]+", line, flags=re.I):
            section = line.lower()
        elif section == "cars" and "," in line:
            fields = re.split(r"[,\s]+", line.strip())
            if len(fields) < 11:
                raise ValueError("Truncated vehicle IDE row")
            try:
                model_id = int(fields[0])
            except ValueError as exc:
                raise ValueError("Invalid vehicle IDE model ID: " + fields[0]) from exc
            if model_id in rows:
                raise ValueError("Duplicate vehicle IDE model ID: " + str(model_id))
            rows[model_id] = {"line": index, "fields": fields}
    return rows


def read_handling(text):
    """Return ordinary SA handling rows, keyed by the native handling name."""
    rows = {}
    for index, original in enumerate(text.splitlines(keepends=True)):
        stripped = original.strip()
        if not stripped or stripped[0] in ";!$%^":
            continue
        fields = stripped.split()
        if len(fields) < 2:
            continue
        if fields[0] in rows:
            raise ValueError("Duplicate handling row: " + fields[0])
        rows[fields[0]] = {"line": index, "fields": fields}
    return rows


def _line_ending(line):
    if line.endswith("\r\n"):
        return "\r\n"
    if line.endswith("\n"):
        return "\n"
    if line.endswith("\r"):
        return "\r"
    return ""


def _ide_replacement(original, fields):
    ending = _line_ending(original)
    body = original[:-len(ending)] if ending else original
    comment = ""
    if "#" in body:
        body, comment = body.split("#", 1)
        comment = "\t#" + comment
    return ", ".join(fields) + comment + ending


def _handling_replacement(original, fields):
    return "\t".join(fields) + _line_ending(original)


def merge_ide(stage_text, fleet_text, native_text, mappings):
    stage_rows = read_ide(stage_text)
    fleet_rows = read_ide(fleet_text)
    native_rows = read_ide(native_text)
    lines = stage_text.splitlines(keepends=True)
    selected = []
    ids = {item["NativeId"] for item in mappings}
    dictionaries = set()
    model_names = set()
    for item in mappings:
        model_id = item["NativeId"]
        if model_id not in stage_rows or model_id not in fleet_rows or model_id not in native_rows:
            raise ValueError("Missing selected native vehicle IDE row: " + str(model_id))
        fields = fleet_rows[model_id]["fields"]
        target = stage_rows[model_id]["fields"]
        native = native_rows[model_id]["fields"]
        if fields[1].lower() != item["NativeName"].lower():
            raise ValueError("Prepared native model name disagrees with its manifest")
        if fields[3].lower() != item.get("PhysicalClass", "car") or fields[4].upper() != item["NativeHandling"].upper():
            raise ValueError("Prepared IDE row has the wrong class or handling name")
        if fields[2].lower() != native[2].lower():
            raise ValueError("Prepared TXD dictionary disagrees with the native vehicle slot")
        if [target[1].lower(), target[2].lower(), target[3].lower(), target[4].upper()] != [
            native[1].lower(), native[2].lower(), native[3].lower(), native[4].upper()
        ]:
            raise ValueError("New stage changed a selected native vehicle identity")
        # A selected TXD dictionary must belong to just this model. Shared
        # dictionaries make a replacement affect an unselected vehicle too.
        dictionary = fields[2].casefold()
        if dictionary in dictionaries:
            raise ValueError("Selected cars share a TXD dictionary: " + fields[2])
        dictionaries.add(dictionary)
        model_name = fields[1].casefold()
        if model_name in model_names:
            raise ValueError("Selected cars share a native DFF name: " + fields[1])
        model_names.add(model_name)
        other_users = [
            other_id for other_id, other in stage_rows.items()
            if other["fields"][2].casefold() == dictionary and other_id not in ids
        ]
        if other_users:
            raise ValueError("Selected TXD dictionary is shared with unselected model IDs: " +
                             fields[2] + " (" + ", ".join(map(str, other_users)) + ")")
        lines[stage_rows[model_id]["line"]] = _ide_replacement(lines[stage_rows[model_id]["line"]], fields)
        selected.append({"NativeId": model_id, "NativeName": fields[1], "Dictionary": fields[2],
                         "NativeHandling": fields[4], "Fields": fields})
    # No selected handling key may be shared by another selected or unselected car.
    for item in mappings:
        uses = [
            model_id for model_id, row in stage_rows.items()
            if row["fields"][4].upper() == item["NativeHandling"].upper()
        ]
        # SA's two fire engines intentionally share FIRETRUK. Keep that native
        # relationship explicit; generic fallback slots are chosen unshared.
        selected_users = {other["NativeId"] for other in mappings if other["NativeHandling"] == item["NativeHandling"]}
        if set(uses) != selected_users and not (item["Name"] == "firetruk" and
                item["NativeId"] == 407 and item["NativeHandling"] == "FIRETRUK" and set(uses) == {407,544}):
            raise ValueError("Selected handling name is shared or ambiguous: " + item["NativeHandling"])
    merged = "".join(lines)
    if len(read_ide(merged)) != len(stage_rows):
        raise ValueError("Vehicle IDE model pool capacity changed")
    final_rows = read_ide(merged)
    for item in mappings:
        if final_rows[item["NativeId"]]["fields"] != fleet_rows[item["NativeId"]]["fields"]:
            raise ValueError("Selected IDE row did not merge exactly")
    return merged, selected


def merge_handling(stage_text, fleet_text, native_text, mappings):
    stage_rows = read_handling(stage_text)
    fleet_rows = read_handling(fleet_text)
    native_rows = read_handling(native_text)
    lines = stage_text.splitlines(keepends=True)
    selected = []
    for item in mappings:
        name = item["NativeHandling"]
        if name not in stage_rows or name not in fleet_rows or name not in native_rows:
            raise ValueError("Missing selected handling row: " + name)
        fields = fleet_rows[name]["fields"]
        if fields[0].upper() != name.upper() or len(fields) != 36:
            raise ValueError("Prepared handling row has an invalid SA shape: " + name)
        if len(stage_rows[name]["fields"]) != 36:
            raise ValueError("Stage handling row has an invalid SA shape: " + name)
        if stage_rows[name]["fields"] != native_rows[name]["fields"]:
            raise ValueError("New stage changed selected native handling before fleet mount: " + name)
        lines[stage_rows[name]["line"]] = _handling_replacement(lines[stage_rows[name]["line"]], fields)
        selected.append({"NativeHandling": name, "Fields": fields})
    merged = "".join(lines)
    # Class physics are separate records in handling.cfg. Mount them together
    # with the common row so a converted bike/boat cannot keep donor physics.
    def special_rows(text):
        result = {}
        for index, line in enumerate(text.splitlines(keepends=True)):
            fields = line.split()
            if len(fields) > 2 and fields[0] in {"!", "%", "$"}:
                key = tuple(fields[:2])
                if key in result:
                    raise ValueError("Duplicate specialized handling row: " + str(key))
                result[key] = (index, fields)
        return result
    stage_special = special_rows(stage_text)
    fleet_special = special_rows(fleet_text)
    native_special = special_rows(native_text)
    merged_lines = merged.splitlines(keepends=True)
    for item in mappings:
        for line in item.get("SpecialHandling", []):
            fields = line.split()
            key = tuple(fields[:2])
            if key not in stage_special or key not in native_special or key not in fleet_special:
                raise ValueError("Missing specialized handling consumer: " + str(key))
            if stage_special[key][1] != native_special[key][1] or fleet_special[key][1] != fields:
                raise ValueError("Specialized handling changed before mount: " + str(key))
            index = stage_special[key][0]
            merged_lines[index] = _handling_replacement(merged_lines[index], fields)
    merged = "".join(merged_lines)
    final_rows = read_handling(merged)
    for item in mappings:
        if final_rows[item["NativeHandling"]]["fields"] != fleet_rows[item["NativeHandling"]]["fields"]:
            raise ValueError("Selected handling row did not merge exactly: " + item["NativeHandling"])
    return merged, selected


def _normalize_relative(relative):
    value = str(relative).replace("\\", "/")
    if value.startswith("/") or re.match(r"^[A-Za-z]:", value):
        raise ValueError("Expected a relative path: " + value)
    parts = [part for part in value.split("/") if part not in ("", ".")]
    if any(part == ".." for part in parts):
        raise ValueError("Relative path leaves its root: " + value)
    return "/".join(parts)


def _asset_path(root, relative):
    path = Path(root).joinpath(*_normalize_relative(relative).split("/"))
    if not inside(root, path):
        raise ValueError("Fleet manifest path leaves the prepared output: " + str(relative))
    return path


def load_fleet(fleet):
    manifest_path = fleet / "manifest.json"
    manifest = read_json(manifest_path)
    if not manifest.get("Prepared") or manifest.get("Installed") or manifest.get("GameExecuted") or manifest.get("CampaignPlayable"):
        raise ValueError("Fleet must be prepared and verified offline, not installed or game-tested")
    vehicles = [row for row in manifest.get("Vehicles", []) if row.get("Prepared")]
    if not vehicles or manifest.get("PreparedCount") != len(vehicles) or len(vehicles) > manifest.get("VehicleDefinitions", 0):
        raise ValueError("Fleet manifest prepared count does not match its vehicle records")
    output_files = {}
    for record in manifest.get("OutputFiles", []):
        relative = _normalize_relative(record.get("RelativePath", ""))
        key = relative.casefold()
        if not relative or key in output_files:
            raise ValueError("Missing or duplicate output file record in fleet manifest")
        output_files[key] = record
    mappings = []
    source_mappings = [dict(row) for row in vehicles]
    for row in vehicles:
        required = ("LcsId", "Name", "NativeId", "NativeName", "NativeHandling")
        if any(row.get(key) in (None, "") for key in required):
            raise ValueError("Prepared fleet vehicle lacks a native binding")
        kind = row.get("PhysicalClass", "car")
        expected_special = {"bike": ("!", 17), "boat": ("%", 16), "heli": ("$", 23), "plane": ("$", 23)}.get(kind)
        if expected_special:
            special = row.get("SpecialHandling", [])
            fields = special[0].split() if len(special) == 1 else []
            if len(fields) != expected_special[1] or fields[:2] != [expected_special[0], row["NativeHandling"]]:
                raise ValueError("Prepared class vehicle lacks its complete specialized physics: " + row["Name"])
            if kind in {"heli", "plane"} and not 186 <= row.get("NativeHandlingId", -1) <= 209:
                raise ValueError("Prepared aircraft has no native flying-pointer identity: " + row["Name"])
        mappings.append({"LcsId": int(row["LcsId"]), "Name": str(row["Name"]).lower(),
                         "NativeId": int(row["NativeId"]), "NativeName": str(row["NativeName"]),
                         "NativeHandling": str(row["NativeHandling"]).upper(),
                         "PhysicalClass": row.get("PhysicalClass", "car"),
                         "SpecialHandling": row.get("SpecialHandling", []),
                         "NativeHandlingId": row.get("NativeHandlingId")})
    for key in ("LcsId", "NativeId", "NativeName"):
        values = [str(item[key]).casefold() for item in mappings]
        if len(set(values)) != len(values):
            raise ValueError("Prepared fleet has duplicate " + key + " bindings")
    for item, source in zip(mappings, vehicles):
        if item["LcsId"] == 192 or item["NativeId"] == 454 or item["NativeName"].casefold() == "tropic":
            if (item["LcsId"], item["NativeId"], item["NativeName"].casefold(), item["PhysicalClass"]) != (192, 454, "tropic", "boat") or not source.get("DedicatedFerryAdapter"):
                raise ValueError("Ferry TROPIC slot requires its dedicated door/ramp adapter")
    # Resolve the prepared model and the TXD dictionary selected by its final IDE row.
    fleet_ide_text = (fleet / "vehicles.ide").read_text(encoding="ascii")
    fleet_rows = read_ide(fleet_ide_text)
    final_assets = []
    for item in mappings:
        if item["NativeId"] not in fleet_rows:
            raise ValueError("Prepared fleet IDE is missing model ID " + str(item["NativeId"]))
        fields = fleet_rows[item["NativeId"]]["fields"]
        if fields[1].lower() != item["NativeName"].lower() or fields[4].upper() != item["NativeHandling"]:
            raise ValueError("Prepared fleet IDE disagrees with vehicle manifest")
        for relative, destination in (
            ("models/" + item["NativeName"] + ".dff", "vehicles/" + item["NativeName"] + ".dff"),
            ("models/" + fields[2] + ".txd", "vehicles/" + fields[2] + ".txd"),
        ):
            key = relative.casefold()
            record = output_files.get(key)
            if record is None:
                raise ValueError("Fleet output manifest does not pin selected model: " + relative)
            source = _asset_path(fleet, relative)
            expected = str(record.get("Sha256", "")).lower()
            if not expected or digest(source).lower() != expected:
                raise ValueError("Prepared fleet model hash mismatch: " + relative)
            final_assets.append({"source": source, "relative": destination,
                                 "sha256": expected, "bytes": source.stat().st_size})
    destinations = [asset["relative"].casefold() for asset in final_assets]
    if len(destinations) != 2 * len(mappings) or len(set(destinations)) != len(destinations):
        raise ValueError("Fleet must produce one distinct DFF/TXD destination pair per vehicle")
    if manifest.get("SharedTextures"):
        shared = manifest["SharedTextures"]
        relative = shared["PreparedPath"]
        destination = shared["TargetPath"]
        if (relative, destination) != ("models/vehicle.txd", "models/generic/vehicle.txd"):
            raise ValueError("Unexpected shared vehicle texture dictionary destination")
        record = output_files.get(relative.casefold())
        source = _asset_path(fleet, relative)
        if not record or digest(source).lower() != record["Sha256"].lower():
            raise ValueError("Shared vehicle texture dictionary hash mismatch")
        final_assets.append({"source": source, "relative": destination,
                             "sha256": record["Sha256"].lower(), "bytes": source.stat().st_size})
    if manifest.get("VehicleColours"):
        colours = manifest["VehicleColours"]
        relative, destination = colours["PreparedPath"], colours["TargetPath"]
        if (relative, destination) != ("vehicle-colours.dat", "data/lcs/vehicle-colours.dat"):
            raise ValueError("Unexpected vehicle colours consumer resource")
        source = _asset_path(fleet, relative)
        record = output_files.get(relative.casefold())
        if not record or digest(source).lower() != record["Sha256"].lower():
            raise ValueError("Vehicle colours resource hash mismatch")
        final_assets.append({"source": source, "relative": destination,
                             "sha256": record["Sha256"].lower(), "bytes": source.stat().st_size})
    if manifest.get("NativeColourLoader"):
        loader = manifest["NativeColourLoader"]
        if (loader["PreparedPath"], loader["TargetPath"]) != ("carcols.dat", "data/carcols.dat"):
            raise ValueError("Unexpected native colour loader resource")
        source = _asset_path(fleet, "carcols.dat")
        record = output_files.get("carcols.dat")
        if not record or digest(source).lower() != record["Sha256"].lower():
            raise ValueError("Native colour loader hash mismatch")
        final_assets.append({"source": source, "relative": "data/carcols.dat",
                             "sha256": record["Sha256"].lower(), "bytes": source.stat().st_size})
    return manifest, manifest_path, source_mappings, mappings, fleet_ide_text, final_assets


def update_hash_pins(campaign, hashes):
    """Refresh only metadata pins that name a file this mount intentionally edits."""
    for section in ("originalResources", "preservedApprovedResources"):
        rows = campaign.get(section)
        if not isinstance(rows, list):
            continue
        for row in rows:
            if not isinstance(row, dict):
                continue
            relative = _normalize_relative(row.get("relative", "")).casefold()
            if relative in hashes and "sha256" in row:
                row["sha256"] = hashes[relative]
    equivalent_hash = hashes["data/lcs/native-equivalents.tsv"]
    campaign["equivalentSha256"] = equivalent_hash


def atomic_write(root, relative, data):
    target = Path(root).joinpath(*_normalize_relative(relative).split("/"))
    if not inside(root, target):
        raise ValueError("Mount target leaves the new campaign stage: " + str(target))
    target.parent.mkdir(parents=True, exist_ok=True)
    if not inside(root, target):
        raise ValueError("Mount target parent leaves the new campaign stage: " + str(target))
    temporary = target.with_name("." + target.name + "." + uuid.uuid4().hex + ".fleet-tmp")
    if not inside(root, temporary):
        raise ValueError("Temporary mount path leaves the new campaign stage")
    try:
        with temporary.open("xb") as stream:
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, target)
    finally:
        if temporary.exists():
            temporary.unlink()


def check_other_overrides(game, basenames):
    modloader = game / "modloader"
    if not modloader.is_dir():
        return
    own_campaign = (modloader / "stories_bridge").resolve()
    conflicts = []
    for path in modloader.rglob("*"):
        if not path.is_file() or path.name.casefold() not in basenames:
            continue
        resolved = path.resolve()
        if resolved.is_relative_to(own_campaign):
            continue
        conflicts.append(str(path))
    if conflicts:
        raise ValueError("Another modloader mod overrides a selected fleet asset/config: " + "; ".join(conflicts[:20]))


def mount(fleet, package, game, replace_existing=False):
    fleet = Path(fleet).resolve()
    package = Path(package).resolve()
    game = Path(game).resolve()
    modloader = (game / "modloader").resolve()
    if package.is_relative_to(modloader) or fleet.is_relative_to(modloader):
        raise ValueError("Fleet preparation and mounting must stay outside the installed modloader directory")
    if fleet == package or fleet.is_relative_to(package) or package.is_relative_to(fleet):
        raise ValueError("Prepared fleet output and new campaign stage must be separate directories")
    if not package.is_dir() or not fleet.is_dir():
        raise ValueError("Prepared fleet and new campaign stage must already exist")

    manifest, manifest_path, source_mappings, mappings, fleet_ide_text, final_assets = load_fleet(fleet)
    campaign_path = package / "metadata/campaign.json"
    campaign = read_json(campaign_path)
    if int(campaign.get("version", 0)) <= 70 or (campaign.get("vehicleFleet") and not replace_existing):
        raise ValueError("Use a new, unmounted campaign stage created after v70")
    if (package / REPORT_PATH).exists() and not replace_existing:
        raise ValueError("Stage already contains campaign-vehicles metadata")
    if replace_existing:
        # Replacing an owned fleet is allowed only in a fully pinned package.
        # This also covers the separately mounted ferry's approved handling.
        pins = read_json(package / "metadata/package-files.json")
        pinned = {row["relative"].replace("\\", "/"): row for row in pins}
        required = [*CONFIG_PATHS, REPORT_PATH, "metadata/campaign.json"]
        for relative in required:
            row = pinned.get(relative)
            if not row or digest(package / relative).lower() != row["sha256"].lower():
                raise ValueError("Owned fleet upgrade input changed: " + relative)
        prior = read_json(package / REPORT_PATH)
        for row in prior["Files"]:
            if digest(package / row["relative"]).lower() != row["sha256"].lower():
                raise ValueError("Previous fleet asset changed: " + row["relative"])

    config_paths = {relative: package / relative for relative in CONFIG_PATHS}
    game_ide = game / "data/vehicles.ide"
    game_handling = game / "data/handling.cfg"
    for path in (*config_paths.values(), game_ide, game_handling, package / PROFILE_PATH):
        if not path.is_file():
            raise ValueError("Required staged/native config is missing: " + str(path))
    before = {relative: digest(path) for relative, path in config_paths.items()}
    before[PROFILE_PATH] = digest(package / PROFILE_PATH)
    if campaign.get("equivalentSha256") and campaign["equivalentSha256"].lower() != before["data/lcs/native-equivalents.tsv"].lower():
        raise ValueError("New stage native-equivalent hash pin does not match its source-binding file")

    source_manifest_sha = digest(manifest_path)
    stage_ide_text = config_paths["data/vehicles.ide"].read_text(encoding="ascii")
    stage_handling_text = config_paths["data/handling.cfg"].read_text(encoding="ascii")
    native_ide_text = game_ide.read_text(encoding="ascii")
    native_handling_text = game_handling.read_text(encoding="ascii")
    if replace_existing:
        names = {item["NativeHandling"] for item in mappings}
        def key(line):
            fields = line.strip().split()
            if not fields or fields[0].startswith(";"):
                return None
            return tuple(fields[:2]) if fields[0] in {"!", "%", "$", "^"} else (fields[0],)
        native_lines = {key(line): line for line in native_handling_text.splitlines(keepends=True) if key(line)}
        restored = []
        for line in stage_handling_text.splitlines(keepends=True):
            identity = key(line)
            if identity and identity[-1] in names:
                if identity not in native_lines:
                    raise ValueError("Owned handling has no native baseline: " + str(identity))
                line = native_lines[identity]
            restored.append(line)
        stage_handling_text = "".join(restored)
    merged_ide, selected_ide = merge_ide(stage_ide_text, fleet_ide_text, native_ide_text, mappings)
    merged_handling, selected_handling = merge_handling(stage_handling_text,
                                                        (fleet / "handling.cfg").read_text(encoding="ascii"),
                                                        native_handling_text, mappings)
    equivalents_text = config_paths["data/lcs/native-equivalents.tsv"].read_text(encoding="ascii")
    equivalence_lines = equivalents_text.splitlines(keepends=True)
    selected_by_id = {item["LcsId"]: item for item in mappings}
    equivalent_matches = {source_id: 0 for source_id in selected_by_id}
    changed_bindings = 0
    new_equivalent_lines = []
    for original in equivalence_lines:
        body = original.rstrip("\r\n")
        if not body or body.startswith("#"):
            new_equivalent_lines.append(original)
            continue
        fields = body.split("\t")
        if len(fields) != 5:
            raise ValueError("Malformed native-equivalent mapping row")
        try:
            source_id = int(fields[0])
        except ValueError as exc:
            raise ValueError("Invalid native-equivalent source model ID") from exc
        item = selected_by_id.get(source_id)
        if item is None:
            new_equivalent_lines.append(original)
            continue
        if fields[4] != "vehicle" or fields[2].lower() != item["Name"]:
            raise ValueError("Selected source vehicle binding changed: " + fields[2])
        equivalent_matches[source_id] += 1
        if int(fields[1]) != item["NativeId"] or fields[3].lower() != item["NativeName"].lower():
            changed_bindings += 1
        fields[1] = str(item["NativeId"])
        fields[3] = item["NativeName"]
        ending = _line_ending(original)
        new_equivalent_lines.append("\t".join(fields) + ending)
    if any(count != 1 for count in equivalent_matches.values()):
        missing = [str(source_id) for source_id, count in equivalent_matches.items() if count != 1]
        raise ValueError("Expected one vehicle source binding for each selected LCS model: " + ", ".join(missing))
    merged_equivalents = "".join(new_equivalent_lines)

    destinations = [asset["relative"] for asset in final_assets]
    check_names = {Path(name).name.casefold() for name in destinations}
    check_names.update(("vehicles.ide", "handling.cfg", "carcols.dat"))
    check_other_overrides(game, check_names)

    payloads = {
        "data/vehicles.ide": merged_ide.encode("ascii"),
        "data/handling.cfg": merged_handling.encode("ascii"),
        "data/lcs/native-equivalents.tsv": merged_equivalents.encode("ascii"),
    }
    if manifest.get("VehicleColours"):
        raw_ini = (package / "LcsSaBridge.ini").read_bytes()
        ini_encoding = "utf-16" if raw_ini.startswith((b"\xff\xfe", b"\xfe\xff")) else "utf-8-sig"
        ini = raw_ini.decode(ini_encoding)
        if not ini.startswith("[LCS]") or re.search(r"^\[(?!LCS\])", ini, re.M):
            raise ValueError("Vehicle colours require the single LCS configuration section")
        ini = re.sub(r"^VehicleColours=.*\r?\n?", "", ini, flags=re.M)
        payloads["LcsSaBridge.ini"] = (ini.rstrip()+"\nVehicleColours=1\n").encode(ini_encoding)
    for asset in final_assets:
        data = asset["source"].read_bytes()
        if digest_bytes(data).lower() != asset["sha256"].lower():
            raise ValueError("Prepared fleet model changed after hash validation: " + asset["relative"])
        payloads[asset["relative"]] = data
    for relative, data in payloads.items():
        if not inside(package, package / relative):
            raise ValueError("Mount destination leaves the new campaign stage: " + relative)

    for relative, data in payloads.items():
        atomic_write(package, relative, data)

    after = {relative: digest(package / relative) for relative in CONFIG_PATHS}
    after[PROFILE_PATH] = digest(package / PROFILE_PATH)
    if manifest.get("VehicleColours"):
        after["LcsSaBridge.ini"] = digest(package / "LcsSaBridge.ini")
    report_files = [
        {"relative": asset["relative"], "sha256": digest(package / asset["relative"]),
         "bytes": (package / asset["relative"]).stat().st_size}
        for asset in final_assets
    ]
    report = {
        "format": 1,
        "sourceManifest": str(manifest_path),
        "sourceManifestSha256": source_manifest_sha,
        "sourcePreparedCount": manifest["PreparedCount"],
        "vehicleCount": len(mappings),
        "gameExecuted": False,
        "installed": False,
        "VehicleMappings": source_mappings,
        "Files": report_files,
        "selectedIDErows": selected_ide,
        "selectedHandlingRows": selected_handling,
        "Configs": [
            {"relative": relative, "beforeSha256": before[relative], "afterSha256": after[relative]}
            for relative in CONFIG_PATHS
        ],
        "Profile": {"relative": PROFILE_PATH, "beforeSha256": before[PROFILE_PATH],
                    "afterSha256": after[PROFILE_PATH]},
        "sourceMappingsChanged": changed_bindings,
    }
    report_bytes = (json.dumps(report, indent=2) + "\n").encode("utf-8")
    atomic_write(package, REPORT_PATH, report_bytes)
    updated_hashes = {key.casefold(): value for key, value in after.items()}
    update_hash_pins(campaign, updated_hashes)
    campaign["vehicleFleet"] = {
        "manifest": REPORT_PATH,
        "sha256": digest_bytes(report_bytes),
        "count": len(mappings),
    }
    campaign["nativeStartupValidated"] = False
    campaign.pop("startupValidationSha256", None)
    atomic_write(package, "metadata/campaign.json", (json.dumps(campaign, indent=2) + "\n").encode("utf-8"))
    return {"mounted": True, "vehicles": len(mappings), "modelFiles": len(final_assets),
            "changedMappings": changed_bindings, "stage": str(package), "installed": False,
            "gameExecuted": False}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fleet", type=Path, required=True, help="Prepared campaign-vehicles output directory")
    parser.add_argument("--package", type=Path, required=True, help="A new campaign stage outside modloader")
    parser.add_argument("--game", type=Path, required=True, help="Read-only native SA reference root")
    parser.add_argument("--replace-existing", action="store_true", help="Upgrade a hash-verified owned fleet in an existing offline campaign stage")
    args = parser.parse_args()
    print(json.dumps(mount(args.fleet, args.package, args.game, args.replace_existing)))
