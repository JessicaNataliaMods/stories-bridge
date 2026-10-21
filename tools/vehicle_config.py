#!/usr/bin/env python3
"""Parse and convert vehicle color configuration without lossy palette reuse.

The normal SA vehicle color table has 128 entries.  This module can prepare an
extended carcols file and a complete LCS-index-to-SA-index map for a consumer
that has explicitly expanded that table; it does not install or activate that
consumer.  Existing SA palette indices are kept byte-for-byte at the front of
the table, and every LCS palette index maps to an exact RGB value.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable, Mapping, Sequence


RGB = tuple[int, int, int]
DEFAULT_LCS_CARCOLS = Path("out/lcs-converted/DATA/CARCOLS.DAT")
DEFAULT_FLEET_MANIFEST = Path("out/campaign-vehicles-20261020-r3/manifest.json")
SA_TABLE_CAPACITY = 128
BYTE_TABLE_CAPACITY = 256

_RGB_RE = re.compile(r"^\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*$")
# Retail SA carcols.dat contains this known typo.  The SA parser first tries
# "%d %d %d" and, under FIX_BUGS, falls back to "%d.%d %d" for this row.
_SA_RGB_TYPO_RE = re.compile(r"^\s*(\d+)\.(\d+)\s*,\s*(\d+)\s*$")
_MODEL_RE = re.compile(r"^\s*([^,\s]+)\s*,?\s*(.*?)\s*$")
_INDEX_RE = re.compile(r"(?<![A-Za-z0-9_])\d+(?![A-Za-z0-9_])")


class CarcolsError(ValueError):
    """Malformed carcols or fleet manifest input."""


class PaletteCapacityError(CarcolsError):
    """The exact RGB union does not fit the requested consumer capacity."""

    def __init__(self, report: "ConversionReport") -> None:
        self.report = report
        super().__init__(
            f"exact palette needs {report.required_palette_rows} entries "
            f"({report.sa_palette_rows} existing SA + "
            f"{len(report.full_palette_missing_rgb)} LCS-only RGBs), "
            f"but max_capacity is {report.max_capacity}; no output was emitted"
        )


@dataclass(frozen=True)
class ColorRow:
    index: int
    rgb: RGB
    line_index: int


@dataclass(frozen=True)
class VehicleColorRow:
    section: str
    model: str
    color_indices: tuple[int, ...]
    line_index: int


@dataclass(frozen=True)
class SectionSpan:
    section: str
    start_line: int
    end_line: int


@dataclass
class ParsedCarcols:
    text: str
    lines: list[str]
    colors: list[ColorRow]
    vehicle_rows: list[VehicleColorRow]
    sections: list[SectionSpan]

    def section(self, name: str) -> SectionSpan:
        matches = [span for span in self.sections if span.section == name]
        if len(matches) != 1:
            raise CarcolsError(
                f"expected exactly one {name!r} section, found {len(matches)}"
            )
        return matches[0]


@dataclass(frozen=True)
class FleetModel:
    lcs_name: str
    sa_name: str


@dataclass
class ConversionReport:
    lcs_palette_rows: int
    lcs_palette_distinct_rgb: int
    sa_palette_rows: int
    sa_palette_distinct_rgb: int
    target_models: int
    target_models_with_lcs_rows: int
    target_models_without_lcs_rows: list[str]
    target_mapping_distinct_rgb: int
    target_mapping_missing_rgb: list[RGB]
    full_palette_missing_rgb: list[RGB]
    required_palette_rows: int
    max_capacity: int
    fits_capacity: bool
    extended_consumer_required: bool
    lcs_to_sa_colour_map: dict[int, int]
    added_palette_rows: list[dict[str, Any]]

    def as_dict(self) -> dict[str, Any]:
        return {
            "LcsPaletteRows": self.lcs_palette_rows,
            "LcsPaletteDistinctRGB": self.lcs_palette_distinct_rgb,
            "SaPaletteRows": self.sa_palette_rows,
            "SaPaletteDistinctRGB": self.sa_palette_distinct_rgb,
            "TargetModels": self.target_models,
            "TargetModelsWithLcsRows": self.target_models_with_lcs_rows,
            "TargetModelsWithoutLcsRows": self.target_models_without_lcs_rows,
            "TargetMappingDistinctRGB": self.target_mapping_distinct_rgb,
            "TargetMappingMissingRGB": [list(rgb) for rgb in self.target_mapping_missing_rgb],
            "FullPaletteMissingRGB": [list(rgb) for rgb in self.full_palette_missing_rgb],
            "RequiredPaletteRows": self.required_palette_rows,
            "MaxCapacity": self.max_capacity,
            "FitsCapacity": self.fits_capacity,
            "ExtendedConsumerRequired": self.extended_consumer_required,
            "LcsToSaColourMap": {
                str(index): sa_index
                for index, sa_index in sorted(self.lcs_to_sa_colour_map.items())
            },
            "AddedPaletteRows": self.added_palette_rows,
        }


@dataclass(frozen=True)
class CarcolsConversion:
    carcols_text: str
    lcs_to_sa_colour_map: dict[int, int]
    report: ConversionReport


def _line_body(line: str) -> str:
    return line.rstrip("\r\n")


def _line_ending(line: str) -> str:
    if line.endswith("\r\n"):
        return "\r\n"
    if line.endswith("\n"):
        return "\n"
    if line.endswith("\r"):
        return "\r"
    return ""


def _parse_rgb(line: str, *, allow_sa_typo: bool, source: str, line_number: int) -> RGB:
    content = line.split("#", 1)[0]
    match = _RGB_RE.fullmatch(content)
    if match is None and allow_sa_typo:
        match = _SA_RGB_TYPO_RE.fullmatch(content)
    if match is None:
        raise CarcolsError(f"{source}:{line_number}: invalid RGB row {line.strip()!r}")
    rgb = tuple(int(part) for part in match.groups())
    if any(channel > 255 for channel in rgb):
        raise CarcolsError(f"{source}:{line_number}: RGB channel outside 0..255: {rgb}")
    return rgb  # type: ignore[return-value]


def parse_carcols(
    text: str,
    *,
    source: str = "<memory>",
    allow_sa_typo: bool = False,
    validate_color_indices: bool = True,
) -> ParsedCarcols:
    """Parse the SA/LCS `col`, `car`, and `car4` sections, retaining raw lines."""
    lines = text.splitlines(keepends=True)
    if not lines and text:
        lines = [text]

    colors: list[ColorRow] = []
    vehicle_rows: list[VehicleColorRow] = []
    sections: list[SectionSpan] = []
    active_section: str | None = None
    active_start = -1

    for line_index, raw_line in enumerate(lines):
        body = _line_body(raw_line)
        content = body.split("#", 1)[0].strip()
        if not content:
            continue
        lower = content.lower()

        if lower in {"col", "car", "car4"}:
            if active_section is not None:
                raise CarcolsError(
                    f"{source}:{line_index + 1}: section {active_section!r} was not closed by end"
                )
            active_section = lower
            active_start = line_index
            continue

        if lower == "end":
            if active_section is not None:
                sections.append(SectionSpan(active_section, active_start, line_index))
                active_section = None
                active_start = -1
            continue

        if active_section == "col":
            colors.append(
                ColorRow(
                    index=len(colors),
                    rgb=_parse_rgb(body, allow_sa_typo=allow_sa_typo, source=source, line_number=line_index + 1),
                    line_index=line_index,
                )
            )
        elif active_section in {"car", "car4"}:
            uncommented = body.split("#", 1)[0]
            match = _MODEL_RE.fullmatch(uncommented)
            if match is None:
                raise CarcolsError(f"{source}:{line_index + 1}: invalid vehicle color row {body!r}")
            model, tail = match.groups()
            indices = tuple(int(token.group()) for token in _INDEX_RE.finditer(tail))
            width = 2 if active_section == "car" else 4
            if not indices or len(indices) % width:
                raise CarcolsError(
                    f"{source}:{line_index + 1}: {active_section} row for {model!r} "
                    f"must contain a nonzero multiple of {width} color indices"
                )
            vehicle_rows.append(
                VehicleColorRow(active_section, model.casefold(), indices, line_index)
            )

    if active_section is not None:
        raise CarcolsError(f"{source}: section {active_section!r} was not closed by end")
    if not colors:
        raise CarcolsError(f"{source}: no colors found in a col section")

    for row in vehicle_rows:
        for index in row.color_indices:
            if validate_color_indices and index >= len(colors):
                raise CarcolsError(
                    f"{source}:{row.line_index + 1}: {row.model!r} references color {index}, "
                    f"but the col section has only {len(colors)} rows"
                )

    return ParsedCarcols(text, lines, colors, vehicle_rows, sections)


def _load_manifest(manifest: Mapping[str, Any] | str | Path) -> Mapping[str, Any]:
    if isinstance(manifest, Mapping):
        return manifest
    path = Path(manifest)
    try:
        return json.loads(path.read_text(encoding="utf-8-sig"))
    except (OSError, json.JSONDecodeError) as exc:
        raise CarcolsError(f"cannot read fleet manifest {path}: {exc}") from exc


def _fleet_models(manifest: Mapping[str, Any] | str | Path) -> list[FleetModel]:
    document = _load_manifest(manifest)
    vehicles = document.get("Vehicles")
    if not isinstance(vehicles, list):
        raise CarcolsError("fleet manifest must contain a Vehicles array")

    result: list[FleetModel] = []
    seen_lcs: set[str] = set()
    seen_sa: set[str] = set()
    for item in vehicles:
        if not isinstance(item, Mapping) or not item.get("Prepared"):
            continue
        lcs_name = item.get("Name")
        sa_name = item.get("NativeName")
        if not isinstance(lcs_name, str) or not lcs_name.strip():
            raise CarcolsError("prepared fleet entry is missing Name")
        if not isinstance(sa_name, str) or not sa_name.strip():
            raise CarcolsError(f"prepared fleet entry {lcs_name!r} is missing NativeName")
        lcs_key, sa_key = lcs_name.casefold(), sa_name.casefold()
        if lcs_key in seen_lcs:
            raise CarcolsError(f"duplicate prepared LCS vehicle {lcs_name!r}")
        if sa_key in seen_sa:
            raise CarcolsError(f"multiple prepared LCS vehicles target SA model {sa_name!r}")
        seen_lcs.add(lcs_key)
        seen_sa.add(sa_key)
        result.append(FleetModel(lcs_name, sa_name))

    if not result:
        raise CarcolsError("fleet manifest has no prepared vehicles")
    return result


def _rgb_index(colors: Sequence[ColorRow]) -> dict[RGB, int]:
    result: dict[RGB, int] = {}
    for row in colors:
        result.setdefault(row.rgb, row.index)
    return result


def _row_by_model(rows: Iterable[VehicleColorRow]) -> dict[str, VehicleColorRow]:
    """Return the last row for each model, matching SA's sequential parser."""
    result: dict[str, VehicleColorRow] = {}
    for row in rows:
        if row.model not in result or row.line_index > result[row.model].line_index:
            result[row.model] = row
    return result


