"""Field-by-field LCS vehicle configuration adapters for San Andreas.

The mappings here follow the parsers and data consumers in:

* ``../res-relcs/src/core/FileLoader.cpp`` (14-field LCS ``cars`` rows)
* ``../res-relcs/src/vehicles/HandlingMgr.cpp`` and ``HandlingMgr.h``
* ``vendor/sa-reference/source_game_sa_FileLoader.cpp`` (15-field SA rows)
* ``vendor/sa-reference/source_game_sa_cHandlingDataMgr.cpp`` and the SA
  ``eVehicleHandlingModelFlags`` / ``eVehicleHandlingFlags`` enums.

No source value is taken from a donor SA vehicle. Values that SA requires but
LCS does not express are explicit adapters with a reason in the returned
provenance. Unmapped active LCS handling bits are returned to the caller so it
can reject that vehicle rather than silently assigning a different meaning.
"""

from __future__ import annotations

import math
from typing import Dict, List, Optional, Sequence, Tuple


# LCS HandlingMgr.h: HANDLING_* bits -> SA's separate modelFlags word.
# Numeric values on the right are from vendor/sa-reference/
# source_game_sa_Enums_eVehicleHandlingModelFlags.h.
MODEL_FLAG_ADAPTERS = {
    0x00000004: (0x00000010, "reverse bonnet"),
    0x00000008: (0x00000020, "hanging boot"),
    0x00000010: (0x00000100, "no doors"),
    0x00000020: (0x00000001, "van body"),
    0x00000040: (0x00000002, "bus body"),
    0x00000080: (0x00000004, "low body"),
    0x00000100: (0x00002000, "double exhaust"),
    0x00000200: (0x00000040, "tailgate boot"),
    0x00000400: (0x00000080, "non-swinging boot"),
    0x00002000: (0x00000800, "no-roof/convertible behavior"),
    0x00004000: (0x00000008, "large body"),
    0x00010000: (0x01000000, "bike physical class"),
    0x00020000: (0x02000000, "helicopter physical class"),
    0x00040000: (0x04000000, "plane physical class"),
    0x00080000: (0x08000000, "boat physical class"),
    0x00100000: (0x00001000, "no exhaust"),
    0x00800000: (0x00000400, "sit-in-boat layout"),
    0x10000000: (0x40000000, "force ground clearance"),
}

# LCS HandlingMgr.h: HANDLING_* bits -> SA's separate handlingFlags word.
# SA flag values are from vendor/sa-reference/source_game_sa_Enums_eVehicleHandlingFlags.h.
HANDLING_FLAG_ADAPTERS = {
    0x00000001: (0x00000001, "1G boost"),
    0x00000002: (0x00000002, "2G boost"),
    0x00000800: (0x00000004, "NPC anti-roll"),
    0x00001000: (0x00000008, "NPC neutral handling"),
    0x00008000: (0x00400000, "halogen lights"),
    0x00200000: (0x00800000, "process rear wheel first"),
    0x01000000: (0x00004000, "wide rear wheel"),
    0x02000000: (0x00000200, "narrow front wheel"),
    0x04000000: (0x00100000, "off-road ability"),
}

# LCS HandlingMgr.h defines HANDBRAKE_TYRE, but its only handling effect in
# Automobile.cpp is commented out. It therefore has no active LCS behavior to
# port and no direct SA flag counterpart.
INERT_LCS_FLAGS = {
    0x00400000: "HANDBRAKE_TYRE: the only LCS behavior is commented out in Automobile.cpp",
}

# HANDLING_UNKNOWN (0x08000000) has an active LCS Renderer.cpp consumer: it
# culls first-person vehicles while the camera looks behind. SA exposes the
# matching VEHICLE_HANDLING_MODEL_NO1FPS_LOOK_BEHIND model flag (0x4000). Map
# only when an explicit LCS heli/plane class bit is also present; otherwise
# preserve the bit as unmapped. The target bit differs in both value and word.

LCS_HANDLING_FIELD_NAMES = (
    "handling_name", "mass", "turn_mass", "drag_multiplier", "center_of_mass_x",
    "center_of_mass_y", "center_of_mass_z", "percent_submerged", "traction_multiplier",
    "traction_loss", "traction_bias", "gear_count", "maximum_velocity",
    "engine_acceleration", "engine_inertia", "drive_type", "engine_type",
    "brake_deceleration", "brake_bias", "abs", "steering_lock", "suspension_force",
    "suspension_damping", "high_speed_compression_damping", "suspension_upper_limit",
    "suspension_lower_limit", "suspension_bias", "suspension_anti_dive",
    "seat_offset", "collision_damage_multiplier", "monetary_value", "combined_flags",
    "front_lights", "rear_lights",
)

SA_HANDLING_FIELD_NAMES = (
    "handling_name", "mass", "turn_mass", "drag_multiplier", "center_of_mass_x",
    "center_of_mass_y", "center_of_mass_z", "percent_submerged", "traction_multiplier",
    "traction_loss", "traction_bias", "gear_count", "maximum_velocity",
    "engine_acceleration", "engine_inertia", "drive_type", "engine_type",
    "brake_deceleration", "brake_bias", "abs", "steering_lock", "suspension_force",
    "suspension_damping", "high_speed_compression_damping", "suspension_upper_limit",
    "suspension_lower_limit", "suspension_bias", "suspension_anti_dive",
    "seat_offset", "collision_damage_multiplier", "monetary_value", "model_flags",
    "handling_flags", "front_lights", "rear_lights", "animation_group",
)

