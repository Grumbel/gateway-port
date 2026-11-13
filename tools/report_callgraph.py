#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception
"""Separate engine and story code in the static call graph.

Reads ``callgraph.json`` from ``tools/disassemble_gateway.py`` and the
handler inventory from ``tools/inventory_native_handlers.py``. Object
handlers, room-exit callbacks and action handlers are reached only through
data tables, so they have no static callers; they are used as roots next to
the MZ entry. Every function is then classified by which roots reach it.
See docs/call-graph.md. The report contains addresses and counts only.
"""
import argparse
import collections
import csv
import json
import pathlib
import sys


def section_key(section, image_offset):
    return 'ovl-%03d:%x' % (section, image_offset)


def load_names(path):
    names = {}
    with open(path, newline='') as fh:
        for row in csv.DictReader(fh, delimiter='\t'):
            if row.get('name'):
                off = int(row['offset'], 16)
                names[(row['file'], off, row['section'])] = row['name']
    return names


class Graph:
    def __init__(self, cg):
        self.fn = {f['id']: f for f in cg['functions']}
        self.alias = {f['id']: f['trampoline_to'] for f in cg['functions']
                      if f.get('trampoline_to') in self.fn}
        self.callees = collections.defaultdict(set)
        self.callers = collections.defaultdict(set)
        self.sites = collections.Counter()
        for e in cg['edges']:
            a, b = e['caller'], self.resolve(e['callee'])
            if b not in self.fn or a == b:
                continue
            self.callees[a].add(b)
            self.callers[b].add(a)
            self.sites[b] += len(e['sites'])

    def resolve(self, fid):
        seen = set()
        while fid in self.alias and fid not in seen:
            seen.add(fid)
            fid = self.alias[fid]
        return fid

    def reach(self, roots, stop=()):
        seen, todo = set(), list(roots)
        while todo:
            f = todo.pop()
            if f in seen or f not in self.fn:
                continue
            seen.add(f)
            todo.extend(c for c in self.callees[f] if c not in stop)
        return seen


def roots(inv, actions, fn):
    out = collections.defaultdict(set)   # kind -> function ids
    for h in inv['handlers']:
        e = h['entry']
        out['object'].add(section_key(e['section'], e['image_offset']))
    for c in inv['callbacks']:
        e = c['entry']
        out['callback'].add(section_key(e['section'], e['image_offset']))
    for a in actions['actions']:
        n = a.get('native_code')
        if n and n.get('section') is not None:
            out['action'].add(section_key(n['section'], n['image_offset']))
    missing = {k: sorted(v - fn.keys()) for k, v in out.items()}
    return {k: v & fn.keys() for k, v in out.items()}, missing


