#!/usr/bin/env node
// The walk grid of the "Gastro Institute 3D" page, from its own geometry, with everything that is drawn solid (furniture,
// book props, decor), and the map checks on it: every door has a free 1.0 m approach on both sides; every room's floor is
// reachable from the entrance (lifts join the storeys); unreachable pockets are allowed only behind a counter.
//   node model_nav.js PAGE.html PACK OUT.json
// OUT holds the grids (walkable / reachable bitsets per storey, 0.25 m cells) that the page and missions.py use.
const fs = require('fs'), vm = require('vm'), path = require('path');
const [page, pack, out] = process.argv.slice(2, 5);
const html = fs.readFileSync(page, 'utf8'), i0 = html.indexOf('<script>var ROOMS='), i1 = html.indexOf('</script>', i0);
const ctx = {console, Math, module: {exports: {}}};
vm.createContext(ctx);
vm.runInContext(html.slice(i0 + 8, i1), ctx);
const FH = ctx.FH, ROOMS = ctx.ROOMS;
const NV = {cs: .25, x0: -34, z0: -27, nx: 272, nz: 256}, N = NV.nx * NV.nz;
const LIFTS = [-14.1, -11.1, 12.5], LZ = 6.35, R = 0.3;                // cabs (page), player radius
const idx = (x, z) => { const i = Math.floor((x - NV.x0) / NV.cs), j = Math.floor((z - NV.z0) / NV.cs); return (i < 0 || j < 0 || i >= NV.nx || j >= NV.nz) ? -1 : j * NV.nx + i; };
const cxz = k => [NV.x0 + (k % NV.nx + .5) * NV.cs, NV.z0 + (Math.floor(k / NV.nx) + .5) * NV.cs];
function sd(px, pz, ax, az, bx, bz) { const dx = bx - ax, dz = bz - az, l = dx * dx + dz * dz; let t = l > 0 ? ((px - ax) * dx + (pz - az) * dz) / l : 0; t = t < 0 ? 0 : t > 1 ? 1 : t; const ex = ax + t * dx - px, ez = az + t * dz - pz; return ex * ex + ez * ez; }
function rastTri(ax, az, bx, bz, cx, cz, G, tol) {
  const cs = NV.cs, h = cs * tol, h2 = h * h, area = (bx - ax) * (cz - az) - (bz - az) * (cx - ax);
  const i0 = Math.max(0, Math.floor((Math.min(ax, bx, cx) - h - NV.x0) / cs)), i1 = Math.min(NV.nx - 1, Math.floor((Math.max(ax, bx, cx) + h - NV.x0) / cs));
  const j0 = Math.max(0, Math.floor((Math.min(az, bz, cz) - h - NV.z0) / cs)), j1 = Math.min(NV.nz - 1, Math.floor((Math.max(az, bz, cz) + h - NV.z0) / cs));
  for (let j = j0; j <= j1; j++) { const pz = NV.z0 + (j + .5) * cs; for (let i = i0; i <= i1; i++) { const px = NV.x0 + (i + .5) * cs; let hit = false;
    if (Math.abs(area) > 1e-7) { const w0 = ((bx - px) * (cz - pz) - (bz - pz) * (cx - px)) / area, w1 = ((cx - px) * (az - pz) - (cz - pz) * (ax - px)) / area; hit = w0 >= 0 && w1 >= 0 && w0 + w1 <= 1; }
    if (!hit) hit = sd(px, pz, ax, az, bx, bz) < h2 || sd(px, pz, bx, bz, cx, cz) < h2 || sd(px, pz, cx, cz, ax, az) < h2;
    if (hit) G[j * NV.nx + i] = 1; } }
}
function rastMB(P, y, fl, bl) { for (let o = 0; o < P.length; o += 9) { const y0 = Math.min(P[o + 1], P[o + 4], P[o + 7]), y1 = Math.max(P[o + 1], P[o + 4], P[o + 7]);
  if (y1 <= y + .12 && y0 >= y - .3 && y1 - y0 < .05) rastTri(P[o], P[o + 2], P[o + 3], P[o + 5], P[o + 6], P[o + 8], fl, .3);
  else if (y1 > y + .2 && y0 < y + 1.8) rastTri(P[o], P[o + 2], P[o + 3], P[o + 5], P[o + 6], P[o + 8], bl, .5); } }
