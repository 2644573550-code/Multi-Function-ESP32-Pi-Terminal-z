#!/usr/bin/env python3
"""Idempotently bind system UI literals to the maintained I18n dictionary.

Does not touch module plugins, user content, URLs, regexes or IME fixtures.
Run after extending I18n.qml when adding UI labels.
"""
from pathlib import Path
import re

root = Path(__file__).resolve().parents[1] / 'qml'
dictionary = set(re.findall(r'"([^"\n]+)"\s*:', (root / 'I18n.qml').read_text()))
for path in root.glob('*.qml'):
    if path.name == 'I18n.qml':
        continue
    text = path.read_text()
    def bind(match):
        literal = match.group(0)
        if match.group(1) not in dictionary:
            return literal
        before = text[max(0, match.start()-7):match.start()]
        return literal if before.endswith('I18n.t(') else 'I18n.t(' + literal + ')'
    updated = re.sub(r'"([^"\n]*)"', bind, text)
    if path.name == 'Main.qml':
        updated = updated.replace('background:Rectangle {color:Theme.surface;', 'background:GlassSurface {color:Theme.surface;')
        updated = re.sub(r'GlassSurface(\s*\{[^\n{}]*?)border\.color:',r'GlassSurface\1borderColor:',updated)
        updated = re.sub(r'GlassSurface(\s*\{[^\n{}]*?)border\.width:',r'GlassSurface\1borderWidth:',updated)
    if updated != text:
        path.write_text(updated)
