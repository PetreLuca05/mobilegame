#!/usr/bin/env python3
"""Write PACK/ENGINE_PROMPT.md (Phase F hand-over): the Claude Code prompt for the engine agent, with the measured figures.
  engine_prompt.py PACK MGPACK
Sizes come from the pack itself: the header is read here, and the reference reader (PACK/mgpack_read.c) is compiled and run
for the string / script measurements, so the prompt and check.py report the same numbers."""
import collections, json, os, re, shutil, struct, subprocess, sys, tempfile

TABLES = ['strings', 'rooms', 'topics', 'atoms', 'facts', 'items', 'opts', 'refs', 'scripts', 'steps', 'props', 'slots', 'figlabels', 'furniture']
HOLDS = {'strings': 'NUL-terminated UTF-8, addressed by u32 offsets', 'rooms': 'map room id, name, storey, rectangle',
         'topics': 'id, title, room, chapter, step; fact / item / script ranges', 'atoms': 'printed text unit: page, topic, kind, layer',
         'facts': 'claim, atom, topic, page, type, flags (needs_review, unverified_ocr)',
         'items': 'stem, explanation, review note, type, flags, option / fact-ref ranges, correct mask',
         'opts': 'options, gaps, card answers, sort / match / order / label entries', 'refs': 'u32: item → facts, teach step → atoms',
         'scripts': 'id, title, topic, room, kind, required, part / parts, prop / slot / npc, step range',
         'steps': 'stage (predict / teach / check), flags, item or atom-ref range', 'props': 'iprop: id, kind, label, room, furniture row (on_model) or none, storey, yaw, x, z',
         'slots': 'islot: id, kind, room, storey, yaw, x, z', 'figlabels': 'figure label text + box on the 200-dpi crop',
         'furniture': 'every furniture piece and door leaf of institute.map: id, kind, room, storey, mount, yaw, x, z, w, d, h'}


def measure(pack, mgpack):
    cc = shutil.which('cc') or shutil.which('gcc') or shutil.which('clang')
    with tempfile.TemporaryDirectory() as td:
        exe = os.path.join(td, 'r')
        subprocess.run([cc, '-std=c11', '-O1', '-Wall', '-Wextra', '-Werror', '-include', mgpack + '.h',
                        os.path.join(pack, 'mgpack_read.c'), '-o', exe], check=True)
        r = subprocess.run([exe, mgpack], capture_output=True, text=True, check=True)
    return dict(l.split('=', 1) for l in r.stdout.splitlines() if '=' in l)