function fillR(G, x0, z0, x1, z1, v) { for (let z = z0 + NV.cs / 2; z < z1; z += NV.cs) for (let x = x0 + NV.cs / 2; x < x1; x += NV.cs) { const i = idx(x, z); if (i >= 0) G[i] = v; } }
function rect(x, z, yaw, w, d) { const q = Math.round(yaw / 90) % 2; const hw = q ? d / 2 : w / 2, hd = q ? w / 2 : d / 2; return [x - hw, z - hd, x + hw, z + hd]; }
// book props (iprop) are furniture in the game too: their footprints are solid
const map = fs.readFileSync(path.join(pack, 'institute.map'), 'utf8'), kinds = JSON.parse(fs.readFileSync(path.join(pack, 'prop_kinds.json'), 'utf8')).kinds;
const iprops = [...map.matchAll(/^iprop\s+(-?\d+)\s+(\S+)\s+(\S+)\s+(\S+)\s+(\S+)\s+(\S+)\s+(\S+)\s+"[^"]*"(\s+on_model\s+\S+)?/mg)]
  .map(m => ({f: +m[1], room: m[2], id: m[3], kind: m[4], x: +m[5], z: +m[6], yaw: +m[7], onm: !!m[8]}));
const furn = JSON.parse(fs.readFileSync(path.join(pack, 'model_furniture.json'), 'utf8')).pieces;
const mbLift = new ctx.MB(); ctx.buildLifts(mbLift);
const mbSite = new ctx.MB(); ctx.buildSite(mbSite);
const WALK = [], BLK = [], DOORS = [];
for (let f = 0; f < 4; f++) {
  const fl = new Uint8Array(N), bl = new Uint8Array(N), y = f * FH, b = ctx.buildFloor(f);
  [b.shell, b.props].concat(b.segs.map(s => s.mb)).forEach(mb => rastMB(mb.p, y, fl, bl));
  if (f === 0) rastMB(mbSite.p, y, fl, bl);
  rastMB(mbLift.p, y, fl, bl);
  for (const p of iprops) if (p.f === f && !p.onm) { const k = kinds[p.kind] || {w: .6, d: .6}; const r = rect(p.x, p.z, p.yaw, k.w, Math.max(k.d, .1)); fillR(bl, r[0], r[1], r[2], r[3], 1); }
  for (const cx of LIFTS) { fillR(bl, cx - 1.05, 4.95, cx + 1.05, 7.75, 0); fillR(fl, cx - 1.05, 4.95, cx + 1.05, 7.75, 1); fillR(bl, cx - 1.25, 4.8, cx + 1.25, 5.0, 1);
    fillR(bl, cx - 1.25, 4.8, cx - 1.05, 7.8, 1); fillR(bl, cx + 1.05, 4.8, cx + 1.25, 7.8, 1); fillR(bl, cx - .85, 7.6, cx + .85, 8.7, 0); fillR(fl, cx - .85, 7.6, cx + .85, 8.7, 1); }
  const w = new Uint8Array(N), nx = NV.nx;
  for (let j = 1; j < NV.nz - 1; j++) for (let i = 1; i < nx - 1; i++) { const k = j * nx + i; if (!fl[k]) continue; let ok = 1;
    for (let dj = -1; dj <= 1 && ok; dj++) for (let di = -1; di <= 1; di++) if (bl[k + dj * nx + di]) { ok = 0; break; } w[k] = ok; }
  WALK.push(w); BLK.push(bl);
  b.doors.forEach(d => DOORS.push({f, room: d.room, x: d.x, z: d.z, len: d.len, cx: d.cx, cz: d.cz, nx: d.nx, nz: d.nz}));
}
// reachability from the entrance, lifts joining the storeys
const ent = ROOMS.filter(r => r.f === 0 && r.entrance)[0];
function snap(f, x, z) { if (WALK[f][idx(x, z)]) return idx(x, z); for (let r = 1; r < 20; r++) for (let dj = -r; dj <= r; dj++) for (let di = -r; di <= r; di++) { const k = idx(x + di * NV.cs, z + dj * NV.cs); if (k >= 0 && WALK[f][k]) return k; } return -1; }
const REACH = [0, 1, 2, 3].map(() => new Uint8Array(N)), start = snap(0, (ent.x0 + ent.x1) / 2, (ent.z0 + ent.z1) / 2 - 1.5);
const q = [[0, start]]; REACH[0][start] = 1; const cabDone = {};
while (q.length) { const [f, k] = q.pop(), c = cxz(k);
  LIFTS.forEach((lx, li) => { if (!cabDone[li] && Math.abs(c[0] - lx) < 1.05 && c[1] > 4.95 && c[1] < 7.75) { cabDone[li] = 1; for (let g = 0; g < 4; g++) { const kk = idx(lx, LZ); if (WALK[g][kk] && !REACH[g][kk]) { REACH[g][kk] = 1; q.push([g, kk]); } } } });
  const ki = k % NV.nx, kj = Math.floor(k / NV.nx);
  for (const [a, bb] of [[1, 0], [-1, 0], [0, 1], [0, -1]]) { const ni = ki + a, nj = kj + bb; if (ni < 0 || nj < 0 || ni >= NV.nx || nj >= NV.nz) continue; const n = nj * NV.nx + ni; if (WALK[f][n] && !REACH[f][n]) { REACH[f][n] = 1; q.push([f, n]); } } }
// door approaches: the opening (hinge along the closed leaf) and 1.0 m deep on both sides must be free of furniture
const SKIP = new Set(['door_leaf', 'rug', 'wall_poster', 'clock', 'notice_poster', 'wall_screen', 'lift_sign', 'pharmacy_sign', 'trophy', 'coats', 'info_screen']);
const solid = furn.filter(p => !SKIP.has(p.kind) && p.mount !== 'floor').map(p => ({id: p.id, kind: p.kind, f: p.f, room: p.room, r: rect(p.x, p.z, p.yaw, p.w, p.d)}))
  .concat(iprops.filter(p => !p.onm && (kinds[p.kind] || {}).mount !== 'wall').map(p => { const k = kinds[p.kind] || {w: .6, d: .6}; return {id: p.id, kind: 'book ' + p.kind, f: p.f, room: p.room, r: rect(p.x, p.z, p.yaw, k.w, k.d)}; }));
const doorRep = DOORS.map(d => {
  const ex = d.x + d.cx * d.len, ez = d.z + d.cz * d.len, x0 = Math.min(d.x, ex), x1 = Math.max(d.x, ex), z0 = Math.min(d.z, ez), z1 = Math.max(d.z, ez);
  const zone = [Math.min(x0, x0 + d.nx, x0 - d.nx), Math.min(z0, z0 + d.nz, z0 - d.nz), Math.max(x1, x1 + d.nx, x1 - d.nx), Math.max(z1, z1 + d.nz, z1 - d.nz)];
  const hits = solid.filter(p => p.f === d.f && p.r[0] < zone[2] - .02 && zone[0] < p.r[2] - .02 && p.r[1] < zone[3] - .02 && zone[1] < p.r[3] - .02).map(p => p.id + ' (' + p.kind + ')');
  // walkable cells across the opening: rows through the wall (normal offsets -0.25, 0, +0.25 m); the fewest counts
  let cells = 1e9;
  for (const o of [-.25, 0, .25]) { const seen = new Set(); for (let t = 0; t <= d.len + 1e-6; t += .05) { const k = idx(d.x + d.cx * t + d.nx * o, d.z + d.cz * t + d.nz * o); if (k >= 0 && WALK[d.f][k]) seen.add(k); } cells = Math.min(cells, seen.size); }
  return {f: d.f, room: d.room, x: +d.x.toFixed(2), z: +d.z.toFixed(2), len: d.len, cells_across: cells, zone: zone.map(v => +v.toFixed(2)), blockers: hits};
});
// rooms: walkable floor, reachable floor, pockets (walkable, unreachable); a pocket is allowed only behind a counter
const COUNTER = new Set(['dispensing_counter', 'station_counter', 'nurse_counter', 'reception_desk']);
const roomRep = ROOMS.map(r => {
  const W = WALK[r.f], RR = REACH[r.f], seen = new Uint8Array(N), pockets = []; let cells = 0, reach = 0;
  for (let z = r.z0 + NV.cs / 2; z < r.z1; z += NV.cs) for (let x = r.x0 + NV.cs / 2; x < r.x1; x += NV.cs) { const k = idx(x, z); if (k < 0 || !W[k]) continue; cells++; if (RR[k]) reach++;
    else if (!seen[k]) { const st = [k], pc = []; seen[k] = 1; while (st.length) { const c = st.pop(); pc.push(c); const ci = c % NV.nx, cj = Math.floor(c / NV.nx);
        for (const [a, bb] of [[1, 0], [-1, 0], [0, 1], [0, -1]]) { const n = (cj + bb) * NV.nx + ci + a, p = cxz(n); if (p[0] > r.x0 && p[0] < r.x1 && p[1] > r.z0 && p[1] < r.z1 && W[n] && !RR[n] && !seen[n]) { seen[n] = 1; st.push(n); } } }
      const pts = pc.map(cxz), cnt = solid.filter(p => p.f === r.f && COUNTER.has(p.kind) && pts.some(([px, pz]) => px > p.r[0] - .8 && px < p.r[2] + .8 && pz > p.r[1] - .8 && pz < p.r[3] + .8));
      pockets.push({cells: pc.length, m2: +(pc.length * NV.cs * NV.cs).toFixed(2), behind_counter: cnt.map(p => p.id), at: pts[0].map(v => +v.toFixed(2))}); } }
  return {id: r.id, f: r.f, type: r.type, cells, reach, pockets};
});
const bits = a => { const o = Buffer.alloc(Math.ceil(a.length / 8)); for (let i = 0; i < a.length; i++) if (a[i]) o[i >> 3] |= 1 << (i & 7); return o.toString('base64'); };
const STAIRS = new Set(ROOMS.filter(r => r.stairs).map(r => r.id));   // the stair flight fills a stairwell: the game collides it as one box
const doorsBad = doorRep.filter(d => d.blockers.length && !STAIRS.has(d.room)), roomsBad = roomRep.filter(r => !STAIRS.has(r.id) && (!r.reach || r.pockets.some(p => p.m2 >= .5 && !p.behind_counter.length)));
fs.writeFileSync(out, JSON.stringify({note: 'walk grid of the 3D page (everything drawn is solid), map checks; built by tools/book/model_nav.js',
  grid: {cs: NV.cs, x0: NV.x0, z0: NV.z0, nx: NV.nx, nz: NV.nz, walk: WALK.map(bits), reach: REACH.map(bits)}, lifts: LIFTS, lift_z: LZ, corr: ctx.CORR,
  exempt: ROOMS.filter(r => r.stairs).map(r => r.id),
  doors: doorRep, rooms: roomRep, doors_blocked: doorsBad.length, rooms_failing: roomsBad.map(r => r.id),
  doors_narrow: doorRep.filter(d => d.cells_across < 2 && !STAIRS.has(d.room)).map(d => d.room + ' ' + d.x + ',' + d.z)}));
const cw = doorRep.filter(d => !STAIRS.has(d.room)).map(d => d.cells_across), byLen = {};
doorRep.filter(d => !STAIRS.has(d.room)).forEach(d => { const k = d.len.toFixed(2); byLen[k] = byLen[k] || [1e9, 0]; byLen[k][0] = Math.min(byLen[k][0], d.cells_across); byLen[k][1]++; });
console.log(`model_nav: door openings (stairwells exempt): walkable cells across min ${Math.min(...cw)}, max ${Math.max(...cw)}; ${cw.filter(c => c < 2).length} doors with fewer than 2; by leaf width ` +
  Object.keys(byLen).sort().map(k => `${k} m: min ${byLen[k][0]} (${byLen[k][1]} doors)`).join(', '));
console.log(`model_nav: ${DOORS.length} doors, ${doorsBad.length} without a free 1.0 m approach; ${ROOMS.length} rooms, ${roomsBad.length} with floor not reachable from the entrance (stairwells exempt: ${STAIRS.size})`);
for (const d of doorsBad) console.log(`  door ${d.room} (storey ${d.f}) at ${d.x},${d.z}: ${d.blockers.join(', ')}`);
for (const r of roomsBad) console.log(`  room ${r.id}: reachable ${r.reach} / ${r.cells} cells; pockets ${r.pockets.filter(p => !p.behind_counter.length).map(p => p.m2 + ' m2 at ' + p.at).join('; ')}`);
