#!/usr/bin/env node
// Rasterise the furniture of the institute model (the "Gastro Institute 3D" page's own geometry code, saved as
// content/src/institute_model.js) into a 0.1 m occupancy grid per storey: every cell under a furniture triangle between
// 6 cm and 2.6 m above the floor (rugs are walked on). check.py's placement check treats these cells as taken.
//   node model_occupancy.js content/src/institute_model.js content/gastro/model_occupancy.json
const fs = require('fs');
const M = require(require('path').resolve(process.argv[2]));
const X0 = -26, Z0 = -18, NX = 520, NZ = 360, C = 0.1;
const out = {note: 'occupancy of the institute model furniture: 0.1 m cells, x from -26, z from -18, 520 x 360 per storey; rows run-length encoded as [start, length] pairs of taken cells', x0: X0, z0: Z0, cell: C, nx: NX, nz: NZ, storeys: []};
for (let f = 0; f < 4; f++) {
  const b = M.buildFloor(f), P = b.props.p, y0 = f * M.FH;
  const g = new Uint8Array(NX * NZ);
  for (let t = 0; t < P.length / 9; t++) {
    let xs = [], ys = [], zs = [];
    for (let v = 0; v < 3; v++) { xs.push(P[t * 9 + v * 3]); ys.push(P[t * 9 + v * 3 + 1]); zs.push(P[t * 9 + v * 3 + 2]); }
    if (Math.max(...ys) < y0 + 0.06 || Math.min(...ys) > y0 + 2.6) continue;   // rugs and floor mats are walked on
    const i0 = Math.max(0, Math.floor((Math.min(...xs) - X0) / C)), i1 = Math.min(NX - 1, Math.floor((Math.max(...xs) - X0 - 1e-6) / C));
    const j0 = Math.max(0, Math.floor((Math.min(...zs) - Z0) / C)), j1 = Math.min(NZ - 1, Math.floor((Math.max(...zs) - Z0 - 1e-6) / C));
    for (let j = j0; j <= j1; j++) for (let i = i0; i <= i1; i++) g[j * NX + i] = 1;
  }
  const rows = [];
  for (let j = 0; j < NZ; j++) { const r = []; let i = 0; while (i < NX) { if (g[j * NX + i]) { let k = i; while (k < NX && g[j * NX + k]) k++; r.push(i, k - i); i = k; } else i++; } rows.push(r); }
  out.storeys.push(rows);
  console.log('storey', f, 'taken cells', g.reduce((a, b) => a + b, 0));
}
fs.writeFileSync(process.argv[3], JSON.stringify(out));