LCS_IDE_FIELD_NAMES = (
    "model_id", "model_name", "texture_dictionary", "vehicle_type", "handling_name",
    "game_name", "animation_file", "vehicle_class", "frequency", "level",
    "component_rules", "wheel_model_id", "wheel_scale", "normal_splay",
)

SA_IDE_FIELD_NAMES = (
    "model_id", "model_name", "texture_dictionary", "vehicle_type", "handling_name",
    "game_name", "animation_file", "vehicle_class", "frequency", "model_flags",
    "component_rules", "wheel_model_id", "wheel_size_front", "wheel_size_rear",
    "wheel_upgrade_class",
)

SA_VEHICLE_CLASSES = {
    "normal", "poorfamily", "richfamily", "executive", "worker", "big", "taxi",
    "moped", "motorbike", "leisureboat", "workerboat", "bicycle", "ignore",
}

# LCS HandlingMgr.h body/path flags -> the SA vehicle association groups written
# as the final field of handling.cfg. These IDs are the SA AssocGroupId values
# consumed by tHandlingData::m_nAnimGroup, not donor-model animation settings.
# Their names are the SA categories used in Vehicle.cpp; IDs are confirmed by
# native SA handling rows (low sports cars=1, LINERUN=2, BUS=16; standard=0).
SA_ANIMATION_GROUP_ADAPTERS = {
    0x00000080: (1, "ANIM_GROUP_LOWCARAMIMS", "low car"),
    0x00000020: (2, "ANIM_GROUP_TRKCARANIMS", "truck"),
    0x00000040: (16, "ANIM_GROUP_BUSCARANIMS", "bus"),
}
SA_BIKE_ANIMATION_GROUP_ADAPTERS = {
    # LCS vehicles.ide animation-file tokens resolve to SA handling animation
    # groups with matching bike classes; these IDs are the native SA bike
    # groups, rather than the replaced model's handling value.
    "bikes": (4, "BIKE"),
    "bikev": (5, "MOPED"),
    "bikeh": (6, "FREEWAY"),
    "biked": (7, "DIRTBIKE"),
}
SA_STANDARD_ANIMATION_GROUP = (0, "ANIM_GROUP_STDCARAMIMS", "standard car")
SA_TRAIN_ANIMATION_GROUP = (0, "ANIM_GROUP_STDCARAMIMS")

LCS_PHYSICAL_CLASS_FLAGS = {
    0x00010000: "bike",
    0x00020000: "heli",
    0x00040000: "plane",
    0x00080000: "boat",
}
SA_VEHICLE_TYPES = {"car", "bike", "boat", "heli", "plane", "train"}
LCS_BIKE_HANDLING_NAMES = (
    "BIKE", "MOPED", "DIRTBIKE", "ANGEL", "DIRTBIK2", "ANGE2", "FREEWAY",
)
LCS_BOAT_HANDLING_NAMES = ("PREDATOR", "SPEEDER", "REEFER")
LCS_FLYING_HANDLING_NAMES = (
    "MAVERICK", "COASTMAV", "POLMAV", "HUNTER", "RCGOBLIN", "RCCOPTER",
)
# vendor/sa-reference/source_game_sa_cHandlingDataMgr.cpp:
# GetFlyingPointer accepts only native handling IDs 186..209.
SA_FLYING_HANDLING_ID_RANGE = (186, 209)


def _field_record(index: int, name: str, value: str, source_index: Optional[int],
                  source_name: str, rule: str) -> Dict[str, object]:
    return {
        "target_index": index,
        "target_field": name,
        "value": value,
        "source_index": source_index,
        "source_field": source_name,
        "rule": rule,
    }


def _adapt_flags(flags: int) -> Tuple[int, int, int, List[Dict[str, object]], List[Dict[str, object]]]:
    model_flags = 0
    handling_flags = 0
    mapped_input = 0
    trace: List[Dict[str, object]] = []
    ignored: List[Dict[str, object]] = []

    for source_bit, (target_bit, meaning) in MODEL_FLAG_ADAPTERS.items():
        if flags & source_bit:
            model_flags |= target_bit
            mapped_input |= source_bit
            trace.append({
                "source_bit": "0x%08X" % source_bit,
                "target_word": "model_flags",
                "target_bit": "0x%08X" % target_bit,
                "meaning": meaning,
                "rule": "semantic bit mapping from LCS HandlingMgr.h to SA model flags",
            })
    for source_bit, (target_bit, meaning) in HANDLING_FLAG_ADAPTERS.items():
        if flags & source_bit:
            handling_flags |= target_bit
            mapped_input |= source_bit
            trace.append({
                "source_bit": "0x%08X" % source_bit,
                "target_word": "handling_flags",
                "target_bit": "0x%08X" % target_bit,
                "meaning": meaning,
                "rule": "semantic bit mapping from LCS HandlingMgr.h to SA handling flags",
            })
    for source_bit, reason in INERT_LCS_FLAGS.items():
        if flags & source_bit:
            mapped_input |= source_bit
            ignored.append({"source_bit": "0x%08X" % source_bit, "reason": reason})

    unknown_aircraft_view = 0x08000000
    aircraft_class_bits = 0x00020000 | 0x00040000
    if flags & unknown_aircraft_view and flags & aircraft_class_bits:
        sa_no_1fps_look_behind = 0x00004000
        model_flags |= sa_no_1fps_look_behind
        mapped_input |= unknown_aircraft_view
        trace.append({
            "source_bit": "0x%08X" % unknown_aircraft_view,
            "target_word": "model_flags",
            "target_bit": "0x%08X" % sa_no_1fps_look_behind,
            "meaning": "first-person look-back aircraft culling",
            "adaptation": "native-system adaptation",
            "rule": (
                "map to SA VEHICLE_HANDLING_MODEL_NO1FPS_LOOK_BEHIND (0x4000), "
                "the native model flag for the same first-person look-back behavior. "
                "Apply only with a proven LCS heli/plane class bit; LCS Renderer.cpp "
                "checks this only for aircraft."
            ),
        })

    unmapped = flags & ~mapped_input
    return model_flags, handling_flags, unmapped, trace, ignored


