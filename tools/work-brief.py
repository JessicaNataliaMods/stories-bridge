"""Print the saved active batch only; never build, scan assets or verify claims."""
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read_report(relative):
    path = (ROOT / relative).resolve()
    if not path.is_relative_to(ROOT / 'reports'):
        raise ValueError('Continuity report must stay inside reports/')
    return json.loads(path.read_text(encoding='utf-8-sig'))


def main():
    state = read_report('reports/local-resume-20261015.json')
    report_name = state.get('activeReport')
    batch = read_report(report_name) if report_name else {}
    validation = batch.get('validation', {})
    # Explicitly historical: this view is not a freshness check or coverage audit.
    brief = {
        'evidence': 'saved reports only; current files/processes not checked',
        'installed': state.get('installed'),
        'rollback': state.get('rollback'),
        'activeReport': report_name,
        'status': batch.get('status'),
        'coverage': batch.get('installedCoverage'),
        'planned': batch.get('sourceCommandsPlanned'),
        'newCompleted': batch.get('newCompletedCommands'),
        'lastValidation': {
            key: validation.get(key) for key in (
                'win32Build', 'ctestPassed', 'buildLog', 'testLog')
        },
        'next': batch.get('next') or state.get('activeWork'),
    }
    print(json.dumps(brief, ensure_ascii=False, indent=2))


if __name__ == '__main__':
    main()
