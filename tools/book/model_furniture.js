#!/usr/bin/env node
// Every piece of furniture the "Gastro Institute 3D" page builds, as data. The page's own geometry code runs under Node
// with piece markers added to this copy only (the page is not changed); every triangle the page puts into a room or a
// corridor must fall inside a marked piece, or the export fails. Doors: every door leaf with its hinge and swing.
//   node model_furniture.js PAGE.html OUT.json [--map MAP]   (--map: rewrite the furniture section of MAP from the page)
const fs = require('fs'), vm = require('vm');
const [page, out] = process.argv.slice(2, 4), mapArg = process.argv.indexOf('--map');
const html = fs.readFileSync(page, 'utf8'), i0 = html.indexOf('<script>var ROOMS='), i1 = html.indexOf('</script>', i0);
let src = html.slice(i0 + 8, i1);
// [anchor, text inserted before it] - each anchor must occur exactly once in the page's code
const P = [
 ["mb.cyl(L.P(awayU(r,w,-w/2+.5,.2),0,-3.6)", "PC(mb,'bin');"], ["L.b(mb,w/2-.13,-1.2,1.3,.03,.8,1.1,COL.white)", "PC(mb,'wall_poster');"],
 ["L.b(mb,-w/2+.13,-1.5,1.9,.03,.4,.4,COL.white)", "PC(mb,'clock');"], ["L.b(mb,-w/2+.4,-2.6,0,.55,.85,.5,COL.white)", "PC(mb,'sink');"],
 ["L.b(mb,u+q*-.75,3.3,0,.04,1.8,.04,COL.steel)", "PC(mb,'iv_pole');"], ["L.b(mb,u+q*-.62,1.2,0,.03,2,2.6,COL.blanket2)", "PC(mb,'bed_curtain');"],
 ["L.b(mb,-w/2+1+i*1.6,3.2,0,1.3,2,.05,COL.white)", "PC(mb,'wc_cubicle');"], ["L.b(mb,w/2-.5,-1,0,.5,.85,2,COL.white)", "PC(mb,'wc_basin');"],
 ["for(i=0;i<8;i++)L.b(mb,0,-3+i*.8,0,2.4", "PC(mb,'stairs');"], ["L.b(mb,0,.5,.4,w-4,.08,.5,COL.wood)", "PC(mb,'bench_seat');"],
 ["L.b(mb,q,-1,.4,.5,.08,1.8,COL.wood)", "PC(mb,'bench_seat');"], ["L.b(mb,0,-2.5,0,2.4,1,.7,COL.white)", "PC(mb,'nurse_counter');"],
 ["L.b(mb,-w/2+.25,0,0,.05,2.2,6,COL.blanket2)", "PC(mb,'bed_curtain');"], ["L.b(mb,-w/2+1.5+i*2.5,3,0,.9,.5,1.5,COL.scrub)", "PC(mb,'infusion_chair');"],
 ["L.b(mb,0,.5,0,.5,.8,.5,COL.steel)", "PC(mb,'or_table');"], ["mb.cyl(L.P(.3,2.6,.5)", "PC(mb,'or_lamp');"],
 ["L.b(mb,-2.2,1.6,0,.7,1.5,.6,COL.steel)", "PC(mb,'anaesthesia_cart');"], ["L.b(mb,2.3,-.5,0,1.2,.9,.6,COL.steel)", "PC(mb,'instrument_table');"],
 ["L.b(mb,-w/2+.5,-2.5+i*2.4,0,.6,.9,1.6,COL.steel)", "PC(mb,'scrub_sink');"], ["L.b(mb,w/2-.5,2.5,0,.6,1.8,1.6,COL.white)", "PC(mb,'cupboard');"],
 ["L.b(mb,0,.6,0,.7,.7,2,COL.steel)", "PC(mb,'exam_couch');"], ["L.b(mb,0,3.8,0,w-1,.9,.7,COL.white)", "PC(mb,'lab_bench');"],
 ["L.b(mb,0,.3,0,2.6,.9,1.1,COL.white)", "PC(mb,'lab_island');"], ["L.b(mb,w/2-.5,-1,0,.7,1.9,.9,COL.steel)", "PC(mb,'tall_cabinet');"],
 ["L.b(mb,0,-.5,0,w-2,1.05,.7,COL.wood)", "PC(mb,'dispensing_counter');"], ["L.b(mb,.8,-.5,1.11,.4,.3,.3,COL.screen)", "PC(mb,'counter_screen');"],
 ["L.b(mb,0,3.55,2.55,.9,.3,.06,COL.green)", "PC(mb,'pharmacy_sign');"], ["mb.cyl(L.P(-1,0,2),1.3,1.3,1.1,14,COL.white,COL.dark)", "PC(mb,'ct_scanner');"],
 ["L.b(mb,2,4.1,1.2,1.6,1,.06,COL.screen)", "PC(mb,'wall_screen');"], ["L.b(mb,1.2,4.1,1.2,1.6,1,.06,COL.screen)", "PC(mb,'wall_screen');"],
 ["L.b(mb,-1.5,1,0,.7,.7,2,COL.steel)", "PC(mb,'exam_couch');"], ["L.b(mb,0,4.1,1,4.5,1.6,.06,COL.screen)", "PC(mb,'wall_screen');"],
 ["L.b(mb,2.6,3,0,.7,1.1,.5,COL.wood)", "PC(mb,'lectern');"], ["L.b(mb,0,1.6-i*1.15,0,w-3.2,.25+i*.18,1.1,COL.wood2)", "PC(mb,'riser');"],
 ["L.b(mb,-2.5+j,1.5-i*1.15,yy,.45,.45,.45,cc)", "PC(mb,'auditorium_seat');"], ["L.b(mb,-3.5,-1.5,0,4,1.05,.8,COL.wood)", "PC(mb,'reception_desk');"],
 ["L.b(mb,-5,3.4,0,3,2.2,.12,COL.screen)", "PC(mb,'info_screen');"], ["L.b(mb,5,3.4,0,3,2.2,.12,COL.screen)", "PC(mb,'info_screen');"],
 ["L.b(mb,q[0],q[1],0,1.6,.5,1.6,COL.stone)", "PC(mb,'planter_tree');"], ["L.b(mb,0,0,0,w-3,1.05,.8,COL.white)", "PC(mb,'station_counter');"],
 ["L.b(mb,-.8,0,1.11,.5,.36,.05,COL.screen)", "PC(mb,'desk_screen');"], ["L.b(mb,.9,0,1.11,.5,.36,.05,COL.screen)", "PC(mb,'desk_screen');"],
 ["L.b(mb,i*1.8,0,.72,1.6,.06,1,COL.wood)", "PC(mb,'reading_desk');"], ["L.b(mb,i*1.8,.1,.78,.5,.04,.35,COL.white)", "PC(mb,'desk_papers');"],
 ["L.b(mb,0,.5,.72,w-3,.07,1.5,COL.wood)", "PC(mb,'conference_table');"], ["L.b(mb,0,4.1,1,3,1.4,.06,COL.screen)", "PC(mb,'wall_screen');"],
 ["L.b(mb,-3+i*2,2.5-j*1.8,.72,1,.05,.6,COL.wood)", "PC(mb,'exam_desk');"], ["L.b(mb,0,4.1,1,4,1.4,.06,COL.screen)", "PC(mb,'wall_screen');"],
 ["L.b(mb,i*2.5,2,0,2,.5,.8,COL.pot)", "PC(mb,'planter');"], ["L.b(mb,0,-1,.45,2.4,.08,.5,COL.wood)", "PC(mb,'bench_seat');"],
 ["L.b(mb,-w/2+1.1,2.2,0,.75,.65,1.9,COL.steel)", "PC(mb,'exam_couch');"],
 ["at(q,.42,0,.6,.45,.5,col)", "PC(mb,'alcove_seat');"], ["at(lo+.45,1.5,0,.4,.4,.4,COL.pot)", "PC(mb,'plant');"],
 ["at(hi-.5,1.45,0,.4,1,.4,COL.white)", "PC(mb,'cooler');"], ["mb.quad([a.x0,y+.01,a.z0]", "PC(mb,'#');"], ["mb.box(-12.5,y+3,AZ+1.5", "PC(mb,'lift_sign');"]];