def plan_carcols_conversion(
    lcs_carcols_text: str,
    sa_carcols_text: str,
    fleet_manifest: Mapping[str, Any] | str | Path,
    *,
    max_capacity: int = SA_TABLE_CAPACITY,
) -> tuple[ParsedCarcols, ParsedCarcols, list[FleetModel], ConversionReport]:
    """Compute an exact append-only palette map and its capacity report."""
    if max_capacity not in {SA_TABLE_CAPACITY, BYTE_TABLE_CAPACITY}:
        raise CarcolsError(
            f"max_capacity must be {SA_TABLE_CAPACITY} for retail SA or "
            f"{BYTE_TABLE_CAPACITY} for an explicitly extended consumer"
        )

    lcs = parse_carcols(lcs_carcols_text, source="LCS CARCOLS.DAT")
    sa = parse_carcols(
        sa_carcols_text,
        source="SA carcols.dat",
        allow_sa_typo=True,
        validate_color_indices=False,
    )
    fleet = _fleet_models(fleet_manifest)
    if len(sa.colors) > BYTE_TABLE_CAPACITY:
        raise CarcolsError(
            f"SA palette has {len(sa.colors)} rows, exceeding the byte-index limit "
            f"{BYTE_TABLE_CAPACITY}"
        )

    lcs_rows = _row_by_model(lcs.vehicle_rows)
    sa_rgb_to_index = _rgb_index(sa.colors)
    final_rgb_to_index = dict(sa_rgb_to_index)
    full_palette_missing: list[RGB] = []
    added_palette_rows: list[dict[str, Any]] = []

    for color in lcs.colors:
        if color.rgb in final_rgb_to_index:
            continue
        sa_index = len(sa.colors) + len(added_palette_rows)
        final_rgb_to_index[color.rgb] = sa_index
        full_palette_missing.append(color.rgb)
        added_palette_rows.append(
            {
                "Index": sa_index,
                "RGB": list(color.rgb),
                "LcsPaletteIndices": [
                    other.index for other in lcs.colors if other.rgb == color.rgb
                ],
            }
        )

    lcs_to_sa = {row.index: final_rgb_to_index[row.rgb] for row in lcs.colors}

    target_rgb: set[RGB] = set()
    target_models_with_rows = 0
    target_models_without_rows: list[str] = []
    for model in fleet:
        row = lcs_rows.get(model.lcs_name.casefold())
        if row is None:
            target_models_without_rows.append(model.lcs_name)
            continue
        target_models_with_rows += 1
        target_rgb.update(lcs.colors[index].rgb for index in row.color_indices)

    target_missing = sorted(target_rgb - set(sa_rgb_to_index))
    all_missing_sorted = sorted(set(full_palette_missing))
    required_rows = len(sa.colors) + len(full_palette_missing)
    report = ConversionReport(
        lcs_palette_rows=len(lcs.colors),
        lcs_palette_distinct_rgb=len({row.rgb for row in lcs.colors}),
        sa_palette_rows=len(sa.colors),
        sa_palette_distinct_rgb=len({row.rgb for row in sa.colors}),
        target_models=len(fleet),
        target_models_with_lcs_rows=target_models_with_rows,
        target_models_without_lcs_rows=target_models_without_rows,
        target_mapping_distinct_rgb=len(target_rgb),
        target_mapping_missing_rgb=target_missing,
        full_palette_missing_rgb=all_missing_sorted,
        required_palette_rows=required_rows,
        max_capacity=max_capacity,
        fits_capacity=required_rows <= max_capacity,
        extended_consumer_required=required_rows > SA_TABLE_CAPACITY,
        lcs_to_sa_colour_map=lcs_to_sa,
        added_palette_rows=added_palette_rows,
    )
    return lcs, sa, fleet, report