def derive_sa_animation_group(ide_fields: Sequence[str],
                             lcs_handling_flags: int,
                             physical_class: Optional[str] = None) -> Tuple[str, str, Dict[str, object]]:
    """Derive SA's animation group from LCS's actual door-path handling flags.

    LCS has no animation-group token: ``PedAI.cpp`` selects standard, low,
    van, or coach/bus entry and door associations from ``HANDLING_IS_LOW``,
    ``HANDLING_IS_VAN`` and ``HANDLING_IS_BUS``. SA stores an association-group
    ID in handling field 35. The adapter maps those LCS behaviors to SA's low,
    truck (the closest SA group to LCS van), and bus groups, respectively.
    Ordinary LCS cars use SA's standard car group. It never borrows the group
    from the SA model slot being replaced.
    """
    if len(ide_fields) < len(LCS_IDE_FIELD_NAMES):
        raise ValueError("LCS vehicles.ide row must have 14 fields")
    vehicle_type = (physical_class or ide_fields[3]).strip().lower()
    if physical_class is None:
        flagged_classes = [
            vehicle_class for bit, vehicle_class in LCS_PHYSICAL_CLASS_FLAGS.items()
            if lcs_handling_flags & bit
        ]
        if len(flagged_classes) > 1:
            raise ValueError("Conflicting LCS physical-class handling flags: " + ", ".join(flagged_classes))
        if flagged_classes:
            vehicle_type = flagged_classes[0]
    if vehicle_type == "ferry":
        # The source ferry has script-positioned boarding markers, not a car
        # entry/door animation path. Its moving ramps are consumed separately
        # by the ferry service. SA still requires this handling token; use the
        # neutral standard group, never a donor vehicle's configuration.
        return "0", "SA-required standard animation group for the source ferry; scripted boarding and ramp transforms are separate consumers", {
            "source_bit": None,
            "source_meaning": "source ferry with scripted boarding markers",
            "target_group": "standard",
            "target_group_id": 0,
        }
    if vehicle_type == "train":
        if ide_fields[3].strip().lower() != "train":
            raise ValueError("LCS train animation group requires an original train IDE type")
        group_id, group_name = SA_TRAIN_ANIMATION_GROUP
        return str(group_id), (
            "adapt the LCS native train path to SA's native train vehicle type; "
            "SA handling.cfg still requires common animation_group ID 0, and no "
            "LCS car door-path group applies"
        ), {
            "source_bit": None,
            "source_meaning": "LCS vehicles.ide type train selects the native train path",
            "source_index": 3,
            "source_field": LCS_IDE_FIELD_NAMES[3],
            "target_group": group_name,
            "target_group_id": group_id,
            "adaptation": "native-system adaptation",
        }
    if vehicle_type != "car":
        if vehicle_type not in SA_VEHICLE_TYPES - {"car"}:
            raise ValueError("Unsupported LCS vehicle type for SA animation-group adapter: " + vehicle_type)
        if vehicle_type == "bike":
            animation_file = ide_fields[6].strip().lower()
            if animation_file not in SA_BIKE_ANIMATION_GROUP_ADAPTERS:
                raise ValueError(
                    "No source-backed SA handling animation group for LCS bike animation file: "
                    + animation_file
                )
            group_id, group_name = SA_BIKE_ANIMATION_GROUP_ADAPTERS[animation_file]
            return str(group_id), (
                "derive the SA-only animation_group from LCS vehicles.ide animation_file "
                "%s; SA group %s is native bike group ID %d" %
                (animation_file, group_name, group_id)
            ), {
                "source_bit": None,
                "source_meaning": "LCS bike animation_file " + animation_file,
                "source_index": 6,
                "source_field": "animation_file",
                "target_group": group_name,
                "target_group_id": group_id,
            }
        # SA reads this word from every common handling row. The LCS door-path
        # flags below are car-specific; vehicle-type animation handling stays
        # with the bike/boat/aircraft/train consumers selected by vehicles.ide.
        group_id, group_name, _ = SA_STANDARD_ANIMATION_GROUP
        return str(group_id), (
            "SA-required standard animation_group token for the %s vehicle "
            "type; no LCS car low/van/bus door-path flag applies" % vehicle_type
        ), {
            "source_bit": None,
            "source_meaning": "LCS type-specific vehicle path; no car door-path flag applies",
            "target_group": group_name,
            "target_group_id": group_id,
        }

    matched = [
        (source_bit, adapter)
        for source_bit, adapter in SA_ANIMATION_GROUP_ADAPTERS.items()
        if lcs_handling_flags & source_bit
    ]
    if len(matched) > 1:
        names = ", ".join(adapter[2] for _, adapter in matched)
        raise ValueError(
            "Conflicting LCS animation-path flags (%s); one SA animation group "
            "cannot preserve multiple LCS paths" % names
        )

    if matched:
        source_bit, (group_id, group_name, semantic) = matched[0]
        return str(group_id), (
            "derive the SA-only animation_group from the LCS combined_flags "
            "bit 0x%08X (%s path); SA group %s is ID %d. SA's "
            "CVehicle::ProcessOpenDoor consumes m_nAnimGroup as AssocGroupId; "
            "the ID matches native SA handling data" %
            (source_bit, semantic, group_name, group_id)
        ), {
            "source_bit": "0x%08X" % source_bit,
            "source_meaning": semantic,
            "target_group": group_name,
            "target_group_id": group_id,
        }

    group_id, group_name, semantic = SA_STANDARD_ANIMATION_GROUP
    return str(group_id), (
        "derive the SA-only animation_group from absence of LCS low/van/bus "
        "path flags; use %s ID %d for the ordinary LCS car path. SA's "
        "CVehicle::ProcessOpenDoor consumes m_nAnimGroup as AssocGroupId; "
        "the ID matches native SA handling data" %
        (group_name, group_id)
    ), {
        "source_bit": None,
        "source_meaning": "ordinary car path (no LCS low/van/bus bit set)",
        "target_group": group_name,
        "target_group_id": group_id,
    }


