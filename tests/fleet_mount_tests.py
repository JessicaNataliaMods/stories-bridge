"""Directed checks for class-physics mounting using authored input records."""
import importlib.util
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("fleet_mount", ROOT / "tools/mount-campaign-vehicles.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

native_common = "TEST " + " ".join(["1"] * 35) + "\n"
fleet_common = "TEST " + " ".join(["2"] * 35) + "\n"
untouched = "! KEEP " + " ".join(["3"] * 15) + "\n"
native_special = "! TEST " + " ".join(["4"] * 15) + "\n"
converted_special = "! TEST " + " ".join(["5"] * 15)
native = native_common + native_special + untouched
fleet = fleet_common + converted_special + "\n" + untouched
mapping = [{"NativeHandling": "TEST", "SpecialHandling": [converted_special]}]
merged, _ = module.merge_handling(native, fleet, native, mapping)
assert [line.split() for line in merged.splitlines()] == [line.split() for line in fleet.splitlines()]
assert untouched in merged

try:
    module.merge_handling(native, fleet.replace("! TEST", "! MISSING"), native, mapping)
except ValueError:
    pass
else:
    raise AssertionError("missing specialized physics must fail before mounting")

try:
    module.merge_handling(native.replace("4 4", "6 6", 1), fleet, native, mapping)
except ValueError:
    pass
else:
    raise AssertionError("changed specialized physics must not be silently replaced")

rows = module.read_ide("cars\n700, test testtxd, bike, TEST, LABEL, bikes, motorbike, 1, 0, 0, 16, 0.6, 0.6, -1\nend\n")
assert rows[700]["fields"][1:5] == ["test", "testtxd", "bike", "TEST"]
print("PASS: common and specialized physics mounted together; unrelated row preserved; missing/changed physics refused; mixed IDE delimiters")
