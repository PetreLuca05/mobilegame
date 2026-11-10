/* ===== book layer: Kumar ch. 32 inside the institute (props, NPC points, room stations, the player, progress) ===== */
var BK=window.BOOK||null,bookOn=true,selA=null,CHC=['c0','c1','c2','c3','c4'];
var KIND_RO={station:'Stație',inspect:'Inspectează',read:'Citește',case:'Caz',colleague:'Coleg',dispense:'Eliberează',directory:'Panou',sort:'Sortează',sequence:'Ordine'};
var PR={done:{},fin:{}};try{var pj=JSON.parse(localStorage.getItem('gastro-book-v1')||'{}');PR.done=pj.done||{};PR.fin=pj.fin||{}}catch(e){}
function saveP(){try{localStorage.setItem('gastro-book-v1',JSON.stringify(PR))}catch(e){}}
var ROOMBY={};ROOMS.forEach(function(r){ROOMBY[r.id]=r});
var factsOfTopic={},anchorsOfRoom={};
if(BK){BK.facts.forEach(function(t,i){(factsOfTopic[BK.topics[t]]=factsOfTopic[BK.topics[t]]||[]).push(i)});BK.anchors.forEach(function(a,i){(anchorsOfRoom[a.room]=anchorsOfRoom[a.room]||[]).push(i)})}
function roomFacts(r){var o=[];tops(r).forEach(function(t){o=o.concat(factsOfTopic[t]||[])});return o}
function doneOf(list){var n=0;list.forEach(function(f){if(PR.done[f])n++});return n}
function scriptDone(s){return!!PR.fin[BK.scripts[s].id]}
/* props: local frame u (across), v (towards the facing side), on the storey floor; yaws are multiples of 90 deg */
function frame3(mb,a){var t=a.yaw*Math.PI/180,fx=Math.round(Math.sin(t)),fz=Math.round(Math.cos(t)),rx=fz,rz=-fx,y=a.f*FH+.01;
  return function(u,v,y0,su,h,sv,col){var x=a.x+u*rx+v*fx,z=a.z+u*rz+v*fz,ax=Math.abs(rx)>.5;mb.b(x,y+y0,z,ax?su:sv,h,ax?sv:su,col)}}
