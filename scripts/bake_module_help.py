#!/usr/bin/env python3
"""Bake module help (spawn-menu descriptions, control hover tooltips) for the WASM build.

Sources:
  resource/tooltips_<lang>.txt   the desktop tooltips (HelpDisplay::LoadTooltips format:
                                 "module~description" lines, then "~control~tooltip" lines)
  scripts/module_help_wasm.txt   English supplement, same format, keyed by the WASM type ids
                                 and control names. "@wasmtype=desktoptype" borrows a desktop
                                 entry for a type whose id differs. Supplement lines win.

Each WASM adapter's control names are read from its kParams table (wasm/src/adapters), so
the table is keyed by the names the canvas reports on hover. A desktop control matches when
the names agree ignoring case, spaces and punctuation ("x freq" == "xfreq"), or through a
desktop wildcard ("gain *" covers gain1..gain4).

Output: wasm/resource-pack/help/module_help.json
  { "version": 1, "fallback": "en",
    "locales": { "en": { "<type>": { "d": "description", "c": { "<control>": "tooltip" } } },
                 "de": { ... only what the translation has ... } } }

Usage: scripts/bake_module_help.py [--check] [--report] [output]
  --check   fail when a wave-4 (#127) module lacks a description or any control tooltip, or
            when fewer than 80% of the other modules' controls have one (CI runs this).
  --report  list every control without help.
"""

import json
import os
import re
import sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
LOCALES = {'eng': 'en', 'deu': 'de', 'esp': 'es', 'ru': 'ru', 'tr': 'tr'}
SUPPLEMENT = os.path.join(ROOT, 'scripts', 'module_help_wasm.txt')
DEFAULT_OUT = os.path.join(ROOT, 'wasm', 'resource-pack', 'help', 'module_help.json')

# The catalogue wave of #127: every control needs help, not just 80%.
WAVE4 = {'fmsynth', 'karplusstrong', 'seaofgrain', 'notechance', 'capo', 'pitchbender', 'notesequencer',
         'bitcrush', 'distortion', 'stereowidth', 'pingpongdelay', 'vocoder', 'send', 'envelope', 'ramper'}
MIN_COVERAGE = 0.8

# Codepoints in the UI SDF atlas (charset() in wasm/tools/sdf_font_baker/bake_sdf_font.cpp).
# A translated string with anything else falls back to English rather than drawing gaps.
FONT_CHARSET = (set(range(32, 127)) | {0xB0, 0xB5, 0xD7, 0x2013, 0x2022, 0x2026, 0x2190, 0x2191, 0x2192,
                                       0x2193, 0x266D, 0x266F, 0xA1, 0xAB, 0xBB, 0xBF} |
                (set(range(0xC0, 0x100)) - {0xF7}) | {0x11E, 0x11F, 0x130, 0x131, 0x15E, 0x15F} |
                {0x401, 0x451} | set(range(0x410, 0x450)) | {0x2018, 0x2019, 0x201C, 0x201D, ord('\n')})


def drawable(text):
    return all(ord(c) in FONT_CHARSET for c in text)


def normalize(name):
    return re.sub(r'[^a-z0-9*]', '', name.lower())


def parse_tooltips(path):
    """{module: {"d": str, "c": [(name, text)]}} in file order."""
    modules = {}
    current = None
    with open(path, encoding='utf-8') as f:
        for raw in f:
            line = raw.rstrip('\n').rstrip('\r')
            if not line.strip() or line.lstrip().startswith('#'):
                continue
            line = line.replace('\\n', '\n')
            tokens = line.split('~')
            if len(tokens) == 2:
                current = modules.setdefault(tokens[0].strip(), {'d': '', 'c': []})
                current['d'] = tokens[1].strip()
            elif len(tokens) == 3 and current is not None:
                current['c'].append((tokens[1].strip(), tokens[2].strip()))
    return modules


def parse_supplement(path):
    aliases = {}
    lines = []
    with open(path, encoding='utf-8') as f:
        for raw in f:
            line = raw.rstrip('\n')
            if line.startswith('@'):
                ours, desktop = line[1:].split('=', 1)
                aliases[ours.strip()] = desktop.strip()
            else:
                lines.append(line)
    tmp = path + '.body'
    # parse_tooltips reads a file; feed it the non-alias lines.
    with open(tmp, 'w', encoding='utf-8') as f:
        f.write('\n'.join(lines))
    try:
        return aliases, parse_tooltips(tmp)
    finally:
        os.remove(tmp)


