#!/usr/bin/env python3
"""Summarize passive USBFS evidence without treating signal grit as packet loss."""
import argparse
import collections
import csv
import json
from pathlib import Path


def summarize(path):
    phases = collections.defaultdict(lambda: {'packets': 0, 'received': 0, 'errors': 0,
                                              'frames': 0, 'lengths': collections.Counter(),
                                              'statuses': collections.Counter()})
    first = last = None
    with Path(path).open(newline='') as stream:
        for row in csv.DictReader(stream):
            index = int(row['packet'])
            # One packet slot per USB millisecond; completion is batched in URBs.
            phase = index // 60000
            result = phases[phase]
            result['packets'] += 1
            received = int(row['received'])
            result['received'] += received
            length = int(row['bytes'])
            urb, packet = int(row['urb_status']), int(row['packet_status'])
            if not received or urb or packet or length % 12:
                result['errors'] += 1
                result['statuses'][f'received={received},urb={urb},packet={packet}'] += 1
            else:
                result['frames'] += length // 12
            result['lengths'][length] += 1
            completion = float(row.get('completed_seconds', 0))
            if received:
                first = completion if first is None else min(first, completion)
                last = completion if last is None else max(last, completion)
    return {'first_completion_seconds': first, 'last_completion_seconds': last,
            'minutes': dict(sorted(phases.items())),
            'interpretation': 'Packet errors are transport evidence; clean statuses alone do not prove clean source audio.'}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('csv')
    args = parser.parse_args()
    print(json.dumps(summarize(args.csv), indent=2))
