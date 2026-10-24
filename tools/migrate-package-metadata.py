"""Remove machine paths from an existing package's JSONs, with a small rollback.

Only JSONs change. Asset bytes, the ASI, startup approval and gameplay settings
are preserved. Hash observations referring to changed JSONs are refreshed.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import stat
import tempfile

from package_metadata import portable_value


def digest(data):
    return hashlib.sha256(data).hexdigest()


def check_path(path):
    for entry in (path, *path.parents):
        if entry.exists() and (entry.is_symlink() or entry.is_junction() or
                getattr(entry.lstat(), 'st_file_attributes', 0) & stat.FILE_ATTRIBUTE_REPARSE_POINT):
            raise ValueError('Reparse points are not allowed in migration paths')


def inventory(root):
    files = {}
    for directory, subdirs, names in os.walk(root, followlinks=False):
        for name in subdirs + names:
            check_path(Path(directory) / name)
        for name in names:
            path = Path(directory) / name
            info = path.stat()
            files[path.relative_to(root).as_posix()] = (info.st_size, info.st_mtime_ns, info.st_ino)
    return files


def encode(value):
    return (json.dumps(value, indent=2, ensure_ascii=True) + '\n').encode('utf-8')


def build_plan(root):
    check_path(root)
    root = root.resolve(strict=True)
    if not (root / 'metadata/campaign.json').is_file():
        raise ValueError('Choose the package directory containing metadata/campaign.json')
    before = inventory(root)
    originals = {name: (root / name).read_bytes() for name in before if name.lower().endswith('.json')}
    original_values = {name: json.loads(data.decode('utf-8-sig')) for name, data in originals.items()}
    values = {name: portable_value(value, root) for name, value in original_values.items()}
    payloads = dict(originals)

    def reference(value):
        if not isinstance(value, str):
            return None
        path = PurePosixPath(value.replace('\\', '/'))
        if path.is_absolute() or '..' in path.parts:
            return None
        return path.as_posix()

    def refresh(value, context=''):
        if isinstance(value, dict):
            if 'sourceManifestSha256' in value and 'sourceManifest' not in value:
                value.pop('sourceManifestSha256')
            for field in ('relative', 'Relative', 'manifest', 'path', 'block'):
                target = reference(value.get(field))
                if target in payloads:
                    for hash_key in ('sha256', 'Sha256', 'SHA256'):
                        if hash_key in value:
                            value[hash_key] = digest(payloads[target])
                    for size_key in ('bytes', 'Bytes'):
                        if size_key in value:
                            value[size_key] = len(payloads[target])
            source = reference(value.get('sourceManifest'))
            if source in payloads and 'sourceManifestSha256' in value:
                value['sourceManifestSha256'] = digest(payloads[source])
            if context == 'animationStorage' and 'sha256' in value and 'data/lcs/animations/storage.json' in payloads:
                value['sha256'] = digest(payloads['data/lcs/animations/storage.json'])
            if 'startupValidationSha256' in value and 'metadata/campaign-startup-validation.json' in payloads:
                value['startupValidationSha256'] = digest(payloads['metadata/campaign-startup-validation.json'])
            for key, item in list(value.items()):
                target = reference(key)
                if target in payloads and isinstance(item, str) and len(item) == 64:
                    value[key] = digest(payloads[target])
                else:
                    refresh(item, key)
        elif isinstance(value, list):
            for item in value:
                refresh(item, context)

    for _ in range(len(values) + 2):
        previous = dict(payloads)
        for name, value in values.items():
            # Historical test input hashes describe the bytes tested then.
            # Keep that evidence instead of pretending the tests ran again.
            if name != 'metadata/campaign-startup-validation.json':
                refresh(value)
            payloads[name] = originals[name] if value == original_values[name] else encode(value)
        if previous == payloads:
            break
    else:
        raise ValueError('Cyclic metadata fingerprints; no changes applied')
    for value in values.values():
        if portable_value(value, root) != value:
            raise ValueError('Absolute path remains after migration; no changes applied')
    if values['metadata/campaign.json'].get('nativeStartupValidated') != original_values['metadata/campaign.json'].get('nativeStartupValidated'):
        raise ValueError('Migration must not alter startup approval')
    for name, old in original_values.items():
        if isinstance(old, dict) and 'nativeModel' in old:
            if old['nativeModel'] != values[name].get('nativeModel'):
                raise ValueError('Migration must not alter runtime model selection')
    changed = {name: data for name, data in payloads.items() if data != originals[name]}
    return root, before, originals, changed


def replace(path, data):
    with tempfile.NamedTemporaryFile(dir=path.parent, delete=False) as stream:
        temporary = Path(stream.name)
        stream.write(data)
    try:
        temporary.replace(path)
    finally:
        temporary.unlink(missing_ok=True)


def migrate(package, backup, apply=False):
    root, before, originals, changed = build_plan(Path(package))
    summary = dict(applied=False, jsonFilesChecked=len(originals), changedFiles=sorted(changed),
                   changedBytes=sum(len(data) for data in changed.values()), assetsChanged=False,
                   startupApprovalChanged=False, gameExecuted=False)
    if not apply or not changed:
        return summary
    backup = Path(backup).absolute()
    check_path(backup)
    if backup.exists() or backup.is_relative_to(root) or root.is_relative_to(backup):
        raise ValueError('Choose a new rollback directory outside the package')
    if inventory(root) != before:
        raise ValueError('Package changed during migration planning')
    for name in changed:
        if (root / name).read_bytes() != originals[name]:
            raise ValueError('JSON changed during migration planning: ' + name)
    backup.mkdir(parents=True)
    for name in changed:
        saved = backup / name
        saved.parent.mkdir(parents=True, exist_ok=True)
        saved.write_bytes(originals[name])
        if saved.read_bytes() != originals[name]:
            raise ValueError('Rollback verification failed; installation unchanged')
    written = []
    try:
        for name, data in changed.items():
            check_path(root / name)
            written.append(name)
            replace(root / name, data)
        after = inventory(root)
        if set(before) != set(after) or any(before[name] != after[name] for name in before if name not in changed):
            raise ValueError('A file outside the planned JSON changes changed')
        for name, data in changed.items():
            if (root / name).read_bytes() != data:
                raise ValueError('Written metadata differs from the migration plan')
        _, _, _, remaining = build_plan(root)
        if remaining:
            raise ValueError('Metadata migration did not reach a stable result')
    except Exception:
        for name in written:
            replace(root / name, originals[name])
        raise
    summary.update(applied=True, rollback=backup.name, unchangedOtherFiles=len(before)-len(changed))
    return summary


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--package', type=Path, required=True)
    parser.add_argument('--backup', type=Path, required=True)
    parser.add_argument('--report', type=Path)
    parser.add_argument('--apply', action='store_true', help='Apply the reviewed JSON-only migration; otherwise preview')
    args = parser.parse_args()
    result = migrate(args.package, args.backup, args.apply)
    if args.report:
        args.report.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(result, indent=2))
