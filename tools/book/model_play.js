/* ===== play mode (BOOK-1 missions): the book through NPCs, on the game's marker and interact systems =====
   A walking player with the game camera (north up, pitch 55, FOV 40, 19 m, spring 8/s + 1.2 m look-ahead), a 0.25 m walk
   grid baked from the model's own triangles, lifts, NPCs placed by room type, markers (diamond, ring, edge icon, floor
   badge), the interact scan (enter 2.0 m, exit 2.5 m, in front of the player) with the bottom-right panel, a dialogue
   card, the mission journal. Missions are the only way into the book: no room lists. */
var MIS=BK&&BK.mis||null,PLAYM=null;
/* ---------- interior: lit (toon, like the rest of the scene), no cast shadows ---------- */
function litInterior(){var lit=function(m){if(!m.isMesh)return;var k=m.userData.kind;if(k&&m.material!==glassMat&&UNLIT[k]&&m.material===UNLIT[k])m.material=MATS[k]||MATS.plain;m.castShadow=false;m.receiveShadow=false;m.userData.cs=false};
  FL.forEach(function(F){F.g.traverse(lit)});bookGroups.forEach(function(g){g.traverse(lit)});cabs.forEach(function(g){g.traverse(lit)});
  doorMeshes.forEach(function(m){var o=m.material;m.material=new THREE.MeshToonMaterial({color:o.color,gradientMap:grad,transparent:o.transparent,opacity:o.opacity});m.castShadow=false})}
