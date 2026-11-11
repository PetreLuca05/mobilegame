#!/usr/bin/env python3
"""mapc: institute.map (MAP-2 text) -> institute.mapb (build time, run by CMake).

The game never parses the text: it reads fixed-size little-endian records in
place (game/imap.h holds the matching structs and checks every size).

Inputs: the map, prop_kinds.json (book prop kinds) and institute_geometry.js
(only its TOPICS line: topic -> chapter, for the door plaques' colours).

Output ("MGMP" version 1):
    magic u32 | version u32 | table_count u32 | pad u32
    table_count x {offset u32, count u32, record_size u32}
    tables, each 8-byte aligned; table 0 = strings (NUL-terminated UTF-8,
    a string field is a u32 offset into it, 0 = empty)

Any malformed or unknown line, a record that refers to something missing, or
a count that is out of range fails the build (exit 1, the line printed).

usage: mapc.py institute.map prop_kinds.json institute_geometry.js OUT.mapb
"""
import json
import re
import struct
import sys

VERSION = 1

# --- enums shared with game/imap.h (same order; the C loader compares the names) ---
ROOM_TYPES = ['exam', 'lab', 'ward_room', 'icu_bay', 'or', 'scrub', 'sterile', 'recovery', 'pharmacy', 'meeting',
              'office', 'reception', 'treatment_bay', 'nurse_station', 'toilet', 'store', 'terrace', 'imaging',
              'waiting', 'triage']
FURN_KINDS = ['alcove_seat', 'anaesthesia_cart', 'anatomy_model', 'aquarium', 'auditorium_seat', 'bed', 'bed_curtain',
              'bench_seat', 'bin', 'cabinet', 'centrifuge', 'chair', 'clock', 'coats', 'conference_table', 'cooler',
              'corridor_bench', 'corridor_chair', 'corridor_plant', 'counter_screen', 'crash', 'ct_scanner',
              'cupboard', 'dental', 'desk', 'desk_papers', 'desk_screen', 'dispensing_counter', 'door_leaf',
              'exam_couch', 'exam_desk', 'extinguisher', 'fridge', 'gurney', 'info_screen', 'infusion_chair',
              'instrument_table', 'iv_pole', 'lab_bench', 'lab_island', 'lectern', 'lift_sign', 'lightbox_stand',
              'locker', 'monitor', 'notice_poster', 'nurse_counter', 'or_lamp', 'or_table', 'oxygen', 'palm',
              'pharmacy_sign', 'plant', 'planter', 'planter_tree', 'podium', 'printer', 'privacy_screen',
              'reading_desk', 'reception_desk', 'recliner', 'riser', 'rug', 'scale', 'scope_cabinet', 'scrub_sink',
              'shelf', 'side_table', 'sink', 'sofa', 'stairs', 'station_counter', 'supply_cart', 'tall_cabinet',
              'trolley_tower', 'trophy', 'ultrasound', 'vending', 'wall_poster', 'wall_screen', 'wc_basin',
              'wc_cubicle', 'whiteboard_stand']
IPROP_KINDS = ['poster', 'plaque', 'board', 'lightbox', 'shelf', 'cabinet', 'counter', 'bench', 'cart', 'monitor',
               'scope_tower', 'model', 'podium', 'microscope', 'iv_stand']
SLOT_KINDS = ['desk', 'chair', 'bench', 'screen', 'board', 'counter', 'bed', 'or_table', 'post', 'scope', 'table',
              'reading_desk']
ISLOT_KINDS = ['npc_stand', 'npc_seat']
MOUNTS = ['free', 'wall', 'floor', 'door']
SITE_KINDS = ['plot', 'gate', 'road', 'carpark', 'path', 'pond', 'tree', 'bench', 'lamp', 'ambulance', 'helipad']
CORR_KINDS = ['ring', 'alcove', 'deck']
LANDINGS = ['G', '1', '2', '3', 'R']
RING = [(-19, -11, 19, -8), (-19, 8, 19, 11), (-19, -8, -16, 8), (16, -8, 19, 8)]

RF_OPEN, RF_ENTRANCE, RF_AMBULANCE, RF_OPEN_N, RF_STAIRS, RF_LOCKER = 1, 2, 4, 8, 16, 32
BX, BZ = 26.0, 18.0
NONE16 = -1

