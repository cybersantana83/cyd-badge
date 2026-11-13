#pragma once
// Painel web servido pela placa no modo configuracao (http://192.168.4.1)
#include <pgmspace.h>

static const char WEB_UI_HTML[] PROGMEM = R"rawliteral(<!doctype html>
<html lang="pt-BR"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1">
<title>CYD Badge</title>
<style>
:root{--bg:#0d1117;--card:#161b22;--line:#30363d;--fg:#e6edf3;--mut:#8b949e;--acc:#f5a623}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--fg);font:15px/1.4 system-ui,-apple-system,Segoe UI,Roboto,sans-serif;padding-bottom:96px}
header{padding:18px 16px 6px}h1{margin:0;font-size:22px}h1 b{color:var(--acc)}header p{margin:4px 0 0;color:var(--mut);font-size:13px}
h2{font-size:13px;letter-spacing:.06em;text-transform:uppercase;color:var(--mut);margin:22px 16px 8px}
.card{background:var(--card);border:1px solid var(--line);border-radius:12px;margin:10px 12px;overflow:hidden}
.ch{display:flex;align-items:center;gap:10px;padding:10px 12px;border-bottom:1px solid var(--line)}
.dot{width:28px;height:28px;border-radius:8px;display:grid;place-items:center;font-weight:700;color:#fff;flex:none}
.ch .t{flex:1;font-weight:600}.ib{background:none;border:1px solid var(--line);color:var(--fg);border-radius:8px;width:34px;height:34px;font-size:16px}
.ib:disabled{opacity:.3}.cb{padding:10px 12px 14px}
label{display:block;font-size:12px;color:var(--mut);margin:10px 0 4px}
input[type=text],input[type=number],input[type=tel],input[type=email],input[type=url],select,textarea{width:100%;padding:10px;border-radius:8px;border:1px solid var(--line);background:#0d1117;color:var(--fg);font:inherit}
textarea{min-height:80px}input[type=color]{width:44px;height:34px;border:1px solid var(--line);border-radius:8px;background:none;padding:2px}
input[type=range]{width:100%}.row{display:flex;gap:10px;align-items:center}.row>*{flex:1}
.chk{display:flex;align-items:center;gap:8px;margin:12px 0 0;color:var(--fg);font-size:14px}.chk input{width:20px;height:20px}
.photo{display:flex;gap:12px;align-items:center;margin-top:10px}.photo img,.photo .ph{width:72px;height:72px;border-radius:12px;object-fit:cover;background:#222;border:2px solid var(--line)}
.photo.wide img,.photo.wide .ph{width:120px;height:90px;border-radius:8px}
.btn{border:0;border-radius:10px;padding:11px 14px;font:inherit;font-weight:600;background:#21262d;color:var(--fg)}
.btn.p{background:var(--acc);color:#111}.btn.w{width:100%}
.grid{display:grid;grid-template-columns:repeat(3,1fr);gap:8px;padding:12px}.grid button{display:flex;flex-direction:column;align-items:center;gap:6px;padding:12px 4px;font-size:12px}
.bar{position:fixed;left:0;right:0;bottom:0;background:rgba(13,17,23,.96);border-top:1px solid var(--line);padding:12px;display:flex;gap:10px}
.bar .btn{flex:1}.toast{position:fixed;left:50%;top:16px;transform:translateX(-50%);background:#238636;color:#fff;padding:10px 16px;border-radius:10px;opacity:0;transition:.3s;pointer-events:none;z-index:20}
.toast.err{background:#da3633}.toast.on{opacity:1}.hint{color:var(--mut);font-size:12px;margin:6px 0 0}
.modal{position:fixed;inset:0;background:rgba(0,0,0,.85);display:none;flex-direction:column;align-items:center;justify-content:center;z-index:10;padding:16px}
.modal.on{display:flex}#cv{touch-action:none;border-radius:8px;max-width:100%;background:#000}.modal .row{width:100%;max-width:320px;margin-top:12px}
</style></head><body>
<header><h1>CYD <b>Badge</b></h1><p>Edite seu crachá e toque em <b>Salvar na placa</b>. O resultado aparece na tela na hora.</p>
<div id="cap" class="card" style="display:none;margin:12px 0 0;padding:12px"><b>Não consegue escolher a foto?</b>
<p class="hint" style="margin:4px 0 10px">A janela de "login de rede" do celular bloqueia o envio de arquivos. Abra o painel no navegador:</p>
<a class="btn p w" style="display:block;text-align:center;text-decoration:none" id="openb" href="http://192.168.4.1/">Abrir no navegador</a>
<p class="hint">Ou feche esta janela e leia o QR 2 da placa com a câmera.</p></div></header>

<h2>Páginas</h2>
<div id="pages"></div>
<div class="card"><div class="ch"><span class="t">Adicionar página</span></div><div class="grid" id="add"></div></div>

<h2>Aparência</h2>
<div class="card"><div class="cb" id="settings"></div></div>

<div class="bar"><button class="btn" id="done">Concluir</button><button class="btn p" id="save">Salvar na placa</button></div>
<div class="toast" id="toast"></div>

<div class="modal" id="modal"><p id="mt" style="margin:0 0 10px">Arraste para posicionar</p><canvas id="cv"></canvas>
<div class="row"><span style="flex:none">🔍</span><input type="range" id="zoom" min="1" max="4" step="0.01" value="1"><button class="btn" id="mrot" style="flex:none">↻ Girar</button></div>
<div class="row"><button class="btn" id="mcancel">Cancelar</button><button class="btn p" id="mok">Usar imagem</button></div></div>

<script>
const $=s=>document.querySelector(s);
const PRESETS={
 profile:{n:'Perfil',c:'#F5A623',i:'👤'},
 linkedin:{n:'LinkedIn',c:'#0A66C2',i:'in',ask:'Seu usuário do LinkedIn',ph:'ex.: joao-silva',url:u=>'https://www.linkedin.com/in/'+u,h:u=>'in/'+u,re:/linkedin\.com\/in\/([^\/?#]+)/i},
 instagram:{n:'Instagram',c:'#D62976',i:'IG',ask:'Seu @ do Instagram',ph:'ex.: cafecomsoldalab',url:u=>'https://instagram.com/'+u,h:u=>'@'+u,re:/instagram\.com\/([^\/?#]+)/i},
 github:{n:'GitHub',c:'#6E7681',i:'GH',ask:'Seu usuário do GitHub',ph:'ex.: octocat',url:u=>'https://github.com/'+u,h:u=>'github.com/'+u,re:/github\.com\/([^\/?#]+)/i},
 x:{n:'X / Twitter',c:'#3A3F44',i:'X',ask:'Seu @ no X',ph:'ex.: usuario',url:u=>'https://x.com/'+u,h:u=>'@'+u,re:/(?:x|twitter)\.com\/([^\/?#]+)/i},
 youtube:{n:'YouTube',c:'#E62117',i:'▶',ask:'Seu @ do canal',ph:'ex.: meucanal',url:u=>'https://youtube.com/@'+u,h:u=>'@'+u,re:/youtube\.com\/@([^\/?#]+)/i},
 tiktok:{n:'TikTok',c:'#EE1D52',i:'TT',ask:'Seu @ do TikTok',ph:'ex.: usuario',url:u=>'https://tiktok.com/@'+u,h:u=>'@'+u,re:/tiktok\.com\/@([^\/?#]+)/i},
 whatsapp:{n:'WhatsApp',c:'#1FAF55',i:'WA',ask:'Seu número com país e DDD',ph:'ex.: 5511999998888',url:u=>'https://wa.me/'+u.replace(/\D/g,''),h:u=>'+'+u.replace(/\D/g,''),re:/wa\.me\/(\d+)/i},
 link:{n:'Site / Link',c:'#8957E5',i:'🔗'},
 event:{n:'Credencial de evento',c:'#E5484D',i:'🎟'},
 vcard:{n:'Contato',c:'#F5A623',i:'📇'},
 wifi:{n:'Wi-Fi',c:'#2F81F7',i:'📶'},
 text:{n:'Texto',c:'#8B949E',i:'T'},
 image:{n:'Imagem',c:'#3FB950',i:'🖼'}
};
let cfg={settings:{},pages:[]};
const uid=()=>'u_'+Date.now().toString(36)+Math.random().toString(36).slice(2,6);

function el(t,a={},...k){const e=document.createElement(t);for(const[x,v]of Object.entries(a)){if(x.startsWith('on'))e[x]=v;else if(x==='html')e.innerHTML=v;else e.setAttribute(x,v)}
 for(const c of k.flat(Infinity)){if(c!=null)e.append(c.nodeType?c:document.createTextNode(c))}return e}
function toast(m,err){const t=$('#toast');t.textContent=m;t.className='toast on'+(err?' err':'');clearTimeout(t._h);t._h=setTimeout(()=>t.className='toast',2600)}

function inferKind(p){
 if(p.kind&&PRESETS[p.kind])return;
 const t=p.type||'profile';
 if(t==='profile'||t==='vcard'||t==='text'||t==='image'){p.kind=t;return}
 const d=p.url||p.data||'';
 if(d.startsWith('WIFI:')){p.kind='wifi';const m=d.match(/S:((?:\\.|[^;])*);/),q=d.match(/P:((?:\\.|[^;])*);/);p.ssid=m?m[1].replace(/\\(.)/g,'$1'):'';p.wifipass=q?q[1].replace(/\\(.)/g,'$1'):'';return}
 for(const[k,v]of Object.entries(PRESETS)){if(v.re){const m=d.match(v.re);if(m){p.kind=k;p.user=decodeURIComponent(m[1]).replace(/^@/,'');return}}}
 p.kind='link';
}
function cleanUser(k,u){u=(u||'').trim();const P=PRESETS[k];if(P.re){const m=u.match(P.re);if(m)u=m[1]}return u.replace(/^@/,'').replace(/\s+/g,'')}

function field(p,key,lab,type='text',ph=''){
 const i=type==='textarea'?el('textarea',{placeholder:ph}):el('input',{type,placeholder:ph});
 i.value=p[key]??'';i.oninput=()=>{p[key]=i.value};return[el('label',{},lab),i]}
function select(p,key,lab,opts){const s=el('select');for(const[v,t]of opts)s.append(el('option',{value:v},t));s.value=p[key]??opts[0][0];p[key]=s.value;s.onchange=()=>p[key]=s.value;return[el('label',{},lab),s]}
function color(p,key,lab,def){const i=el('input',{type:'color'});i.value=p[key]||def;i.oninput=()=>p[key]=i.value;return el('div',{class:'row',style:'margin-top:10px'},el('span',{style:'color:var(--mut);font-size:13px'},lab),el('div',{style:'flex:none'},i))}
function photo(p,w,h,round,lab){
 const img=p.image?el('img',{src:'/api/file?path='+encodeURIComponent(p.image)+'&t='+Date.now()}):el('div',{class:'ph'});
 const f=el('input',{type:'file',accept:'image/*',style:'display:none'});
 f.onchange=()=>{if(f.files[0])cropper(f.files[0],w,h,round,async blob=>{
  const path='/badge/img/'+uid()+'.jpg';
  if(await upload(blob,path)){p.image=path;if(w===h){p.image_px=w;delete p.image_size}render();toast('Imagem enviada')}else toast('Falha ao enviar a imagem',1)})};
 return el('div',{class:'photo'+(w===h?'':' wide')},img,el('div',{},el('button',{class:'btn',onclick:()=>f.click()},p.image?'Trocar '+lab:'Escolher '+lab),f))}

function body(p){
 const k=p.kind,P=PRESETS[k],b=[];
 if(k==='profile'){b.push(photo(p,160,160,(p.image_shape||'circle')==='circle','foto'),field(p,'name','Nome'),field(p,'title','Cargo'),field(p,'subtitle','Subtítulo / tags','text','ex.: Segurança • Hardware'),
  select(p,'image_shape','Formato da foto',[['circle','Redonda'],['square','Quadrada']]),select(p,'layout','Disposição',[['center','Foto em cima'],['side','Foto ao lado']]))}
 else if(P.url){b.push(field(p,'user',P.ask,k==='whatsapp'?'tel':'text',P.ph),field(p,'note','Texto de apoio (opcional)','text','ex.: Vamos conectar!'))}
 else if(k==='link'){b.push(field(p,'label','Título do cabeçalho','text','ex.: Meu site'),field(p,'url','Link completo','url','https://...'),field(p,'handle','Texto em destaque','text','ex.: meusite.com'),field(p,'note','Texto de apoio (opcional)'))}
 else if(k==='event'){
  const code=field(p,'data','Código do QR do crachá','text','ex.: p0117100946');
  const cam=el('input',{type:'file',accept:'image/*',capture:'environment',style:'display:none'});
  cam.onchange=()=>{if(cam.files[0])readQr(cam.files[0],p,code[1])};
  if(p.boost===undefined)p.boost=true;
  const bc=el('input',{type:'checkbox'});bc.checked=!!p.boost;bc.onchange=()=>p.boost=bc.checked;
  b.push(el('button',{class:'btn p w',style:'margin-top:10px',onclick:()=>cam.click()},'📷 Ler o QR do crachá'),cam,
   el('p',{class:'hint'},'Tire uma foto do QR do crachá de papel. O código é lido aqui no celular e a placa gera o mesmo QR, nítido e grande.'),
   ...code,field(p,'label','Nome do evento','text','ex.: IT Summit 2026'),field(p,'handle','Seu nome (como no crachá)'),field(p,'note','Empresa ou texto (opcional)'),
   el('label',{class:'chk'},bc,'Brilho máximo nesta página (ajuda o leitor do expositor)'),
   el('p',{class:'hint'},'Na placa: clique duplo no botão BOOT vai direto para esta página.'))}
 else if(k==='wifi'){b.push(field(p,'ssid','Nome da rede'),field(p,'wifipass','Senha (vazio se aberta)'),field(p,'note','Texto de apoio (opcional)','text','ex.: Rede para convidados'))}
 else if(k==='vcard'){b.push(field(p,'name','Nome'),field(p,'org','Empresa'),field(p,'phone','Telefone','tel'),field(p,'email','E-mail','email'),field(p,'url','Site','url'),field(p,'note','Texto na tela (opcional)','text','ex.: Escaneie para salvar meu contato'))}
 else if(k==='text'){b.push(field(p,'label','Título do cabeçalho'),field(p,'title','Título'),field(p,'body','Texto','textarea'))}
 else if(k==='image'){b.push(photo(p,320,240,false,'imagem'),field(p,'caption','Legenda (opcional)'))}
 if(k!=='image')b.push(color(p,'accent','Cor de destaque',P.c));
 return b}

function render(){
 const box=$('#pages');box.innerHTML='';
 cfg.pages.forEach((p,i)=>{const P=PRESETS[p.kind];
  const t=p.kind==='profile'?(p.name||'Perfil'):(P.n);
  const mv=d=>()=>{const j=i+d;[cfg.pages[i],cfg.pages[j]]=[cfg.pages[j],cfg.pages[i]];render()};
  box.append(el('div',{class:'card'},
   el('div',{class:'ch'},el('div',{class:'dot',style:'background:'+(p.accent||P.c)},P.i),el('span',{class:'t'},(i+1)+'. '+t),
    el('button',{class:'ib',onclick:mv(-1),...(i===0?{disabled:''}:{})},'↑'),el('button',{class:'ib',onclick:mv(1),...(i===cfg.pages.length-1?{disabled:''}:{})},'↓'),
    el('button',{class:'ib',onclick:()=>{if(cfg.pages.length<2)return toast('O crachá precisa de pelo menos 1 página',1);if(confirm('Remover esta página?')){cfg.pages.splice(i,1);render()}}},'✕')),
   el('div',{class:'cb'},body(p))))});
}
function renderAdd(){const g=$('#add');for(const[k,P]of Object.entries(PRESETS))g.append(el('button',{class:'btn',onclick:()=>{const p={kind:k};if(k==='profile')p.image_shape='circle';cfg.pages.push(p);render();setTimeout(()=>scrollTo(0,$('#pages').lastChild.offsetTop-10),50)}},el('div',{class:'dot',style:'background:'+P.c},P.i),P.n))}

function renderSettings(){const s=cfg.settings,b=$('#settings');b.innerHTML='';
 const br=el('input',{type:'range',min:10,max:255});br.value=s.brightness??180;br.oninput=()=>s.brightness=+br.value;
 const ss=el('input',{type:'number',min:0,max:3600});ss.value=s.slideshow_seconds??0;ss.oninput=()=>s.slideshow_seconds=Math.max(0,+ss.value||0);
 const chk=(k,l)=>{const c=el('input',{type:'checkbox'});c.checked=!!s[k];c.onchange=()=>s[k]=c.checked;return el('label',{class:'chk'},c,l)};
 if(s.rotation===undefined)s.rotation=1;
 b.append(el('label',{},'Brilho'),br,...select(s,'rotation','Orientação da tela',[['1','Paisagem'],['3','Paisagem (invertida)'],['0','Retrato'],['2','Retrato (invertido)']]),
  el('label',{},'Trocar de página sozinho a cada (segundos, 0 = desligado)'),ss,
  color(s,'bg','Cor de fundo','#0D1117'),color(s,'fg','Cor do texto','#E6EDF3'),color(s,'muted','Cor do texto secundário','#8B949E'),color(s,'accent','Cor de destaque padrão','#F5A623'),
  chk('led_follow_accent','LED da placa acompanha a cor da página'),chk('invert_colors','Corrigir cores invertidas'),chk('touch_invert_x','Inverter lado do toque'),
  el('p',{class:'hint'},'Na placa: toque à direita avança, à esquerda volta. Segure 3 s (ou o botão BOOT) para abrir este painel.'))}

function finalize(){
 const s=Object.assign({},cfg.settings);s.rotation=+s.rotation;
 const pages=cfg.pages.map(p=>{const o=Object.assign({},p),k=p.kind,P=PRESETS[k];
  if(!o.accent&&k!=='profile'&&k!=='image')o.accent=P.c;
  if(['profile','vcard','text','image'].includes(k))o.type=k;else o.type='qr';
  if(P.url){const u=cleanUser(k,p.user);o.user=u;o.url=P.url(u);o.handle=P.h(u);o.label=P.n;delete o.data}
  if(k==='link'&&!o.label)o.label='Link';
  if(k==='wifi'){const e=v=>(v||'').replace(/([\\;,:"])/g,'\\$1');o.data='WIFI:T:'+(p.wifipass?'WPA':'nopass')+';S:'+e(p.ssid)+';P:'+e(p.wifipass)+';;';o.label='Wi-Fi';o.handle=p.ssid||'';delete o.url}
  if(k==='vcard'){o.label=o.label||'Salvar contato';o.handle=p.name||''}
  if(k==='event'){o.type='qr';o.layout='big';o.label=o.label||'Credencial';o.data=(p.data||'').trim();o.boost=p.boost!==false;delete o.url}
  if(k==='profile'||k==='image')o.image=p.image||'';
  return o});
 return{settings:s,pages}}

async function upload(blob,path){try{const fd=new FormData();fd.append('file',blob,'img.jpg');const r=await fetch('/api/upload?path='+encodeURIComponent(path),{method:'POST',body:fd});return r.ok}catch(e){return false}}

$('#save').onclick=async()=>{const b=$('#save');b.disabled=true;b.textContent='Salvando...';
 try{const r=await fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(finalize())});
  if(r.ok)toast('Salvo! Confira na placa');else toast('Erro: '+await r.text(),1)}catch(e){toast('Sem conexão com a placa',1)}
 b.disabled=false;b.textContent='Salvar na placa'};
$('#done').onclick=async()=>{if(!confirm('Salvar e sair do modo configuração? O Wi-Fi da placa será desligado.'))return;
 try{await fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(finalize())});await fetch('/api/exit',{method:'POST'})}catch(e){}
 document.body.innerHTML='<header><h1>Pronto! ✅</h1><p>O crachá está atualizado. Para editar de novo, segure o dedo na tela por 3 segundos.</p></header>'};

// ---- leitura do QR do cracha ----
let jsqrP=null;
function loadJsQR(){return jsqrP||(jsqrP=new Promise((ok,no)=>{const sc=document.createElement('script');sc.src='/jsqr.js';sc.onload=()=>ok(window.jsQR);sc.onerror=no;document.head.append(sc)}))}
async function readQr(file,p,input){
 toast('Lendo o QR...');
 let img;try{img=await loadImage(file)}catch(e){return toast('Não consegui abrir a foto',1)}
 let txt=null;
 if('BarcodeDetector' in window){try{const r=await new BarcodeDetector({formats:['qr_code']}).detect(img);if(r.length)txt=r[0].rawValue}catch(e){}}
 if(txt==null){
  let jsQR;try{jsQR=await loadJsQR()}catch(e){return toast('Não consegui carregar o leitor',1)}
  for(const max of [1400,1000,700]){
   const s=Math.min(1,max/Math.max(img.width,img.height)),w=Math.round(img.width*s),h=Math.round(img.height*s);
   const c=document.createElement('canvas');c.width=w;c.height=h;const g=c.getContext('2d');g.drawImage(img,0,0,w,h);
   const r=jsQR(g.getImageData(0,0,w,h).data,w,h,{inversionAttempts:'attemptBoth'});if(r&&r.data){txt=r.data;break}}}
 if(txt==null)return toast('Não achei o QR. Chegue mais perto e evite reflexo',1);
 p.data=txt;input.value=txt;toast('QR lido: '+(txt.length>30?txt.slice(0,30)+'…':txt))}

// ---- recorte de imagem ----
let cs={};
async function loadImage(file){
 // Fotos de celular guardam a rotacao no EXIF; createImageBitmap aplica isso
 if(window.createImageBitmap){try{return await createImageBitmap(file,{imageOrientation:'from-image'})}catch(e){}}
 return await new Promise((ok,no)=>{const i=new Image();i.onload=()=>ok(i);i.onerror=no;i.src=URL.createObjectURL(file)})}
function setupCrop(img){const vw=cs.vw,vh=cs.vh,base=Math.max(vw/img.width,vh/img.height);
 Object.assign(cs,{img,base,z:1,x:(vw-img.width*base)/2,y:(vh-img.height*base)/2});$('#zoom').value=1;draw()}
async function cropper(file,w,h,round,cb){
 let img;try{img=await loadImage(file)}catch(e){return toast('Não consegui abrir essa imagem',1)}
 const cv=$('#cv'),vw=Math.min(300,innerWidth-32),vh=Math.round(vw*h/w);cv.width=vw;cv.height=vh;
 cs={cv,vw,vh,round,w,h,cb};
 $('#mt').textContent=round?'Arraste e use o zoom para enquadrar o rosto':'Arraste e use o zoom para enquadrar';
 $('#modal').classList.add('on');setupCrop(img)}
function rotateImg(){const s=cs.img,c=document.createElement('canvas');c.width=s.height;c.height=s.width;
 const g=c.getContext('2d');g.translate(c.width,0);g.rotate(Math.PI/2);g.drawImage(s,0,0);setupCrop(c)}
function clamp(){const s=cs.base*cs.z,W=cs.img.width*s,H=cs.img.height*s;cs.x=Math.min(0,Math.max(cs.vw-W,cs.x));cs.y=Math.min(0,Math.max(cs.vh-H,cs.y))}
function draw(){const{cv,img,vw,vh}=cs,c=cv.getContext('2d'),s=cs.base*cs.z;clamp();c.clearRect(0,0,vw,vh);c.drawImage(img,cs.x,cs.y,img.width*s,img.height*s);
 if(cs.round){c.save();c.fillStyle='rgba(0,0,0,.55)';c.beginPath();c.rect(0,0,vw,vh);c.moveTo(vw,vh/2);c.arc(vw/2,vh/2,vw/2,0,Math.PI*2,true);c.closePath();c.fill('evenodd');c.restore()}}
(()=>{const cv=$('#cv');let d=null;
 const start=(x,y)=>d={x,y},move=(x,y)=>{if(!d)return;cs.x+=x-d.x;cs.y+=y-d.y;d={x,y};draw()},end=()=>d=null;
 if(window.PointerEvent){
  cv.onpointerdown=e=>{start(e.clientX,e.clientY);try{cv.setPointerCapture(e.pointerId)}catch(_){}};
  cv.onpointermove=e=>move(e.clientX,e.clientY);cv.onpointerup=cv.onpointercancel=end;
 }else{
  cv.addEventListener('touchstart',e=>{const t=e.touches[0];start(t.clientX,t.clientY);e.preventDefault()},{passive:false});
  cv.addEventListener('touchmove',e=>{const t=e.touches[0];move(t.clientX,t.clientY);e.preventDefault()},{passive:false});
  cv.addEventListener('touchend',end);cv.onmousedown=e=>start(e.clientX,e.clientY);onmousemove=e=>move(e.clientX,e.clientY);onmouseup=end}
 $('#mrot').onclick=rotateImg;
 $('#zoom').oninput=e=>{const cx=cs.vw/2,cy=cs.vh/2,old=cs.base*cs.z;cs.z=+e.target.value;const n=cs.base*cs.z;cs.x=cx-(cx-cs.x)*n/old;cs.y=cy-(cy-cs.y)*n/old;draw()};
 $('#mcancel').onclick=()=>$('#modal').classList.remove('on');
 $('#mok').onclick=()=>{const o=document.createElement('canvas');o.width=cs.w;o.height=cs.h;const c=o.getContext('2d'),k=cs.w/cs.vw,s=cs.base*cs.z*k;
  c.fillStyle='#000';c.fillRect(0,0,cs.w,cs.h);c.imageSmoothingQuality='high';c.drawImage(cs.img,cs.x*k,cs.y*k,cs.img.width*s,cs.img.height*s);
  $('#modal').classList.remove('on');o.toBlob(b=>cs.cb(b),'image/jpeg',0.9)}})();

fetch('/api/config').then(r=>r.json()).then(j=>{cfg=j;cfg.settings=cfg.settings||{};cfg.pages=(cfg.pages||[]);cfg.pages.forEach(inferKind);render();renderSettings()})
 .catch(()=>{cfg={settings:{},pages:[{kind:'profile',image_shape:'circle'}]};render();renderSettings();toast('Não consegui ler a configuração',1)});
renderAdd();
(()=>{const ua=navigator.userAgent,c=$('#cap'),b=$('#openb');
 // Android: intent abre o Chrome direto, mesmo de dentro da janela de login de rede
 if(/Android/i.test(ua)){b.href='intent://192.168.4.1/#Intent;scheme=http;package=com.android.chrome;end';b.textContent='Abrir no Chrome'}
 const webview=/; wv\)|CaptiveNetworkSupport|CaptivePortalLogin/i.test(ua)||(/iPhone|iPad/i.test(ua)&&!/Safari/i.test(ua));
 c.style.display='block'})();
</script></body></html>)rawliteral";
