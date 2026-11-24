#!/usr/bin/env python3
"""Summarize retained audio runs, tolerating an incomplete last CSV row.

This reports transport evidence, never an automatic audible-crackle verdict.
"""
import csv
import json
from pathlib import Path
import sys

def summarize(path):
    valid, invalid, start, end = 0, 0, None, None
    faults, states, captures = [], [], []
    maxima = {}
    with path.open(errors='replace', newline='') as stream:
        for row in csv.DictReader(stream):
            try:
                seconds = float(row['seconds'])
                status = int(row['status'])
                # A power-cut tail may parse into a dictionary with missing fields.
                values = {key: int(row[key]) for key in (
                    'capture_frames', 'capture_xruns', 'input_starvation',
                    'ring_overflow', 'output_xruns', 'max_capture_gap_us',
                    'max_fx_render_us')}
                event = row['event']
                if not event or None in row:
                    raise ValueError('incomplete/extra fields')
            except (KeyError, ValueError, TypeError):
                invalid += 1
                continue
            valid += 1
            start = seconds if start is None else min(start, seconds)
            end = seconds if end is None else max(end, seconds)
            for key, value in values.items():
                maxima[key] = max(value, maxima.get(key, 0))
            detail = row.get('detail', '')
            if event in ('capture_error', 'capture_recovery', 'counter_change'):
                faults.append(dict(seconds=seconds, event=event, status=status, **values))
            if event == 'usb_status':
                states.append(dict(seconds=seconds, detail=detail))
            if event in ('capture_start', 'capture_parameters', 'capture_close'):
                captures.append(dict(seconds=seconds, event=event, status=status, detail=detail))
    system = path.with_suffix('.system.log')
    text = system.read_text(errors='replace') if system.exists() else ''
    return dict(file=str(path), identity=text.splitlines()[0] if text else 'legacy/unknown boot',
                valid_rows=valid, rejected_rows=invalid, first_seconds=start, last_seconds=end,
                clean_segment_end='SEGMENT_END' in text, maximum_counters=maxima,
                faults=faults, connections=states, capture_events=captures,
                logger_warnings=[line for line in text.splitlines()
                                 if 'logger_queue_dropped_total=' in line or
                                 'checkpoint failed' in line])

def main():
    if len(sys.argv) != 2:
        raise SystemExit('Usage: analyze_audio_history.py LOG_FILE_OR_DIRECTORY')
    root = Path(sys.argv[1])
    paths = [root] if root.is_file() else sorted(
        set(root.rglob('diagnostic-*.csv')) | set(root.rglob('transport-*.csv')))
    print(json.dumps(dict(note='Counters and sample summaries do not establish audible crackle causation.',
                          runs=[summarize(path) for path in paths]), indent=2))

if __name__ == '__main__':
    main()