var KB={
 poster:function(B,c){B(0,0,1.05,.74,.98,.04,COL.white);B(0,.025,1.1,.64,.88,.02,COL[c]);B(0,.04,1.75,.44,.07,.01,COL.white);B(0,.04,1.55,.5,.04,.01,COL.white);B(0,.04,1.42,.38,.04,.01,COL.white)},
 plaque:function(B,c){B(0,0,1.45,.42,.3,.04,COL.white);B(0,.022,1.48,.36,.24,.01,COL[c]);B(0,.03,1.62,.26,.04,.01,COL.white)},
 board:function(B,c){B(0,0,.88,1.62,1.04,.06,COL.steel);B(0,.035,.92,1.54,.96,.02,COL.white);B(0,.06,.86,1.5,.04,.12,COL.steel);B(-.35,.05,1.5,.6,.05,.01,COL[c]);B(.3,.05,1.25,.7,.04,.01,COL.c0)},
 lightbox:function(B,c){B(0,0,1.15,.92,.66,.08,COL.white);B(-.2,.045,1.2,.38,.52,.01,COL.screen);B(.22,.045,1.2,.38,.52,.01,COL.screen);B(0,.05,1.12,.2,.04,.01,COL[c])},
 shelf:function(B,c){B(0,0,0,1.2,1.9,.4,COL.wood2);for(var k=0;k<4;k++)for(var i=0;i<4;i++)B(-.45+i*.3,.18,.22+k*.44,.22,.3,.04,COL[[c,'white','c2','c1'][(i+k)%4]])},
 cabinet:function(B,c){B(0,0,0,.8,1.3,.5,COL.steel);for(var i=0;i<4;i++)B(0,.26,.12+i*.3,.6,.02,.01,COL.dark);B(0,.26,1.32,.3,.06,.01,COL[c])},
 counter:function(B,c){B(0,0,0,1.6,1,.6,COL.wood);B(0,0,1,1.66,.05,.66,COL.white);B(-.4,.1,1.05,.3,.2,.2,COL[c]);B(.3,0,1.05,.4,.3,.3,COL.screen)},
 bench:function(B,c){B(0,0,0,1.8,.88,.7,COL.white);B(0,0,.88,1.84,.05,.74,COL.dark);for(var i=0;i<5;i++)B(-.7+i*.35,-.15,.93,.12,.2+.06*(i%3),.12,COL[i%2?c:'c2'])},
 cart:function(B,c){B(0,0,.15,.6,.7,.45,COL.steel);B(0,0,.85,.64,.04,.48,COL.white);B(0,.23,.3,.5,.16,.02,COL[c]);B(0,.23,.55,.5,.16,.02,COL.c2);B(0,0,0,.5,.15,.35,COL.dark)},
 monitor:function(B,c){B(0,0,0,.42,.05,.42,COL.steel);B(0,0,.05,.06,1.15,.06,COL.steel);B(0,.02,1.15,.5,.38,.08,COL.screen);B(0,.065,1.18,.42,.3,.01,COL[c])},
 scope_tower:function(B,c){B(0,0,0,.6,1.45,.5,COL.steel);B(0,.26,.95,.5,.38,.02,COL.screenOn);B(0,0,1.45,.68,.45,.08,COL.screen);B(0,.05,1.5,.58,.35,.01,COL[c])},
 model:function(B,c){B(0,0,0,.45,.85,.45,COL.wood2);B(0,0,.85,.38,.55,.24,COL.pink);B(-.07,.13,1,.12,.18,.02,COL.c4);B(.06,.13,1.15,.14,.12,.02,COL[c])},
 podium:function(B,c){B(0,0,0,.6,1.05,.45,COL.wood2);B(0,-.03,1.05,.7,.06,.55,COL.wood);B(0,.24,.5,.4,.3,.01,COL[c])},
 microscope:function(B,c){B(0,0,.72,.8,.06,.6,COL.white);[-.34,.34].forEach(function(u){[-.24,.24].forEach(function(v){B(u,v,0,.05,.72,.05,COL.steel)})});B(0,-.05,.78,.26,.06,.3,COL.dark);B(0,-.12,.84,.08,.4,.08,COL.dark);B(0,-.02,1.18,.1,.08,.26,COL.dark);B(0,.06,1.2,.05,.12,.05,COL.dark);B(0,.02,.95,.2,.03,.16,COL.steel);B(.25,.1,.78,.18,.1,.12,COL[c])},
 iv_stand:function(B,c){B(0,0,0,.45,.04,.06,COL.steel);B(0,0,0,.06,.04,.45,COL.steel);B(0,0,0,.04,1.9,.04,COL.steel);B(0,0,1.75,.3,.03,.03,COL.steel);B(.12,0,1.4,.14,.32,.06,COL.water);B(.12,0,1.1,.01,.3,.01,COL.white)}};
function npcMesh(mb,a,c){var y=a.f*FH+.01,s=a.k==='npc_seat',b=s?.45:0;mb.cyl([a.x,y+b,a.z],.21,.18,s?.55:1.15,10,COL.coat,COL.coat);
  mb.cyl([a.x,y+b+(s?.48:1.0),a.z],.22,.2,.08,10,COL[c],COL[c]);mb.cyl([a.x,y+b+(s?.56:1.08),a.z],.15,.14,.3,10,COL.skin,COL.hair)}
var bookPicks=[[],[],[],[]],bookGroups=[],bookMarks=[],AMK={},mkGeo=new THREE.OctahedronGeometry(.2),mkMat={},mkDone=new THREE.MeshBasicMaterial({color:0x3ecf8e});
CHC.forEach(function(c,i){mkMat[i]=new THREE.MeshBasicMaterial({color:parseInt(COL[c],16)})});
if(BK){var bm=[new MB(),new MB(),new MB(),new MB()];
  for(var bf=0;bf<4;bf++){var mg=new THREE.Group();FL[bf].g.add(mg);bookMarks.push(mg)}
  BK.anchors.forEach(function(a,i){if(a.t==='s')return;var c=CHC[a.ch]||'c0',y=a.f*FH;
    if(!a.onm){if(a.t==='p'&&KB[a.k])KB[a.k](frame3(bm[a.f],a),c);else if(a.t==='n')npcMesh(bm[a.f],a,c)}
    var p=new THREE.Mesh(new THREE.BoxGeometry(1,2.2,1),pickMat);p.position.set(a.x,y+1.1,a.z);p.userData.a=i;FL[a.f].g.add(p);bookPicks[a.f].push(p);
    var m=new THREE.Mesh(mkGeo,mkMat[a.ch]||mkMat[0]);m.position.set(a.x,y+2.45,a.z);m.userData.ph=a.x*.7+a.z;bookMarks[a.f].add(m);AMK[i]=m});
  bm.forEach(function(b,f){var g=mesh(b,false,2);FL[f].g.add(g);bookGroups.push(g)})}