def convert_handling(lcs_fields: Sequence[str], native_name: str,
                     ide_fields: Sequence[str],
                     physical_class: Optional[str] = None) -> Dict[str, object]:
    """Convert one 34-token LCS handling row into SA's 36-token row.

    Returns ``fields``, one provenance entry per output field, mapped flag-bit
    details, ignored inert bits, and ``unmapped_flags``. The caller must reject
    a non-zero ``unmapped_flags`` value before preparing the vehicle.
    """
    fields = [str(value).strip() for value in lcs_fields]
    if len(fields) != 34:
        raise ValueError("LCS handling row must have 34 fields")
    if not native_name or any(ch.isspace() for ch in native_name):
        raise ValueError("SA handling name must be a non-empty token")
    if len(ide_fields) < len(LCS_IDE_FIELD_NAMES):
        raise ValueError("LCS vehicles.ide row must have 14 fields")
    if fields[0].casefold() != ide_fields[4].strip().casefold():
        raise ValueError("LCS IDE handling name does not match the handling row")

    for index, value in enumerate(fields[1:31], 1):
        if index in (15, 16):
            if value.upper() not in {"F", "R", "4", "P", "D", "E"}:
                raise ValueError("Invalid LCS transmission type at field %d" % index)
        else:
            try:
                number = float(value)
            except ValueError as error:
                raise ValueError("Non-numeric LCS handling field %d" % index) from error
            if not math.isfinite(number):
                raise ValueError("Nonfinite LCS handling field %d" % index)

    try:
        lcs_flags = int(fields[31], 16)
    except ValueError as error:
        raise ValueError("Invalid hexadecimal LCS handling flags") from error
    if lcs_flags < 0 or lcs_flags > 0xFFFFFFFF:
        raise ValueError("LCS handling flags are outside uint32 range")

    model_flags, handling_flags, unmapped, flag_trace, ignored_flags = _adapt_flags(lcs_flags)
    class_bits = [
        vehicle_class for bit, vehicle_class in LCS_PHYSICAL_CLASS_FLAGS.items()
        if lcs_flags & bit
    ]
    if len(class_bits) > 1:
        raise ValueError("Conflicting LCS physical-class handling flags: " + ", ".join(class_bits))
    if physical_class is not None:
        physical_class = physical_class.strip().lower()
        if physical_class not in SA_VEHICLE_TYPES | {"ferry"}:
            raise ValueError("Unsupported LCS physical class: " + physical_class)
        if class_bits and physical_class != "ferry" and class_bits[0] != physical_class:
            raise ValueError("LCS handling physical-class flag does not match requested class")
    animation_group, animation_reason, animation_adapter = derive_sa_animation_group(
        ide_fields, lcs_flags, physical_class
    )

    output = fields[:31] + ["%X" % model_flags, "%X" % handling_flags]
    output += fields[32:34] + [animation_group]
    output[0] = native_name
    # LCS HandlingMgr.cpp leaves switch case 23 unassigned; SA consumes it as
    # high-speed compression damping. Zero preserves LCS behavior instead of
    # adding a new damping effect from an unrelated donor row.
    output[23] = "0.0"
    if len(output) != 36:
        raise AssertionError("Internal SA handling layout must have 36 fields")

    provenance: List[Dict[str, object]] = []
    provenance.append(_field_record(
        0, SA_HANDLING_FIELD_NAMES[0], native_name, 0,
        LCS_HANDLING_FIELD_NAMES[0],
        "use the explicitly assigned target handling key for this native model binding",
    ))
    for index in range(1, 31):
        if index == 23:
            provenance.append({
                "target_index": index,
                "target_field": SA_HANDLING_FIELD_NAMES[index],
                "value": "0.0",
                "source_index": index,
                "source_field": LCS_HANDLING_FIELD_NAMES[index],
                "source_value": fields[index],
                "rule": "LCS HandlingMgr.cpp comments case 23 out as unused; SA consumes this field, so zero preserves LCS behavior without adding damping",
            })
        else:
            provenance.append(_field_record(
                index, SA_HANDLING_FIELD_NAMES[index], fields[index], index,
                LCS_HANDLING_FIELD_NAMES[index],
                "same parsed data field in both handling loaders; retain the original LCS token",
            ))

    model_hex = "%X" % model_flags
    handling_hex = "%X" % handling_flags
    provenance.extend([
        {
            "target_index": 31,
            "target_field": "model_flags",
            "value": model_hex,
            "source_index": 31,
            "source_field": "combined_flags",
            "source_value": fields[31],
            "rule": "translate each supported LCS feature bit into the SA modelFlags word",
        },
        {
            "target_index": 32,
            "target_field": "handling_flags",
            "value": handling_hex,
            "source_index": 31,
            "source_field": "combined_flags",
            "source_value": fields[31],
            "rule": "translate each supported LCS feature bit into the SA handlingFlags word",
        },
        _field_record(33, "front_lights", fields[32], 32,
                      LCS_HANDLING_FIELD_NAMES[32],
                      "same enumerated light-size field in both loaders"),
        _field_record(34, "rear_lights", fields[33], 33,
                      LCS_HANDLING_FIELD_NAMES[33],
                      "same enumerated light-size field in both loaders"),
        {
            "target_index": 35,
            "target_field": "animation_group",
            "value": animation_group,
            "source_index": animation_adapter.get("source_index", 31),
            "source_field": animation_adapter.get("source_field", LCS_HANDLING_FIELD_NAMES[31]),
            "source_value": (
                ide_fields[animation_adapter["source_index"]].strip()
                if animation_adapter.get("source_index") is not None
                and animation_adapter["source_index"] < len(ide_fields)
                else fields[31]
            ),
            "animation_group_adapter": animation_adapter,
            "rule": animation_reason,
        },
    ])

    return {
        "fields": output,
        "provenance": provenance,
        "flag_adapters": flag_trace,
        "animation_group_adapter": animation_adapter,
        "ignored_flags": ignored_flags,
        "unmapped_flags": unmapped,
    }