def scan_adapters():
    """{type: [control names]} from the adapter headers (typeId) and sources (kParams)."""
    include = os.path.join(ROOT, 'wasm', 'include', 'BespokeWasm', 'adapters')
    sources = os.path.join(ROOT, 'wasm', 'src', 'adapters')
    types = {}
    for header in sorted(os.listdir(include)):
        if not header.endswith('ModuleAdapter.h'):
            continue
        text = open(os.path.join(include, header), encoding='utf-8').read()
        match = re.search(r'typeId\(\) const override \{ return "([a-z0-9]+)"; \}', text)
        if not match:
            continue
        source = os.path.join(sources, header[:-2] + '.cpp')
        names = []
        if os.path.exists(source):
            body = open(source, encoding='utf-8').read()
            table = re.search(r'kParams\[\] = \{(.*?)\n\s*\};', body, re.S)
            if table:
                names = re.findall(r'\.name = "([^"]+)"', table.group(1))
        types[match.group(1)] = names
    return types


def match_control(control, entries):
    """Tooltip for our `control` among desktop (name, text) entries, or None."""
    key = normalize(control)
    for name, text in entries:
        if text and normalize(name) == key:
            return text
    for name, text in entries:
        pattern = normalize(name)
        if text and '*' in pattern:
            regex = '^' + re.escape(pattern).replace('\\*', '.*') + '$'
            if re.match(regex, key):
                return text
    return None


def build_locale(types, desktop, aliases, supplement, undrawable):
    table = {}
    for wasm_type, controls in types.items():
        desktop_entry = desktop.get(aliases.get(wasm_type, wasm_type))
        extra = supplement.get(wasm_type) if supplement else None
        description = (extra['d'] if extra and extra['d'] else '') or (desktop_entry['d'] if desktop_entry else '')
        if not drawable(description):
            undrawable.append(description)
            description = ''
        tips = {}
        for control in controls:
            text = match_control(control, extra['c']) if extra else None
            if not text and desktop_entry:
                text = match_control(control, desktop_entry['c'])
            if text and not drawable(text):
                undrawable.append(text)
                text = None
            if text:
                tips[control] = text
        if description or tips:
            entry = {}
            if description:
                entry['d'] = description
            if tips:
                entry['c'] = tips
            table[wasm_type] = entry
    return table


def coverage(types, english):
    missing = []
    wave4_ok = True
    covered = total = 0
    for wasm_type, controls in sorted(types.items()):
        entry = english.get(wasm_type, {})
        tips = entry.get('c', {})
        absent = [c for c in controls if c not in tips]
        if wasm_type in WAVE4:
            if absent or not entry.get('d'):
                wave4_ok = False
        else:
            covered += len(controls) - len(absent)
            total += len(controls)
        if absent or not entry.get('d'):
            missing.append((wasm_type, bool(entry.get('d')), absent))
    return wave4_ok, (covered / total if total else 1.0), missing


def main(argv):
    check = '--check' in argv
    report = '--report' in argv
    paths = [a for a in argv if not a.startswith('--')]
    out_path = paths[0] if paths else DEFAULT_OUT

    types = scan_adapters()
    aliases, supplement = parse_supplement(SUPPLEMENT)
    locales = {}
    for code, short in LOCALES.items():
        path = os.path.join(ROOT, 'resource', 'tooltips_%s.txt' % code)
        if not os.path.exists(path):
            continue
        desktop = parse_tooltips(path)
        undrawable = []
        table = build_locale(types, desktop, aliases, supplement if short == 'en' else None, undrawable)
        if undrawable:
            print('module help: %s: %d strings use characters the UI font lacks (English shown instead)' %
                  (short, len(undrawable)))
        if short == 'en' and undrawable:
            print('English help must stay inside the UI font charset:', undrawable[0], file=sys.stderr)
            return 1
        if table:
            locales[short] = table

    english = locales.get('en', {})
    wave4_ok, other_coverage, missing = coverage(types, english)
    if report:
        for wasm_type, has_description, absent in missing:
            print('%-14s %s %s' % (wasm_type, '' if has_description else '[no description]', ' '.join(absent)))
    print('module help: %d types, wave-4 complete: %s, other controls covered: %.0f%%' %
          (len(types), 'yes' if wave4_ok else 'NO', other_coverage * 100.0))

    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    with open(out_path, 'w', encoding='utf-8') as f:
        json.dump({'version': 1, 'fallback': 'en', 'locales': locales}, f, ensure_ascii=False, sort_keys=True,
                  separators=(',', ':'))
        f.write('\n')
    if check and (not wave4_ok or other_coverage < MIN_COVERAGE):
        print('module help coverage below the bar (see --report)', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
