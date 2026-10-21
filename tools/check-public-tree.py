"""Check the exact Git index before committing/pushing source-only content."""
import argparse
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
ROOT_FILES = {'LICENSE', 'README.md', 'AGENTS.md', 'CONVENTIONS.md', 'CMakeLists.txt',
              '.gitignore', '.gitattributes', 'LcsSaBridge.ini.example'}
FOLDERS = {'src', 'include', 'tools', 'tests', 'docs', 'third_party', '.github', '.githooks'}
EXTENSIONS = {'.cpp', '.c', '.hpp', '.h', '.inc', '.py', '.ps1', '.md', '.txt', '.yml', '.yaml'}
PRIVATE_FILES = {'src/world_sector_layers.inc'}
SECRETS = re.compile(rb'-----BEGIN (?:RSA |OPENSSH |EC )?PRIVATE KEY-----|gh[pousr]_[A-Za-z0-9]{36,}|github_pat_[A-Za-z0-9_]{70,}|AKIA[0-9A-Z]{16}')
PERSONAL_PATH = re.compile(rb'[A-Za-z]:[\\/]+Users[\\/]|[A-Za-z]:[\\/]+gta_original[\\/]', re.I)


def check():
    raw = subprocess.check_output(['git', 'ls-files', '--stage', '-z'], cwd=ROOT)
    entries = []
    for row in raw.split(b'\0'):
        if not row:
            continue
        meta, name = row.split(b'\t', 1)
        mode, oid, stage = meta.split()
        entries.append((name.decode('utf-8'), mode, oid, stage))
    blob = subprocess.run(['git', 'cat-file', '--batch'], cwd=ROOT,
                          input=b'\n'.join(e[2] for e in entries)+b'\n',
                          stdout=subprocess.PIPE, check=True).stdout
    cursor = 0
    failures = []
    for name, mode, oid, stage in entries:
        end = blob.index(b'\n', cursor)
        size = int(blob[cursor:end].split()[2])
        data = blob[end+1:end+1+size]
        cursor = end+2+size
        path = pathlib.PurePosixPath(name)
        allowed = name in ROOT_FILES or (len(path.parts)>1 and path.parts[0] in FOLDERS and
                  (path.suffix in EXTENSIONS or path.parts[0] == '.githooks'))
        reason = None
        if not allowed or name in PRIVATE_FILES:
            reason = 'not on the public source allowlist'
        elif mode not in (b'100644', b'100755') or stage != b'0':
            reason = 'links, submodules and unresolved entries are forbidden'
        elif not (ROOT/path).resolve().is_relative_to(ROOT):
            reason = 'path resolves outside the public repository'
        elif size > 1024*1024 or b'\0' in data:
            reason = 'binary or oversized payload'
        elif SECRETS.search(data):
            reason = 'credential/private-key pattern'
        elif PERSONAL_PATH.search(data):
            reason = 'personal installation path'
        else:
            try:
                data.decode('utf-8')
            except UnicodeDecodeError:
                reason = 'non-UTF-8 source needs review'
        if reason:
            failures.append(f'{name}: {reason}')
    if failures:
        print('\n'.join(failures), file=sys.stderr)
        return 1
    print(f'PASS: {len(entries)} indexed files; source-only allowlist, no private links, game binaries or detected secrets.')
    return 0


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--staged', action='store_true', help='Check the entire exact index, including the staged change')
    parser.add_argument('--tracked', action='store_true', help='Check all indexed files')
    parser.parse_args()
    raise SystemExit(check())