def _format_mapping_line(model_name: str, indices: Sequence[int], newline: str) -> str:
    return f"{model_name}, " + ", ".join(str(index) for index in indices) + newline


def _build_output_carcols(
    lcs: ParsedCarcols,
    sa: ParsedCarcols,
    fleet: Sequence[FleetModel],
    report: ConversionReport,
) -> str:
    color_map = report.lcs_to_sa_colour_map
    lcs_rows = _row_by_model(lcs.vehicle_rows)
    sa_target_names = {model.sa_name.casefold() for model in fleet}

    edits: dict[int, str] = {}
    removed: set[int] = set()
    insertions: dict[int, list[str]] = {}
    mapping_spans = {name: sa.section(name) for name in ("car", "car4")}
    newline_by_section: dict[str, str] = {}
    for name, span in mapping_spans.items():
        newline_by_section[name] = _line_ending(sa.lines[span.end_line]) or "\n"

    sa_rows_by_model: dict[str, list[VehicleColorRow]] = {}
    for row in sa.vehicle_rows:
        if row.model in sa_target_names:
            sa_rows_by_model.setdefault(row.model, []).append(row)

    for model in fleet:
        native_key = model.sa_name.casefold()
        source_row = lcs_rows.get(model.lcs_name.casefold())
        if source_row is None:
            # LCS models without CARCOLS entries use CCar's default color 0/0.
            output_section = "car"
            output_indices = (color_map[0], color_map[0])
        else:
            output_section = source_row.section
            output_indices = tuple(color_map[index] for index in source_row.color_indices)

        old_rows = sa_rows_by_model.get(native_key, [])
        matching = [row for row in old_rows if row.section == output_section]
        if matching:
            first = matching[0]
            line = _format_mapping_line(model.sa_name, output_indices, _line_ending(sa.lines[first.line_index]))
            edits[first.line_index] = line
            for duplicate in old_rows:
                if duplicate.line_index != first.line_index:
                    removed.add(duplicate.line_index)
        else:
            removed.update(row.line_index for row in old_rows)
            span = mapping_spans[output_section]
            insertions.setdefault(span.end_line, []).append(
                _format_mapping_line(model.sa_name, output_indices, newline_by_section[output_section])
            )

    color_section = sa.section("col")
    if report.added_palette_rows:
        newline = _line_ending(sa.lines[color_section.end_line]) or "\n"
        for item in report.added_palette_rows:
            rgb = item["RGB"]
            indices = ",".join(str(index) for index in item["LcsPaletteIndices"])
            insertions.setdefault(color_section.end_line, []).append(
                f"{rgb[0]},{rgb[1]},{rgb[2]}\t# LCS palette indices {indices}{newline}"
            )

    output: list[str] = []
    for line_index, raw_line in enumerate(sa.lines):
        output.extend(insertions.get(line_index, ()))
        if line_index in removed:
            continue
        output.append(edits.get(line_index, raw_line))
    return "".join(output)