def _handling_special_values(source_text: str, marker: str,
                             source_handling: str) -> List[str]:
    """Find one original ``!``, ``%`` or ``$`` row by handling key."""
    if len(marker) != 1 or marker not in "!%$":
        raise ValueError("Unsupported special handling marker")
    wanted = source_handling.casefold()
    matches: List[List[str]] = []
    for raw_line in source_text.splitlines():
        line = raw_line.strip()
        if not line or line.startswith(";") or not line.startswith(marker):
            continue
        values = line[1:].split()
        if values and values[0].casefold() == wanted:
            matches.append(values)
    if len(matches) != 1:
        if not matches:
            raise ValueError("Original LCS %s handling row is missing for %s" % (marker, source_handling))
        raise ValueError("Duplicate original LCS %s handling rows for %s" % (marker, source_handling))
    return matches[0]


def _special_rows(source_text: str, marker: str) -> List[List[str]]:
    """Read all rows of one class from the original config, in loader order."""
    if len(marker) != 1 or marker not in "!%$":
        raise ValueError("Unsupported special handling marker")
    rows: List[List[str]] = []
    for raw_line in source_text.splitlines():
        line = raw_line.strip()
        if not line or line.startswith(";") or not line.startswith(marker):
            continue
        fields = line[1:].split()
        if len(fields) < 2:
            raise ValueError("Malformed original LCS %s handling row" % marker)
        rows.append(fields)
    return rows


def _effective_flying_values(source_text: str, source_handling: str) -> List[str]:
    """Resolve LCS flying pointer slots, including its non-flight slot-0 fallback.

    HandlingMgr::GetFlyingPointer maps IDs outside the six-entry flying range
    to FlyingHandlingData[0]. Rows load sequentially, so the last row mapped to
    the selected slot is effective.
    """
    wanted = source_handling.upper()
    source_common_keys = {
        fields[0].upper()
        for raw_line in source_text.splitlines()
        for line in [raw_line.strip()]
        if line and not line.startswith((";", "!", "$", "%", "^"))
        for fields in [line.split()]
        if fields
    }
    if wanted not in source_common_keys:
        raise ValueError(
            "Original LCS common handling row is missing for flying source key %s"
            % source_handling
        )
    if wanted in LCS_FLYING_HANDLING_NAMES:
        target_slot = LCS_FLYING_HANDLING_NAMES.index(wanted)
    else:
        target_slot = 0

    effective: Optional[List[str]] = None
    for fields in _special_rows(source_text, "$"):
        source_key = fields[0].upper()
        source_slot = (
            LCS_FLYING_HANDLING_NAMES.index(source_key)
            if source_key in LCS_FLYING_HANDLING_NAMES else 0
        )
        if source_slot == target_slot:
            effective = fields
    if effective is None:
        raise ValueError(
            "Original LCS flying pointer has no initialized $ row for %s" % source_handling
        )
    return effective


