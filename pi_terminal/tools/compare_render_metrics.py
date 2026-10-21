#!/usr/bin/env python3
"""Compare steady-state real-device render benchmarks (not idle FPS)."""
import argparse
import json
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('quality', type=Path)
    parser.add_argument('efficient', type=Path)
    args = parser.parse_args()
    reports = [json.loads(path.read_text()) for path in (args.quality, args.efficient)]
    for report in reports:
        if not report.get('continuous_animation'):
            parser.error('Both reports must use --render-benchmark and the warmup-aware metrics implementation')
    for field in ('qt_version', 'architecture', 'width', 'height'):
        if reports[0].get(field) != reports[1].get(field):
            parser.error(f'Incompatible benchmark {field}')
    result = {}
    for name, report in zip(('quality', 'efficient'), reports):
        result[name] = {key: report.get(key) for key in (
            'average_frame_events_per_second', 'median_frame_interval_ms',
            'p95_frame_interval_ms', 'frames_over_50_ms', 'memory')}
    fps = reports[1]['average_frame_events_per_second']
    result['average_at_least_30fps'] = fps >= 30
    result['scope'] = 'Continuous home-carousel animation, not all plugins or worst-case guaranteed FPS'
    print(json.dumps(result, ensure_ascii=False, indent=2))


if __name__ == '__main__':
    main()