TABLES = ['strings', 'floors', 'rooms', 'doors', 'windows', 'corridors', 'voids', 'lifts', 'stairs', 'slots', 'islots',
          'iprops', 'props', 'site', 'spawns', 'learn', 'room_types', 'furn_kinds', 'iprop_kinds']

# record layouts (struct format, little endian, no padding beyond what is written here)
FMT = {
    'floors': '<iI',                 # storey, name
    'rooms': '<IIIIffffbBHbBHHH',    # id name type_name role x0 z0 x1 z1 storey type flags chapter pad first_door door_count pad
    'doors': '<fffhbB',              # x z w room storey side
    'windows': '<fffhbB',
    'corridors': '<ffffbBH',         # x0 z0 x1 z1 storey kind pad
    'voids': '<ffffbBH',
    'lifts': '<IffBBBB',             # name x z landing_mask landing_count bed pad
    'stairs': '<ff',
    'slots': '<fffhbB',              # x z yaw room storey kind
    'islots': '<IfffhbBhH',          # id x z yaw room storey kind furniture pad
    'iprops': '<IIfffhbBhH',         # id label x z yaw room storey kind furniture pad
    'props': '<IffffffffhbBBBH',     # id x z yaw w d h open_x open_z room storey kind mount pad pad
    'site': '<BbBBffffff',           # kind storey side pad v0..v5
    'spawns': '<ffbBH',              # x z storey pad pad
    'learn': '<IIIhbB',              # pack kind topic room chapter pad
    'room_types': '<I',
    'furn_kinds': '<I',
    'iprop_kinds': '<I',
}


class MapError(Exception):
    pass


class Strings:
    def __init__(self):
        self.data = bytearray(b'\0')
        self.index = {'': 0}

    def add(self, s):
        if s in self.index:
            return self.index[s]
        off = len(self.data)
        b = s.encode('utf-8')
        if b'\0' in b:
            raise MapError('NUL in a string')
        self.data += b + b'\0'
        self.index[s] = off
        return off


def num(tok, what):
    try:
        v = float(tok)
    except ValueError:
        raise MapError('%s: not a number: %r' % (what, tok))
    if v != v or abs(v) > 1e5:
        raise MapError('%s: out of range: %r' % (what, tok))
    return v


def integer(tok, what, lo, hi):
    try:
        v = int(tok)
    except ValueError:
        raise MapError('%s: not an integer: %r' % (what, tok))
    if v < lo or v > hi:
        raise MapError('%s: %d not in %d..%d' % (what, v, lo, hi))
    return v


TOKEN = re.compile(r'"([^"]*)"|(\S+)')


def tokenize(line):
    """tokens of a line up to an unquoted '#'; quoted strings keep their spaces (returned as ('q', text))"""
    out = []
    pos = 0
    while pos < len(line):
        while pos < len(line) and line[pos].isspace():
            pos += 1
        if pos >= len(line) or line[pos] == '#':
            break
        m = TOKEN.match(line, pos)
        if not m:
            raise MapError('unterminated quote')
        if m.group(1) is not None:
            out.append(('q', m.group(1)))
        else:
            if '"' in m.group(2):
                raise MapError('stray quote in %r' % m.group(2))
            out.append(('w', m.group(2)))
        pos = m.end()
    return out


def words(toks, n_min, n_max, what):
    if not (n_min <= len(toks) <= n_max):
        raise MapError('%s: %d fields, expected %d..%d' % (what, len(toks), n_min, n_max))
    return toks


def plain(tok, what):
    if tok[0] != 'w':
        raise MapError('%s: unexpected quoted string' % what)
    return tok[1]


def quoted(tok, what):
    if tok[0] != 'q':
        raise MapError('%s: expected a quoted string' % what)
    return tok[1]


