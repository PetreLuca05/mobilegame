#!/usr/bin/env python3
"""Fix prop / slot placement failures found by check.py's placement check, by moving the object to the nearest valid
spot and rewriting its T.prop / T.slot line in tools/book/topics/*.py.
  placeprops.py PACK [--dry]
Wall-mounted kinds go flush against a wall and face into the room (same wall and spot first); free kinds keep their yaw
and move to the nearest spot that passes every rule. Run compile.py afterwards (make.sh does)."""
import glob, importlib.util, json, math, os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))


def load_check(pack):
    spec = importlib.util.spec_from_file_location('gastro_check', os.path.join(pack, 'check.py'))
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


def main(pack, dry=False):
    c = load_check(pack)
    K = json.load(open(os.path.join(pack, 'prop_kinds.json'), encoding='utf-8'))
    rooms, iprops, islots = c.parse_map(open(os.path.join(pack, 'institute.map'), encoding='utf-8').read())
    iprops = [list(p) for p in iprops]
    islots = [list(p) for p in islots]
    for p in iprops:
        p[3] = K['model_to_kind'].get(p[3], p[3])
    moved = {}

    def tup():
        return [tuple(p) for p in iprops], [tuple(p) for p in islots]

    for _round in range(6):
        ip, isl = tup()
        fails = c.placement_check(rooms, ip, isl, K)
        if not fails:
            break
        names = []
        for rid, name, why in fails:
            if name not in names:
                names.append(name)
        for name in names:
            rec = next((p for p in iprops if p[2] == name), None) or next((p for p in islots if p[2] == name), None)
            is_prop = rec in iprops
            room = rooms[rec[1]]
            ix0, iz0, ix1, iz1 = room['x0'] + 0.1, room['z0'] + 0.1, room['x1'] - 0.1, room['z1'] - 0.1
            if is_prop:
                k = K['kinds'].get(rec[3], {'w': 0.6, 'd': 0.6, 'mount': 'free'})
            else:
                k = dict(K['slot_kinds'].get(rec[3], {'w': 0.5, 'd': 0.5}), mount='free')
            ox, oz, oyaw = rec[4], rec[5], rec[6]
            cands = []
            if k['mount'] == 'wall':
                hd = k['d'] / 2
                walls = [(0, None, iz0 + hd), (90, ix0 + hd, None), (180, None, iz1 - hd), (270, ix1 - hd, None)]
                for yaw, fx, fz in walls:
                    hw = k['w'] / 2
                    if fx is None:
                        lo, hi = ix0 + hw, ix1 - hw
                        n = int((hi - lo) / 0.05)
                        pts = [(round(lo + i * 0.05, 2), round(fz, 3)) for i in range(n + 1)]
                    else:
                        lo, hi = iz0 + hw, iz1 - hw
                        n = int((hi - lo) / 0.05)
                        pts = [(round(fx, 3), round(lo + i * 0.05, 2)) for i in range(n + 1)]
                    for x, z in pts:
                        cost = math.hypot(x - ox, z - oz) + (0 if yaw == int(oyaw) % 360 else 1.5)
                        cands.append((cost, x, z, yaw))
            else:
                n_x, n_z = int((ix1 - ix0) / 0.05), int((iz1 - iz0) / 0.05)
                for i in range(n_x + 1):
                    for j in range(n_z + 1):
                        x, z = round(ix0 + i * 0.05, 2), round(iz0 + j * 0.05, 2)
                        cands.append((math.hypot(x - ox, z - oz), x, z, int(oyaw)))
            cands.sort()
            # cheap rules first: inside, wall clearance, door approach, overlap
            ip, isl = tup()
            others = [o for o in c.room_objects(room, rec[1], ip, isl, K) if o[0] != name]
            appr = []
            for dx, dz, dw, side in room['doors']:
                if side in 'NS':
                    zz = dz + (1.0 if abs(dz - room['z0']) < abs(dz - room['z1']) else -1.0)
                    appr.append((dx - dw / 2, min(dz, zz), dx + dw / 2, max(dz, zz)))
                else:
                    xx = dx + (1.0 if abs(dx - room['x0']) < abs(dx - room['x1']) else -1.0)
                    appr.append((min(dx, xx), dz - dw / 2, max(dx, xx), dz + dw / 2))
            clear = 0.0 if k['mount'] == 'wall' else 0.3

            def cheap(x, z, yaw):
                rc = c.footprint(x, z, yaw, k['w'], k['d'])
                if min(rc[0] - ix0, ix1 - rc[2], rc[1] - iz0, iz1 - rc[3]) < clear - 0.001:
                    return False
                if any(c.overlap(rc, a) for a in appr):
                    return False
                return not any(c.overlap(rc, o[1]) for o in others)
            done = False
            room_names = {o[2] for o in iprops + islots if o[1] == rec[1]} - {name}
            base = set(c.placement_check(rooms, ip, isl, K, only=room_names)) if room_names else set()
            for cost, x, z, yaw in [t for t in cands if cheap(t[1], t[2], t[3])][:60]:
                rec[4], rec[5], rec[6] = x, z, yaw
                ip, isl = tup()
                if not c.placement_check(rooms, ip, isl, K, only={name}):
                    # must not break the room's other objects
                    if not room_names or set(c.placement_check(rooms, ip, isl, K, only=room_names)) <= base:
                        done = True
                        break
            if done:
                moved[name] = (rec[1], ox, oz, oyaw, rec[4], rec[5], rec[6])
            else:
                rec[4], rec[5], rec[6] = ox, oz, oyaw
                print(f'  could not place {name} in {rec[1]}')
    ip, isl = tup()
    left = c.placement_check(rooms, ip, isl, K)
    print(f'moved {len(moved)}; failures left {len(left)}')
    for f in left:
        print('  ', f)
    if dry:
        return moved, left
    files = glob.glob(os.path.join(HERE, 'topics', '*.py'))
    for name, (rid, ox, oz, oyaw, x, z, yaw) in moved.items():
        pat = re.compile(r"(T\.(?:prop|slot)\([^,]+,\s*[^,]+,\s*'" + re.escape(name) + r"',\s*'[^']+',\s*)(-?[\d.]+),\s*(-?[\d.]+),\s*(-?\d+)")
        hit = 0
        for f in files:
            s = open(f, encoding='utf-8').read()
            s2, n = pat.subn(lambda m: f'{m.group(1)}{x:g}, {z:g}, {yaw}', s)
            if n:
                open(f, 'w', encoding='utf-8').write(s2)
                hit += n
        if hit != 1:
            print(f'  {name}: {hit} source lines rewritten (expected 1)')
    json.dump({n: {'room': v[0], 'from': v[1:4], 'to': v[4:7]} for n, v in moved.items()},
              open(os.path.join(pack, 'placement_moves.json'), 'w', encoding='utf-8'), indent=1)
    return moved, left


if __name__ == '__main__':
    main(sys.argv[1], '--dry' in sys.argv)
