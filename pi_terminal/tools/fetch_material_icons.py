#!/usr/bin/env python3
"""Vendor Google's Apache-2.0 Material Icons Round SVGs; no runtime downloads."""
from pathlib import Path
import hashlib
import json
import urllib.request
import xml.etree.ElementTree as ET
import argparse

ROOT = Path(__file__).resolve().parents[1] / 'qml/icons/material'
def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--only', nargs='+', help='Fetch only these manifest entries; leave other assets unchanged')
    args = parser.parse_args()
    for variant in ('dark','light'): (ROOT / variant).mkdir(parents=True,exist_ok=True)
    sources=json.loads((ROOT/'SOURCES.json').read_text())
    manifest = dict(sources)
    if args.only:
        unknown = set(args.only) - sources.keys()
        if unknown:
            parser.error('Unknown icons: ' + ', '.join(sorted(unknown)))
    for name,record in sources.items():
        if args.only and name not in args.only:
            continue
        url=record['source']
        original=urllib.request.urlopen(url,timeout=30).read()
        assert ET.fromstring(original).tag.endswith('svg'), name
        manifest[name]={'source':url,'sha256':hashlib.sha256(original).hexdigest()}
        for variant,color in (('dark','#081b29'),('light','#f4f8ff')):
            svg=original.decode().replace('<svg ',f'<svg fill="{color}" ',1)
            (ROOT/variant/f'{name}.svg').write_text('<!-- Google Material Icons Round; color modified for terminal theme; glyph unchanged. Apache-2.0. -->\n'+svg+'\n')
    (ROOT/'SOURCES.json').write_text(json.dumps(manifest,indent=2)+'\n')
    print(f'Vendored {len(args.only or sources)} Material Icons Round glyphs in two color variants.')

if __name__=='__main__': main()