def convert_carcols(
    lcs_carcols_text: str,
    sa_carcols_text: str,
    fleet_manifest: Mapping[str, Any] | str | Path,
    *,
    max_capacity: int = SA_TABLE_CAPACITY,
) -> CarcolsConversion:
    """Return converted carcols text and a complete LCS palette index map.

    The 127-entry SA palette remains an unchanged prefix.  New exact LCS RGBs
    are appended and require a runtime consumer whose table capacity is at
    least ``max_capacity``; this function never installs or patches that
    consumer.
    """
    lcs, sa, fleet, report = plan_carcols_conversion(
        lcs_carcols_text,
        sa_carcols_text,
        fleet_manifest,
        max_capacity=max_capacity,
    )
    if not report.fits_capacity:
        raise PaletteCapacityError(report)
    output = _build_output_carcols(lcs, sa, fleet, report)
    return CarcolsConversion(output, dict(report.lcs_to_sa_colour_map), report)


def _read_carcols(path: Path, *, allow_sa_typo: bool = False) -> str:
    try:
        text = path.read_text(encoding="cp1252")
    except OSError as exc:
        raise CarcolsError(f"cannot read {path}: {exc}") from exc
    # Parse here to surface source-aware diagnostics before analysis.
    parse_carcols(
        text,
        source=str(path),
        allow_sa_typo=allow_sa_typo,
        validate_color_indices=not allow_sa_typo,
    )
    return text