// loops written without braces get them, so the marker runs once per piece
const B = [["if(t==='office'||t==='meeting')L.b(mb,0,-.3,0,Math.min(4,w-2.5),.02,2.6,COL[n%2?'c3':'c0']);", "if(t==='office'||t==='meeting'){PC(mb,'rug');L.b(mb,0,-.3,0,Math.min(4,w-2.5),.02,2.6,COL[n%2?'c3':'c0'])}"], ["for(i=0;i<Math.floor(w-1);i++)L.b(mb,-w/2+1+i,3.8,0,.9,1.9,.5,COL[i%2?'c0':'steel']);", "for(i=0;i<Math.floor(w-1);i++){PC(mb,'locker');L.b(mb,-w/2+1+i,3.8,0,.9,1.9,.5,COL[i%2?'c0':'steel'])}"]];
for (const [a, b] of B) { if (src.split(a).length !== 2) throw new Error('page changed: ' + a); src = src.replace(a, b); }
for (const [a, ins] of P) { if (src.split(a).length !== 2) throw new Error('page changed, anchor not found once: ' + a); src = src.replace(a, ins + a); }
const REC = [];                                     // {mb, kind, room, start}
let CUR = '#';
const ctx = {console, Math, module: {exports: {}}};
ctx.PC = function (mb, kind) { REC.push({mb, kind, room: CUR, start: mb.h.length}); };
vm.createContext(ctx);
vm.runInContext(src + ';this.__=module.exports;', ctx);
const H = ['bed', 'chair', 'desk', 'monitor', 'shelf', 'cabinet', 'plant', 'trolleyTower'];
for (const n of H) { const f = ctx[n]; ctx[n] = function (mb) { ctx.PC(mb, n === 'trolleyTower' ? 'trolley_tower' : n); f.apply(null, arguments); ctx.PC(mb, '?'); }; }
const REN = {screen: 'privacy_screen', board: 'whiteboard_stand', cart: 'supply_cart', model: 'anatomy_model', lightbox: 'lightbox_stand', table: 'side_table', scopes: 'scope_cabinet'};
for (const n of Object.keys(ctx.PROP)) { const f = ctx.PROP[n]; ctx.PROP[n] = function (mb) { ctx.PC(mb, REN[n] || n); f.apply(null, arguments); ctx.PC(mb, '?'); }; }
const ci = ctx.corridorItem, KC = ['corridor_bench', 'corridor_plant', 'extinguisher', 'corridor_chair', 'notice_poster', 'gurney'];
ctx.corridorItem = function (mb, k) { ctx.PC(mb, KC[k]); ci.apply(null, arguments); ctx.PC(mb, '#'); };
const fb = ctx.furnish; ctx.furnish = function (mb, r) { CUR = r.id; ctx.PC(mb, '?'); fb(mb, r); ctx.PC(mb, '#'); CUR = '#'; };
const bc = ctx.buildCore; ctx.buildCore = function (mb, f) { CUR = 'corridor' + f; ctx.PC(mb, '#'); bc(mb, f); ctx.PC(mb, '#'); CUR = '#'; };
const ROOMS = ctx.ROOMS, RB = {}; ROOMS.forEach(r => RB[r.id] = r);
const FAC = {S: 0, N: 180, E: 90, W: 270};           // furniture faces the door: door south -> faces +z (yaw 0)
const pieces = [], bad = [], doors = [];
for (let f = 0; f < 4; f++) {
  REC.length = 0;
  const b = ctx.buildFloor(f), y0 = f * ctx.FH;
  const recs = REC.filter(r => r.mb === b.props || r.mb === b.shell);
  for (const mb of [b.props, b.shell]) {
    const L = recs.filter(r => r.mb === mb);
    L.forEach((r, k) => {
      const end = k + 1 < L.length ? L[k + 1].start : mb.h.length;
      if (end <= r.start || r.kind === '#') return;
      if (r.kind === '?') { bad.push(`storey ${f} ${r.room}: ${end - r.start} triangles outside any marked piece`); return; }
      let x0 = 1e9, x1 = -1e9, z0 = 1e9, z1 = -1e9, ymax = -1e9;
      for (let t = r.start * 9; t < end * 9; t += 3) { const x = mb.p[t], y = mb.p[t + 1], z = mb.p[t + 2]; x0 = Math.min(x0, x); x1 = Math.max(x1, x); z0 = Math.min(z0, z); z1 = Math.max(z1, z); ymax = Math.max(ymax, y); }
      pieces.push({f, room: r.room, kind: r.kind, x0, x1, z0, z1, h: ymax - y0});
    });
  }
  b.doors.forEach(d => doors.push({f, room: d.room, x: d.x, z: d.z, len: d.len, yaw: (Math.round(Math.atan2(d.cx, d.cz) * 180 / Math.PI) + 360) % 360, nx: d.nx, nz: d.nz}));
}
if (bad.length) { console.error(bad.join('\n')); process.exit(1); }
const r2 = v => Math.round(v * 100) / 100, cnt = {}, outRecs = [];
for (const p of pieces) {
  const room = RB[p.room], yaw = room ? FAC[room.door] : 0, cx = (p.x0 + p.x1) / 2, cz = (p.z0 + p.z1) / 2;
  const ex = p.x1 - p.x0, ez = p.z1 - p.z0, w = yaw % 180 === 0 ? ex : ez, d = yaw % 180 === 0 ? ez : ex;
  let mount = 'free';
  if (p.h < 0.06) mount = 'floor';
  else if (room && Math.min(p.x0 - room.x0, room.x1 - p.x1, p.z0 - room.z0, room.z1 - p.z1) < 0.25) mount = 'wall';
  const key = p.room + '.' + p.kind; cnt[key] = (cnt[key] || 0) + 1;
  outRecs.push({id: key + '.' + cnt[key], f: p.f, room: p.room, kind: p.kind, x: r2(cx), z: r2(cz), yaw, w: r2(w), d: r2(d), h: r2(p.h), mount});
}
doors.forEach(d => { const key = d.room + '.door_leaf'; cnt[key] = (cnt[key] || 0) + 1;
  outRecs.push({id: key + '.' + cnt[key], f: d.f, room: d.room, kind: 'door_leaf', x: r2(d.x), z: r2(d.z), yaw: d.yaw, w: r2(d.len), d: 0.05, h: 2.15, mount: 'door', opens: [d.nx, d.nz]}); });
