"""Estimate pending command size from local source/ELF data; never execute GTA.

Source lines and linear spacing between switch entries are estimates, not the
full transitive size or implementation effort. Shared tails and engine callees
are deliberately not counted as completed command implementations.
"""
import argparse
import importlib.util
import json
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]


def source_cases(directory):
    cases = {}
    for path in sorted(directory.glob('Script*.cpp')):
        if path.name == 'ScriptDebug.cpp':
            continue
        text = path.read_text()
        # Retain line numbers while excluding comments from the estimate.
        stripped = re.sub(r'/\*.*?\*/', lambda m: '\n' * m[0].count('\n'), text, flags=re.S)
        stripped = re.sub(r'//[^\n]*', '', stripped)
        labels = list(re.finditer(r'^([ \t]*)case (COMMAND_\w+):', stripped, re.M))
        for index, label in enumerate(labels):
            end = labels[index + 1].start() if index + 1 < len(labels) else len(stripped)
            default = re.search(r'^' + re.escape(label[1]) + r'default:', stripped[label.end():end], re.M)
            if default:
                end = label.end() + default.start()
            body = stripped[label.end():end]
            # Consecutive aliases share the following body, rather than having
            # an imaginary zero-sized implementation.
            following = index + 1
            while not body.strip() and following < len(labels):
                start = labels[following].end()
                end = labels[following + 1].start() if following + 1 < len(labels) else len(stripped)
                body = stripped[start:end]
                following += 1
            lines = [line.strip() for line in body.splitlines()
                     if line.strip() and line.strip() not in ('{', '}')]
            cases.setdefault(label[2], []).append(dict(
                source=str(path.resolve()), line=stripped[:label.start()].count('\n') + 1,
                logicalLinesEstimate=len(lines),
                engineCalls=sorted(set(re.findall(r'\b([A-Za-z_]\w*::[A-Za-z_]\w*)\s*\(', body)))))
    return cases


def rank(audit, executable, source):
    spec = importlib.util.spec_from_file_location('rank_original', ROOT/'tools/inspect-original-command.py')
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    image = module.OriginalImage(executable)
    image.require_profile()
    cases = source_cases(source)
    entries = {}
    for command in audit['commands']:
        dispatch = image.handler(command['opcode'])
        if dispatch['table'] is not None and dispatch['group'] not in entries:
            entries[dispatch['group']] = sorted(set(image.word(dispatch['table'] + n*4)
                                                    for n in range(dispatch['count'])))
    result = []
    for command in audit['commands']:
        if command['sourceHandlerClauses']:
            continue
        dispatch = image.handler(command['opcode'])
        later = [entry for entry in entries.get(dispatch['group'], []) if entry > dispatch['handler']]
        span = min(later) - dispatch['handler'] if later else None
        reverse = cases.get(command['name'], [])
        result.append(dict(opcode=command['opcode'], name=command['name'],
                           instructionSites=command['instructionSites'],
                           retailHandler=f"0x{dispatch['handler']:08X}",
                           retailDefault=dispatch['default'],
                           linearEntrySpanBytes=span,
                           sourceCases=reverse,
                           sourceLinesEstimate=min((case['logicalLinesEstimate'] for case in reverse), default=None)))
    result.sort(key=lambda row: (row['retailDefault'], row['sourceLinesEstimate'] is None,
                                row['sourceLinesEstimate'] or 0, row['opcode']))
    assert len(result) == audit['commandsWithoutSourceHandlerClause']
    return dict(gameExecuted=False, runtimeSupportGranted=False, executableSha256=image.sha256,
                pendingCommands=len(result),
                method='Non-default first, then estimated uncommented source lines; dependencies require review',
                limitations=['Entry spacing is not reachable handler size: shared tails and called functions are excluded.',
                             'Source is reconstructed; retail contracts and SA consumers must be verified.',
                             'Small handlers may require large subsystems; release defaults do not grant support.'],
                commands=result)


def markdown(report):
    lines = ['# Comandos pendentes: estimativa de tamanho', '',
             f"{report['pendingCommands']} comandos sem cláusula. Jogo não executado; nenhum suporte concedido.", '',
             'Ordem inicial: handlers fora do default retail, depois linhas lógicas estimadas do código reconstruído.',
             'Revisar dependências antes de implementar. Bytes representam espaço até a próxima entrada do switch,',
             'não o tamanho completo do comando. Helpers e caminhos compartilhados podem dominar o trabalho.', '',
             '| Opcode | Comando | Linhas estimadas | Espaço entre entradas (bytes) | Ocorrências | Default retail |',
             '| ---: | --- | ---: | ---: | ---: | --- |']
    for row in report['commands']:
        lines.append(f"| {row['opcode']} | {row['name'].removeprefix('COMMAND_')} | "
                     f"{row['sourceLinesEstimate'] if row['sourceLinesEstimate'] is not None else '?'} | "
                     f"{row['linearEntrySpanBytes'] if row['linearEntrySpanBytes'] is not None else '?'} | "
                     f"{row['instructionSites']} | {'sim' if row['retailDefault'] else 'não'} |")
    return '\n'.join(lines) + '\n'


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--audit', type=pathlib.Path, required=True)
    parser.add_argument('--executable', type=pathlib.Path, required=True)
    parser.add_argument('--source', type=pathlib.Path, required=True)
    parser.add_argument('--output', type=pathlib.Path, required=True)
    args = parser.parse_args()
    report = rank(json.loads(args.audit.read_text()), args.executable, args.source)
    args.output.write_text(json.dumps(report, indent=2, ensure_ascii=False) + '\n', encoding='utf-8')
    args.output.with_suffix('.md').write_text(markdown(report), encoding='utf-8')
    print(json.dumps(dict(pendingCommands=report['pendingCommands'], gameExecuted=False,
                         defaultHandlers=sum(row['retailDefault'] for row in report['commands']),
                         withoutSourceSize=sum(row['sourceLinesEstimate'] is None for row in report['commands']))))