def compile_map(map_path, kinds_path, js_path):
    kinds = json.load(open(kinds_path, encoding='utf-8'))['kinds']
    for k in kinds:
        if k not in IPROP_KINDS:
            raise MapError('prop_kinds.json: kind %r unknown to mapc / imap.h' % k)
    js = open(js_path, encoding='utf-8').read()
    m = re.search(r'^var TOPICS=(\{.*?\});', js, re.M)
    if not m:
        raise MapError('institute_geometry.js: no TOPICS line')
    topics = {k: v['c'] for k, v in json.loads(m.group(1)).items()}

    S = Strings()
    T = {t: [] for t in TABLES if t != 'strings'}
    rooms = []                # dicts, in file order
    room_ix = {}
    cur_floor = None
    cur_room = None
    stairs_pts = []
    furn_ix = {}

    lines = open(map_path, encoding='utf-8').read().split('\n')
    for ln, raw in enumerate(lines, 1):
        try:
            toks = tokenize(raw)
            if not toks:
                continue
            key = plain(toks[0], 'keyword')
            a = toks[1:]
            if key == 'plot':
                words(a, 4, 4, key)
                T['site'].append((SITE_KINDS.index('plot'), -1, 0, 0) + tuple(num(plain(t, key), key) for t in a) + (0.0, 0.0))
            elif key == 'gate':
                words(a, 5, 5, key)
                st = integer(plain(a[0], key), 'storey', -1, -1)
                side = plain(a[4], key)
                if side not in 'NSEW' or len(side) != 1:
                    raise MapError('gate side %r' % side)
                v = [num(plain(t, key), key) for t in a[1:4]]
                T['site'].append((SITE_KINDS.index('gate'), st, ord(side), 0, v[0], v[1], v[2], 0.0, 0.0, 0.0))
            elif key in ('road', 'path'):
                words(a, 5, 5, key)
                st = integer(plain(a[0], key), 'storey', -1, -1)
                v = [num(plain(t, key), key) for t in a[1:5]]
                T['site'].append((SITE_KINDS.index(key), st, 0, 0) + tuple(v) + (0.0, 0.0))
            elif key == 'carpark':
                words(a, 6, 6, key)
                st = integer(plain(a[0], key), 'storey', -1, -1)
                v = [num(plain(t, key), key) for t in a[1:6]]
                T['site'].append((SITE_KINDS.index(key), st, 0, 0) + tuple(v) + (0.0,))
            elif key == 'pond':
                words(a, 4, 4, key)
                st = integer(plain(a[0], key), 'storey', -1, -1)
                v = [num(plain(t, key), key) for t in a[1:4]]
                T['site'].append((SITE_KINDS.index(key), st, 0, 0) + tuple(v) + (0.0, 0.0, 0.0))
            elif key == 'spawn':
                words(a, 3, 3, key)
                st = integer(plain(a[0], key), 'storey', -1, 4)
                T['spawns'].append((num(plain(a[1], key), key), num(plain(a[2], key), key), st, 0, 0))
            elif key == 'floor':
                words(a, 2, 2, key)
                cur_floor = integer(plain(a[0], key), 'storey', 0, 4)
                if any(f[0] == cur_floor for f in T['floors']):
                    raise MapError('floor %d twice' % cur_floor)
                T['floors'].append((cur_floor, S.add(quoted(a[1], key))))
                cur_room = None
            elif key in ('corr', 'void'):
                words(a, 5, 5, key)
                st = integer(plain(a[0], key), 'storey', 0, 4)
                if st != cur_floor:
                    raise MapError('%s on storey %d outside its floor block' % (key, st))
                r = tuple(num(plain(t, key), key) for t in a[1:5])
                if r[0] >= r[2] or r[1] >= r[3]:
                    raise MapError('empty rectangle')
                if key == 'void':
                    T['voids'].append(r + (st, 0, 0))
                else:
                    kind = 'deck' if st == 4 else ('ring' if r in RING else 'alcove')
                    T['corridors'].append(r + (st, CORR_KINDS.index(kind), 0))
            elif key == 'room':
                words(a, 8, 9, key)
                st = integer(plain(a[0], key), 'storey', 0, 3)
                if st != cur_floor:
                    raise MapError('room outside its floor block')
                rid = plain(a[1], key)
                if rid in room_ix:
                    raise MapError('room %s twice' % rid)
                rtype = plain(a[2], key)
                if rtype not in ROOM_TYPES:
                    raise MapError('room type %r unknown' % rtype)
                rect = [num(plain(t, key), key) for t in a[3:7]]
                if rect[0] >= rect[2] or rect[1] >= rect[3]:
                    raise MapError('empty room rectangle')
                name = quoted(a[7], key)
                flags = 0
                if len(a) == 9:
                    if plain(a[8], key) != 'open':
                        raise MapError('expected "open" after the room name')
                    flags |= RF_OPEN
                cur_room = {'id': rid, 'storey': st, 'type': rtype, 'rect': rect, 'name': name, 'flags': flags,
                            'doors': [], 'topics': [], 'role': '', 'ix': len(rooms)}
                rooms.append(cur_room)
                room_ix[rid] = cur_room['ix']
            elif key in ('door', 'window'):
                words(a, 5, 5, key)
                st = integer(plain(a[0], key), 'storey', 0, 3)
                if cur_room is None or cur_room['storey'] != st:
                    raise MapError('%s outside a room block' % key)
                x, z, w = (num(plain(t, key), key) for t in a[1:4])
                side = plain(a[4], key)
                if side not in ('N', 'S', 'E', 'W'):
                    raise MapError('side %r' % side)
                if w <= 0:
                    raise MapError('width <= 0')
                rec = (x, z, w, cur_room['ix'], st, ord(side))
                if key == 'door':
                    cur_room['doors'].append(len(T['doors']))
                    T['doors'].append(rec)
                else:
                    T['windows'].append(rec)
            elif key == 'slot':
                words(a, 5, 5, key)
                st = integer(plain(a[0], key), 'storey', 0, 3)
                if cur_room is None or cur_room['storey'] != st:
                    raise MapError('slot outside a room block')
                kind = plain(a[1], key)
                if kind not in SLOT_KINDS:
                    raise MapError('slot kind %r unknown' % kind)
                x, z, yaw = (num(plain(t, key), key) for t in a[2:5])
                T['slots'].append((x, z, yaw, cur_room['ix'], st, SLOT_KINDS.index(kind)))
            elif key == 'learn':
                words(a, 4, 5, key)
                st = integer(plain(a[0], key), 'storey', 0, 3)
                if len(a) == 4:  # learn <storey> <room> * <role>
                    rid, pack, kind, topic = plain(a[1], key), plain(a[2], key), plain(a[3], key), ''
                    if pack != '*':
                        raise MapError('learn: 4 fields only for a role (pack *)')
                else:
                    rid, pack, kind, topic = (plain(t, key) for t in a[1:5])
                if rid not in room_ix:
                    raise MapError('learn: room %s unknown' % rid)
                r = rooms[room_ix[rid]]
                if r['storey'] != st:
                    raise MapError('learn: storey differs from its room')
                chap = -1
                if pack == '*':
                    if not r['role']:
                        r['role'] = kind
                elif kind == 'topic':
                    if topic not in topics:
                        raise MapError('learn: topic %s has no chapter in institute_geometry.js' % topic)
                    chap = topics[topic]
                    r['topics'].append(topic)
                T['learn'].append((S.add(pack), S.add(kind), S.add(topic), r['ix'], chap, 0))
            elif key == 'lift':
                words(a, 5, 5, key)
                name = plain(a[0], key)
                x, z = num(plain(a[1], key), key), num(plain(a[2], key), key)
                lands = plain(a[3], key).split(',')
                mask = 0
                for l in lands:
                    if l not in LANDINGS:
                        raise MapError('lift landing %r' % l)
                    mask |= 1 << LANDINGS.index(l)
                use = plain(a[4], key)
                if use not in ('public', 'bed'):
                    raise MapError('lift use %r' % use)
                T['lifts'].append((S.add(name), x, z, mask, len(lands), 1 if use == 'bed' else 0, 0))
            elif key == 'stairs':
                words(a, 2, 2, key)
                p = (num(plain(a[0], key), key), num(plain(a[1], key), key))
                stairs_pts.append(p)
                T['stairs'].append(p)
            elif key == 'prop':
                if a and plain(a[0], key) in ('-1', '4'):
                    # site prop: prop <storey> <kind> x z yaw [p q r]
                    st = integer(plain(a[0], key), 'storey', -1, 4)
                    kind = plain(a[1], key)
                    if kind not in SITE_KINDS:
                        raise MapError('site prop kind %r unknown' % kind)
                    if len(a) not in (5, 8):
                        raise MapError('site prop: %d fields' % len(a))
                    v = [num(plain(t, key), key) for t in a[2:]] + [0.0] * 6
                    T['site'].append((SITE_KINDS.index(kind), st, 0, 0) + tuple(v[:6]))
                    continue
                words(a, 11, 14, key)
                st = integer(plain(a[0], key), 'storey', 0, 3)
                rid, pid, kind = plain(a[1], key), plain(a[2], key), plain(a[3], key)
                if kind not in FURN_KINDS:
                    raise MapError('furniture kind %r unknown' % kind)
                x, z, yaw, w, d, h = (num(plain(t, key), key) for t in a[4:10])
                if yaw not in (0, 90, 180, 270):
                    raise MapError('furniture yaw %g not a multiple of 90' % yaw)
                if w <= 0 or d <= 0 or h < 0:
                    raise MapError('furniture size')
                mount = plain(a[10], key)
                if mount not in MOUNTS:
                    raise MapError('mount %r' % mount)
                ox = oz = 0.0
                if len(a) > 11:
                    if len(a) != 14 or plain(a[11], key) != 'opens':
                        raise MapError('expected "opens nx nz"')
                    ox, oz = num(plain(a[12], key), key), num(plain(a[13], key), key)
                if (mount == 'door') != (kind == 'door_leaf') or (kind == 'door_leaf') != (len(a) == 14):
                    raise MapError('door leaves (and only they) are mount door with "opens"')
                if rid in room_ix:
                    room = room_ix[rid]
                    if rooms[room]['storey'] != st:
                        raise MapError('furniture storey differs from its room')
                elif rid == 'corridor%d' % st:
                    room = NONE16
                else:
                    raise MapError('furniture room %s unknown' % rid)
                if pid in furn_ix:
                    raise MapError('furniture id %s twice' % pid)
                furn_ix[pid] = len(T['props'])
                if kind == 'locker' and room >= 0:
                    rooms[room]['flags'] |= RF_LOCKER
                T['props'].append([S.add(pid), x, z, yaw, w, d, h, ox, oz, room, st, FURN_KINDS.index(kind),
                                   MOUNTS.index(mount), 0, 0])
            elif key in ('iprop', 'islot'):
                st = integer(plain(a[0], key), 'storey', 0, 3)
                rid, pid, kind = plain(a[1], key), plain(a[2], key), plain(a[3], key)
                if rid not in room_ix or rooms[room_ix[rid]]['storey'] != st:
                    raise MapError('%s: room %s unknown on storey %d' % (key, rid, st))
                x, z, yaw = (num(plain(t, key), key) for t in a[4:7])
                rest = a[7:]
                label = ''
                if key == 'iprop':
                    if not rest:
                        raise MapError('iprop without a label')
                    label = quoted(rest[0], key)
                    rest = rest[1:]
                    if kind not in IPROP_KINDS:
                        raise MapError('iprop kind %r unknown' % kind)
                elif kind not in ISLOT_KINDS:
                    raise MapError('islot kind %r unknown' % kind)
                furn = NONE16
                if rest:
                    if len(rest) != 2 or plain(rest[0], key) != 'on_model':
                        raise MapError('expected "on_model <furniture id>"')
                    fid = plain(rest[1], key)
                    if fid not in furn_ix:
                        raise MapError('on_model %s: no such furniture (it must come before)' % fid)
                    furn = furn_ix[fid]
                if key == 'iprop':
                    T['iprops'].append((S.add(pid), S.add(label), x, z, yaw, room_ix[rid], st, IPROP_KINDS.index(kind),
                                        furn, 0))
                else:
                    T['islots'].append((S.add(pid), x, z, yaw, room_ix[rid], st, ISLOT_KINDS.index(kind), furn, 0))
            else:
                raise MapError('unknown record %r' % key)
        except MapError as e:
            raise MapError('%s:%d: %s\n    %s' % (map_path, ln, e, raw))

    # furniture first (the iprop on_model references need the rows) - the map lists furniture before iprops; an
    # on_model to a later row fails above. Room flags from the doors and the stairs.
    for r in rooms:
        x0, z0, x1, z1 = r['rect']
        for di in r['doors']:
            x, z, w, _, _, side = T['doors'][di]
            side = chr(side)
            on = (side == 'N' and abs(z - z0) < 1e-3) or (side == 'S' and abs(z - z1) < 1e-3) or \
                 (side == 'W' and abs(x - x0) < 1e-3) or (side == 'E' and abs(x - x1) < 1e-3)
            if not on:
                raise MapError('room %s: door at %g,%g not on its %s wall' % (r['id'], x, z, side))
            if side == 'S' and z1 == BZ and w > 3:
                r['flags'] |= RF_ENTRANCE
            if side == 'E' and x1 == BX and w > 2:
                r['flags'] |= RF_AMBULANCE
            if side == 'N' and w >= (x1 - x0) - 1e-3:
                r['flags'] |= RF_OPEN_N
        for (sx, sz) in stairs_pts:
            if x0 < sx < x1 and z0 < sz < z1:
                r['flags'] |= RF_STAIRS

    for r in rooms:
        chap = topics[r['topics'][0]] if r['topics'] else -1
        T['rooms'].append((S.add(r['id']), S.add(r['name']), S.add(r['type']), S.add(r['role']), r['rect'][0],
                           r['rect'][1], r['rect'][2], r['rect'][3], r['storey'], ROOM_TYPES.index(r['type']),
                           r['flags'], chap, 0, r['doors'][0] if r['doors'] else 0xFFFF, len(r['doors']), 0))
        # a room's doors are contiguous (they follow its line)
        if r['doors'] and r['doors'] != list(range(r['doors'][0], r['doors'][0] + len(r['doors']))):
            raise MapError('room %s: doors not contiguous' % r['id'])
    for n in ROOM_TYPES:
        T['room_types'].append((S.add(n),))
    for n in FURN_KINDS:
        T['furn_kinds'].append((S.add(n),))
    for n in IPROP_KINDS:
        T['iprop_kinds'].append((S.add(n),))

    # sanity: what the scene needs, within the engine's limits (game/imap.h)
    need = {'rooms': (1, 128), 'doors': (1, 160), 'props': (1, 2048), 'iprops': (0, 512), 'islots': (0, 128),
            'slots': (0, 256), 'lifts': (1, 4), 'floors': (1, 5), 'spawns': (1, 16), 'corridors': (1, 64)}
    for t, (lo, hi) in need.items():
        if not (lo <= len(T[t]) <= hi):
            raise MapError('%d %s, expected %d..%d' % (len(T[t]), t, lo, hi))
    return S, T