fs.writeFileSync(out, JSON.stringify({note: 'furniture of the institute model page, exported by tools/book/model_furniture.js', pieces: outRecs}, null, 0));
const kinds = new Set(outRecs.map(r => r.kind));
console.log(`furniture pieces ${outRecs.length - doors.length} + door leaves ${doors.length}; kinds ${kinds.size}`);
if (mapArg > 0) {                                   // write the records into the map (the file is the source of truth)
  const mp = process.argv[mapArg + 1], M = fs.readFileSync(mp, 'utf8'), MARK = '# ---- furniture (institute model) ----';
  const head = M.split(MARK)[0].replace(/\s+$/, '');
  const lines = [head, '', MARK,
    '# prop <floor> <room> <id> <kind> <x> <z> <yaw> <w> <d> <h> <wall|free|floor|door> [opens <nx> <nz>]',
    '#   every piece of furniture, one record each: x z = footprint centre (door_leaf: the hinge), yaw 0 faces +z (south),',
    '#   w across the facing direction, d along it, h height (m); the 3D page must place exactly these (check.py compares)'];
  for (const r of outRecs) lines.push(`prop ${r.f} ${r.room} ${r.id} ${r.kind} ${r.x} ${r.z} ${r.yaw} ${r.w} ${r.d} ${r.h} ${r.mount}` + (r.opens ? ` opens ${r.opens[0]} ${r.opens[1]}` : ''));
  fs.writeFileSync(mp, lines.join('\n') + '\n');
  console.log('wrote the furniture section of ' + mp);
}