litInterior();
if(MIS){(function(){
var D2R=Math.PI/180,DOM=function(t,c,x){return el(t,c,x)};
var CHCOL=['#'+COL.c0,'#'+COL.c1,'#'+COL.c2,'#'+COL.c3,'#'+COL.c4];
var CAST={},CASTI=MIS.cast;CASTI.forEach(function(c){CAST[c.id]=c});
var SIX={};BK.scripts.forEach(function(s,i){SIX[s.id]=i});
/* ---------- walk grid ---------- */
var NV={cs:.25,x0:-34,z0:-27,nx:272,nz:256,w:[]};
function nvI(x,z){var i=Math.floor((x-NV.x0)/NV.cs),j=Math.floor((z-NV.z0)/NV.cs);return(i<0||j<0||i>=NV.nx||j>=NV.nz)?-1:j*NV.nx+i}
function walk(f,x,z){var i=nvI(x,z);return i>=0&&NV.w[f]&&NV.w[f][i]===1}
function sd(px,pz,ax,az,bx,bz){var dx=bx-ax,dz=bz-az,l=dx*dx+dz*dz,t=l>0?((px-ax)*dx+(pz-az)*dz)/l:0;t=t<0?0:t>1?1:t;var ex=ax+t*dx-px,ez=az+t*dz-pz;return ex*ex+ez*ez}
function rastTri(ax,az,bx,bz,cx,cz,G,tol){var cs=NV.cs,h=cs*tol,i0=Math.max(0,Math.floor((Math.min(ax,bx,cx)-h-NV.x0)/cs)),i1=Math.min(NV.nx-1,Math.floor((Math.max(ax,bx,cx)+h-NV.x0)/cs)),
  j0=Math.max(0,Math.floor((Math.min(az,bz,cz)-h-NV.z0)/cs)),j1=Math.min(NV.nz-1,Math.floor((Math.max(az,bz,cz)+h-NV.z0)/cs)),area=(bx-ax)*(cz-az)-(bz-az)*(cx-ax),h2=h*h;
  for(var j=j0;j<=j1;j++){var pz=NV.z0+(j+.5)*cs;for(var i=i0;i<=i1;i++){var px=NV.x0+(i+.5)*cs,hit=false;
    if(Math.abs(area)>1e-7){var w0=((bx-px)*(cz-pz)-(bz-pz)*(cx-px))/area,w1=((cx-px)*(az-pz)-(cz-pz)*(ax-px))/area;hit=w0>=0&&w1>=0&&w0+w1<=1}
    if(!hit)hit=sd(px,pz,ax,az,bx,bz)<h2||sd(px,pz,bx,bz,cx,cz)<h2||sd(px,pz,cx,cz,ax,az)<h2;if(hit)G[j*NV.nx+i]=1}}}
var DECOR={};[COL.plant,COL.pot,COL.leaf,COL.leaf2].forEach(function(c){DECOR[c]=1});
function rastMB(P,y,fl,bl,H){for(var o=0;o<P.length;o+=9){if(H&&DECOR[H[o/9]])continue;var y0=Math.min(P[o+1],P[o+4],P[o+7]),y1=Math.max(P[o+1],P[o+4],P[o+7]);
  if(y1<=y+.12&&y0>=y-.3&&y1-y0<.05)rastTri(P[o],P[o+2],P[o+3],P[o+5],P[o+6],P[o+8],fl,.3);else if(y1>y+.2&&y0<y+1.8)rastTri(P[o],P[o+2],P[o+3],P[o+5],P[o+6],P[o+8],bl,.5)}}
function fillR(G,x0,z0,x1,z1,v){for(var z=z0+NV.cs/2;z<z1;z+=NV.cs)for(var x=x0+NV.cs/2;x<x1;x+=NV.cs){var i=nvI(x,z);if(i>=0)G[i]=v}}
var LIFTS=[-14.1,-11.1,12.5],LZ=6.35;
function bakeNav(){var N=NV.nx*NV.nz;for(var f=0;f<4;f++){var fl=new Uint8Array(N),bl=new Uint8Array(N),y=f*FH,M=FL[f].mb;
  [M.shell,M.props].concat(M.segs.map(function(s){return s.mb})).concat(typeof bm!=='undefined'&&bm[f]?[bm[f]]:[]).forEach(function(mb){rastMB(mb.p,y,fl,bl,mb.h)});if(f===0)rastMB(mbSite.p,y,fl,bl,null);
  rastMB(mbLift.p,y,fl,bl);
  LIFTS.forEach(function(cx){fillR(bl,cx-1.05,4.95,cx+1.05,7.75,0);fillR(fl,cx-1.05,4.95,cx+1.05,7.75,1);fillR(bl,cx-1.25,4.8,cx+1.25,5.0,1);fillR(bl,cx-1.25,4.8,cx-1.05,7.8,1);fillR(bl,cx+1.05,4.8,cx+1.25,7.8,1);
    fillR(bl,cx-.85,7.6,cx+.85,8.7,0);fillR(fl,cx-.85,7.6,cx+.85,8.7,1)});
  var w=new Uint8Array(N),nx=NV.nx;for(var j=1;j<NV.nz-1;j++)for(var i=1;i<nx-1;i++){var k=j*nx+i;if(!fl[k])continue;var ok=1;
    for(var dj=-1;dj<=1&&ok;dj++)for(var di=-1;di<=1;di++)if(bl[k+dj*nx+di]){ok=0;break}w[k]=ok}
  NV.w[f]=w}}
var t0=performance.now();bakeNav();var bakeMs=Math.round(performance.now()-t0);
/* A*: 8 neighbours, no corner cutting, octile; string-pulled by grid line of sight */
var NN=NV.nx*NV.nz,AS={g:new Float32Array(NN),from:new Int32Array(NN),seen:new Uint32Array(NN),closed:new Uint32Array(NN),gen:0,heap:new Int32Array(NN*2),hf:new Float32Array(NN*2)};
var DI=[1,-1,0,0,1,1,-1,-1],DJ=[0,0,1,-1,1,-1,1,-1];
function cellXZ(k){return[NV.x0+(k%NV.nx+.5)*NV.cs,NV.z0+(((k/NV.nx)|0)+.5)*NV.cs]}
function snap(f,x,z,maxr){if(walk(f,x,z))return[x,z];var cs=NV.cs,best=null,bd=1e9;for(var r=1;r*cs<=(maxr||2);r++){for(var dj=-r;dj<=r;dj++)for(var di=-r;di<=r;di++){if(Math.max(Math.abs(di),Math.abs(dj))!==r)continue;var X=x+di*cs,Z=z+dj*cs;if(walk(f,X,Z)){var d=di*di+dj*dj;if(d<bd){bd=d;best=[X,Z]}}}if(best)return best}return null}
function los(f,a,b){var nx=NV.nx,x0=a%nx,y0=(a/nx)|0,x1=b%nx,y1=(b/nx)|0,dx=Math.abs(x1-x0),dy=Math.abs(y1-y0),sx=x0<x1?1:-1,sy=y0<y1?1:-1,err=dx-dy,W=NV.w[f];
  for(var n=0;n<4000;n++){if(!W[y0*nx+x0])return false;if(x0===x1&&y0===y1)return true;var e2=2*err;if(e2>-dy&&e2<dx&&(!W[y0*nx+x0+sx]||!W[(y0+sy)*nx+x0]))return false;if(e2>-dy){err-=dy;x0+=sx}if(e2<dx){err+=dx;y0+=sy}}return false}
function navPath(f,sx,sz,tx,tz){var s=snap(f,sx,sz,1.5),t=snap(f,tx,tz,2.5);if(!s||!t)return null;var nx=NV.nx,Wf=NV.w[f],A=AS,gen=++A.gen,hn=0;
  var si=nvI(s[0],s[1]),ti=nvI(t[0],t[1]),tI=ti%nx,tJ=(ti/nx)|0;
  function hh(k){var dx=Math.abs(k%nx-tI),dz=Math.abs(((k/nx)|0)-tJ);return dx+dz+(Math.SQRT2-2)*Math.min(dx,dz)}
  function push(k,fv){var i=hn++;while(i>0){var p=(i-1)>>1;if(A.hf[p]<=fv)break;A.heap[i]=A.heap[p];A.hf[i]=A.hf[p];i=p}A.heap[i]=k;A.hf[i]=fv}
  function pop(){var top=A.heap[0],k=A.heap[--hn],fv=A.hf[hn],i=0;for(;;){var l=2*i+1;if(l>=hn)break;var r=l+1,c=(r<hn&&A.hf[r]<A.hf[l])?r:l;if(A.hf[c]>=fv)break;A.heap[i]=A.heap[c];A.hf[i]=A.hf[c];i=c}A.heap[i]=k;A.hf[i]=fv;return top}
  A.g[si]=0;A.seen[si]=gen;A.from[si]=-1;push(si,hh(si));var found=false,it=0;
  while(hn>0&&hn<NN*2-8&&it++<300000){var k=pop();if(A.closed[k]===gen)continue;A.closed[k]=gen;if(k===ti){found=true;break}var ki=k%nx,kj=(k/nx)|0;
    for(var d=0;d<8;d++){var di=DI[d],dj=DJ[d],ni=ki+di,nj=kj+dj;if(ni<0||nj<0||ni>=nx||nj>=NV.nz)continue;var n=nj*nx+ni;if(!Wf[n]||A.closed[n]===gen)continue;
      if(di&&dj&&(!Wf[kj*nx+ni]||!Wf[nj*nx+ki]))continue;var g=A.g[k]+(di&&dj?Math.SQRT2:1);if(A.seen[n]!==gen||g<A.g[n]){A.seen[n]=gen;A.g[n]=g;A.from[n]=k;push(n,g+hh(n))}}}
  if(!found)return null;var cells=[];for(var c=ti;c!==-1;c=A.from[c])cells.push(c);cells.reverse();
  var pts=[[s[0],s[1]]],a=0;for(var i=2;i<cells.length;i++)if(!los(f,cells[a],cells[i])){pts.push(cellXZ(cells[i-1]));a=i-1}pts.push([t[0],t[1]]);return pts}
function plen(p){var L=0;for(var i=1;i<p.length;i++)L+=Math.hypot(p[i][0]-p[i-1][0],p[i][1]-p[i-1][1]);return L}
function route(fr,to){if(fr.f===to.f){var p=navPath(fr.f,fr.x,fr.z,to.x,to.z);return p?[{f:fr.f,pts:p}]:null}
  var best=null;LIFTS.forEach(function(lx){var a=navPath(fr.f,fr.x,fr.z,lx,LZ),b=a&&navPath(to.f,lx,LZ,to.x,to.z);if(a&&b){var L=plen(a)+plen(b);if(!best||L<best.L)best={L:L,legs:[{f:fr.f,pts:a},{ride:lx,to:to.f},{f:to.f,pts:b}]}}});return best&&best.legs}
var REACH=null;
function reachMap(){if(REACH)return REACH;var N=NV.nx*NV.nz,nx=NV.nx,R=[0,1,2,3].map(function(){return new Uint8Array(N)}),L=ROOMS.filter(function(r){return r.f===0&&r.entrance})[0]||ROOMBY.g_lobby,s=snap(0,(L.x0+L.x1)/2,(L.z0+L.z1)/2-1,4);
  var q=[[0,nvI(s[0],s[1])]];R[0][q[0][1]]=1;var cabs2={};
  while(q.length){var e=q.pop(),f=e[0],k=e[1],ki=k%nx,kj=(k/nx)|0,c=cellXZ(k),cb=inCab(c[0],c[1]);if(cb>=0&&!cabs2[cb]){cabs2[cb]=1;for(var g=0;g<4;g++){var kk=nvI(LIFTS[cb],LZ);if(NV.w[g][kk]&&!R[g][kk]){R[g][kk]=1;q.push([g,kk])}}}
    for(var d=0;d<4;d++){var ni=ki+DI[d],nj=kj+DJ[d];if(ni<0||nj<0||ni>=nx||nj>=NV.nz)continue;var n=nj*nx+ni;if(NV.w[f][n]&&!R[f][n]){R[f][n]=1;q.push([f,n])}}}
  REACH=R;return R}
function reachable(f,x,z){var i=nvI(x,z);return i>=0&&reachMap()[f][i]===1}
function talkable(f,x,z,r){r=r||1.7;for(var dz=-r;dz<=r;dz+=NV.cs)for(var dx=-r;dx<=r;dx+=NV.cs)if(dx*dx+dz*dz<=r*r&&reachable(f,x+dx,z+dz))return true;return false}
/* a presenter out of the player's reach (behind a counter, in a room whose door the model's furniture blocks) steps to the
   nearest spot within talking range, staying on their own side when there is one (the game snaps slots at load the same way) */
var MOVED=[];
function talkSpot(f,x,z,tag){if(talkable(f,x,z))return null;var best=null,alt=null,R0=roomAt(f,x,z);for(var R=.25;R<=6&&!best;R+=.25)for(var a=0;a<32;a++){var X=x+Math.sin(a/32*6.283)*R,Z=z+Math.cos(a/32*6.283)*R;
    if(!walk(f,X,Z)||!talkable(f,X,Z,1.4)||(R0&&roomAt(f,X,Z)!==R0))continue;if(!reachable(f,X,Z)){best=[X,Z];break}if(!alt)alt=[X,Z]}var r=best||alt;if(r)MOVED.push(tag+' '+Math.hypot(r[0]-x,r[1]-z).toFixed(1)+' m'+(R0?' in '+R0.id:''));return r}
function faceReach(f,x,z){var b=null;for(var a=0;a<16;a++)for(var R=.5;R<=2;R+=.5){var X=x+Math.sin(a/16*6.283)*R,Z=z+Math.cos(a/16*6.283)*R;if(reachable(f,X,Z)&&(!b||R<b[1])){b=[a/16*360,R]}}return b?b[0]:0}
function inCab(x,z){for(var i=0;i<LIFTS.length;i++)if(Math.abs(x-LIFTS[i])<1.05&&z>4.95&&z<7.75)return i;return-1}
/* ---------- people ---------- */
var NP=[],npG=[0,1,2,3].map(function(){var g=new THREE.Group();world.add(g);return g});
var discGeo=new THREE.CircleGeometry(.36,20).rotateX(-Math.PI/2),discMat=new THREE.MeshBasicMaterial({color:0x173a3a,transparent:true,opacity:.16,depthWrite:false});
function rng(s){return function(){s|=0;s=s+0x6D2B79F5|0;var t=Math.imul(s^s>>>15,1|s);t=t+Math.imul(t^t>>>7,61|t)^t;return((t^t>>>14)>>>0)/4294967296}}
function addNPC(o){var n=NPCK.build(o.look,{outline:!!o.cast||!!o.me,lod:o.lod,pose:o.pose});o.n=n;o.ph=(o.look.seed||1)%97*.13;o.nod=0;o.hy=0;o.y=o.y||0;
  if(o.pose==='lie'||o.pose==='sit'){}else{var d=new THREE.Mesh(discGeo,discMat);d.position.y=.012;n.root.add(d)}
  place(o);if(!o.me)NP.push(o);return o}
function place(o){var n=o.n;if(n.root.parent)n.root.parent.remove(n.root);npG[o.f].add(n.root);n.root.position.set(o.x,o.f*FH+(o.y||0),o.z);n.root.rotation.y=(o.yaw||0)*D2R}
var HAIRC=['#2b1d14','#4a2f1d','#6b4423','#a0703c','#d9b26f','#1d1d1d'];
function look(kind,r){var sex=r()<.5?'f':'m',age=kind==='patient'?(r()<.55?2:1):(r()<.65?0:1),pk=function(a){return a[(r()*a.length)|0]};
  var outfit={doctor:'#f4f7f8',resident:'#ffffff',nurse:pk(['#2bb3a3','#3a86c0','#7a6ad8','#2bb3a3']),tech:'#d8ecf7',patient:pk(['#9cc9e8','#a8d8d0','#c4d3ee']),visitor:pk(['#e07a5f','#3d5a80','#81b29a','#f2cc8f','#9b5de5','#ef476f','#457b9d']),surgeon:'#5aa36b',pharmacist:'#ffffff',guide:'#2f6fb7',researcher:'#eef6fb'}[kind]||'#ffffff';
  var under={doctor:pk(['#3b6ea8','#4a5568','#2d6a4f','#7b4b94']),resident:'#2bb3a3',visitor:pk(['#2f3640','#4a5568','#6b4f3a','#3d405b']),patient:outfit,nurse:outfit,surgeon:outfit,tech:'#4a5568',pharmacist:'#2d6a4f',guide:'#2f3640',researcher:'#4a5568'}[kind]||'#4a5568';
  return{kind:kind,sex:sex,age:age,skin:(r()*4)|0,hair:sex==='f'?pk(['long','bun','ponytail','curly','short']):pk(['short','short','curly','bald']),hair_color:age===2?'#c9c9c9':pk(HAIRC),outfit:outfit,under:under,seed:(r()*1e6)|0}}
var BEDY={bed:.62,exam_couch:.66,gurney:.72,or_table:.95},SEATY={chair:1,infusion_chair:1,recliner:1,dental:1,alcove_seat:1,sofa:1,corridor_chair:1,auditorium_seat:1,bench_seat:1,corridor_bench:1};
var WORK={desk:1,exam_desk:1,reading_desk:1,lab_bench:1,lab_island:1,dispensing_counter:1,station_counter:1,nurse_counter:1,reception_desk:1,trolley_tower:1,ultrasound:1,centrifuge:1};
function nearNPC(f,x,z,r){for(var i=0;i<NP.length;i++){var o=NP[i];if(o.f===f&&Math.hypot(o.x-x,o.z-z)<r)return o}return null}
function frontOf(p,dist,side){var t=p[5]*D2R,fx=Math.sin(t),fz=Math.cos(t);return[p[3]+fx*(p[7]/2+dist)+fz*side,p[4]+fz*(p[7]/2+dist)-fx*side]}
var castNP={},spots={};
function populate(){var r=rng(20261111),FU=BK.furn||[],posted={};
  CASTI.forEach(function(c){var p=MIS.posts[c.id];if(!p)return;posted[p.room]=1;if(p.pose==='stand'){var ts=talkSpot(p.f,p.x,p.z,'post '+c.id);if(ts)p={room:p.room,f:p.f,x:ts[0],z:ts[1],yaw:faceReach(p.f,ts[0],ts[1]),pose:'stand',y:0}}castNP[c.id]=addNPC({look:{kind:c.kind,sex:c.sex,age:c.age,skin:c.skin,hair:c.hair,hair_color:c.hair_color,outfit:c.outfit,under:c.under,seed:c.id.length*7919+c.id.charCodeAt(0)},
    cast:c,f:p.f,x:p.x,z:p.z,yaw:p.yaw,pose:p.pose,y:p.y||0,lod:0,home:p,talk:true})});
  FU.forEach(function(p){var f=p[0],R=ROOMBY[p[1]],k=p[2];if(!R)return;var t=R.type;if(nearNPC(f,p[3],p[4],.8))return;
    if(BEDY[k]&&(t==='ward_room'||t==='recovery'||t==='icu_bay')&&r()<.7)addNPC({look:look('patient',r),f:f,x:p[3],z:p[4],yaw:p[5],pose:'lie',y:BEDY[k],lod:1,talk:t!=='icu_bay',seat:1});
    else if(SEATY[k]&&(t==='waiting'||t==='triage'||t==='reception')&&r()<.45)addNPC({look:look(r()<.6?'patient':'visitor',r),f:f,x:p[3],z:p[4],yaw:p[5],pose:'sit',lod:1,talk:true,seat:1});
    else if(SEATY[k]&&t==='ward_room'&&r()<.18)addNPC({look:look('visitor',r),f:f,x:p[3],z:p[4],yaw:p[5],pose:'sit',lod:1,talk:true,seat:1});
    else if(k==='infusion_chair'&&r()<.6)addNPC({look:look('patient',r),f:f,x:p[3],z:p[4],yaw:p[5],pose:'sit',lod:1,talk:true,seat:1})});
  var staff={exam:['doctor',.5],lab:['tech',.6],office:['doctor',.5],pharmacy:['pharmacist',.9],nurse_station:['nurse',1],reception:['guide',1],ward_room:['nurse',.3],icu_bay:['nurse',.8],recovery:['nurse',.6],imaging:['tech',.6],treatment_bay:['nurse',.5],triage:['nurse',.8]};
  ROOMS.forEach(function(R){var s=staff[R.type];if(!s||posted[R.id]||r()>s[1])return;var W=FU.filter(function(p){return p[1]===R.id&&WORK[p[2]]});if(!W.length)return;
    var p=W[(r()*W.length)|0];[.5,-.5,0].some(function(side){var q=frontOf(p,.45,side);if(walk(R.f,q[0],q[1])&&!nearNPC(R.f,q[0],q[1],.8)){addNPC({look:look(s[0],r),f:R.f,x:q[0],z:q[1],yaw:(p[5]+180)%360,pose:'stand',lod:1,talk:true});return true}return false})});
  /* walkers: staff and visitors along the corridors of each storey, never outdoors */
  var CP=[];CORR.forEach(function(c){for(var f=0;f<4;f++)for(var i=0;i<6;i++){var x=c[0]+(c[2]-c[0])*(i+.5)/6,z=(c[1]+c[3])/2;if(walk(f,x,z))CP.push([f,x,z])}});
  for(var f=0;f<4;f++){var pts=CP.filter(function(q){return q[0]===f});for(var i=0;i<(f===0?7:5)&&pts.length>1;i++){var q=pts[(r()*pts.length)|0],kind=['nurse','doctor','visitor','resident','tech','patient'][(r()*6)|0];
    if(kind==='patient'&&f!==2)kind='visitor';addNPC({look:look(kind,r),f:f,x:q[1],z:q[2],yaw:0,pose:'stand',lod:0,talk:false,walker:{pts:pts,path:null,i:0,wait:r()*3}})}}}
populate();
/* the player: a student in a short white coat */
var ME=addNPC({me:true,look:{kind:'student',sex:'f',age:0,skin:1,hair:'ponytail',hair_color:'#4a2f1d',outfit:'#ffffff',under:'#0e8f83',seed:7},f:0,x:0,z:0,yaw:180,pose:'stand',lod:0});
var xr=new THREE.MeshBasicMaterial({color:0x000000,transparent:true,opacity:.6,depthFunc:THREE.GreaterDepth,depthWrite:false});
var xs=[];ME.n.root.traverse(function(m){if(m.isMesh&&m.material!==discMat&&m.material.side!==THREE.BackSide&&!m.userData.outline)xs.push(m)});xs.forEach(function(m){var c=new THREE.Mesh(m.geometry,xr);c.renderOrder=5;c.userData.outline=true;m.add(c)});
var PLY={f:0,x:0,z:13.5,yaw:180,vx:0,vz:0,on:false,ride:null,au:null,frozen:false};
/* ---------- missions: state, objective ---------- */
var MS=PR.mis||(PR.mis={acc:{},end:{},arc:{},track:-1});
function stepsOf(m){var L=[];m.quests.forEach(function(q,qi){q.steps.forEach(function(s,si){L.push({q:q,qi:qi,si:si,s:s})})});return L}
function mDone(m){return stepsOf(m).filter(function(x){return PR.fin[x.s.script]}).length}
function mTotal(m){return stepsOf(m).length}
function tracked(){var i=MS.track;if(i<0||i>=MIS.missions.length||MS.end[MIS.missions[i].id]){i=MIS.missions.findIndex(function(m){return!MS.end[m.id]});MS.track=i}return i<0?null:MIS.missions[i]}
function objective(){var m=tracked();if(!m)return null;var g=MIS.posts[m.giver];
  if(!MS.acc[m.id])return{m:m,type:'accept',who:m.giver,f:castNP[m.giver].f,x:castNP[m.giver].x,z:castNP[m.giver].z};
  var L=stepsOf(m),nx=L.filter(function(x){return!PR.fin[x.s.script]})[0];
  if(nx){var s=nx.s,pz=s.patient&&s.patient.cast===s.presenter?s.patient:null;return{m:m,type:'step',st:nx,who:s.presenter,f:s.f,x:pz?pz.x:s.stand[0],z:pz?pz.z:s.stand[1]}}
  return{m:m,type:'debrief',who:m.giver,f:castNP[m.giver].f,x:castNP[m.giver].x,z:castNP[m.giver].z}}
var OBJ=null;
/* stage the cast for the objective: the presenter at the step's stand point, the patient in the step's bed or seat */
function stage(){OBJ=objective();var want={};
  if(OBJ&&OBJ.type==='step'){var s=OBJ.st.s,ts=s.patient&&s.patient.cast===s.presenter?null:talkSpot(s.f,s.stand[0],s.stand[1],s.script);
    want[s.presenter]=ts?{f:s.f,x:ts[0],z:ts[1],yaw:faceReach(s.f,ts[0],ts[1]),pose:'stand',y:0}:{f:s.f,x:s.stand[0],z:s.stand[1],yaw:s.stand[2],pose:'stand',y:0};
    if(s.patient)want[s.patient.cast]={f:s.f,x:s.patient.x,z:s.patient.z,yaw:s.patient.yaw,pose:s.patient.pose,y:s.patient.pose==='lie'?s.patient.y:0}}
  Object.keys(castNP).forEach(function(id){var o=castNP[id],w=want[id]||o.home;if(o.f===w.f&&Math.hypot(o.x-w.x,o.z-w.z)<.05&&o.pose===w.pose)return;
    var seen=PLY.on&&o.f===PLY.f&&w.f===PLY.f&&Math.hypot(o.x-PLY.x,o.z-PLY.z)<16&&w.pose==='stand'&&o.pose==='stand';
    if(seen){var p=navPath(o.f,o.x,o.z,w.x,w.z);if(p){o.go={pts:p,i:1,yaw:w.yaw};return}}
    o.f=w.f;o.x=w.x;o.z=w.z;o.yaw=w.yaw;o.pose=w.pose;o.y=w.y||0;o.go=null;place(o)});
  updObj()}
/* ---------- HUD ---------- */
var hud=DOM('div','phud');hud.hidden=true;app.appendChild(hud);
var objEl=DOM('button','pobj');hud.appendChild(objEl);objEl.addEventListener('click',function(){openJournal()});
var pauseB=DOM('button','pbtn acc');pauseB.setAttribute('aria-label','Pauză');pauseB.innerHTML='<i class="ic-pause"></i>';hud.appendChild(pauseB);pauseB.addEventListener('click',function(){openPause()});
var hintEl=DOM('div','phint','WASD sau săgeți · click pe podea: mergi acolo · E: vorbește');hud.appendChild(hintEl);
var panel=DOM('div','ia');panel.hidden=true;hud.appendChild(panel);
var mkEl=DOM('button','mk');mkEl.hidden=true;mkEl.innerHTML='<i class="dia"></i><i class="chev"></i><b class="dist"></b><b class="fl"></b>';hud.appendChild(mkEl);mkEl.addEventListener('click',function(){goObjective()});
var clouds=[];for(var ci=0;ci<6;ci++){var c=DOM('div','cloud','…');c.hidden=true;hud.appendChild(c);clouds.push(c)}
var greetEl=DOM('div','greet');greetEl.hidden=true;hud.appendChild(greetEl);
var dlg=DOM('div','dlg');dlg.hidden=true;dlg.setAttribute('role','dialog');app.appendChild(dlg);
var modal=DOM('div','pmod');modal.hidden=true;app.appendChild(modal);
var banner=DOM('div','bnr');banner.hidden=true;app.appendChild(banner);
var stick=DOM('div','stick');stick.hidden=true;stick.innerHTML='<i class="knob"></i>';hud.appendChild(stick);
/* portrait renderer for the panel and the dialogue: the NPC's head in 3D, nods on tap */
var PREN=null,PSC=new THREE.Scene(),PCAM=new THREE.PerspectiveCamera(26,1,.1,10),PHEAD=null,PNOD=0,HEADS={};
try{PREN=new THREE.WebGLRenderer({alpha:true,antialias:true,preserveDrawingBuffer:true});PREN.setPixelRatio(Math.min(2,window.devicePixelRatio||1));PREN.setSize(96,96);PREN.domElement.className='phead'}catch(e){PREN=null}
PSC.add(new THREE.HemisphereLight(0xffffff,0xb8d6a0,.75));var pl=new THREE.DirectionalLight(0xfff6e0,.55);pl.position.set(.6,1,1.4);PSC.add(pl);PCAM.position.set(0,.04,1.25);PCAM.lookAt(0,.02,0);
function headOf(o){var key=JSON.stringify(o.look);if(!HEADS[key])HEADS[key]=NPCK.head(o.look);return HEADS[key]}
function portrait(o){if(!PREN)return;if(PHEAD)PSC.remove(PHEAD);PHEAD=headOf(o);PSC.add(PHEAD);PNOD=0}
function drawPortrait(dt){if(!PREN||!PHEAD)return;PNOD=Math.max(0,PNOD-dt);PHEAD.rotation.set(Math.sin(PNOD*14)*.22*PNOD/.6,Math.sin(time*.7)*.18,0);PREN.render(PSC,PCAM)}
function who(id){var c=CAST[id];return c?c.name:''}
function roomTxt(f,rid){var R=ROOMBY[rid];return(R?R.sign:'')+' · '+(f===0?'parter':'etajul '+f)}
function updObj(){if(!OBJ){objEl.innerHTML='<i class="dot" style="background:'+CHCOL[0]+'"></i><span><b>Toate misiunile sunt gata</b><small>Deschide jurnalul pentru recapitulare</small></span>';return}
  var m=OBJ.m,t=OBJ.type==='accept'?'Vorbește cu '+who(m.giver)+': un caz nou':OBJ.type==='debrief'?'Raportează la '+who(m.giver):'Vorbește cu '+who(OBJ.who);
  var rid=OBJ.type==='step'?OBJ.st.s.room:castNP[OBJ.who].home.room;
  objEl.innerHTML='<i class="dot" style="background:'+CHCOL[m.chapter]+'"></i><span><b></b><small></small></span><em></em>';objEl.querySelector('b').textContent=m.title;
  objEl.querySelector('small').textContent=t+' · '+roomTxt(OBJ.f,rid);objEl.querySelector('em').textContent=mDone(m)+'/'+mTotal(m)}
/* ---------- markers (game: spinning diamond, ground ring, edge icon with chevron and distance, floor badge) ---------- */
var mkG=new THREE.Group(),mkD=new THREE.Mesh(new THREE.OctahedronGeometry(.26),new THREE.MeshToonMaterial({color:0xffc933,gradientMap:grad}));mkD.scale.set(1,1.45,1);mkG.add(mkD);
var mkO=new THREE.Mesh(mkD.geometry,new THREE.MeshBasicMaterial({color:0x173a3a,side:THREE.BackSide}));mkO.scale.setScalar(1.14);mkD.add(mkO);
var mkR=new THREE.Mesh(new THREE.RingGeometry(.62,.8,48).rotateX(-Math.PI/2),new THREE.MeshBasicMaterial({color:0xffc933,transparent:true,opacity:.6,depthWrite:false}));mkG.add(mkR);world.add(mkG);mkG.visible=false;
var mkT=0,v3b=new THREE.Vector3();
function markerFrame(dt){mkT+=dt;var o=OBJ,N=o&&castNP[o.who];if(!PLY.on||!o||!N||PLY.ride){mkG.visible=false;mkEl.hidden=true;return}
  var col=CHCOL[o.m.chapter];mkD.material.color.set(col);mkR.material.color.set(col);var same=N.f===PLY.f;
  mkG.visible=same;var top=N.pose==='lie'?1.25:N.pose==='sit'?1.75:2.35;mkG.position.set(N.x,N.f*FH,N.z);mkD.position.y=top+Math.sin(mkT*2.4)*.1;mkD.rotation.y=mkT*1.8;var s=1+Math.sin(mkT*3)*.06;mkR.scale.set(s,1,s);
  v3b.set(N.x,N.f*FH+top,N.z).project(camera);var sx=(v3b.x*.5+.5)*W,sy=(-v3b.y*.5+.5)*H,on=same&&v3b.z<1&&sx>40&&sx<W-40&&sy>90&&sy<H-60,pr=panel.hidden?null:panel.getBoundingClientRect();
  if(on&&pr&&sx>pr.left-30&&sy>pr.top-30)on=false;
  mkEl.hidden=false;mkEl.classList.toggle('on',on);mkEl.style.setProperty('--c',col);
  if(on){mkEl.style.transform='translate('+(sx-22)+'px,'+(sy-22)+'px)';return}
  var cx=W/2,cy=H/2,dx=sx-cx,dy=sy-cy;if(v3b.z>1){dx=-dx;dy=-dy}if(!same){dx=N.x-PLY.x;dy=N.z-PLY.z}var L=Math.hypot(dx,dy)||1;dx/=L;dy/=L;
  var mx=34,top0=96,bot=H-42,k=Math.min((dx>0?W-mx-cx:cx-mx)/Math.abs(dx||1e-6),(dy>0?bot-cy:cy-top0)/Math.abs(dy||1e-6));var ex=cx+dx*k,ey=cy+dy*k;
  if(pr&&ex>pr.left-34&&ey>pr.top-34){if(Math.abs(dx)>Math.abs(dy))ey=Math.min(ey,pr.top-40);else ex=Math.min(ex,pr.left-40)}
  mkEl.style.transform='translate('+(ex-22)+'px,'+(ey-22)+'px)';mkEl.querySelector('.chev').style.transform='rotate('+Math.atan2(dy,dx)+'rad)';
  if(!markerFrame.t||time-markerFrame.t>.25){markerFrame.t=time;mkEl.querySelector('.dist').textContent=Math.round(Math.hypot(N.x-PLY.x,N.z-PLY.z)+Math.abs(N.f-PLY.f)*FH)+' m';
    mkEl.querySelector('.fl').textContent=same?'':(N.f>PLY.f?'↑ ':'↓ ')+(N.f===0?'P':'E'+N.f)}}
/* ---------- interact (scan at 10 Hz; enter 2.0 m, exit 2.5 m; in front unless it is the objective; panel bottom right) ---------- */
var IA={cur:null,t:0};
function scan(){var best=null,bs=-1e9,fx=Math.sin(PLY.yaw*D2R),fz=Math.cos(PLY.yaw*D2R);
  var cab=inCab(PLY.x,PLY.z);if(cab>=0&&!PLY.au){IA.cur={lift:cab};return}
  NP.forEach(function(o){if(o.f!==PLY.f||(!o.talk&&!o.cast))return;var dx=o.x-PLY.x,dz=o.z-PLY.z,d=Math.hypot(dx,dz),lim=IA.cur&&IA.cur.o===o?2.5:2.0;if(o.pose==='lie')lim+=.6;if(d>lim)return;
    var isObj=OBJ&&OBJ.who===(o.cast&&o.cast.id);if(!isObj&&d>.5&&(dx*fx+dz*fz)/d<-.15)return;var sc=(isObj?100:0)+(o.cast?10:0)-d;if(sc>bs){bs=sc;best=o}});
  IA.cur=best?{o:best}:null}
function renderPanel(){var c=IA.cur;if(!c||dlg.hidden===false||!modal.hidden||PL){panel.hidden=true;panel._k=null;return}var key=c.lift!==undefined?'lift'+PLY.f:c.o;if(panel._k===key){panel.hidden=false;return}panel._k=key;panel.textContent='';panel.hidden=false;
  if(c.lift!==undefined){panel.appendChild(DOM('div','eb','Lift'));panel.appendChild(DOM('b','nm','Alege etajul'));var row=DOM('div','keys');
    ['P','1','2','3'].forEach(function(t,f){var b=DOM('button','key'+(f===PLY.f?' here':''),t);b.disabled=f===PLY.f;b.addEventListener('click',function(){rideTo(f)});row.appendChild(b)});panel.appendChild(row);return}
  var o=c.o,isObj=OBJ&&o.cast&&OBJ.who===o.cast.id;if(PREN){portrait(o);panel.appendChild(PREN.domElement)}
  var tx=DOM('div','tx');tx.appendChild(DOM('b','nm',o.cast?o.cast.name:({patient:'Pacient',visitor:'Vizitator',nurse:'Asistentă',doctor:'Medic',tech:'Tehnician',pharmacist:'Farmacist',guide:'Recepție',resident:'Rezident',surgeon:'Chirurg'}[o.look.kind]||'')));
  tx.appendChild(DOM('small',null,o.cast?o.cast.role:'Spune bună ziua'));if(isObj)tx.appendChild(DOM('span','chip','Misiune · '+OBJ.m.title));panel.appendChild(tx);
  var b=DOM('button','talk','Vorbește');b.addEventListener('click',function(){talk(o)});panel.appendChild(b)}
/* clouds: NPCs who want to talk (staff at posts, anyone seated, cast); up to 6 within 4 m; the objective's is solid */
function cloudFrame(){var L=[];if(PLY.on&&dlg.hidden&&!PL)NP.forEach(function(o){if(o.f!==PLY.f||!o.talk||o.walker)return;var d=Math.hypot(o.x-PLY.x,o.z-PLY.z);if(d<4.5)L.push([d,o])});
  L.sort(function(a,b){return a[0]-b[0]});clouds.forEach(function(c,i){var e=L[i];if(!e){c.hidden=true;return}var o=e[1],top=o.pose==='lie'?1.05:o.pose==='sit'?1.45:2.0;v3b.set(o.x,o.f*FH+top,o.z).project(camera);
    c.hidden=v3b.z>1;c.classList.toggle('act',IA.cur&&IA.cur.o===o);c.style.transform='translate('+((v3b.x*.5+.5)*W+14).toFixed(1)+'px,'+((-v3b.y*.5+.5)*H-34).toFixed(1)+'px)'})}
/* ---------- talking ---------- */
var DQ=null;
function say(o,lines,acts,onEnd){DQ={o:o,lines:lines.slice(),acts:acts,end:onEnd,i:0};if(o&&o.nodT!==undefined)o.nod=.6;showLine()}
function showLine(){var q=DQ;dlg.textContent='';dlg.hidden=false;var o=q.o;if(o&&PREN){portrait(o);PNOD=.6;dlg.appendChild(PREN.domElement)}
  var tx=DOM('div','tx');if(o){tx.appendChild(DOM('b','nm',o.cast?o.cast.name:''));if(o.cast)tx.appendChild(DOM('small',null,o.cast.role))}
  var ln=DOM('div','lines');ln.appendChild(DOM('p',null,q.lines[q.i]));tx.appendChild(ln);dlg.appendChild(tx);var foot=DOM('div','acts');
  if(q.i<q.lines.length-1){var n=DOM('button',null,'Continuă');n.addEventListener('click',function(){q.i++;showLine()});foot.appendChild(n)}
  else(q.acts||[['Continuă',null]]).forEach(function(a,i){var b=DOM('button',a[2]||'',a[0]);b.addEventListener('click',function(){dlg.hidden=true;DQ=null;if(a[1])a[1]()});foot.appendChild(b)});
  dlg.appendChild(foot);var last=foot.lastChild;if(last)last.focus()}
function arcOf(ch){return MIS.arcs.filter(function(a){return a.chapter===ch})[0]}
function talk(o){if(!o)return;o.nod=.6;var id=o.cast&&o.cast.id;OBJ=objective();
  if(OBJ&&id===OBJ.who){var m=OBJ.m;
    if(OBJ.type==='accept'){var L=[],arc=arcOf(m.chapter);if(arc&&!MS.arc[m.chapter]&&arc.giver===id)L=L.concat(arc.intro);L=L.concat(m.intro);L.push('Cazul: '+m.case);
      say(o,L,[['Mai târziu',null,'ghost'],['Accept cazul',function(){MS.acc[m.id]=1;MS.arc[m.chapter]=1;saveP();stage();toast('Caz nou: '+m.title)}]]);return}
    if(OBJ.type==='debrief'){var L2=m.outro.slice(),idx=MIS.missions.indexOf(m),nxt=MIS.missions[idx+1],lastOfArc=!nxt||nxt.chapter!==m.chapter,arc2=arcOf(m.chapter);if(lastOfArc&&arc2)L2=L2.concat(arc2.outro);
      say(o,L2,[['Închide cazul',function(){MS.end[m.id]=1;saveP();reward(m);MS.track=-1;stage()}]]);return}
    if(OBJ.type==='step'){var st=OBJ.st,s=st.s,L3=[];if(st.si===0)L3=L3.concat(st.q.intro);L3.push(s.line);
      say(o,L3,[['Mai târziu',null,'ghost'],['Începe',function(){playStep(o,st)}]]);return}}
  if(o.cast){var g=MIS.greetings[o.cast.kind]||MIS.greetings.doctor||['Bună ziua!'];greet(o,g[(Math.random()*g.length)|0]);return}
  var gg=MIS.greetings[o.look.kind]||MIS.greetings.patient||['Bună ziua!'];greet(o,gg[(Math.random()*gg.length)|0])}
var greetT=0;function greet(o,line){greetEl.textContent='';greetEl.hidden=false;greetEl.appendChild(DOM('b',null,o.cast?o.cast.name:''));greetEl.appendChild(DOM('span',null,line));greetT=2.5;o.nod=.6}
function playStep(o,st){var s=st.s,si=SIX[s.script];PLAYM={who:o.cast?o.cast.name:'',role:o.cast?o.cast.role:'',onEnd:function(S,c,a){},onClose:function(fin){PLAYM=null;
    if(!fin){stage();return}var good=PL0.answered===0||PL0.correct===PL0.answered,L=[good?s.after[0]:s.after[1]];var q=st.q,lastQ=q.steps.every(function(x){return PR.fin[x.script]});if(lastQ&&q.done)L.push(q.done);
    say(o,L,[['Continuă',function(){stage();var nb=OBJ;if(nb&&nb.type==='step')toast('Următorul: '+who(nb.who)+' · '+roomTxt(nb.f,nb.st.s.room));else if(nb&&nb.type==='debrief')toast('Toți pașii sunt gata: raportează la '+who(nb.m.giver))}]])}};
  openPlayer(si)}
var PL0={correct:0,answered:0};
/* the reward (T3): banner + burst */
function reward(m){var n=0;stepsOf(m).forEach(function(x){n+=BK.scripts[SIX[x.s.script]].covers.length});banner.textContent='';banner.hidden=false;banner.classList.remove('go');void banner.offsetWidth;banner.classList.add('go');
  banner.appendChild(DOM('b',null,'Misiune completă!'));banner.appendChild(DOM('span',null,m.title));banner.appendChild(DOM('small',null,mDone(m)+' pași · '+n+' fapte în caz'));
  for(var i=0;i<26;i++){var p=DOM('i','cf');p.style.setProperty('--a',(i/26*360)+'deg');p.style.setProperty('--d',(90+(i*37)%80)+'px');p.style.background=['#FFC933','#FF7A6B','#12A594','#FFFFFF'][i%4];banner.appendChild(p)}
  setTimeout(function(){banner.hidden=true},2600)}
/* ---------- movement ---------- */
var KEYS={},STK={id:null,x:0,y:0,vx:0,vy:0};
addEventListener('keydown',function(e){if(!PLY.on||e.target.tagName==='INPUT'||e.target.tagName==='SELECT')return;var k=e.key.toLowerCase();if(['w','a','s','d','arrowup','arrowdown','arrowleft','arrowright'].indexOf(k)>=0){KEYS[k]=1;cancelAuto();e.preventDefault()}
  if(k==='e'&&!PL&&dlg.hidden&&modal.hidden&&IA.cur&&IA.cur.o)talk(IA.cur.o);if(k==='escape'&&!PL&&!dlg.hidden){dlg.hidden=true;DQ=null}});
addEventListener('keyup',function(e){KEYS[e.key.toLowerCase()]=0});
function inputVec(){var x=(KEYS.d||KEYS.arrowright?1:0)-(KEYS.a||KEYS.arrowleft?1:0),z=(KEYS.s||KEYS.arrowdown?1:0)-(KEYS.w||KEYS.arrowup?1:0);if(x||z){var l=Math.hypot(x,z);return[x/l,z/l]}
  if(STK.id!==null){var m=Math.hypot(STK.vx,STK.vy);if(m>8){var a=Math.min(1,(m-8)/(60*.75-8));return[STK.vx/m*a,STK.vy/m*a]}}return null}
function cancelAuto(){if(PLY.au){PLY.au=null}}
function goTo(f,x,z,then,label){var legs=route({f:PLY.f,x:PLY.x,z:PLY.z},{f:f,x:x,z:z});if(!legs){toast('Nu găsesc drumul până acolo');return false}PLY.au={legs:legs,li:0,pi:1,then:then,stuck:0,best:1e9,retry:0,goal:{f:f,x:x,z:z},wait:0};return true}
function goObjective(){var o=OBJ;if(!o)return;var N=castNP[o.who];if(!N)return;var t=standNear(N);goTo(N.f,t[0],t[1],function(){var c=castNP[o.who];if(c)PLY.yaw=Math.atan2(c.x-PLY.x,c.z-PLY.z)/D2R})}
function standNear(N){var best=null;[1.0,1.3,1.6,1.9].some(function(r){for(var a=0;a<24;a++){var ang=a/24*Math.PI*2+(N.yaw||0)*D2R,x=N.x+Math.sin(ang)*r,z=N.z+Math.cos(ang)*r;
    if(reachable(N.f,x,z)){var d=Math.hypot(x-PLY.x,z-PLY.z)+(a===0?-1.5:0)+r;if(!best||d<best[2])best=[x,z,d]}}return!!best});return best||[N.x,N.z]}
function autoVec(dt){var A=PLY.au;if(!A)return null;var L=A.legs[A.li];if(!L){var th=A.then;PLY.au=null;if(th)th();return null}
  if(L.ride!==undefined){if(PLY.f===L.to&&!PLY.ride){A.li++;A.pi=1;A.best=1e9;A.stuck=0;return null}if(PLY.ride)return null;var rx=L.ride-PLY.x,rz=LZ-PLY.z,rd=Math.hypot(rx,rz);
    if(rd>.3)return[rx/rd*Math.min(1,rd+.2),rz/rd*Math.min(1,rd+.2)];A.wait+=dt;if(A.wait>.3){A.wait=0;rideTo(L.to,true)}return null}
  if(PLY.f!==L.f)return null;var p=L.pts[A.pi];if(!p){A.li++;A.pi=1;A.best=1e9;return null}var dx=p[0]-PLY.x,dz=p[1]-PLY.z,d=Math.hypot(dx,dz),last=A.pi===L.pts.length-1&&A.li===A.legs.length-1;
  if(d<(last?.3:.45)){A.pi++;A.best=1e9;A.stuck=0;return autoVec(0)}
  if(d<A.best-.05){A.best=d;A.stuck=0}else{A.stuck+=dt;if(A.stuck>1.5){if(A.retry++<1){var g=A.goal,th2=A.then,r=A.retry;if(goTo(g.f,g.x,g.z,th2)){PLY.au.retry=r}return null}PLY.au=null;toast('M-am blocat: încearcă alt drum');return null}}
  var sp=last&&d<1?Math.max(.35,d):1;return[dx/d*sp,dz/d*sp]}
function rideTo(f,auto){if(f===PLY.f||PLY.ride)return;var c=inCab(PLY.x,PLY.z);if(c<0)return;PLY.x=LIFTS[c];PLY.z=LZ;PLY.ride={to:f,t:0};PLY.vx=PLY.vz=0;panel._k=null;audioDing();setView(f);if(!auto)PLY.au=null}
function audioDing(){}
function moveP(dt){if(PLY.ride){var y=cabs[0].position.y-.02;ME.n.root.position.set(PLY.x,y,PLY.z);if(!ride&&view===PLY.ride.to){PLY.f=PLY.ride.to;PLY.ride=null;place(ME);ME.f=PLY.f;place(ME)}return}
  var iv=PLY.frozen?null:inputVec();if(iv)cancelAuto();var av=iv?null:autoVec(dt),v=iv||av||[0,0],tvx=v[0]*4.5,tvz=v[1]*4.5,k=Math.min(1,dt*14);PLY.vx+=(tvx-PLY.vx)*k;PLY.vz+=(tvz-PLY.vz)*k;
  var n=Math.max(1,Math.ceil(dt*60));for(var i=0;i<n;i++){var h=dt/n,nx=PLY.x+PLY.vx*h,nz=PLY.z+PLY.vz*h;
    if(walk(PLY.f,nx,nz)){PLY.x=nx;PLY.z=nz}else if(walk(PLY.f,nx,PLY.z)){PLY.x=nx;PLY.vz*=.6}else if(walk(PLY.f,PLY.x,nz)){PLY.z=nz;PLY.vx*=.6}else{PLY.vx*=.3;PLY.vz*=.3}}
  var sp=Math.hypot(PLY.vx,PLY.vz);if(sp>.25){var ty=Math.atan2(PLY.vx,PLY.vz)/D2R,dy=((ty-PLY.yaw+540)%360)-180;PLY.yaw+=dy*Math.min(1,dt*12)}
  ME.f=PLY.f;ME.x=PLY.x;ME.z=PLY.z;if(ME.n.root.parent!==npG[PLY.f])place(ME);ME.n.root.position.set(PLY.x,PLY.f*FH,PLY.z);ME.n.root.rotation.y=PLY.yaw*D2R;
  NPCK.pose(ME.n,sp>.3?'walk':'stand',time,sp/2.8)}
/* camera: the game's top-down camera */
var CAM={x:0,y:0,z:0,ok:false},FOV0=camera.fov;
function playCam(dt){var py=PLY.ride?cabs[0].position.y-.02:PLY.f*FH,lx=PLY.vx/4.5*1.2,lz=PLY.vz/4.5*1.2,tx=PLY.x+lx,ty=py+.9,tz=PLY.z+lz;
  if(!CAM.ok){CAM.x=tx;CAM.y=ty;CAM.z=tz;CAM.ok=true}var k=1-Math.exp(-dt*8);CAM.x+=(tx-CAM.x)*k;CAM.y+=(ty-CAM.y)*k;CAM.z+=(tz-CAM.z)*k;
  var p=55*D2R;camera.position.set(CAM.x,CAM.y+19*Math.sin(p),CAM.z+19*Math.cos(p));camera.lookAt(CAM.x,CAM.y,CAM.z);if(camera.fov!==40){camera.fov=40;camera.updateProjectionMatrix()}camera.updateMatrixWorld()}
/* the wall piece in front of the player fades (the page's own fade), doors open as people come near */
function roomAt(f,x,z){for(var i=0;i<ROOMS.length;i++){var r=ROOMS[i];if(r.f===f&&x>r.x0&&x<r.x1&&z>r.z0&&z<r.z1)return r}return null}
function wallFade(){if(ride)return;var r=roomAt(PLY.f,PLY.x,PLY.z),sg=null;if(r)sg=FL[r.f].mb.segs.filter(function(q){return q.room===r.id&&q.side==='s'})[0];
  else{var best=null;FL[PLY.f].mb.segs.forEach(function(q){if(q.side!=='n')return;var R=ROOMBY[q.room];if(!R||PLY.x<R.x0-.3||PLY.x>R.x1+.3)return;var dz=R.z0-PLY.z;if(dz>0&&dz<4&&(!best||dz<best.d))best={q:q,d:dz}});sg=best&&best.q}
  if((sg||null)!==faded)fadeSeg(sg||null)}
function doorsFrame(dt){var ppl=[[PLY.f,PLY.x,PLY.z]];NP.forEach(function(o){if(o.walker||o.go)ppl.push([o.f,o.x,o.z])});
  doorMeshes.forEach(function(m){var d=m.userData.d,f=Math.round(m.position.y/FH),near=ppl.some(function(p){return p[0]===f&&Math.hypot(m.position.x-p[1],m.position.z-p[2])<2.4});
    m.userData.pt=(m.userData.pt||0)+((near?1:0)-(m.userData.pt||0))*Math.min(1,dt*9);var a=m.userData.pt*DOOR_OPEN,dx=d.cx*Math.cos(a)+d.nx*Math.sin(a),dz=d.cz*Math.cos(a)+d.nz*Math.sin(a);m.rotation.y=-Math.atan2(dz,dx)})}
/* NPC life: walkers, the cast walking to their spot, idle, heads turn to the player, nods */
function npcFrame(dt){var vf=PLY.on?PLY.f:(typeof view==='number'&&view<4?view:-1);npG.forEach(function(g,f){g.visible=f===vf});
  NP.forEach(function(o){if(o.f!==vf)return;var n=o.n;
    if(o.walker&&PLY.on){var W=o.walker;if(!W.path){W.wait-=dt;if(W.wait<=0){var q=W.pts[(Math.random()*W.pts.length)|0];W.path=navPath(o.f,o.x,o.z,q[1],q[2]);W.i=1;if(!W.path)W.wait=2}}
      else{var p=W.path[W.i];if(!p){W.path=null;W.wait=2+Math.random()*5}else{var dx=p[0]-o.x,dz=p[1]-o.z,d=Math.hypot(dx,dz);if(d<.3)W.i++;else{var s=Math.min(d,1.25*dt);o.x+=dx/d*s;o.z+=dz/d*s;o.yaw=Math.atan2(dx,dz)/D2R}}}
      n.root.position.set(o.x,o.f*FH,o.z);n.root.rotation.y=o.yaw*D2R;NPCK.pose(n,W.path?'walk':'stand',time+o.ph,W.path?.55:0);return}
    if(o.go){var g2=o.go,p2=g2.pts[g2.i];if(!p2){o.go=null;o.yaw=g2.yaw;n.root.rotation.y=o.yaw*D2R}else{var ex=p2[0]-o.x,ez=p2[1]-o.z,e=Math.hypot(ex,ez);if(e<.25)g2.i++;else{var s2=Math.min(e,1.4*dt);o.x+=ex/e*s2;o.z+=ez/e*s2;o.yaw=Math.atan2(ex,ez)/D2R}
      n.root.position.set(o.x,o.f*FH,o.z);n.root.rotation.y=o.yaw*D2R;NPCK.pose(n,'walk',time+o.ph,.6)}return}
    if(n.lod===0)NPCK.pose(n,o.pose==='stand'&&IA.cur&&IA.cur.o===o&&!dlg.hidden?'talk':o.pose,time+o.ph,0);
    if(n.head){var want=0;if(PLY.on&&o.pose!=='lie'){var dx2=PLY.x-o.x,dz2=PLY.z-o.z,d2=Math.hypot(dx2,dz2);if(d2<3.5){var a=Math.atan2(dx2,dz2)/D2R-o.yaw;a=((a+540)%360)-180;want=Math.max(-70,Math.min(70,a))*D2R}}
      o.hy+=(want-o.hy)*Math.min(1,dt*6);n.head.rotation.y=o.hy;if(o.nod>0){o.nod=Math.max(0,o.nod-dt);n.head.rotation.x=Math.sin(o.nod*14)*.25*o.nod/.6}}})}
/* ---------- input on the 3D view while playing: tap = walk there (or to the person tapped); left 45 % on touch = stick ---------- */
var tp={};var rc=new THREE.Raycaster(),plane=new THREE.Plane(new THREE.Vector3(0,1,0),0),hitV=new THREE.Vector3();
app.addEventListener('pointerdown',function(e){if(!PLY.on||e.target!==canvas)return;e.stopPropagation();e.preventDefault();
  if(e.pointerType==='touch'&&e.clientX<W*.45&&STK.id===null){STK.id=e.pointerId;STK.x=e.clientX;STK.y=e.clientY;STK.vx=STK.vy=0;stick.hidden=false;stick.style.transform='translate('+(e.clientX-60)+'px,'+(e.clientY-60)+'px)';stick.firstChild.style.transform='';cancelAuto();return}
  tp[e.pointerId]={x:e.clientX,y:e.clientY,t:performance.now(),m:0}},true);
app.addEventListener('pointermove',function(e){if(!PLY.on)return;if(e.pointerId===STK.id){STK.vx=e.clientX-STK.x;STK.vy=e.clientY-STK.y;var m=Math.hypot(STK.vx,STK.vy),c=Math.min(m,60)/(m||1);stick.firstChild.style.transform='translate('+(STK.vx*c)+'px,'+(STK.vy*c)+'px)';e.stopPropagation();return}
  var p=tp[e.pointerId];if(p){p.m+=Math.abs(e.clientX-p.x)+Math.abs(e.clientY-p.y);p.x=e.clientX;p.y=e.clientY;e.stopPropagation()}},true);
function upP(e){if(!PLY.on)return;if(e.pointerId===STK.id){STK.id=null;STK.vx=STK.vy=0;stick.hidden=true;e.stopPropagation();return}var p=tp[e.pointerId];delete tp[e.pointerId];if(!p)return;e.stopPropagation();
  if(p.m>12||performance.now()-p.t>400||PLY.ride||PL||!dlg.hidden||!modal.hidden)return;tapAt(e.clientX,e.clientY)}
app.addEventListener('pointerup',upP,true);app.addEventListener('pointercancel',upP,true);app.addEventListener('wheel',function(e){if(PLY.on&&e.target===canvas){e.stopPropagation();e.preventDefault()}},{capture:true,passive:false});
function tapAt(cx,cy){var r=canvas.getBoundingClientRect(),best=null,bd=46;NP.forEach(function(o){if(o.f!==PLY.f)return;var top=o.pose==='lie'?.8:o.pose==='sit'?1.1:1.2;v3b.set(o.x,o.f*FH+top,o.z).project(camera);var d=Math.hypot((v3b.x*.5+.5)*W-(cx-r.left),(-v3b.y*.5+.5)*H-(cy-r.top));if(d<bd){bd=d;best=o}});
  if(best){var t=standNear(best);goTo(best.f,t[0],t[1],function(){PLY.yaw=Math.atan2(best.x-PLY.x,best.z-PLY.z)/D2R});return}
  rc.setFromCamera(new THREE.Vector2((cx-r.left)/r.width*2-1,-((cy-r.top)/r.height)*2+1),camera);plane.constant=-PLY.f*FH;if(rc.ray.intersectPlane(plane,hitV)){var s=snap(PLY.f,hitV.x,hitV.z,1.2);if(s)goTo(PLY.f,s[0],s[1]);else toast('Acolo nu se poate merge')}}
/* ---------- the journal: missions per arc (the only way into the book) ---------- */
var jTab=0;
function openJournal(){modal.textContent='';modal.hidden=false;var box=DOM('div','pcard jr');modal.appendChild(box);var hd=DOM('div','jhd');hd.appendChild(DOM('h2',null,'Misiuni'));
  var dn=Object.keys(PR.done).length;hd.appendChild(DOM('small',null,'Kumar cap. 32 · '+dn+' / '+BK.facts.length+' fapte învățate'));var x=DOM('button','pbtn x');x.setAttribute('aria-label','Închide');x.innerHTML='<i class="ic-x"></i>';x.addEventListener('click',closeModal);hd.appendChild(x);box.appendChild(hd);
  var tabs=DOM('div','seg jtabs');MIS.arcs.forEach(function(a,i){var b=DOM('button',null,a.title);b.setAttribute('aria-pressed',i===jTab?'true':'false');b.addEventListener('click',function(){jTab=i;openJournal()});tabs.appendChild(b)});box.appendChild(tabs);
  var arc=MIS.arcs[jTab],list=DOM('div','qs');MIS.missions.forEach(function(m,mi){if(m.chapter!==arc.chapter)return;var d=mDone(m),n=mTotal(m),st=MS.end[m.id]?'Terminată':MS.acc[m.id]?'În curs':'Nouă',tr=tracked()===m;
    var c=DOM('div','mcard'+(tr?' tr':''));c.style.setProperty('--c',CHCOL[m.chapter]);var top=DOM('div','mt');top.appendChild(DOM('b',null,m.title));top.appendChild(DOM('span','st '+(MS.end[m.id]?'ok':MS.acc[m.id]?'on':''),st));c.appendChild(top);
    c.appendChild(DOM('p',null,m.case));c.appendChild(DOM('small',null,'Îți dă cazul: '+who(m.giver)+(m.patient?' · Pacient: '+who(m.patient):'')+' · '+m.quests.length+' etape · '+n+' pași'));c.appendChild(progressBar(d,n));
    var det=DOM('details');det.appendChild(DOM('summary',null,'Etapele cazului'));m.quests.forEach(function(q){var qd=q.steps.filter(function(s){return PR.fin[s.script]}).length,s0=q.steps[0],r=DOM('div','qrow'+(qd===q.steps.length?' ok':''));
      r.appendChild(DOM('b',null,(TOPICS[q.topic]?TOPICS[q.topic].t:q.topic)));r.appendChild(DOM('small',null,who(q.host)+' · '+(s0?roomTxt(s0.f,s0.room):'')+' · '+qd+'/'+q.steps.length));det.appendChild(r)});c.appendChild(det);
    var acts=DOM('div','acts');if(!tr&&!MS.end[m.id]){var tb=DOM('button','ghost','Urmărește');tb.addEventListener('click',function(){MS.track=mi;saveP();stage();openJournal()});acts.appendChild(tb)}
    if(!MS.end[m.id]){var gb=DOM('button',null,PLY.on?'Du-mă acolo':'Joacă');gb.addEventListener('click',function(){MS.track=mi;saveP();closeModal();if(!PLY.on)enterPlay();stage();goObjective()});acts.appendChild(gb)}
    c.appendChild(acts);list.appendChild(c)});box.appendChild(list)}
function closeModal(){modal.hidden=true;modal.textContent=''}
function openPause(){modal.textContent='';modal.hidden=false;var box=DOM('div','pcard pause');modal.appendChild(box);box.appendChild(DOM('h2',null,'Pauză'));
  [['Reia',closeModal,''],['Misiuni',openJournal,'ghost'],['Ieși din joc',function(){closeModal();exitPlay()},'ghost']].forEach(function(a){var b=DOM('button',a[2],a[0]);b.addEventListener('click',a[1]);box.appendChild(b)})}
/* ---------- enter / leave ---------- */
var saved=null;
function enterPlay(){if(PLY.on)return;if(PL)closePlayer();saved={labels:showLabels,marks:showMarks,fov:camera.fov};showLabels=false;showMarks=false;PLY.on=true;document.body.classList.add('playing');hud.hidden=false;
  var p=PR.pos;if(p&&walk(p.f,p.x,p.z)){PLY.f=p.f;PLY.x=p.x;PLY.z=p.z}else{var L=ROOMS.filter(function(r){return r.f===0&&r.entrance})[0]||ROOMBY.g_lobby,s=snap(0,(L.x0+L.x1)/2,(L.z0+L.z1)/2-1.5,4);PLY.f=0;PLY.x=s[0];PLY.z=s[1]}
  setView(PLY.f);if(ride)ride.t=1;CAM.ok=false;ME.f=PLY.f;place(ME);stage();sel=null;selBox.visible=false}
function exitPlay(){if(!PLY.on)return;PLY.on=false;PLY.au=null;document.body.classList.remove('playing');hud.hidden=true;dlg.hidden=true;showLabels=saved.labels;showMarks=saved.marks;camera.fov=saved.fov;camera.updateProjectionMatrix();fadeSeg(null);setView('all');mkG.visible=false}
/* the top bar: Play and the journal also from the model view */
var bp=DOM('button','jp-play','Joacă misiunile');bp.id='bPlay';var bj=DOM('button','jp-jr','Misiuni');bj.id='bJr';var tb2=$('top');tb2.insertBefore(bj,$('bookChip'));tb2.insertBefore(bp,bj);
bp.addEventListener('click',enterPlay);bj.addEventListener('click',openJournal);
/* the player (layer) reports the end of a script and its close */
window.__playHook=function(ev,fin){if(ev==='score'){PL0.correct=fin.correct;PL0.answered=fin.answered;return}if(PLAYM&&ev==='close')PLAYM.onClose(fin)};
var lastSave=0;
window.__playFrame=function(dt){if(!PLY.on){npcFrame(dt);return}moveP(dt);playCam(dt);wallFade();doorsFrame(dt);npcFrame(dt);IA.t+=dt;if(IA.t>.1){IA.t=0;scan()}renderPanel();markerFrame(dt);cloudFrame();
  if(greetT>0){greetT-=dt;if(greetT<=0)greetEl.hidden=true}if(!panel.hidden||!dlg.hidden)drawPortrait(dt);
  if(time-lastSave>5&&!PLY.ride){lastSave=time;PR.pos={f:PLY.f,x:+PLY.x.toFixed(2),z:+PLY.z.toFixed(2)};saveP()}};
window.__play={enter:enterPlay,exit:exitPlay,journal:openJournal,goObjective:goObjective,talk:function(){if(IA.cur&&IA.cur.o)talk(IA.cur.o)},state:function(){return{f:PLY.f,x:PLY.x,z:PLY.z,obj:OBJ&&{type:OBJ.type,who:OBJ.who,f:OBJ.f,x:OBJ.x,z:OBJ.z,m:OBJ.m.id},ia:IA.cur&&(IA.cur.o?(IA.cur.o.cast?IA.cur.o.cast.id:'amb'):'lift'),auto:!!PLY.au,ride:!!PLY.ride,npcs:NP.length,bake:bakeMs,mk:mkG.visible&&[+mkG.position.x.toFixed(2),+(mkG.position.y+mkD.position.y).toFixed(2),+mkG.position.z.toFixed(2)],mkHud:!mkEl.hidden&&mkEl.className}},
  dlg:function(){return!dlg.hidden&&{text:dlg.textContent}},click:function(label){var b=[].slice.call(document.querySelectorAll('.dlg button,.ia button,.pmod button')).filter(function(x){return x.textContent===label})[0];if(b)b.click();return!!b},
  reach:reachMap,
  audit:function(){var L=ROOMS.filter(function(r){return r.f===0&&r.entrance})[0]||ROOMBY.g_lobby,s0=snap(0,(L.x0+L.x1)/2,(L.z0+L.z1)/2-1,4),from={f:0,x:s0[0],z:s0[1]},bad=[],n=0,far=[];
    function chk(tag,f,x,z){n++;var t=standNear({f:f,x:x,z:z,yaw:0});if(!reachable(f,t[0],t[1])){var R=roomAt(f,x,z);bad.push(tag+' @'+(R?R.id:'?'))}}
    MIS.missions.forEach(function(m){m.quests.forEach(function(q){q.steps.forEach(function(st){var pz=st.patient&&st.patient.cast===st.presenter,ts=pz?null:talkSpot(st.f,st.stand[0],st.stand[1],st.script);
      var x=pz?st.patient.x:ts?ts[0]:st.stand[0],z=pz?st.patient.z:ts?ts[1]:st.stand[1];n++;if(!talkable(st.f,x,z,pz?2.4:1.9))bad.push(st.script+' @'+st.room)})})});
    Object.keys(castNP).forEach(function(id){var o=castNP[id];n++;if(!talkable(o.home.f,o.home.x,o.home.z,o.home.pose==='lie'?2.4:1.9))bad.push('post '+id)});
    var mv={};MOVED.forEach(function(t){mv[t]=1});return{checked:n,unreachable:bad,moved:Object.keys(mv)}},
  teleport:function(f,x,z){var s=snap(f,x,z,3);if(!s)return false;PLY.f=f;PLY.x=s[0];PLY.z=s[1];setView(f);if(ride)ride.t=1;CAM.ok=false;place(ME);return true},walk:walk,route:route,MS:MS,stage:stage,NP:NP};
stage();
})()}
