"""Where and when the task observer may rewrite the hooked task routines, as data only.

Usage: task_observer_install_contract.py <gta_sa.exe>

Source contract (src/asi.cpp): the observer is installed in exactly one place, install_task_observer_at_startup(), which
is called only from on_startup_scripts() before original_startup_process(); it is not reachable from DllMain/InitializeASI,
from a frame callback or from the campaign. The byte-exchange exclusion flag is set only inside that function.

Image contract (SA 1.0 US, vendor/sa-reference/Game.cpp + capstone): the startup hook site 0x53BCC9 is the call to
CTheScripts::Process inside CGame::Initialise (0x53BC80..0x53BCF0), i.e. the first script pass of game start-up, run by the
thread that later runs CGame::Process (0x53BEE0), whose loop call is the separate site 0x53BFC7 (same callee). At that
point no ped, task, event or script command exists and the main loop that can enter the hooked task routines has not
started; that is the concrete exclusion for the multi-byte exchange (no thread suspension, no generic framework).
Limit: a third-party thread that calls CTask routines concurrently during start-up is not excluded by this evidence.
"""
import pathlib
import re
import struct
import sys

import capstone

ROOT = pathlib.Path(__file__).resolve().parents[1]


def image(path):
    data = pathlib.Path(path).read_bytes()
    pe = struct.unpack_from('<I', data, 0x3C)[0]
    count = struct.unpack_from('<H', data, pe + 6)[0]
    optional = struct.unpack_from('<H', data, pe + 20)[0]
    base = struct.unpack_from('<I', data, pe + 52)[0]
    sections = []
    for i in range(count):
        o = pe + 24 + optional + i * 40
        size, va, raw_size, raw = struct.unpack_from('<IIII', data, o + 8)
        sections.append((base + va, max(size, raw_size), raw))

    def read(address, size):
        for va, length, raw in sections:
            if va <= address and address + size <= va + length:
                return data[raw + address - va:raw + address - va + size]
        raise AssertionError(hex(address))
    return read


def function_body(source, name):
    start = source.index(name)
    brace = source.index('{', start)
    depth = 0
    for i in range(brace, len(source)):
        depth += source[i] == '{'
        depth -= source[i] == '}'
        if depth == 0:
            return source[brace:i + 1]
    raise AssertionError(name)


def main(executable):
    source = (ROOT / 'src/asi.cpp').read_text(encoding='utf-8')
    installs = [m.start() for m in re.finditer(r'->install\(', source)]
    assert len(installs) == 1, 'the observer has exactly one install call'
    body = function_body(source, 'void install_task_observer_at_startup()')
    assert source.index('->install(') > source.index('void install_task_observer_at_startup()') and \
        source.index('->install(') < source.index('void install_task_observer_at_startup()') + len(body) + 60
    assert source.count('task_observer_window=true') == 1 and 'task_observer_window=true' in body
    assert 'if(installed)lcs::task_memory_original_prefix=&own_task_original_prefix;' in body, 'own release detour must be authenticated before native task guards run'
    assert source.count('task_memory_original_prefix=') == 1, 'only successful startup installation publishes the authenticator'
    authenticator = function_body(source, 'bool own_task_original_prefix(')
    assert 'task_observer->owns_original_prefix(' in authenticator, 'authenticate owned entry and trampoline, never just a JMP opcode'
    callers = [m.start() for m in re.finditer(r'install_task_observer_at_startup\(\)', source)]
    assert len(callers) == 2, 'definition and a single call'
    startup = function_body(source, 'void __cdecl on_startup_scripts()')
    assert 'install_task_observer_at_startup();' in startup
    assert startup.index('install_task_observer_at_startup();') < startup.index('original_startup_process();')
    for forbidden in ('InitializeASI', 'DllMain', 'on_game_process'):
        assert 'install_task_observer' not in function_body(source, forbidden), forbidden
    assert 'install_task_observer()' not in source, 'the old frame-time installer is gone'
    constructor = function_body(source, 'explicit Campaign(') if 'explicit Campaign(' in source else function_body(source, 'Campaign(')
    publish = constructor.index('task_observer->set_release_probe(route_service->walking_release_probe())')
    assert publish > constructor.index('runtime.start_main()'), 'publish only after setup/start succeeds (constructor unwind)'
    destructor = function_body(source, '~Campaign()')
    assert destructor.index('set_release_probe({})') < destructor.index('sa_crusher_cleanup()'), 'withdraw borrowed callback before cleanup can release tasks'

    read = image(executable)
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    startup_call = list(md.disasm(read(0x53BCC9, 5), 0x53BCC9))[0]
    loop_call = list(md.disasm(read(0x53BFC7, 5), 0x53BFC7))[0]
    assert startup_call.mnemonic == 'call' and loop_call.mnemonic == 'call'
    assert int(startup_call.op_str, 16) == int(loop_call.op_str, 16) == 0x46A000, 'both sites call CTheScripts::Process'
    game = (ROOT / 'vendor/sa-reference/Game.cpp').read_text(encoding='utf-8')
    assert 'RH_ScopedInstall(Initialise, 0x53BC80)' in game and 'RH_ScopedInstall(ReInitGameObjectVariables, 0x53BCF0)' in game
    assert 'RH_ScopedInstall(Process, 0x53BEE0)' in game
    assert 0x53BC80 <= 0x53BCC9 < 0x53BCF0 and not (0x53BEE0 <= 0x53BCC9)
    assert 0x53BEE0 <= 0x53BFC7
    startup_source = (ROOT / 'src/asi.cpp').read_text(encoding='utf-8')
    assert re.search(r'startup_process_call\s*=\s*0x53BCC9', startup_source) and re.search(r'process_call\s*=\s*0x53BFC7', startup_source)
    print('PASS: the task observer is installed once, from the CGame::Initialise start-up script pass, never from DllMain or a frame')


if __name__ == '__main__':
    main(*sys.argv[1:])