function anchorDone(i){var a=BK.anchors[i];return a.s.length&&a.s.every(scriptDone)}
function bookHit(ray){selA=null;if(!BK||!bookOn||typeof view!=='number'||view>3)return null;var h=ray.intersectObjects(bookPicks[view],false)[0];return h?h.object.userData.a:null}
function pickAnchor(i){selA=i;pick(ROOMBY[BK.anchors[i].room]);selA=i;card()}
function anchorName(a){if(a.t==='p')return a.label;if(a.t==='n'){var s=a.s.length?BK.scripts[a.s[0]]:null;return'Personaj: '+(s&&s.npc?s.npc:'colegul din cameră')}return'Stația camerei: '+ROOMBY[a.room].label}
function el(tag,cls,text){var e=document.createElement(tag);if(cls)e.className=cls;if(text!=null)e.textContent=text;return e}
function scriptRow(si){var s=BK.scripts[si],r=el('div','sr'),d=el('div'),b=el('button',scriptDone(si)?'done':'',scriptDone(si)?'Rejoacă':'Joacă');
  d.appendChild(el('b',null,s.title));d.appendChild(el('small',null,(KIND_RO[s.kind]||s.kind)+' · '+s.steps.length+' pași'+(scriptDone(si)?' · terminat ✓':'')));
  b.setAttribute('data-play',si);r.appendChild(d);r.appendChild(b);return r}
function anchorBlock(i,open){var a=BK.anchors[i],box=el('div','an'),t=el('b',null,anchorName(a));
  if(a.t==='p')t.appendChild(el('small',null,' · '+a.k.replace('_',' ')+(a.onm?' (mobilier existent)':'')));box.appendChild(t);
  a.s.forEach(function(si){box.appendChild(scriptRow(si))});return box}
function progressBar(d,n){var w=el('div','bar'),i=el('i');i.style.width=(n?Math.round(100*d/n):0)+'%';w.appendChild(i);return w}
function bookCard(){if(!BK||selA===null)return false;if(!sel){selA=null;return false}var a=BK.anchors[selA],r=ROOMBY[a.room],c=$('card');c.textContent='';
  c.appendChild(el('div','eb',FLOORS[r.f]+' · '+r.sign));c.appendChild(el('h2',null,anchorName(a)));
  var fs=[];a.s.forEach(function(si){fs=fs.concat(BK.scripts[si].covers)});var d=doneOf(fs);
  c.appendChild(el('p',null,a.s.length+' scenarii · '+d+' / '+fs.length+' fapte învățate'));c.appendChild(progressBar(d,fs.length));
  var bk=el('div','bk');a.s.forEach(function(si){bk.appendChild(scriptRow(si))});c.appendChild(bk);
  var back=el('button',null,'← Toată camera');back.style.cssText='margin-top:10px;padding:7px 12px;border-radius:10px;background:var(--chip)';back.addEventListener('click',function(){selA=null;card()});c.appendChild(back);return true}
function bookRoomExtra(){if(!BK||!sel)return;var list=anchorsOfRoom[sel.id];if(!list)return;var c=$('card'),fs=roomFacts(sel),d=doneOf(fs),bk=el('div','bk');
  bk.appendChild(el('h3',null,'Cartea în această cameră'));bk.appendChild(el('p',null,d+' / '+fs.length+' fapte învățate · camera e completă când toate sunt răspunse corect'));bk.appendChild(progressBar(d,fs.length));
  list.slice().sort(function(x,y){return(BK.anchors[x].t==='s'?0:1)-(BK.anchors[y].t==='s'?0:1)}).forEach(function(i){bk.appendChild(anchorBlock(i))});c.appendChild(bk)}