def classify(g, root):
    """Add the engine roots to ``root`` and classify every function.

    Returns (class per function, story-private set, API users, API calling
    units, API call sites)."""
    table_roots = set().union(*root.values())
    # The RTLink start-up patches its jump into the C start-up at run time, and
    # the engine reaches more code through function pointers: every resident
    # function without a static caller is therefore an engine root, except
    # RTLink stubs, which the handler tables point at.
    root['engine'] = {f for f, v in g.fn.items()
                      if v['unit'] in ('exe-startup', 'exe') and not g.callers[f]
                      and f not in table_roots and f not in g.alias}

    # Reachability per root kind. Code dispatched through a table is not
    # engine code just because the engine dispatches it: each walk stops at
    # the roots of the other kinds.
    all_roots = set().union(*root.values())
    reach = {k: g.reach(v, stop=all_roots - v) for k, v in root.items()}

    def cls(fid):
        kinds = sorted(k for k, s in reach.items() if fid in s)
        return '+'.join(kinds) if kinds else 'unreached'

    fclass = {fid: cls(fid) for fid in g.fn}
    story_kinds = {'object', 'callback', 'callback+object'}
    private = {f for f, c in fclass.items() if c in story_kinds}

    # The story-facing API: functions outside the story-private code that it
    # calls directly, counted by distinct object handlers / room callbacks.
    api_users, api_sections, api_sites = (collections.Counter(), collections.defaultdict(set),
                                          collections.Counter())
    for r in root['object'] | root['callback']:
        mine = g.reach([r], stop=all_roots - {r}) & private
        used = set()
        for f in mine:
            for c in g.callees[f]:
                if c not in private:
                    used.add(c)
                    api_sections[c].add(g.fn[f]['unit'])
        for c in used:
            api_users[c] += 1
    for f in private:
        for c in g.callees[f]:
            if c not in private:
                api_sites[c] += 1

    return fclass, private, api_users, api_sections, api_sites


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('callgraph')
    ap.add_argument('--inventory', required=True)
    ap.add_argument('--actions', required=True)
    ap.add_argument('--tracker', default=str(pathlib.Path(__file__).resolve().parents[1]
                                             / 'decomp' / 'functions.tsv'))
    ap.add_argument('--section', type=int, action='append', default=[],
                    help='also list the outside callees of this overlay section')
    ap.add_argument('--top', type=int, default=40)
    ap.add_argument('--json', help='write the per-function classification here')
    args = ap.parse_args(argv)

    g = Graph(json.loads(pathlib.Path(args.callgraph).read_text()))
    inv = json.loads(pathlib.Path(args.inventory).read_text())
    acts = json.loads(pathlib.Path(args.actions).read_text())
    names = load_names(args.tracker)
    root, missing = roots(inv, acts, g.fn)
    fclass, private, api_users, api_sections, api_sites = classify(g, root)

    def name(fid):
        f = g.fn[fid]
        file, _, off = f['ident'].partition(':')
        off = int(off.split()[0], 16)
        sec = '' if f['section'] is None else str(f['section'])
        return names.get((file, off, sec), '')

    print('# Call-graph classification\n')
    print('functions %d, resolved edges %d, trampolines %d' % (
        len(g.fn), sum(len(v) for v in g.callees.values()), len(g.alias)))
    print('roots: ' + ', '.join('%s %d' % (k, len(v)) for k, v in sorted(root.items())))
    for k, v in missing.items():
        if v:
            print('  %s roots without a decoded function: %d' % (k, len(v)))
    print('\n## Classes (function count, bytes)\n')
    byc = collections.defaultdict(lambda: [0, 0])
    for fid, c in fclass.items():
        byc[c][0] += 1
        byc[c][1] += g.fn[fid]['bytes']
    for c, (n, b) in sorted(byc.items(), key=lambda x: -x[1][1]):
        print('  %-36s %5d %7d' % (c, n, b))

    print('\n## Units: story-private bytes vs. the rest\n')
    print('  %-12s %5s %7s %7s %7s' % ('unit', 'fn', 'bytes', 'story', 'other'))
    for u in sorted({f['unit'] for f in g.fn.values()}):
        fs = [f for f in g.fn if g.fn[f]['unit'] == u]
        st = sum(g.fn[f]['bytes'] for f in fs if f in private)
        tot = sum(g.fn[f]['bytes'] for f in fs)
        print('  %-12s %5d %7d %7d %7d' % (u, len(fs), tot, st, tot - st))

    print('\n## Story-facing API (direct calls from story-private code)\n')
    print('  %-28s %-30s %5s %4s %5s  %s' % ('function', 'class', 'users', 'sect', 'sites',
                                            'name'))
    for fid in sorted(api_users, key=lambda f: (-api_users[f], f))[:args.top]:
        print('  %-28s %-30s %5d %4d %5d  %s' % (
            g.fn[fid]['ident'], fclass[fid], api_users[fid], len(api_sections[fid]),
            api_sites[fid], name(fid)))
    print('  (%d API functions in total)' % len(api_users))

    for sec in args.section:
        unit = 'ovl-%03d' % sec
        mine = [f for f in g.fn if g.fn[f]['unit'] == unit]
        print('\n## Section %d: %d functions (%d story-private), outside callees\n' % (
            sec, len(mine), sum(1 for f in mine if f in private)))
        out = collections.Counter()
        for f in mine:
            for c in g.callees[f]:
                if g.fn[c]['unit'] != unit:
                    out[c] += 1
        for c, n in sorted(out.items(), key=lambda x: (-api_users[x[0]], x[0])):
            print('  %-28s %-30s users %4d  %s' % (g.fn[c]['ident'], fclass[c],
                                                   api_users[c], name(c)))

    if args.json:
        pathlib.Path(args.json).write_text(json.dumps([
            dict(id=fid, ident=g.fn[fid]['ident'], cls=fclass[fid], story_private=fid in private,
                 api_users=api_users[fid], name=name(fid),
                 callees=sorted(g.fn[c]['ident'] for c in g.callees[fid]))
            for fid in sorted(g.fn)], indent=1) + '\n')
    return 0


if __name__ == '__main__':
    sys.exit(main())