def _main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--lcs-carcols", type=Path, default=DEFAULT_LCS_CARCOLS)
    parser.add_argument("--sa-carcols", type=Path, required=True)
    parser.add_argument("--fleet-manifest", type=Path, default=DEFAULT_FLEET_MANIFEST)
    parser.add_argument("--max-capacity", type=int, default=SA_TABLE_CAPACITY)
    parser.add_argument("--output", type=Path, help="write converted carcols only when explicitly requested")
    parser.add_argument("--map-output", type=Path, help="write the LCS index map as JSON")
    args = parser.parse_args(argv)

    try:
        lcs_text = _read_carcols(args.lcs_carcols)
        sa_text = _read_carcols(args.sa_carcols, allow_sa_typo=True)
        manifest = _load_manifest(args.fleet_manifest)
        lcs, sa, fleet, report = plan_carcols_conversion(
            lcs_text, sa_text, manifest, max_capacity=args.max_capacity
        )
        print(json.dumps(report.as_dict(), indent=2, sort_keys=True))
        if args.output is not None or args.map_output is not None:
            converted = convert_carcols(
                lcs_text, sa_text, manifest, max_capacity=args.max_capacity
            )
            if args.output is not None:
                args.output.parent.mkdir(parents=True, exist_ok=True)
                with args.output.open("w", encoding="cp1252", newline="") as handle:
                    handle.write(converted.carcols_text)
            if args.map_output is not None:
                args.map_output.parent.mkdir(parents=True, exist_ok=True)
                payload = {
                    "LcsToSaColourMap": {
                        str(index): sa_index
                        for index, sa_index in sorted(converted.lcs_to_sa_colour_map.items())
                    },
                    "AddedPaletteRows": report.added_palette_rows,
                }
                args.map_output.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")
        return 0 if report.fits_capacity else 2
    except (CarcolsError, OSError) as exc:
        print(f"vehicle_config: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(_main())
