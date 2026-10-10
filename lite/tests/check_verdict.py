"""Interpret real repository gate reports without equating exit zero to proof."""
import re


def verdict(name, exit_code, output):
    result = {'state': 'passed' if exit_code == 0 else 'failed'}
    if exit_code == 1223:
        return {'state': 'cancelled'}
    if exit_code != 0:
        return result
    if name == 'Link checks' and 'nothing to verify' in output:
        result['state'] = 'skipped'
    if name == 'Declaration agreement':
        existing = re.search(r'(\d+) disagreement\(s\)', output)
        if existing and int(existing[1]):
            result.update(state='passed-with-baseline', existingDisagreements=int(existing[1]))
    if name == 'ROM data comparison':
        counts = {label.lower(): int(match[1].replace(',', ''))
                  for label in ('VERIFIED', 'PARTIAL', 'DIFFERS', 'UNNAMED')
                  if (match := re.search(r'^\s*' + label + r'\s+([\d,]+)(?:\s|$)', output, re.M))}
        result['dataCounts'] = counts
        if len(counts) != 4:
            result['state'] = 'unverified: incomplete data report'
        elif counts['differs']:
            result['state'] = 'completed-with-differences'
        elif counts['partial'] or counts['unnamed']:
            result['state'] = 'completed-with-partial-data'
        elif not counts['verified']:
            result['state'] = 'skipped: no data verified'
    return result


if __name__ == '__main__':
    import argparse
    import json
    import pathlib
    parser = argparse.ArgumentParser(description='Reclassify retained live-check results; no checks rerun')
    parser.add_argument('output')
    folder = pathlib.Path(parser.parse_args().output)
    summary = folder / 'summary.json'
    rows = json.loads(summary.read_text(encoding='utf-8'))
    for row in rows:
        if 'exit' in row and 'log' in row:
            row.update(verdict(row['name'], row['exit'], pathlib.Path(row['log']).read_text(encoding='utf-8')))
    summary.write_text(json.dumps(rows, indent=2), encoding='utf-8')
    print(json.dumps(rows, indent=2))
