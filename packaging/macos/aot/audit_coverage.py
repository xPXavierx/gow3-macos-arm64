#!/usr/bin/env python3
"""Inventory static AOT gaps without running the game. Local generated C only.

A gap site is not evidence of reachability: scanner candidates can include data.
The public summary contains counts only; addresses stay in the local build tree.
"""
import argparse
from collections import Counter, defaultdict
import json
from pathlib import Path
import re

FUNCTION = re.compile(r'^/\* (0x[0-9a-f]+), (\d+) bytes, (\d+) instructions \*/$')
GAP = re.compile(r'/\* (\w+) \*/.*c->rip = B \+ (0x[0-9a-f]+)ull;.*rc->step\(c\)')


def audit(directory):
    sites = defaultdict(set)
    affected = defaultdict(set)
    functions = set()
    instructions = 0
    for path in sorted(directory.glob('recomp_*.c')):
        function = None
        for line in path.read_text().splitlines():
            match = FUNCTION.match(line)
            if match:
                function = match[1]
                if function not in functions:
                    functions.add(function)
                    instructions += int(match[3])
            match = GAP.search(line)
            if match:
                mnemonic, offset = match.groups()
                sites[mnemonic].add(offset)
                affected[mnemonic].add(function)
    ranked = sorted(sites, key=lambda key: (-len(sites[key]), key))
    summary = {
        'scope': 'Discovered static function candidates; includes possible data and unreachable instructions. Not runtime coverage or percent of game completion.',
        'function_candidates': len(functions),
        'decoded_instruction_instances': instructions,
        'distinct_gap_sites': sum(map(len, sites.values())),
        'gap_mnemonics': len(sites),
        'families': [{'mnemonic': name, 'sites': len(sites[name]),
                      'affected_function_candidates': len(affected[name])} for name in ranked],
    }
    return summary, {name: sorted(sites[name], key=lambda value: int(value, 16)) for name in ranked}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    args = parser.parse_args()
    summary, sites = audit(args.directory)
    (args.directory / 'coverage-summary.json').write_text(json.dumps(summary, indent=2) + '\n')
    (args.directory / 'coverage-sites.local.json').write_text(json.dumps(sites, indent=2) + '\n')
    print(json.dumps(summary, indent=2))


if __name__ == '__main__':
    main()