def write(out_path, S, T):
    ntab = len(TABLES)
    head = 16 + ntab * 12
    blobs = []
    descs = []
    off = (head + 7) & ~7
    for t in TABLES:
        if t == 'strings':
            data, count, size = bytes(S.data), len(S.data), 1
        else:
            fmt = FMT[t]
            size = struct.calcsize(fmt)
            data = b''.join(struct.pack(fmt, *row) for row in T[t])
            count = len(T[t])
        descs.append((off, count, size))
        blobs.append((off, data))
        off = (off + len(data) + 7) & ~7
    out = bytearray(off)
    struct.pack_into('<4sIII', out, 0, b'MGMP', VERSION, ntab, 0)
    for i, d in enumerate(descs):
        struct.pack_into('<III', out, 16 + i * 12, *d)
    for o, data in blobs:
        out[o:o + len(data)] = data
    with open(out_path, 'wb') as f:
        f.write(out)
    return off


def main():
    if len(sys.argv) != 5:
        print(__doc__.strip().split('\n')[-1], file=sys.stderr)
        return 2
    try:
        S, T = compile_map(sys.argv[1], sys.argv[2], sys.argv[3])
    except (MapError, OSError, ValueError, KeyError) as e:
        print('mapc: %s' % e, file=sys.stderr)
        return 1
    size = write(sys.argv[4], S, T)
    print('mapc: %d rooms, %d doors, %d furniture, %d book props, %d slots + %d NPC points, %d site pieces; %d bytes'
          % (len(T['rooms']), len(T['doors']), len(T['props']), len(T['iprops']), len(T['slots']), len(T['islots']),
             len(T['site']), size))
    return 0


if __name__ == '__main__':
    sys.exit(main())
