"""Keep machine-specific provenance out of portable package metadata."""
import json
import pathlib
import re
import tempfile

_WINDOWS_PATH = re.compile(r'[A-Za-z]:[\\/]|\\\\[^\\]+\\')
_OMIT = object()


def portable_value(value, package):
    """Keep package references; omit external absolute paths, including nested ones."""
    if isinstance(value, dict):
        result = {}
        for key, item in value.items():
            cleaned = portable_value(item, package)
            if cleaned is not _OMIT and not _WINDOWS_PATH.search(key):
                result[key] = cleaned
        return result
    if isinstance(value, list):
        return [cleaned for item in value
                if (cleaned := portable_value(item, package)) is not _OMIT]
    if isinstance(value, str):
        path = pathlib.Path(value)
        if path.is_absolute() or pathlib.PurePosixPath(value).is_absolute() or _WINDOWS_PATH.search(value):
            try:
                return path.resolve().relative_to(package.resolve()).as_posix()
            except (ValueError, OSError):
                return _OMIT
    return value


def make_portable(package):
    """Sanitize a staging package in place, never following metadata links.

    Hashes are observations, not approval tokens. Callers must regenerate their
    file inventory after this step, since JSON serialization can change bytes.
    """
    package = pathlib.Path(package).resolve()
    def visit(directory):
        if directory.is_symlink() or directory.is_junction():
            raise ValueError('Linked metadata directory is not a portable package')
        for path in directory.iterdir():
            if path.is_symlink() or path.is_junction():
                raise ValueError('Linked metadata entry is not a portable package')
            if path.is_dir():
                visit(path)
            elif path.suffix.lower() == '.json':
                original = json.loads(path.read_text(encoding='utf-8-sig'))
                cleaned = portable_value(original, package)
                if cleaned is _OMIT:
                    cleaned = None
                if cleaned != original:
                    # Replace the metadata inode so a stage sharing a hardlink
                    # with its rollback cannot mutate that rollback in place.
                    with tempfile.NamedTemporaryFile(mode='w', encoding='utf-8',
                                                     dir=path.parent, delete=False) as stream:
                        temporary = pathlib.Path(stream.name)
                        stream.write(json.dumps(cleaned, indent=2) + '\n')
                    try:
                        temporary.replace(path)
                    finally:
                        temporary.unlink(missing_ok=True)
    visit(package)