def _validate_special_numbers(values: Sequence[str], label: str) -> None:
    for index, value in enumerate(values, 1):
        try:
            number = float(value)
        except ValueError as error:
            raise ValueError("Non-numeric %s field %d" % (label, index)) from error
        if not math.isfinite(number):
            raise ValueError("Nonfinite %s field %d" % (label, index))


def convert_special_handling(source_text: str, source_handling: str,
                             native_handling: str,
                             physical_class: str, *,
                             native_handling_id: Optional[int] = None) -> List[str]:
    """Convert class-specific LCS rows consumed by SA's handling loader.

    Returns ready-to-write SA ``handling.cfg`` lines. Bike fields have the same
    order in both loaders. Boat fields are reordered to SA's loader layout.
    LCS flying rows omit three SA-consumed values (gear-up ratio, gear-down
    lean, and wind multiplier); zero disables these SA-only influences because
    LCS has no gear-lean or weather-wind handling consumer. Cars and trains have
    no class-specific row and return an empty list. Aircraft output requires
    the target common handling ID so an out-of-range name cannot silently write
    SA's shared flying-slot-0 fallback.
    """
    vehicle_class = physical_class.strip().lower()
    if not source_handling or any(ch.isspace() for ch in source_handling):
        raise ValueError("LCS handling name must be a non-empty token")
    if not native_handling or any(ch.isspace() for ch in native_handling):
        raise ValueError("SA handling name must be a non-empty token")
    if vehicle_class in {"car", "train", "ferry"}:
        return []
    if vehicle_class == "bike":
        if source_handling.upper() not in LCS_BIKE_HANDLING_NAMES:
            raise ValueError(
                "LCS bike special handling key is outside the bike pointer range: "
                + source_handling
            )
        fields = _handling_special_values(source_text, "!", source_handling)
        if len(fields) != 16:
            raise ValueError("LCS bike handling row must contain a key and 15 values")
        _validate_special_numbers(fields[1:], "LCS bike handling")
        return ["! " + native_handling + " " + " ".join(fields[1:])]
    if vehicle_class == "boat":
        if source_handling.upper() not in LCS_BOAT_HANDLING_NAMES:
            raise ValueError(
                "LCS boat special handling key is outside the boat pointer range: "
                + source_handling
            )
        fields = _handling_special_values(source_text, "%", source_handling)
        if len(fields) != 15:
            raise ValueError("LCS boat handling row must contain a key and 14 values")
        _validate_special_numbers(fields[1:], "LCS boat handling")
        # LCS places camera height before the move/turn resistance vectors;
        # SA stores it after those vectors.
        sa_values = fields[1:8] + fields[9:15] + fields[8:9]
        return ["% " + native_handling + " " + " ".join(sa_values)]
    if vehicle_class in {"heli", "plane"}:
        if native_handling_id is None:
            raise ValueError(
                "SA flying special handling requires the target common handling ID "
                "to prove it is inside the native flying-pointer range"
            )
        if not SA_FLYING_HANDLING_ID_RANGE[0] <= native_handling_id <= SA_FLYING_HANDLING_ID_RANGE[1]:
            raise ValueError(
                "SA target handling ID is outside GetFlyingPointer's native range 186..209"
            )
        fields = _effective_flying_values(source_text, source_handling)
        if len(fields) != 19:
            raise ValueError("LCS flying handling row must contain a key and 18 values")
        _validate_special_numbers(fields[1:], "LCS flying handling")
        # SA inserts gear-up ratio, gear-down lean, and weather-wind multiplier
        # after attack lift. LCS ProcessFlying has no gear-lean or wind-drag
        # handling consumer; explicit zeroes preserve that absence of behavior.
        sa_values = fields[1:12] + ["0.0", "0.0", "0.0"] + fields[12:]
        return ["$ " + native_handling + " " + " ".join(sa_values)]
    raise ValueError("Unsupported LCS physical class for special handling: " + vehicle_class)


