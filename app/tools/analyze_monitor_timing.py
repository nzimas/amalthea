#!/usr/bin/env python3
"""Summarize native monitor CSVs; uses only the Python standard library."""
import argparse
import csv
import json
from pathlib import Path
from statistics import mean, median


def summarize(path, nominal):
    with path.open() as stream:
        rows = [{key: float(value) for key, value in row.items()}
                for row in csv.DictReader(stream)]
    if len(rows) < 100 or any(int(row['flags']) & 3 != 3 for row in rows):
        raise ValueError(f'{path}: insufficient valid sample/host timestamps')
    fit = rows[30:-30]
    # Center before fitting to avoid cancellation with large host timestamps.
    times = [row['host_seconds'] - fit[0]['host_seconds'] for row in fit]
    samples = [row['sample_time'] - fit[0]['sample_time'] for row in fit]
    tm, sm = mean(times), mean(samples)
    rate = sum((t-tm)*(s-sm) for t, s in zip(times, samples)) / sum((t-tm)**2 for t in times)
    result = {
        'callbacks': len(rows), 'nominal_hz': nominal, 'measured_hz': rate,
        'clock_ppm': (rate/nominal-1)*1e6,
        'timestamp_discontinuities': sum(abs(b['sample_time']-a['sample_time']-a['frames']) > .01
                                         for a, b in zip(rows, rows[1:])),
        'maximum_callback_gap_ms': max(b['callback_seconds']-a['callback_seconds']
                                       for a, b in zip(rows, rows[1:]))*1000,
    }
    if 'output' in path.stem:
        steady = [row for row in rows if row['fill'] > 0
                  and row['callback_seconds'] > rows[0]['callback_seconds']+1]
        if not steady:
            raise ValueError(f'{path}: no primed monitor interval')
        fill = [row['fill'] for row in steady]
        result['buffer_fill_min_median_max'] = [min(fill), median(fill), max(fill)]
        result['last_correction_ppm'] = rows[-1]['correction']*1e6
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('prefix', type=Path)
    parser.add_argument('--input-rate', type=float, default=44100)
    parser.add_argument('--output-rate', type=float, default=48000)
    args = parser.parse_args()
    report = {name: summarize(Path(f'{args.prefix}-{name}.csv'), rate)
              for name, rate in [('input', args.input_rate), ('output', args.output_rate)]}
    print(json.dumps(report, indent=2))
