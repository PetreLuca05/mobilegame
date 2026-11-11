// NPCK: procedural "Jelly Pop" toon characters for the hospital model page (three.js r128, ES5).
// One global. NPCK.build(look,opt) / NPCK.pose(npc,pose,t,k) / NPCK.head(look).
var NPCK=(function(){
  var SKIN=['#f8d7bd','#e6b48c','#b9835a','#7b5035'];
  var R=0.14, HIP=0.80, NECK=1.41, HEADC=1.56, ARMY=1.33, SEAT=0.46, LEGX=0.085;
  var PI=Math.PI, TAU=PI*2;
  var EYE=[0.035,0.03,0.05], DARK=[0.18,0.20,0.25], STETH=[0.16,0.17,0.21];
  var BAGS=['#d9534f','#f0ad4e','#3f7fbf','#4a9d6f','#8e6bbf'];
  var LOD=[
    {nt:8,nh:8,mh:5,nl:5,ne:6,ms:3,det:1},
    {nt:6,nh:6,mh:4,nl:4,ne:4,ms:2,det:0}
  ];
  var PORTRAIT={nt:8,nh:16,mh:9,nl:5,ne:12,ms:3,det:2};
  var cache={}, toonMat=null, lineMat=null;

  // ---------- materials ----------
  function mats(){
    if(toonMat) return;
    var tex=new THREE.DataTexture(new Uint8Array([165,255]),2,1,THREE.LuminanceFormat);
    tex.minFilter=THREE.NearestFilter; tex.magFilter=THREE.NearestFilter; tex.generateMipmaps=false; tex.needsUpdate=true;
    toonMat=new THREE.MeshToonMaterial({vertexColors:true,gradientMap:tex});
    lineMat=new THREE.ShaderMaterial({
      uniforms:{w:{value:0.018}},
      // eyes (the only near-black vertex colour) get no hull: a hull around a stub on the face leaves a skin gap
      vertexShader:'uniform float w;void main(){float k=step(0.06,max(color.r,max(color.g,color.b)));gl_Position=projectionMatrix*modelViewMatrix*vec4(position+normal*(w*k),1.0);}',
      fragmentShader:'void main(){gl_FragColor=vec4(0.0,0.0,0.0,1.0);}',
      side:THREE.BackSide, vertexColors:true
    });
  }

  // ---------- colour helpers ----------
  function hex(h){ var c=new THREE.Color(h); return [c.r,c.g,c.b]; }
  function mix(a,b,t){ return [a[0]+(b[0]-a[0])*t,a[1]+(b[1]-a[1])*t,a[2]+(b[2]-a[2])*t]; }
  function shade(a,f){ return [Math.min(1,a[0]*f),Math.min(1,a[1]*f),Math.min(1,a[2]*f)]; }
  function rng(seed){
    var a=((seed|0)*2654435761)|0;
    return function(){ a=(a+0x6D2B79F5)|0; var t=Math.imul(a^(a>>>15),1|a); t=(t+Math.imul(t^(t>>>7),61|t))^t; return ((t^(t>>>14))>>>0)/4294967296; };
  }

  // ---------- tiny geometry kit: everything is a "lathe" of elliptical rings ----------
  function Acc(){ return {p:[],c:[]}; }
  function vtx(acc,m,p,col){
    var x=p[0],y=p[1],z=p[2];
    if(m){ var e=m.elements; acc.p.push(e[0]*x+e[4]*y+e[8]*z+e[12], e[1]*x+e[5]*y+e[9]*z+e[13], e[2]*x+e[6]*y+e[10]*z+e[14]); }
    else acc.p.push(x,y,z);
    acc.c.push(col[0],col[1],col[2]);
  }
  function tri(acc,m,a,b,c,col){ vtx(acc,m,a,col); vtx(acc,m,b,col); vtx(acc,m,c,col); }
  // rings: [y, rx, rz, cz, cx] ascending in y; rx==0 is a pole. a=0 is +Z. col: [r,g,b] or fn(band,seg).
  function lathe(acc,m,rings,n,col,ph,bump){
    var pts=[],j,i,r,row,a,f,c,lo,hi,p00,p01,p10,p11;
    ph=ph||0;
    for(j=0;j<rings.length;j++){
      r=rings[j]; row=[];
      for(i=0;i<=n;i++){
        a=((i%n)+ph)*TAU/n; f=bump?bump(j,i%n):1;
        row.push([(r[4]||0)+r[1]*f*Math.sin(a), r[0], (r[3]||0)+r[2]*f*Math.cos(a)]);
      }
      pts.push(row);
    }
    for(j=0;j<rings.length-1;j++){
      lo=rings[j][1]===0; hi=rings[j+1][1]===0;
      for(i=0;i<n;i++){
        c=(typeof col==='function')?col(j,i):col;
        p00=pts[j][i]; p01=pts[j][i+1]; p10=pts[j+1][i]; p11=pts[j+1][i+1];
        if(!lo&&!hi){ tri(acc,m,p00,p01,p11,c); tri(acc,m,p00,p11,p10,c); }
        else if(lo&&!hi) tri(acc,m,p00,p11,p10,c);
        else if(!lo&&hi) tri(acc,m,p00,p01,p11,c);
      }
    }
  }
  function ellipsoid(acc,m,cx,cy,cz,rx,ry,rz,n,mb,col,ph){
    var rings=[],j,phi,k;
    for(j=0;j<=mb;j++){
      phi=-PI/2+PI*j/mb; k=(j===0||j===mb)?0:Math.cos(phi);
      rings.push([cy+ry*Math.sin(phi), rx*k, rz*k, cz, cx]);
    }
    lathe(acc,m,rings,n,col,ph);
  }
  function box(acc,m,cx,cy,cz,w,h,d,col){
    var s=0.70711;
    lathe(acc,m,[[cy-h/2,0,0,cz,cx],[cy-h/2,w*s,d*s,cz,cx],[cy+h/2,w*s,d*s,cz,cx],[cy+h/2,0,0,cz,cx]],4,col,0.5);
  }
  function mul(m0,m){ return m0?m0.clone().multiply(m):m; }
  function xf(tx,ty,tz,rx,ry,rz){
    var m=new THREE.Matrix4();
    m.makeRotationFromEuler(new THREE.Euler(rx||0,ry||0,rz||0));
    m.setPosition(tx,ty,tz);
    return m;
  }
  // open tube from a to b
  function tube(acc,m0,a,b,r,n,col){
    var va=new THREE.Vector3(a[0],a[1],a[2]), vb=new THREE.Vector3(b[0],b[1],b[2]);
    var d=vb.clone().sub(va), L=d.length(); d.normalize();
    var q=new THREE.Quaternion().setFromUnitVectors(new THREE.Vector3(0,1,0),d);
    var m=new THREE.Matrix4().compose(va.add(vb).multiplyScalar(0.5),q,new THREE.Vector3(1,1,1));
    lathe(acc,mul(m0,m),[[-L/2,r,r],[L/2,r,r]],n,col,0);
  }
  function addGeo(acc,geo,m){
    var p=geo.attributes.position.array,c=geo.attributes.color.array,i;
    for(i=0;i<p.length;i+=3) vtx(acc,m,[p[i],p[i+1],p[i+2]],[c[i],c[i+1],c[i+2]]);
  }
  // non-indexed geometry with normals smoothed over coincident positions (so the hull outline has no cracks)
  function toGeo(acc){
    var p=acc.p,nv=p.length/3,nor=new Float32Array(p.length),map={},keys=new Array(nv),i,j,k,key,l,e,f,
        ax,ay,az,bx,by,bz,nx,ny,nz,len,dup;
    for(i=0;i<nv;i+=3){
      j=i*3;
      ax=p[j+3]-p[j]; ay=p[j+4]-p[j+1]; az=p[j+5]-p[j+2];
      bx=p[j+6]-p[j]; by=p[j+7]-p[j+1]; bz=p[j+8]-p[j+2];
      nx=ay*bz-az*by; ny=az*bx-ax*bz; nz=ax*by-ay*bx;
      len=Math.sqrt(nx*nx+ny*ny+nz*nz); if(len<1e-12) len=1;
      nx/=len; ny/=len; nz/=len;
      for(k=0;k<3;k++){
        e=(i+k)*3;
        key=Math.round(p[e]*4000)+'_'+Math.round(p[e+1]*4000)+'_'+Math.round(p[e+2]*4000);
        keys[i+k]=key; l=map[key]; if(!l){ l=map[key]=[]; }
        dup=false;
        for(f=0;f<l.length;f+=3) if(l[f]*nx+l[f+1]*ny+l[f+2]*nz>0.985){ dup=true; break; }
        if(!dup) l.push(nx,ny,nz);
      }
    }
    for(key in map){
      l=map[key]; nx=0; ny=0; nz=0;
      for(f=0;f<l.length;f+=3){ nx+=l[f]; ny+=l[f+1]; nz+=l[f+2]; }
      len=Math.sqrt(nx*nx+ny*ny+nz*nz);
      if(len<1e-6){ nx=l[0]; ny=l[1]; nz=l[2]; len=1; }
      map[key]=[nx/len,ny/len,nz/len];
    }
    for(i=0;i<nv;i++){ l=map[keys[i]]; nor[i*3]=l[0]; nor[i*3+1]=l[1]; nor[i*3+2]=l[2]; }
    var g=new THREE.BufferGeometry();
    g.setAttribute('position',new THREE.BufferAttribute(new Float32Array(p),3));
    g.setAttribute('normal',new THREE.BufferAttribute(nor,3));
    g.setAttribute('color',new THREE.BufferAttribute(new Float32Array(acc.c),3));
    g.computeBoundingSphere();
    g.userData.tris=nv/3;
    return g;
  }

  // ---------- look -> spec ----------
  function lookKey(l){
    return [l.kind,l.sex,l.age|0,l.skin|0,l.hair,l.hair_color,l.outfit,l.under,l.seed|0].join('|');
  }
  function spec(look){
    var kind=look.kind||'visitor', seed=look.seed|0, rnd=rng(seed), age=look.age|0, fem=look.sex==='f';
    var out=shade(hex(look.outfit||'#ffffff'),0.95+rnd()*0.07);
    var und=shade(hex(look.under||'#7fa8c9'),0.94+rnd()*0.1);
    var skin=hex(SKIN[Math.max(0,Math.min(3,look.skin|0))]);
    var hair=shade(hex(look.hair_color||'#3b2a20'),0.9+rnd()*0.2);
    if(age===2) hair=mix(hair,[0.84,0.84,0.86],0.68);
    var s=(1+(rnd()*2-1)*0.04)*(age===2?0.955:(age===0?0.985:1));
    var trousers=mix(und,DARK,0.78);
    var o={
      kind:kind, seed:seed, fem:fem, age:age, s:s, phase:rnd()*TAU,
      skin:skin, hairCol:hair, hair:look.hair||'short',
      hem:0, open:false, top:out, front:null, vfront:null, collar:null, vneck:false, trim:null,
      legU:trousers, legL:trousers, thighSit:null, shoe:[0.2,0.2,0.24],
      slU:out, slL:out,
      stetho:false, lanyard:null, clip:null, glasses:false, cap:null, mask:false, fob:false, pack:null
    };
    var b1=(seed&1)===1, b2=(seed&2)===2;
    if(kind==='doctor'){ o.hem=0.50; o.front=und; o.stetho=true; if(b1) o.clip=[0.86,0.74,0.55]; }
    else if(kind==='resident'||kind==='student'){
      o.hem=0.66; o.front=und; o.legU=o.legL=shade(und,0.93); o.shoe=[0.93,0.93,0.95];
      o.lanyard=kind==='student'?[0.90,0.36,0.30]:[0.20,0.45,0.85];
      if(kind==='student') o.clip=hex(BAGS[seed%BAGS.length]);
    }
    else if(kind==='surgeon'){
      o.vneck=true; o.legU=o.legL=shade(out,0.92); o.slL=skin; o.mask=true; o.shoe=[0.90,0.93,0.95];
      o.cap=b1?[mix(out,[1,1,1],0.55),shade(out,0.72)]:[shade(out,0.74),shade(out,0.74)];
    }
    else if(kind==='nurse'){ o.vneck=true; o.legU=o.legL=shade(out,0.9); o.slL=skin; o.fob=b1; o.shoe=[0.95,0.95,0.96]; o.trim=null; }
    else if(kind==='tech'){ o.hem=0.60; o.vfront=und; }
    else if(kind==='researcher'){ o.hem=0.60; o.front=und; o.glasses=true; }
    else if(kind==='pharmacist'){ o.hem=0.56; o.vfront=und; o.collar=und; }
    else if(kind==='librarian'){ o.hem=0.70; o.front=und; o.glasses=true; o.shoe=[0.36,0.25,0.2]; }
    else if(kind==='guide'){ o.hem=0.70; o.front=und; o.lanyard=[0.95,0.72,0.2]; }
    else if(kind==='patient'){
      o.hem=0.54; o.trim=mix(out,[0.25,0.45,0.75],0.38); o.collar=o.trim;
      o.legU=o.legL=skin; o.thighSit=out; o.slL=skin; o.shoe=[0.93,0.89,0.80];
    }
    else { // visitor
      o.legU=o.legL=und; if(b2) o.slL=skin; if(b1) o.pack=hex(BAGS[(seed>>2)%BAGS.length]);
      o.shoe=mix(und,[0.95,0.95,0.95],0.55);
    }
    return o;
  }

  // ---------- parts ----------
  function torsoRings(sp,sit){
    var f=sp.fem, sx=f?0.180:0.198, cx=f?0.176:0.190, wx=f?0.152:0.166, hx=f?0.174:0.164;
    var st=sp.age===2?1:0;
    var skirt=sp.hem>0&&!sit, rings=[], tags=[];
    if(skirt){
      var hr=hx+0.022+(0.84-sp.hem)*0.07, hz=0.128+(0.84-sp.hem)*0.07;
      rings.push([sp.hem,0,0]); tags.push('cap');
      rings.push([sp.hem,hr,hz]);
      if(sp.trim){ tags.push('trim'); rings.push([sp.hem+0.07,hr-0.003,hz-0.003]); }
      tags.push('skirt');
    } else {
      rings.push([0.695,0,0]); tags.push('cap');
      rings.push([0.71,hx*0.86,0.105]); tags.push('hip');
    }
    rings.push([0.84,hx,0.126]); tags.push('belly');
    rings.push([1.02,wx,0.118,st*0.006]); tags.push('chest');
    rings.push([1.22,cx,0.136,st*0.016]); tags.push('upper');
    rings.push([1.335,sx,0.120,st*0.03]); tags.push('yoke');
    rings.push([1.395,0.118,0.088,st*0.04]); tags.push('neck');
    rings.push([1.428,0.056,0.056,st*0.045]); tags.push('neck');
    rings.push([1.44,0,0,st*0.045]);
    return {rings:rings,tags:tags,sx:sx,neckZ:st*0.05,armZ:st*0.028};
  }
  function buildTorso(sp,D,sit){
    var T=torsoRings(sp,sit), acc=Acc(), n=D.nt, rings=T.rings, tags=T.tags;
    var capCol=sp.hem>0&&!sit?shade(sp.top,0.72):sp.legU;
    function col(b,i){
      var t=tags[b], fr=(i===n-1);
      if(t==='cap') return capCol;
      if(t==='neck') return sp.skin;
      if(t==='trim') return sp.trim;
      if(t==='skirt') return fr&&sp.front?(sp.hem<0.69?sp.legU:sp.front):sp.top;
      if(t==='hip'||t==='belly'||t==='chest') return fr&&sp.front?sp.front:sp.top;
      if(t==='upper') return fr?(sp.front||sp.vfront||sp.top):sp.top;
      // yoke
      if(fr) return sp.vneck?sp.skin:(sp.front||sp.collar||sp.vfront||sp.top);
      return sp.collar||sp.top;
    }
    lathe(acc,null,rings,n,col,0.5);
    // front surface z of the torso at height y and offset x
    function fz(y,x){
      var j,a,b,t,rx,rz,cz,q;
      for(j=1;j<rings.length-2;j++) if(rings[j+1][0]>=y) break;
      a=rings[j]; b=rings[j+1]; t=Math.max(0,Math.min(1,(y-a[0])/(b[0]-a[0])));
      rx=a[1]+(b[1]-a[1])*t; rz=a[2]+(b[2]-a[2])*t; cz=(a[3]||0)+((b[3]||0)-(a[3]||0))*t;
      q=1-(x*x)/(rx*rx); if(q<0) q=0;
      return cz+rz*Math.sqrt(q)*Math.cos(PI/n);
    }
    var sg,x,e=0.012,nz=T.neckZ;
    if(sp.stetho){
      for(sg=-1;sg<=1;sg+=2){
        tube(acc,null,[sg*0.082,1.416,nz-0.06],[sg*0.086,1.412,nz+0.035],0.011,3,STETH);
        tube(acc,null,[sg*0.086,1.414,nz+0.03],[sg*0.07,1.30,fz(1.30,0.07)+e],0.011,3,STETH);
        tube(acc,null,[sg*0.07,1.305,fz(1.30,0.07)+e],[sg*0.012,1.175,fz(1.175,0)+e],0.011,3,STETH);
      }
      ellipsoid(acc,null,0,1.165,fz(1.165,0)+0.008,0.027,0.027,0.016,6,2,[0.72,0.75,0.8],0);
    }
    if(sp.lanyard&&D.det){
      for(sg=-1;sg<=1;sg+=2)
        tube(acc,null,[sg*0.075,1.405,nz+0.04],[sg*0.008,1.19,fz(1.19,0)+0.01],0.012,3,sp.lanyard);
      box(acc,null,0,1.15,fz(1.15,0)+0.008,0.058,0.075,0.012,[0.97,0.97,0.98]);
    }
    if(sp.fob&&D.det) box(acc,null,-0.085,1.235,fz(1.235,0.085)+0.006,0.034,0.05,0.012,[0.95,0.8,0.3]);
    if(sp.pack) ellipsoid(acc,null,0,1.17,-0.175,0.125,0.16,0.085,6,D.ms,sp.pack,0);
    return {geo:toGeo(acc),T:T};
  }
  function buildLeg(sp,D){
    var acc=Acc(), n=D.nl, ph=n===4?0.5:0;
    lathe(acc,null,[[-0.75,0.056,0.056],[-0.40,0.066,0.066],[-0.03,0.078,0.078]],n,function(b){ return b===0?sp.legL:sp.legU; },ph);
    ellipsoid(acc,null,0,-0.755,0.032,0.064,0.045,0.108,n,D.ms,sp.shoe,ph);
    return toGeo(acc);
  }
  function buildLegSit(sp,D){
    var acc=Acc(), n=D.nl, ph=n===4?0.5:0, th=sp.thighSit||sp.legU;
    lathe(acc,xf(0,0,0,PI/2,0,0),[[-0.04,0.08,0.08],[0.41,0.072,0.072],[0.47,0,0]],n,th,ph);
    lathe(acc,xf(0,0,0.40),[[-0.415,0.056,0.056],[0,0.068,0.068]],n,sp.legL,ph);
    ellipsoid(acc,null,0,-SEAT+0.045,0.432,0.064,0.045,0.108,n,D.ms,sp.shoe,ph);
    return toGeo(acc);
  }
  function buildArm(sp,D,item){
    var acc=Acc(), n=D.nl, ph=n===4?0.5:0;
    lathe(acc,null,[[-0.47,0.045,0.045],[-0.24,0.051,0.051],[0,0.058,0.058],[0.04,0.04,0.04],[0.053,0,0]],n,
      function(b){ return b===0?sp.slL:sp.slU; },ph);
    ellipsoid(acc,null,0,-0.5,0,0.05,0.06,0.05,n,D.ms,sp.skin,ph);
    if(item){
      if(sp.kind==='doctor'){ box(acc,null,0.06,-0.47,0.03,0.016,0.24,0.17,item); }
      else box(acc,null,0.062,-0.49,0.02,0.026,0.17,0.125,item);
    }
    return toGeo(acc);
  }
  function scaleRings(a,k){
    var o=[],i; for(i=0;i<a.length;i++) o.push([a[i][0]*k,a[i][1]*k,a[i][2]*k,(a[i][3]||0)*k]); return o;
  }
  // head centre at the origin of m0
  function addHead(acc,m0,sp,D){
    var n=D.nh, sg, m, i, st=sp.hair, hc=sp.hairCol;
    ellipsoid(acc,m0,0,0,0,R,R*1.02,R,n,D.mh,sp.skin,0.5);
    var ey=-0.10*R, er=R*Math.cos(PI/n)/Math.cos(Math.min(PI/n,0.37)), ek=1;
    for(sg=-1;sg<=1;sg+=2){
      m=mul(m0,xf(0,0,0,0,sg*0.37,0)).multiply(xf(0,ey,er,PI/2,0,0));
      lathe(acc,m,[[-0.14*R,0.115*R*ek,0.19*R*ek],[0.035*R,0.115*R*ek,0.19*R*ek],[0.07*R,0,0]],D.ne,EYE,0);
    }
    if(sp.glasses&&D.det){
      var ng=D.det===2?12:6, r1=0.215*R, r2=0.27*R, gz=er+0.07*R, a0,a1,gc=[0.055,0.05,0.058];
      for(sg=-1;sg<=1;sg+=2){
        m=mul(m0,xf(0,0,0,0,sg*0.37,0)).multiply(xf(0,ey,gz));
        for(i=0;i<ng;i++){
          a0=i*TAU/ng; a1=(i+1)*TAU/ng;
          tri(acc,m,[r1*Math.cos(a0),r1*Math.sin(a0),0],[r2*Math.cos(a0),r2*Math.sin(a0),0],[r2*Math.cos(a1),r2*Math.sin(a1),0],gc);
          tri(acc,m,[r1*Math.cos(a0),r1*Math.sin(a0),0],[r2*Math.cos(a1),r2*Math.sin(a1),0],[r1*Math.cos(a1),r1*Math.sin(a1),0],gc);
        }
        tube(acc,m0,[sg*(gz*Math.sin(0.37)+r2*0.9),ey,gz*Math.cos(0.37)-0.02*R],[sg*1.0*R,ey+0.02*R,0.05*R],0.02*R,3,gc);
      }
      tube(acc,m0,[-0.13*R,ey+0.03*R,gz+0.02*R],[0.13*R,ey+0.03*R,gz+0.02*R],0.02*R,3,gc);
    }
    var shortR=[[-0.30,0,0,-0.45],[-0.28,1.04,0.67,-0.45],[0.18,1.09,0.97,-0.15],[0.62,0.93,0.93,-0.06],[0.95,0.53,0.53,-0.03],[1.09,0,0,-0.02]];
    var longLow=[[-1.55,0,0,-0.6],[-1.5,0.8,0.4,-0.6],[-0.8,1.02,0.5,-0.58]];
    var top=1.02, rings;
    if(sp.cap){
      if(st!=='bald'){
        rings=(st==='long'?longLow:[shortR[0]]).concat([[-0.28,1.05,0.68,-0.45],[0.18,1.07,0.95,-0.15],[0.4,0,0,-0.1]]);
        lathe(acc,m0,scaleRings(rings,R),n,hc,0.5);
      }
      var cc=sp.cap;
      lathe(acc,m0,scaleRings([[0.14,1.08,1.08,-0.02],[0.22,1.13,1.13,-0.03],[0.78,1.12,1.12,-0.05],[1.1,0.78,0.78,-0.05],[1.2,0,0,-0.05]],R),n,
        function(b,i){ return b===0?shade(cc[1],0.85):((i&1)?cc[0]:cc[1]); },0.5);
      top=1.2;
    } else if(st!=='bald'){
      if(st==='curly'){
        rings=[[-0.45,0,0,-0.3],[-0.42,1.12,0.8,-0.3],[0.15,1.22,1.08,-0.12],[0.68,1.06,1.06,-0.05],[1.05,0.6,0.6,-0.03],[1.2,0,0,-0.02]];
        lathe(acc,m0,scaleRings(rings,R),n,hc,0.5,function(j,i){ return 1+0.085*(((i+j)&1)?1:-1); });
        top=1.2;
      } else {
        rings=st==='long'?longLow.concat([[-0.28,1.06,0.68,-0.45]]).concat(shortR.slice(2)):shortR;
        lathe(acc,m0,scaleRings(rings,R),n,hc,0.5);
        top=1.09;
        if(st==='bun') ellipsoid(acc,m0,0,0.80*R,-0.80*R,0.42*R,0.40*R,0.42*R,D.det===2?10:6,D.det===2?5:3,shade(hc,0.92),0);
        if(st==='ponytail'){
          m=mul(m0,xf(0,0.42*R,-0.98*R,0.5,0,0));
          ellipsoid(acc,m,0,-0.62*R,0,0.27*R,0.74*R,0.25*R,D.det===2?10:6,D.det===2?5:3,shade(hc,0.92),0);
        }
      }
    }
    if(sp.mask&&D.det!==2) box(acc,m0,0,-1.06*R,0.72*R,0.6*R,0.3*R,0.1*R,[0.75,0.89,0.94]);
    return top;
  }

  function parts(look,lod){
    var key=lookKey(look)+'#'+lod, g=cache[key];
    if(g) return g;
    var sp=spec(look), D=LOD[lod], t=buildTorso(sp,D,false), acc=Acc();
    var top=addHead(acc,xf(0,HEADC-NECK,0),sp,D);
    g={
      sp:sp, torso:t.geo, torsoSit:sp.hem>0?buildTorso(sp,D,true).geo:t.geo,
      head:toGeo(acc), leg:buildLeg(sp,D), legSit:buildLegSit(sp,D),
      armL:buildArm(sp,D,sp.clip), armR:buildArm(sp,D,null),
      armX:t.T.sx+0.032, armZ:t.T.armZ, neckZ:t.T.neckZ,
      height:sp.s*(HEADC+top*R)
    };
    cache[key]=g;
    return g;
  }

  // ---------- assembly ----------
  function mk(geo,ol){
    var m=new THREE.Mesh(geo,toonMat), o=null;
    m.castShadow=false; m.receiveShadow=false;
    if(ol){ o=new THREE.Mesh(geo,lineMat); o.castShadow=false; o.receiveShadow=false; m.add(o); }
    m.userData.ol=o;
    return m;
  }
  function pivot(geo,ol,x,y,z){
    var p=new THREE.Object3D(); p.position.set(x,y,z);
    var m=mk(geo,ol); p.add(m); p.userData.mesh=m;
    return p;
  }
  function setGeo(p,geo){
    var m=p.userData.mesh||p;
    if(m.geometry!==geo){ m.geometry=geo; if(m.userData.ol) m.userData.ol.geometry=geo; }
  }
  function assemble(g,ol){
    var root=new THREE.Group(), body=new THREE.Group(), head=new THREE.Group();
    var torso=mk(g.torso,ol);
    root.scale.setScalar(g.sp.s); root.add(body); body.add(torso);
    head.position.set(0,NECK,g.neckZ); head.add(mk(g.head,ol)); body.add(head);
    var legL=pivot(g.leg,ol,LEGX,HIP,0), legR=pivot(g.leg,ol,-LEGX,HIP,0);
    var armL=pivot(g.armL,ol,g.armX,ARMY,g.armZ), armR=pivot(g.armR,ol,-g.armX,ARMY,g.armZ);
    body.add(legL); body.add(legR); body.add(armL); body.add(armR);
    var npc={root:root,body:body,head:head,legL:legL,legR:legR,armL:armL,armR:armR,height:g.height,lod:0,
      _d:{g:g,torso:torso,ph:0,t:null}};
    pose(npc,'stand',0,0);
    return npc;
  }
  function build(look,opt){
    mats(); opt=opt||{};
    var ol=opt.outline!==false;
    if(!opt.lod) return assemble(parts(look,0),ol);
    var p=opt.pose==='sit'||opt.pose==='lie'?opt.pose:'stand';
    var key=lookKey(look)+'#1#'+p, c=cache[key];
    if(!c){
      var tmp=assemble(parts(look,1),false), acc=Acc();
      pose(tmp,p,0,0); tmp._d.torso.scale.set(1,1,1);
      tmp.root.updateMatrixWorld(true);
      tmp.root.traverse(function(o){ if(o.isMesh) addGeo(acc,o.geometry,o.matrixWorld); });
      c=cache[key]={geo:toGeo(acc),height:tmp.height};
    }
    var root=new THREE.Group(); root.add(mk(c.geo,ol));
    return {root:root,body:null,head:null,legL:null,legR:null,armL:null,armR:null,height:c.height,lod:1};
  }

  // ---------- poses ----------
  function tri2(x){ return Math.asin(0.96*Math.sin(x))/1.287; } // soft triangle wave: near-constant foot speed
  function pose(npc,p,t,k){
    var d=npc&&npc._d; if(!d) return;
    var g=d.g, s=g.sp.s, body=npc.body, lL=npc.legL, lR=npc.legR, aL=npc.armL, aR=npc.armR, torso=d.torso;
    t=t||0; if(k===undefined||k===null) k=1;
    var sit=p==='sit';
    setGeo(lL,sit?g.legSit:g.leg); setGeo(lR,sit?g.legSit:g.leg); setGeo(torso,sit?g.torsoSit:g.torso);
    body.position.set(0,0,0); body.rotation.set(0,0,0);
    lL.rotation.set(0,0,0); lR.rotation.set(0,0,0); lL.scale.set(1,1,1); lR.scale.set(1,1,1);
    aL.rotation.set(0,0,0.07); aR.rotation.set(0,0,-0.07);
    var br=Math.sin(t*TAU/3.5+g.sp.phase), up=0.005*br;
    torso.scale.set(1+0.006*br,1+0.003*br,1+0.022*br);
    aL.position.y=ARMY+up; aR.position.y=ARMY+up; npc.head.position.y=NECK+up;
    var dt=d.t===null?0:t-d.t; if(!(dt>0&&dt<0.5)) dt=0; d.t=t;
    if(p==='walk'){
      if(k<0) k=0;
      var amp=k<=1?0.42*k:Math.min(0.8,0.42+(k-1)*0.55);
      d.ph+=dt*TAU/0.9*Math.max(1,k);
      if(d.ph>TAU*64) d.ph-=TAU*64;
      var a=amp*tri2(d.ph), sw=amp*Math.sin(d.ph);
      lL.rotation.x=a; lR.rotation.x=-a;
      aL.rotation.x=-sw*0.85; aR.rotation.x=sw*0.85;
      body.position.y=-HIP*(1-Math.cos(a))+0.006*amp+(k>1?0.05*Math.min(1,k-1)*Math.abs(Math.sin(d.ph)):0);
      if(k>1) body.rotation.x=Math.min(0.16,(k-1)*0.2);
    } else if(p==='sit'){
      body.position.y=SEAT/s-HIP;
      lL.scale.setScalar(1/s); lR.scale.setScalar(1/s);
      aL.rotation.set(-0.66,0,-0.2); aR.rotation.set(-0.66,0,0.2);
    } else if(p==='lie'){
      body.rotation.x=-PI/2;
      body.position.set(0,0.132,HEADC-0.75/s);
      aL.rotation.z=0.1; aR.rotation.z=-0.1;
      torso.scale.set(1,1,1+0.022*br);
    } else if(p==='talk'){
      aR.rotation.set(-1.0+0.22*Math.sin(t*5.3+g.sp.phase),0,0.22+0.12*Math.sin(t*3.1));
      aL.rotation.x=-0.08+0.04*Math.sin(t*2.3);
    }
  }

  // ---------- portrait ----------
  function head(look){
    mats();
    var key=lookKey(look)+'#head', geo=cache[key];
    if(!geo){ var acc=Acc(); addHead(acc,null,spec(look),PORTRAIT); geo=cache[key]=toGeo(acc); }
    var grp=new THREE.Group(); grp.add(mk(geo,false));
    return grp;
  }

  return {build:build,pose:pose,head:head};
})();