def convert_ide(lcs_fields: Sequence[str], *, model_id: int, model_name: str,
                texture_dictionary: str, handling_name: str,
                wheel_model_asset: Optional[str] = None,
                physical_class: Optional[str] = None,
                lcs_handling_flags: Optional[int] = None) -> Dict[str, object]:
    """Adapt an LCS vehicle row to SA's native vehicles.ide layout.

    The optional physical_class selects the native SA vehicle type, while the
    source IDE type controls the meaning of original LCS misc/wheel fields.
    This distinction matters for LCS Dodo and aircraft whose IDE type is car:
    their misc is a wheel ID even though the matching plane/helicopter handling
    flag selects SA's physical type. Cross-type car adaptation therefore
    requires the original handling flags and accepts only the matching plane
    or helicopter class bit.
    Converted car wheel geometry is embedded and receives misc -1; bikes
    preserve steering angle and scale. Positive source plane LOD model IDs
    are rejected because SA consumes the corresponding misc token as a wheel
    model ID, with no proven equivalent for the original LOD reference. SA's
    native train type accepts the common vehicle row and ignores wheel-only
    values.
    """
    source = [str(value).strip() for value in lcs_fields]
    if len(source) < len(LCS_IDE_FIELD_NAMES):
        raise ValueError("LCS vehicles.ide row must have 14 fields")
    source_type = source[3].lower()
    vehicle_class = (physical_class or source_type).strip().lower()
    if source_type not in {"car", "bike", "boat", "train", "heli", "plane", "ferry"}:
        raise ValueError("Unsupported LCS vehicles.ide source type: " + source_type)
    if vehicle_class not in SA_VEHICLE_TYPES:
        raise ValueError("Unsupported LCS vehicle class for SA vehicles.ide: " + vehicle_class)
    if source_type != vehicle_class:
        if source_type != "car" or vehicle_class not in {"plane", "heli"}:
            raise ValueError(
                "LCS IDE type %s does not match requested physical class %s"
                % (source_type, vehicle_class)
            )
        if lcs_handling_flags is None:
            raise ValueError(
                "LCS car IDE to %s adaptation requires original handling flags"
                % vehicle_class
            )
        if not isinstance(lcs_handling_flags, int) or not 0 <= lcs_handling_flags <= 0xFFFFFFFF:
            raise ValueError("LCS handling flags are outside uint32 range")
        flagged_classes = [
            source_class for bit, source_class in LCS_PHYSICAL_CLASS_FLAGS.items()
            if lcs_handling_flags & bit
        ]
        if len(flagged_classes) != 1 or flagged_classes[0] != vehicle_class:
            raise ValueError(
                "LCS car IDE physical class does not match its handling flags"
            )
    if source[7].lower() not in SA_VEHICLE_CLASSES:
        raise ValueError("LCS vehicle class is not accepted by the SA FileLoader: " + source[7])
    if not model_name or not texture_dictionary or not handling_name:
        raise ValueError("SA model, TXD, and handling binding names must be provided")
    for label, value in (("model", model_name), ("TXD", texture_dictionary), ("handling", handling_name)):
        if any(ch.isspace() for ch in value):
            raise ValueError("SA %s name must be one token" % label)

    wheel_scale: Optional[float] = None
    if source_type in {"car", "bike"}:
        try:
            wheel_scale = float(source[12])
        except ValueError as error:
            raise ValueError("Invalid LCS wheel scale") from error
        if not math.isfinite(wheel_scale) or wheel_scale <= 0.0 or wheel_scale > 4.0:
            raise ValueError("LCS wheel scale is outside the converter's supported range")
    if source_type == "car" and not wheel_model_asset:
        raise ValueError("Converted original LCS car wheel asset must be identified")
    try:
        frequency = int(source[8], 10)
        component_rules = int(source[10], 16)
    except ValueError as error:
        raise ValueError("Invalid LCS frequency or component-rules field") from error
    if not 0 <= frequency <= 0xFFFF:
        raise ValueError("LCS vehicle frequency is outside SA's uint16 field")
    if not 0 <= component_rules <= 0xFFFFFFFF:
        raise ValueError("LCS component rules are outside uint32 range")

    misc_value = "-1"
    front_wheel_size = rear_wheel_size = "0.7"
    misc_field = "wheel_model_id"
    misc_source_field = LCS_IDE_FIELD_NAMES[11]
    misc_rule = "LCS does not consume misc for this type; use the no-wheel-model default"
    wheel_rule = "use the SA FileLoader's explicit 0.7 wheel-size defaults for a source type with no wheel-scale consumer"
    if source_type == "car":
        misc_rule = "source car wheel geometry is converted and embedded by --sa-car; -1 prevents a second standalone wheel model"
        misc_field = "wheel_model_id"
        front_wheel_size = rear_wheel_size = source[12]
        wheel_rule = "map LCS's single wheel scale to the SA wheel size"
    elif source_type == "bike":
        if vehicle_class != "bike":
            raise ValueError("LCS bike steering semantics require the SA bike IDE type")
        try:
            int(source[11], 10)
        except ValueError as error:
            raise ValueError("Invalid LCS bike steering angle/misc field") from error
        misc_value = source[11]
        misc_field = "bike_steer_angle"
        misc_rule = "the LCS and SA vehicle loaders both consume misc as the bike steering angle"
        front_wheel_size = rear_wheel_size = source[12]
        wheel_rule = "map LCS's single wheel scale to the SA wheel size"
    elif source_type == "plane":
        try:
            plane_lod_id = int(source[11], 10)
        except ValueError as error:
            raise ValueError("Invalid LCS plane LOD model ID") from error
        if plane_lod_id < -1:
            raise ValueError("LCS plane LOD model ID must be -1 or nonnegative")
        if plane_lod_id >= 0:
            raise ValueError(
                "LCS plane LOD model ID has no proven equivalent in SA's vehicles.ide misc field, "
                "which SA consumes as a wheel model ID"
            )
        misc_source_field = "plane_lod_model_id"
        misc_rule = (
            "LCS -1 means no external plane LOD; retain SA wheel_model_id -1 "
            "instead of binding an unrelated wheel model"
        )
        front_wheel_size = rear_wheel_size = "1.0"
        wheel_rule = "LCS FileLoader fixes plane wheel scale at 1.0; it does not consume the source scale token"
    elif source_type == "train":
        misc_field = "unused_optional_misc"
        misc_rule = (
            "LCS train loader does not consume misc; SA's native train branch "
            "accepts the optional token but does not assign it as a wheel model"
        )

    output = [
        str(int(model_id)), model_name, texture_dictionary, vehicle_class, handling_name,
        source[5], source[6], source[7], source[8], "0", source[10], misc_value,
        front_wheel_size, rear_wheel_size, "-1",
    ]
    vehicle_type_rule = (
        "retain the original LCS train class as SA's native VEHICLE_TYPE_TRAIN; "
        "the SA loader accepts the common vehicle row and selects its native train renderer"
        if vehicle_class == "train" else
        "select the native SA vehicle type from the physical class; retain source IDE type separately when interpreting original misc/wheel fields"
    )
    provenance: List[Dict[str, object]] = [
        _field_record(0, "model_id", output[0], None, "binding", "assigned unique native SA model slot"),
        _field_record(1, "model_name", model_name, None, "converted_model", "name of converted original LCS DFF"),
        _field_record(2, "texture_dictionary", texture_dictionary, None, "converted_texture_dictionary", "name of merged original LCS TXD"),
        _field_record(3, "vehicle_type", vehicle_class, 3, LCS_IDE_FIELD_NAMES[3], vehicle_type_rule),
        _field_record(4, "handling_name", handling_name, 4, LCS_IDE_FIELD_NAMES[4], "use the explicitly assigned target handling key for this native model binding"),
        _field_record(5, "game_name", source[5], 5, LCS_IDE_FIELD_NAMES[5], "retain the original LCS display label"),
        _field_record(6, "animation_file", source[6], 6, LCS_IDE_FIELD_NAMES[6], "retain the original LCS animation-file token"),
        _field_record(7, "vehicle_class", source[7], 7, LCS_IDE_FIELD_NAMES[7], "retain the LCS population class accepted by SA FileLoader"),
        _field_record(8, "frequency", source[8], 8, LCS_IDE_FIELD_NAMES[8], "retain original LCS spawn frequency"),
        {
            "target_index": 9, "target_field": "model_flags", "value": "0",
            "source_index": None, "source_field": None,
            "rule": "SA-only uint8 model flags have no LCS IDE counterpart; use the empty flag set instead of inheriting the replaced SA model's flags",
        },
        _field_record(10, "component_rules", source[10], 10, LCS_IDE_FIELD_NAMES[10], "retain original LCS component-rule bitfield"),
        {
            "target_index": 11, "target_field": misc_field, "value": misc_value,
            "source_index": 11, "source_field": misc_source_field,
            "source_value": source[11], "derived_asset": wheel_model_asset,
            "rule": misc_rule,
        },
        _field_record(12, "wheel_size_front", front_wheel_size, 12 if source_type in {"car", "bike"} else None, LCS_IDE_FIELD_NAMES[12] if source_type in {"car", "bike"} else "SA FileLoader default", (
            "parsed as an optional token but ignored by SA's native train branch"
            if source_type == "train" else wheel_rule
        )),
        _field_record(13, "wheel_size_rear", rear_wheel_size, 12 if source_type in {"car", "bike"} else None, LCS_IDE_FIELD_NAMES[12] if source_type in {"car", "bike"} else "SA FileLoader default", (
            "parsed as an optional token but ignored by SA's native train branch"
            if source_type == "train" else wheel_rule
        )),
        {
            "target_index": 14, "target_field": "wheel_upgrade_class", "value": "-1",
            "source_index": None, "source_field": None,
            "rule": "LCS has no SA wheel-upgrade class; -1 disables SA-only wheel-mod lookup for this converted fleet model",
        },
    ]
    # Fields without a target counterpart remain explicit for the caller.
    unmapped = [
        {
            "source_index": 9, "source_field": "level", "value": source[9],
            "reason": "LCS Loader stores this value in CVehicleModelInfo::m_level; SA vehicles.ide has model_flags here, which have different semantics.",
        },
        {
            "source_index": 13, "source_field": "normal_splay", "value": source[13],
            "reason": "LCS stores normalSplay on CVehicleModelInfo, but the selected LCS vehicle-model source has no consumer for it and SA vehicles.ide has no matching field.",
        },
    ]
    if source_type not in {"car", "bike"}:
        unmapped.append({
            "source_index": 12, "source_field": "wheel_scale", "value": source[12],
            "reason": "The LCS loader does not assign this token to the source IDE type; SA-only wheel-size fields use the documented loader default or LCS plane constant.",
        })
    if source_type not in {"car", "bike", "plane"}:
        unmapped.append({
            "source_index": 11, "source_field": "misc", "value": source[11],
            "reason": (
                "The LCS train loader does not consume misc; SA's native train path does not use the optional wheel-model fields."
                if source_type == "train" else
                "The LCS loader does not consume misc for this type; SA receives -1 so it cannot attach a donor wheel model."
            ),
        })
    return {
        "fields": output,
        "provenance": provenance,
        "unmapped_source_fields": unmapped,
        "required_special_handling": vehicle_class in {"bike", "boat", "heli", "plane"},
    }