$('card').addEventListener('click',function(e){var b=e.target.closest('[data-play]');if(b)openPlayer(+b.getAttribute('data-play'))});
function updateChip(){if(!BK)return;var c=$('bookChip'),n=BK.facts.length,d=Object.keys(PR.done).length;c.hidden=false;c.textContent='';c.appendChild(document.createTextNode('Kumar cap. 32 · '+d+' / '+n+' fapte'));c.appendChild(el('small',null,BK.scripts.length+' scenarii în '+Object.keys(anchorsOfRoom).length+' camere'))}
updateChip();
function bookFrame(time){bookGroups.forEach(function(g){g.visible=bookOn});bookMarks.forEach(function(g,f){g.visible=bookOn&&showMarks&&view===f;if(!g.visible)return;
  g.children.forEach(function(m){m.rotation.y=time*1.8;m.position.y=f*FH+2.45+Math.sin(time*2.2+m.userData.ph)*.08})})}
function refreshMarks(){Object.keys(AMK).forEach(function(i){AMK[i].material=anchorDone(+i)?mkDone:(mkMat[BK.anchors[i].ch]||mkMat[0])})}
if(BK)refreshMarks();
$('tBook').addEventListener('click',function(){bookOn=!bookOn;$('tBook').setAttribute('aria-pressed',bookOn?'true':'false')});
/* ---------------- the player ---------------- */
var PL=null,rnd=function(seed){var s=seed*9301+49297;return function(){s=(s*16807)%2147483647;return s/2147483647}};
function shuffled(arr,seed){var a=arr.slice(),r=rnd(seed+7);for(var i=a.length-1;i>0;i--){var j=Math.floor(r()*(i+1)),t=a[i];a[i]=a[j];a[j]=t}return a}
function norm(s){return String(s).normalize('NFC').toLowerCase().replace(/ş/g,'ș').replace(/ţ/g,'ț').replace(/\s+/g,' ').replace(/^[\s„"“(]+|[\s”".,;:)]+$/g,'')}
function openPlayer(si){PL={s:si,k:0,checked:false,ok:false,learned:0,answered:0,correct:0};$('play').hidden=false;renderStep()}
function closePlayer(){$('play').hidden=true;PL=null;refreshMarks();updateChip();card()}
document.addEventListener('keydown',function(e){if(e.key==='Escape'&&PL)closePlayer()});
function renderStep(){var S=BK.scripts[PL.s],box=document.querySelector('#play .pbox'),r=ROOMBY[S.room];box.textContent='';
  var hd=el('div','ph'),meta=el('div','meta');hd.appendChild(el('div','eb',r.sign+' · '+(KIND_RO[S.kind]||S.kind)+(S.parts>1?' · partea '+S.part+' din '+S.parts:'')));
  var h2=el('h2',null,S.title);h2.id='pTitle';hd.appendChild(h2);var end=PL.k>=S.steps.length;
  meta.appendChild(el('span',null,end?'Gata':'Pasul '+(PL.k+1)+' din '+S.steps.length));meta.appendChild(el('span',null,PL.correct+' corecte din '+PL.answered));hd.appendChild(meta);
  hd.appendChild(progressBar(Math.min(PL.k,S.steps.length),S.steps.length));box.appendChild(hd);
  var body=el('div','pb'),foot=el('div','pf'),close=el('button','ghost','Închide');close.addEventListener('click',closePlayer);foot.appendChild(close);
  if(end){PR.fin[S.id]=1;saveP();body.appendChild(el('div','res ok','Scenariu terminat: '+PL.correct+' din '+PL.answered+' itemi corecți.'));
    var fs=S.covers,d=doneOf(fs);body.appendChild(el('p',null,d+' / '+fs.length+' fapte ale scenariului sunt acum învățate.'));
    var nx=BK.anchors[S.a].s.filter(function(x){return!scriptDone(x)})[0];if(nx!==undefined){var nb=el('button',null,'Următorul: '+BK.scripts[nx].title);nb.addEventListener('click',function(){openPlayer(nx)});foot.appendChild(nb)}
    box.appendChild(body);box.appendChild(foot);return}
  var st=S.steps[PL.k];
  if(st[0]===0){body.appendChild(el('div','stage','Lecție'));if(st[st.length-1]===-1)body.appendChild(el('p',null,'Recapitulare: aceeași lecție ca la începutul părții anterioare.'));
    st.slice(1).forEach(function(ai){if(ai<0)return;var A=BK.atoms[ai],d=el('div','at'+(A[2]==='heading'?' h':''));d.appendChild(el('span',null,A[0]));d.appendChild(el('small',null,'p. '+A[1]));body.appendChild(d)});
    var go=el('button',null,'Continuă →');go.addEventListener('click',function(){PL.k++;renderStep()});foot.appendChild(go)}
  else{var it=BK.items[st[1]];body.appendChild(el('div','stage',st[0]===1?'Încearcă înainte de lecție':'Verificare'));
    var chips=el('div','chips');if(it.flags.indexOf('needs_review')>=0)chips.appendChild(el('span',null,'De verificat: erată sau formulare tipărită'));if(it.flags.indexOf('unverified_ocr')>=0)chips.appendChild(el('span',null,'Text ascuns în PDF: citire nesigură'));if(chips.children.length)body.appendChild(chips);
    var W=widget(it,st[1],body),chk=el('button',null,it.type==='card'?'Arată răspunsul':'Verifică');
    chk.addEventListener('click',function(){if(PL.checked)return;var ok=W.check();if(ok===undefined)return;PL.checked=true;finish(it,ok,body,foot,chk)});foot.appendChild(chk)}
  box.appendChild(body);box.appendChild(foot);box.scrollTop=0}
