#!/usr/bin/env python3
"""§3 storey plans: plan/<storey>.png = room outlines, doors, the institute model's own furniture (grey), every book prop
and NPC point with its kind and script ids; props moved from their authored position (institute_original.map = the map at
commit 33e4990) in orange with a line from where they were. Also plan/moves.json and the list of moves over 1.0 m.
  plans.py PACK"""
import collections, importlib.util, json, math, os, re, sys
from PIL import Image, ImageDraw, ImageFont

PX, PAD = 34, 40
FONT = '/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf'


def main(pack):
    spec = importlib.util.spec_from_file_location('chk', os.path.join(pack, 'check.py'))
    c = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(c)
    src = open(os.path.join(pack, 'institute.map'), encoding='utf-8').read()
    rooms, ips, isl = c.parse_map(src)
    onm = c.on_model_ids(src)
    occ = c.load_occupancy(pack)
    K = json.load(open(os.path.join(pack, 'prop_kinds.json'), encoding='utf-8'))
    orig = {}
    op = os.path.join(pack, 'institute_original.map')
    if os.path.exists(op):
        _, oi, os_ = c.parse_map(open(op, encoding='utf-8').read())
        orig = {p[2]: (p[4], p[5]) for p in oi + os_}
    scripts = json.load(open(os.path.join(pack, 'scripts.json'), encoding='utf-8'))['scripts']
    by_anchor = collections.defaultdict(list)
    for s in scripts:
        a = s.get('prop') or s.get('slot')
        if a:
            by_anchor[a].append(re.sub(r'^s-[a-z_]+?-(?=[a-z0-9])', '', s['id']) if s.get('topic') else s['id'])
    os.makedirs(os.path.join(pack, 'plan'), exist_ok=True)
    f_s, f_m = ImageFont.truetype(FONT, 11), ImageFont.truetype(FONT, 13)
    W, H = int(52 * PX + 2 * PAD), int(36 * PX + 2 * PAD)
    X = lambda x: PAD + (x + 26) * PX
    Z = lambda z: PAD + (z + 18) * PX
    moves = []
    for f in range(4):
        im = Image.new('RGB', (W, H + 60), (250, 249, 246))
        d = ImageDraw.Draw(im)
        g = occ['g'][f]
        for j in range(occ['nz']):
            for i in range(occ['nx']):
                if g[j * occ['nx'] + i]:
                    x, z = occ['x0'] + i * occ['c'], occ['z0'] + j * occ['c']
                    d.rectangle([X(x), Z(z), X(x + occ['c']), Z(z + occ['c'])], fill=(214, 214, 210))
        for rid, r in rooms.items():
            if r['floor'] != f:
                continue
            d.rectangle([X(r['x0']), Z(r['z0']), X(r['x1']), Z(r['z1'])], outline=(60, 70, 80), width=2)
            d.text((X(r['x0']) + 4, Z(r['z0']) + 3), rid, font=f_m, fill=(60, 70, 80))
            for dx, dz, dw, side in r['doors']:
                if side in 'NS':
                    d.line([X(dx - dw / 2), Z(dz), X(dx + dw / 2), Z(dz)], fill=(250, 249, 246), width=5)
                else:
                    d.line([X(dx), Z(dz - dw / 2), X(dx), Z(dz + dw / 2)], fill=(250, 249, 246), width=5)
        for p in [q for q in ips if q[0] == f] + [q + ('',) for q in isl if q[0] == f]:
            fl, rid, pid, kind, x, z, yaw = p[:7]
            k = K['kinds'].get(kind) or K['slot_kinds'].get(kind) or {'w': .5, 'd': .5}
            rc = c.footprint(x, z, yaw, k['w'], k['d'])
            o = orig.get(pid)
            dist = math.hypot(x - o[0], z - o[1]) if o else 0.0
            moved = dist > 0.05
            col = (230, 120, 20) if moved else ((40, 120, 200) if kind.startswith('npc') else (20, 140, 90))
            if pid in onm:
                col = (140, 80, 200)
            if moved:
                d.line([X(o[0]), Z(o[1]), X(x), Z(z)], fill=(230, 120, 20), width=1)
                d.ellipse([X(o[0]) - 3, Z(o[1]) - 3, X(o[0]) + 3, Z(o[1]) + 3], outline=(230, 120, 20))
                moves.append({'id': pid, 'room': rid, 'storey': f, 'from': [o[0], o[1]], 'to': [x, z], 'm': round(dist, 2), 'kind': kind,
                              'on_model': pid in onm})
            d.rectangle([X(rc[0]), Z(rc[1]), X(rc[2]), Z(rc[3])], fill=col)
            fx, fz = math.sin(math.radians(yaw)), math.cos(math.radians(yaw))
            d.line([X(x), Z(z), X(x + fx * .45), Z(z + fz * .45)], fill=(20, 20, 20), width=2)
            lab = f"{kind}: {', '.join(by_anchor.get(pid, [])) or '-'}"
            d.text((X(rc[2]) + 2, Z(rc[1]) - 2), lab, font=f_s, fill=(20, 20, 20))
        d.text((PAD, H + 10), f'Storey {f}: green = book prop, blue = NPC point, violet = on the model\'s own furniture, orange = moved '
               f'(line from the authored position), grey = the model\'s furniture. Tick = facing direction. Labels: kind: script ids.',
               font=f_m, fill=(20, 20, 20))
        im.save(os.path.join(pack, 'plan', f'{f}.png'), optimize=True)
    moves.sort(key=lambda m: -m['m'])
    json.dump(moves, open(os.path.join(pack, 'plan', 'moves.json'), 'w', encoding='utf-8'), indent=1)
    big = [m for m in moves if m['m'] > 1.0]
    print(f'plans written; {len(moves)} props / points moved from their authored position; {len(big)} by more than 1.0 m')
    return moves


if __name__ == '__main__':
    main(sys.argv[1])
