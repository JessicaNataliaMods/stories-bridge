"""Mount a user-converted native graph and enable stock SA population."""
import importlib.util
import json
import pathlib
import shutil
import struct

spec = importlib.util.spec_from_file_location('native_path_audit', pathlib.Path(__file__).with_name('validate-sa-paths.py'))
audit = importlib.util.module_from_spec(spec)
spec.loader.exec_module(audit)


def enable_population(code):
    code = bytearray(code)
    # Offsets are those of the independently validated map bootstrap. Change
    # only the two floats; player initialization, yields and loop stay intact.
    for start, opcode in ((651, 0x01eb), (658, 0x03de)):
        if len(code) < start + 7 or code[start:start + 3] != struct.pack('<HB', opcode, 6):
            raise ValueError('Bootstrap density operand changed; validate the new template')
        value = struct.unpack_from('<f', code, start + 3)[0]
        if value not in (0.0, 1.0):
            raise ValueError('Unexpected bootstrap density; refusing to replace it')
        struct.pack_into('<f', code, start + 3, 1.0)
    return bytes(code)


def mount_navigation(package, source):
    report = audit.validate(source / 'paths.img')
    conversion = json.loads((source / 'conversion.json').read_text())
    if not report['VehicleNodes'] or not report['PedNodes'] or report['PathsImgSha256'] != conversion['pathsImgSha256']:
        raise ValueError('Converted population graph missing or hash mismatch')
    shutil.copyfile(source / 'paths.img', package / 'models/lcs/paths.img')
    (package / 'metadata/navigation.json').write_text(json.dumps(dict(conversion=conversion, nativeValidation=report), indent=2), encoding='utf-8')
    script = package / 'data/script/main.scm'
    before = script.read_bytes()
    after = enable_population(before)
    script.write_bytes(after)
    changed = [i for i, (a, b) in enumerate(zip(before, after)) if a != b]
    return dict(graph=report, nativeVehicleDensity=1.0, nativePedestrianDensity=1.0,
                bootstrapChangedBytes=changed, stockSaPopulation=True, source='Original PS2 CPathFind',
                runtimeTrafficVerified=False, sourceCarLaneWidth=5.0, nativeCarLaneWidth=5.4)
