#!/usr/bin/env python3
"""Report release .wasm sizes against wasm/size-budget.json and fail far over budget.

Sizes are gzip -9 (what a static host serves); the budget is the sum of the baseline sizes
plus the allowance. Over budget warns; more than failOverBudgetPercent over fails.
In GitHub Actions the table is appended to the job summary.

Usage: scripts/check_wasm_size.py [dist_dir]   (default wasm/dist)
"""

import gzip
import json
import os
import sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))


def gzip_size(path):
    with open(path, 'rb') as f:
        return len(gzip.compress(f.read(), 9, mtime=0))


def kib(n):
    return '%.1f KiB' % (n / 1024.0)


def main(argv):
    dist = argv[0] if argv else os.path.join(ROOT, 'wasm', 'dist')
    with open(os.path.join(ROOT, 'wasm', 'size-budget.json'), encoding='utf-8') as f:
        budget = json.load(f)

    rows = []
    total = 0
    baseline_total = 0
    for name, baseline in budget['baseline'].items():
        path = os.path.join(dist, name)
        if not os.path.exists(path):
            print('::error::%s is missing; build the release WASM first' % path)
            return 1
        raw = os.path.getsize(path)
        size = gzip_size(path)
        total += size
        baseline_total += baseline
        rows.append((name, raw, size, size - baseline))

    limit = baseline_total + budget['allowance']
    fail_at = limit * (1.0 + budget['failOverBudgetPercent'] / 100.0)
    lines = [
        '| Module | .wasm | gzip | vs. baseline |',
        '|---|---:|---:|---:|',
    ]
    for name, raw, size, delta in rows:
        lines.append('| %s | %s | %s | %+.1f KiB |' % (name, kib(raw), kib(size), delta / 1024.0))
    lines.append('| **total** | | **%s** | %+.1f KiB |' % (kib(total), (total - baseline_total) / 1024.0))
    lines.append('')
    lines.append('Budget %s gzip (baseline + %s allowance); fails above %s. Used %.0f%% of the allowance.' %
                 (kib(limit), kib(budget['allowance']), kib(fail_at),
                  100.0 * (total - baseline_total) / budget['allowance']))
    report = '\n'.join(lines)
    print(report)
    summary = os.environ.get('GITHUB_STEP_SUMMARY')
    if summary:
        with open(summary, 'a', encoding='utf-8') as f:
            f.write('### WASM size\n\n' + report + '\n')

    if total > fail_at:
        print('::error::WASM is %s gzip, more than %d%% over the %s budget' %
              (kib(total), budget['failOverBudgetPercent'], kib(limit)))
        return 1
    if total > limit:
        print('::warning::WASM is %s gzip, over the %s budget' % (kib(total), kib(limit)))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