def main(pack, mgpack):
    J = lambda n: [json.loads(l) for l in open(os.path.join(pack, n), encoding='utf-8')]
    facts, items = J('facts.jsonl'), J('items.jsonl')
    scripts = json.load(open(os.path.join(pack, 'scripts.json'), encoding='utf-8'))['scripts']
    mp = open(os.path.join(pack, 'institute.map'), encoding='utf-8').read()
    K = json.load(open(os.path.join(pack, 'prop_kinds.json'), encoding='utf-8'))
    props = re.findall(r'^iprop\s+(-?\d+)\s+(\S+)\s+(\S+)\s+(\S+)', mp, re.M)
    slots = re.findall(r'^islot\s+(-?\d+)\s+(\S+)\s+(\S+)\s+(\S+)', mp, re.M)
    figs = json.load(open(os.path.join(pack, 'fig', 'figures.json'), encoding='utf-8'))
    data = open(mgpack, 'rb').read()
    magic, version, nf, ni = struct.unpack_from('<4sIII', data, 0)
    descs = [struct.unpack_from('<III', data, 16 + 12 * i) for i in range(16)]
    kv = measure(pack, mgpack)
    size = len(data)
    types = collections.Counter(i['type'] for i in items)
    kinds = collections.Counter(s['kind'] for s in scripts)
    per_storey = collections.Counter(int(p[0]) for p in props)
    kinds_storey = collections.defaultdict(set)
    for p in props:
        kinds_storey[int(p[0])].add(p[3])
    used = collections.Counter(p[3] for p in props)
    new = [k for k in used if K['kinds'][k]['status'] == 'new']
    vis = collections.defaultdict(set)
    for line in mp.splitlines():
        m = re.match(r'(slot|prop)\s+(-?\d+)\s+(\S+)', line)
        if m:
            vis[int(m.group(2))].add(m.group(3))
    for p in props:
        vis[int(p[0])].add(p[3])
    room_floor = {m.group(2): int(m.group(1)) for m in re.finditer(r'^room\s+(-?\d+)\s+(\S+)', mp, re.M)}
    anchors = collections.defaultdict(set)
    for p in props + slots:
        anchors[p[1]].add(p[2])
    for s in scripts:
        anchors[s['room']].add(s.get('prop') or s.get('slot') or s.get('npc') or 'trigger:' + s.get('trigger', ''))
    anch_storey = collections.Counter()
    for r, a in anchors.items():
        anch_storey[room_floor.get(r, -9)] += len(a)
    ia = sorted(len(a) for a in anchors.values())
    groups = collections.Counter(s['group'] for s in scripts if s.get('group'))
    req = [s for s in scripts if s.get('required')]
    pools = [s for s in scripts if not s.get('topic')]
    fmt = lambda v: ', '.join(map(str, v)) if isinstance(v, list) else str(v)
    pools_table = '\n'.join(f'| `{s["id"]}` | {s["kind"]} | {s["room"]} | {s.get("slot") or s.get("prop") or "-"} | '
                            f'{s.get("opens_when", "always")} | chapters {fmt(s["pool"].get("chapters"))}; types {fmt(s["pool"].get("types"))}'
                            + ''.join(f'; {k} {fmt(v)}' for k, v in s['pool'].items() if k not in ('chapters', 'types')) + ' |' for s in pools)
    furn = [m.groups() for m in re.finditer(r'^prop\s+(-?\d+)\s+(\S+)\s+(\S+\.\d+)\s+(\S+)\s+\S+\s+\S+\s+\S+\s+\S+\s+\S+\s+\S+\s+(wall|free|floor|door)', mp, re.M)]
    onm_ids = set(re.findall(r'^iprop\s+-?\d+\s+\S+\s+(\S+)\s.*\son_model\s+\S+\s*$', mp, re.M))
    gof = {k: g for g, ks in K.get('mesh_groups', {}).items() for k in ks}
    vk, vm_ = collections.defaultdict(set), collections.defaultdict(set)
    for fl, room, fid, kind, mount in furn:
        if mount != 'door':
            vk[int(fl)].add('f:' + kind); vm_[int(fl)].add(gof.get(kind, 'f:' + kind))
    for p in props:
        if p[2] not in onm_ids:
            vk[int(p[0])].add('b:' + p[3]); vm_[int(p[0])].add(gof.get('book:' + p[3], 'b:' + p[3]))
    allm = {gof.get(k, 'f:' + k) for _, _, _, k, mnt in furn if mnt != 'door'} | {gof.get('book:' + p[3], 'b:' + p[3]) for p in props if p[2] not in onm_ids}
    tpl = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'engine_prompt_template.md'), encoding='utf-8').read()
    kinds_table = '\n'.join(f'| {k} | {v["status"]} | {v["mount"]} | {v["w"]:.2f} × {v["d"]:.2f} | {used.get(k, 0)} | {v["desc"]} |'
                            for k, v in K['kinds'].items())
    layout = '\n'.join(f'| {i} | {t} | {descs[i][2]} | {descs[i][1]:,} | {descs[i][0]:,} | {HOLDS[t]} |' for i, t in enumerate(TABLES))
    vals = {
        'FACTS': f'{len(facts):,}', 'ITEMS': f'{len(items):,}',
        'ITEM_TYPES': ', '.join(f'{k} {v}' for k, v in types.most_common()),
        'SCRIPTS': f'{len(scripts)}', 'REQ_SCRIPTS': f'{len(req)}', 'NPOOLS': str(len(pools)), 'POOLS_TABLE': pools_table,
        'SCRIPT_KINDS': ', '.join(f'{k} {v}' for k, v in kinds.most_common()),
        'SPLIT': f'{len(groups)} longer required scripts are stored as {sum(groups.values())} parts',
        'PROPS': f'{len(props)}', 'SLOTS': f'{len(slots)}', 'NKINDS': str(len(used)), 'NNEW': str(len(new)),
        'NEW_KINDS': ', '.join(f'**{k}** ({K["kinds"][k]["desc"]})' for k in new) or 'none',
        'KINDS_TABLE': kinds_table, 'LAYOUT_TABLE': layout, 'VERSION': str(version),
        'KINDS_STOREY': ', '.join(f'storey {f}: {len(vis[f])}' for f in sorted(vis)),
        'PROPS_STOREY': ', '.join(f'storey {k}: {per_storey[k]} props / {len(kinds_storey[k])} kinds' for k in sorted(per_storey)),
        'IA_ROOM': f'min {ia[0]}, median {ia[len(ia) // 2]}, max {ia[-1]}',
        'MAX_ANCHORS_STOREY': str(max(anch_storey.values()) if anch_storey else 0),
        'MAX_KINDS_STOREY': str(max((len(v) for v in kinds_storey.values()), default=0)),
        'PACK_BYTES': f'{size:,}', 'PACK_MB': f'{size / 1048576:.2f}',
        'STRING_BYTES': f'{int(kv["string_table_bytes"]):,}', 'NSTRINGS': f'{int(kv["strings"]):,}',
        'LONGEST': f'{int(kv["longest_string_bytes"]):,}', 'BIG_SCRIPT': f'`{kv["largest_script"]}`',
        'BIG_STEPS': kv['largest_script_steps'], 'BIG_BYTES': f'{int(kv["largest_script_string_bytes"]):,}',
        'MAX_STEM': f'{max(len(i["stem"]) for i in items):,}',
        'MAX_OPT': f'{max([len(o["t"]) for i in items for o in i.get("options", [])] + [len(a) for i in items for a in i.get("answers", [])] + [len(i.get("answer", "")) for i in items] + [len(e["t"]) for i in items for e in i.get("entries", [])] + [len(x) for i in items for p in i.get("pairs", []) for x in (p["left"], p["right"])] + [len(x) for i in items for x in i.get("sequence", [])]):,}',
        'MAX_EXPL': f'{max(len(i["explanation"]) for i in items):,}',
        'MAX_ATOM': f'{max(len(a["text"]) for a in J("atoms.jsonl")):,}',
        'FURN_PIECES': f'{sum(1 for x in furn if x[4] != "door"):,}', 'FURN_DOORS': str(sum(1 for x in furn if x[4] == 'door')),
        'FURN_KINDS': str(len({x[3] for x in furn if x[4] != 'door'})),
        'KIND_ROWS': '\n'.join(f'| {f} | {len(vk[f])} | {len(vm_[f])} |' for f in sorted(vk)),
        'MAX_MESH': str(max(len(v) for v in vm_.values())), 'MAX_KINDS': str(max(len(v) for v in vk.values())),
        'SUM_MESH': str(sum(len(v) for v in vm_.values())), 'SUM_KINDS': str(sum(len(v) for v in vk.values())),
        'MESH_GROUPS': '; '.join(f'**{g}** ({", ".join(k.replace("book:", "book ") for k in ks)})' for g, ks in K.get('mesh_groups', {}).items()),
        'MESH_TOTAL': str(len(allm)), 'MESH_MB': f'{len(allm) * 300 * 3 * 28 / 1048576:.1f}',
        'INST_KB': f'{(len(furn) + len(props) + len(slots)) * 64 / 1024:.0f}', 'FT_KB': f'{len(furn) * 36 / 1024:.0f}',
        'FURN_COLLIDERS': f'{sum(1 for x in furn if x[4] in ("free", "wall")):,}',
        'BITSET': f'{(len(facts) + 7) // 8}', 'FIGS': str(len(figs)),
        'FIG_MB': f'{len(figs) * 1024 * 1024 * 4 / 1048576 / 2:.0f}',
    }
    M = json.load(open(os.path.join(pack, 'missions.json'), encoding='utf-8')) if os.path.exists(os.path.join(pack, 'missions.json')) else {'people': [], 'cast': [], 'missions': []}
    ppl = M.get('people', [])
    per = [sum(1 for x in ppl if x['f'] == f) for f in range(4)]
    walk = [sum(1 for x in ppl if x['f'] == f and x['mode'] == 'walk') for f in range(4)]
    vals.update({'PEOPLE': str(len(ppl)), 'CAST_N': str(len(M.get('cast', []))), 'BG_N': str(sum(1 for x in ppl if not x['cast'])),
                 'WALKERS': str(sum(walk)), 'STATIC_N': str(len(ppl) - sum(walk)), 'PEOPLE_MAX': str(max(per) if per else 0),
                 'PEOPLE_ROWS': '\n'.join(f'| {f} | {per[f]} | {sum(1 for x in ppl if x["f"] == f and x["cast"])} | {walk[f]} |' for f in range(4)),
                 'DLG_LINES': f'{int(kv.get("dialogue_lines", 0)):,}', 'DLG_BYTES': f'{int(kv.get("dialogue_bytes", 0)):,}',
                 'DLG_LONGEST': kv.get('longest_dialogue_line_bytes', '0'), 'CASE_BYTES': kv.get('case_state_bytes', '0'),
                 'PEOPLE_KB': f'{len(ppl) * 44 / 1024:.1f}', 'NPC_TOTAL': str(len(ppl))})
    nvp = os.path.join(pack, 'model_nav.json')
    NVJ = json.load(open(nvp, encoding='utf-8')) if os.path.exists(nvp) else {'doors': [], 'exempt': []}
    dw = [d.get('cells_across', 0) for d in NVJ['doors'] if d['room'] not in set(NVJ.get('exempt', []))] or [0]
    vals.update({'DOOR_MIN': str(min(dw)), 'DOOR_MAX': str(max(dw)), 'DOORS_CHECKED': str(len(dw))})
    for k, v in vals.items():
        tpl = tpl.replace('{' + k + '}', v)
    left = sorted(set(re.findall(r'\{[A-Z][A-Z_]+\}', tpl)) - {'{MG_GEN_DIR}'})
    if left:
        sys.exit(f'unfilled placeholders: {left}')
    open(os.path.join(pack, 'ENGINE_PROMPT.md'), 'w', encoding='utf-8').write(tpl)
    print('ENGINE_PROMPT.md', len(tpl), 'chars;', vals['FACTS'], 'facts,', vals['ITEMS'], 'items,', vals['PACK_BYTES'], 'bytes pack')


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