function finish(it,ok,body,foot,chk){if(it.type==='card'){chk.remove();var y=el('button',null,'Am știut'),n=el('button','ghost','Nu am știut');
    [y,n].forEach(function(b,i){b.addEventListener('click',function(){done(it,i===0,body,foot,[y,n])});foot.appendChild(b)});return}
  done(it,ok,body,foot,[chk])}
function done(it,ok,body,foot,rm){rm.forEach(function(b){b.remove()});PL.answered++;if(ok){PL.correct++;it.f.forEach(function(f){PR.done[f]=1});saveP()}
  body.appendChild(el('div','res '+(ok?'ok':'bad'),ok?'Corect: faptele acestui item sunt învățate.':'Greșit: faptele rămân de învățat.'));
  if(it.review_note)body.appendChild(el('div','ex','Notă: '+it.review_note));body.appendChild(el('div','ex',it.explanation));
  var go=el('button',null,'Continuă →');go.addEventListener('click',function(){PL.k++;PL.checked=false;renderStep()});foot.appendChild(go);go.focus()}
function widget(it,ix,body){var t=it.type,W={};
  if(t!=='cloze')body.appendChild(el('p','stem',it.stem));
  if(t==='simplu'||t==='multiplu'){var picked={},bs=[];if(t==='multiplu')body.appendChild(el('p',null,'Alege toate variantele corecte (2–4).'));
    it.options.forEach(function(o,i){var b=el('button','opt');b.setAttribute('aria-pressed','false');b.appendChild(el('b',null,'ABCDE'[i]));var sp=el('span',null,o.t);b.appendChild(sp);
      b.addEventListener('click',function(){if(PL.checked)return;if(t==='simplu'){picked={};bs.forEach(function(x){x.setAttribute('aria-pressed','false')})}picked[i]=!picked[i];b.setAttribute('aria-pressed',picked[i]?'true':'false')});bs.push(b);body.appendChild(b)});
    W.check=function(){var any=Object.keys(picked).some(function(k){return picked[k]});if(!any){toast('Alege o variantă');return undefined}var ok=true;
      it.options.forEach(function(o,i){var p=!!picked[i];if(p!==o.ok)ok=false;if(o.ok||p)bs[i].classList.add(o.ok?'ok':'bad');if(o.why){var w=el('span','why',o.why);bs[i].lastChild.appendChild(w)}});return ok}}
  else if(t==='cloze'){var p=el('p','stem cz'),parts=it.stem.split('____'),ins=[];parts.forEach(function(s,i){p.appendChild(document.createTextNode(s));if(i<parts.length-1){var x=document.createElement('input');x.id='cz'+ix+'_'+i;x.setAttribute('aria-label','Golul '+(i+1));x.style.width=Math.max(5,(it.answers[i]||'').length+1)+'ch';ins.push(x);p.appendChild(x)}});body.appendChild(p);
    W.check=function(){var ok=true;ins.forEach(function(x,i){var g=norm(x.value)===norm(it.answers[i]);if(!g){ok=false;var e=el('em',null,it.answers[i]);x.after(e)}x.classList.add(g?'ok':'bad');x.disabled=true});return ok}}
  else if(t==='card'){W.check=function(){body.appendChild(el('div','at',it.answer));return true}}
  else if(t==='sort'||t==='match'||t==='label'){var rows=[],choices,keys,labelsOf=[];
    if(t==='sort'){keys=it.entries.map(function(e){return e.g});choices=it.groups.slice();shuffled(it.entries.map(function(e,i){return i}),ix).forEach(function(i){rows.push([it.entries[i].t,it.entries[i].g])})}
    else if(t==='match'){choices=shuffled(it.pairs.map(function(p){return p[1]}),ix);it.pairs.forEach(function(p){rows.push([p[0],p[1]])})}
    else{var fg=BK.figs[it.figure],fig=el('div','fig'),im=document.createElement('img');im.src='fig/'+it.figure+'.jpg';im.alt='Figura '+it.figure.replace('fig_','').replace('_','.');fig.appendChild(im);
      var used={};it.labels.forEach(function(lt,n){var L=(fg&&fg.labels||[]).filter(function(l,k){if(used[k]||l[0]!==lt)return false;used[k]=1;return true})[0],b=el('i',null,String(n+1));
        if(L&&fg.w){b.style.left=(100*L[1]/fg.w)+'%';b.style.top=(100*L[2]/fg.h)+'%';b.style.width=Math.max(2.2,100*(L[3]-L[1])/fg.w)+'%';b.style.height=Math.max(2.6,100*(L[4]-L[2])/fg.h)+'%';fig.appendChild(b)}
        rows.push(['Eticheta '+(n+1),lt])});body.appendChild(fig);choices=shuffled(it.labels,ix)}
    var sels=rows.map(function(rw,n){var r=el('div','row'),s=document.createElement('select');s.id='sel'+ix+'_'+n;s.setAttribute('aria-label',rw[0]);s.appendChild(new Option('— alege —',''));
      (t==='sort'?choices:choices).forEach(function(c){s.appendChild(new Option(c,c))});r.appendChild(el('span',null,rw[0]));r.appendChild(s);body.appendChild(r);return[r,s,rw[1]]});
    W.check=function(){if(sels.some(function(x){return!x[1].value})){toast('Completează toate rândurile');return undefined}var ok=true;
      sels.forEach(function(x){var g=x[1].value===x[2];if(!g){ok=false;x[0].querySelector('span').appendChild(el('em',null,'corect: '+x[2]))}x[0].classList.add(g?'ok':'bad');x[1].disabled=true});return ok}}
  else if(t==='order'){var cur=shuffled(it.sequence.map(function(s,i){return i}),ix),list=el('div');list.style.cssText='display:grid;gap:6px';body.appendChild(el('p',null,'Așază în ordine cu săgețile.'));
    function draw(){list.textContent='';cur.forEach(function(i,pos){var r=el('div','row');r.appendChild(el('span',null,(pos+1)+'. '+it.sequence[i]));
      var u=el('button',null,'↑'),d=el('button',null,'↓');u.setAttribute('aria-label','Mută mai sus');d.setAttribute('aria-label','Mută mai jos');
      u.addEventListener('click',function(){if(PL.checked||!pos)return;var t2=cur[pos-1];cur[pos-1]=cur[pos];cur[pos]=t2;draw()});d.addEventListener('click',function(){if(PL.checked||pos===cur.length-1)return;var t2=cur[pos+1];cur[pos+1]=cur[pos];cur[pos]=t2;draw()});
      r.appendChild(u);r.appendChild(d);list.appendChild(r)})}draw();body.appendChild(list);
    W.check=function(){var ok=cur.every(function(i,pos){return i===pos});list.querySelectorAll('.row').forEach(function(r,pos){r.classList.add(cur[pos]===pos?'ok':'bad')});
      if(!ok)body.appendChild(el('div','ex','Ordinea corectă: '+it.sequence.map(function(s,i){return(i+1)+'. '+s}).join('  ·  ')));return ok}}
  return W}
window.__book={openPlayer:openPlayer,pickAnchor:pickAnchor,BK:BK,PR:PR,openAt:function(si,k){openPlayer(si);PL.k=k;renderStep()}};
