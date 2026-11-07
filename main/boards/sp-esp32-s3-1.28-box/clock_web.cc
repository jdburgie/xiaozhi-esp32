// round-box-clock: see clock_web.h.
#include "clock_web.h"

#include "board.h"
#include "backlight.h"
#include "clock_overlay.h"
#include "clock_sd.h"
#include "clock_settings.h"
#include "clock_slideshow.h"
#include "clock_chime.h"
#include "clock_web_icons.h"

#include <dirent.h>
#include <sys/stat.h>
#include <cctype>
#include <cstdlib>

#include <cJSON.h>
#include <esp_app_desc.h>
#include <esp_heap_caps.h>
#include <esp_http_server.h>
#include <esp_log.h>
#include <esp_netif.h>
#include <esp_timer.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <mdns.h>

#include <cstdio>
#include <cstring>
#include <mutex>
#include <shared_mutex>
#include <string>

#define TAG "ClockWeb"

namespace clock_web {
namespace {

// ---------------------------------------------------------------- page
const char kIndexHtml[] = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Round Box Clock</title>
<link rel="icon" href="/favicon.ico" sizes="any">
<link rel="icon" href="/favicon.svg" type="image/svg+xml">
<link rel="apple-touch-icon" href="/apple-touch-icon.png">
<style>
/* Three Oak Woods palette (threeoakwoods-com): forest, deep, paper, ink, gold */
:root{--bg:#f5f1e6;--card:#fffefb;--ink:#2b3a33;--muted:#58665e;--line:#e3d9c3;--accent:#2c654b;--accent-hover:#214d3b;--gold:#8a5a12;--pale:#eaf0e8;--danger:#9b2c22}
@media (prefers-color-scheme:dark){:root{--bg:#1c2621;--card:#24322c;--ink:#f0ece0;--muted:#a9b5ad;--line:#35473e;--accent:#4f9a76;--accent-hover:#5fae88;--gold:#e6c692;--pale:#2b3d34;--danger:#e0806f}}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--ink);font:15px/1.55 "Segoe UI",system-ui,sans-serif}
main{max-width:620px;margin:0 auto;padding:22px 16px 48px}
h1{font:800 1.75rem/1.18 Nunito,"Segoe UI",system-ui,sans-serif;letter-spacing:-.02em;margin:.2em 0 .1em}h1 small{display:block;font:600 .85rem "Segoe UI",system-ui,sans-serif;color:var(--muted);letter-spacing:0;margin-top:4px}
.eyebrow{color:var(--gold);font-size:11px;letter-spacing:.18em;text-transform:uppercase;font-weight:700;margin:0 0 6px}
section{background:var(--card);border:1px solid var(--line);border-radius:14px;padding:16px 18px;margin:16px 0}
h2{font-size:11px;margin:0 0 10px;letter-spacing:.16em;text-transform:uppercase;color:var(--gold);font-weight:700}
.row{display:flex;align-items:center;justify-content:space-between;gap:12px;padding:8px 0;border-top:1px solid var(--line)}
.row:first-of-type{border-top:0}label{flex:1}
input[type=color]{width:52px;height:36px;border:1px solid var(--line);border-radius:8px;background:none;padding:2px}
select,input[type=range]{font:inherit}select{padding:4px 8px;border-radius:8px;border:1px solid var(--line);background:var(--card);color:var(--ink)}
input[type=range]{width:180px;accent-color:var(--accent)}
.dials{display:flex;gap:18px;justify-content:center;margin:4px 0 8px}
.dial{width:96px;height:96px;border-radius:50%;display:grid;place-items:center;font:700 1.1rem Georgia,serif;letter-spacing:.08em;border:4px solid var(--accent)}
input[type=checkbox]{width:20px;height:20px;accent-color:var(--accent)}
.dial span{font-size:.7rem;display:block;text-align:center;letter-spacing:.04em;opacity:.8}
button{font:inherit;font-weight:600;border:1px solid var(--accent);border-radius:8px;padding:9px 18px;background:var(--accent);color:#fff;cursor:pointer;transition:background .15s}
button:hover{background:var(--accent-hover)}button:disabled{opacity:.5;cursor:default}
button.ghost{background:none;color:var(--accent);border-color:var(--line)}button.ghost:hover{background:var(--pale)}
button.sm{padding:4px 10px;font-size:.82rem}button.del{color:var(--danger)}
button:focus-visible,input:focus-visible,select:focus-visible{outline:3px solid var(--accent);outline-offset:2px}
.actions{display:flex;gap:10px;justify-content:flex-end;margin-top:12px;flex-wrap:wrap}
.actions.left{justify-content:flex-start}
#msg{min-height:1.3em;color:var(--muted);font-size:.9rem;text-align:right}
.note{color:var(--muted);font-size:.86rem;margin:6px 0 0}
dl{display:grid;grid-template-columns:auto 1fr;gap:4px 14px;margin:0;font-size:.92rem}dt{color:var(--muted)}dd{margin:0;word-break:break-word}
.drop{border:2px dashed var(--line);border-radius:12px;padding:16px;text-align:center;transition:background .15s,border-color .15s}
.drop.over{background:var(--pale);border-color:var(--accent)}.drop small{display:block;color:var(--muted);margin-top:6px}
#up{min-height:1.3em;color:var(--muted);font-size:.9rem;margin:8px 0 0}
.grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(128px,1fr));gap:14px;margin-top:14px}
.pic{text-align:center;font-size:.8rem;color:var(--muted)}
.pic canvas,.pic .ph{width:100%;aspect-ratio:1;border-radius:50%;background:#111;display:block;border:3px solid var(--line)}
.pic.on canvas{border-color:var(--accent)}
.pic .ph{display:grid;place-items:center;color:#888;font-size:.75rem;padding:12px}
.pic .nm{margin:4px 0;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.pic .b{display:flex;gap:6px;justify-content:center}
</style></head><body><main>
<p class="eyebrow">Three Oak Woods</p>
<h1>Round Box Clock<small id="sub">roundclock.local</small></h1>

<section><h2>Face</h2>
<div class="dials"><div class="dial" id="pvDay">XII<span>day</span></div><div class="dial" id="pvNight">XII<span>night</span></div></div>
<div class="row"><label for="face">Face colour <small>(becomes the hands at night)</small></label><input type="color" id="face"></div>
<div class="row"><label for="ink">Hands &amp; numerals</label><input type="color" id="ink"></div>
<div class="row"><label for="secs">Second hand</label><input type="checkbox" id="secs"></div>
</section>

<section><h2>Day &amp; night</h2>
<div class="row"><label for="ds">Day face from</label><select id="ds"></select></div>
<div class="row"><label for="ns">Night face from</label><select id="ns"></select></div>
</section>

<section><h2>Display</h2>
<div class="row"><label for="bl">Brightness <b id="blv"></b></label><input type="range" id="bl" min="5" max="100" step="5"></div>
</section>

<section><h2>Slideshow</h2>
<div class="row"><label for="ss">Change picture every</label><select id="ss"></select></div>
<div class="row"><label for="shuf">Shuffle</label><input type="checkbox" id="shuf"></div>
<div class="row"><label id="sst">Stopped</label><span class="actions" style="margin:0"><button class="ghost sm" id="ssStart">Start</button><button class="ghost sm" id="ssNext">Next</button><button class="ghost sm" id="ssStop">Stop</button></span></div>
<p class="note">Tap the clock to start or stop the slideshow. Say "Jarvis" to talk; pictures step aside during a conversation and come back after.</p>
</section>

<section><h2>Ship's bell</h2>
<div class="row"><label for="chime">Strike the bells every half hour</label><input type="checkbox" id="chime"></div>
<div class="row"><label for="qf">Quiet from</label><select id="qf"></select></div>
<div class="row"><label for="qt">Quiet until</label><select id="qt"></select></div>
<div class="row"><label for="rb">Ring now</label><span class="actions" style="margin:0"><select id="rb"></select><button class="ghost sm" id="ring">Ring</button></span></div>
<p class="note">One bell at half past the first hour of each four-hour watch, up to eight bells at 4, 8 and 12 o'clock, struck in pairs. Silent during a conversation and in quiet hours (same hour = never quiet). Loudness follows the speaker volume.</p>
</section>

<div class="actions"><button class="ghost" id="reset">Defaults</button><button id="save">Save</button></div>
<div id="msg"></div>

<section><h2>Pictures</h2>
<div class="drop" id="drop"><input type="file" id="file" accept="image/*" multiple hidden>
<button id="pick">Add pictures…</button><small>or drop them here. Each is cropped to fill the round screen.</small></div>
<div id="up"></div>
<div class="grid" id="grid"><small>Loading…</small></div>
</section>

<section><h2>Status</h2><dl id="st"><dt>Loading</dt><dd>…</dd></dl></section>
</main>
<script>
const $=id=>document.getElementById(id);
const hex=n=>'#'+n.toString(16).padStart(6,'0');
const num=s=>parseInt(s.slice(1),16);
const esc=s=>String(s).replace(/[&<>"]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]));
const S=240,BYTES=S*S*2;
const DEF={face:0xCBB07E,ink:0x1C1A18,day_start:7,night_start:19,show_seconds:true,brightness:50,slide_secs:10,slide_shuffle:false,chime_enabled:true,quiet_from:22,quiet_to:7};
const INTERVALS=[3,5,10,15,20,30,45,60,120,300,600,1800,3600];
const fmtSecs=s=>s<60?s+' seconds':s<3600?(s/60)+(s==60?' minute':' minutes'):(s/3600)+(s==3600?' hour':' hours');
for(let n=0;n<=8;n++)$('rb').add(new Option(n?n+(n>1?' bells':' bell'):'Current',n));
for(const id of['ds','ns','qf','qt'])for(let h=0;h<24;h++)$(id).add(new Option(String(h).padStart(2,'0')+':00',h));
for(const s of INTERVALS)$('ss').add(new Option(fmtSecs(s),s));
function setInterval_(v){if(![...$('ss').options].some(o=>+o.value==v))$('ss').add(new Option(fmtSecs(v),v));$('ss').value=v;}
function preview(){const f=$('face').value,i=$('ink').value;
 Object.assign($('pvDay').style,{background:f,color:i});Object.assign($('pvNight').style,{background:'#000',color:f});
 $('blv').textContent=$('bl').value+'%';}
function fill(s){$('face').value=hex(s.face);$('ink').value=hex(s.ink);$('secs').checked=s.show_seconds;
 $('ds').value=s.day_start;$('ns').value=s.night_start;$('bl').value=s.brightness;
 setInterval_(s.slide_secs??10);$('shuf').checked=!!s.slide_shuffle;
 $('chime').checked=s.chime_enabled??true;$('qf').value=s.quiet_from??22;$('qt').value=s.quiet_to??7;preview();}
async function load(){const r=await fetch('/api/settings');fill(await r.json());}
async function save(){const body={face:num($('face').value),ink:num($('ink').value),show_seconds:$('secs').checked,
 day_start:+$('ds').value,night_start:+$('ns').value,brightness:+$('bl').value,slide_secs:+$('ss').value,slide_shuffle:$('shuf').checked,
 chime_enabled:$('chime').checked,quiet_from:+$('qf').value,quiet_to:+$('qt').value};
 $('msg').textContent='Saving…';
 const r=await fetch('/api/settings',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)});
 $('msg').textContent=r.ok?'Saved. The clock updates within a second.':'Not saved: '+await r.text();}
const fmtUp=s=>{const d=Math.floor(s/86400),h=Math.floor(s%86400/3600),m=Math.floor(s%3600/60);return(d?d+'d ':'')+h+'h '+m+'m';};
let current='';
async function status(){try{const s=await(await fetch('/api/status')).json();
 const sd=s.sd.mounted?`${s.sd.name} · ${s.sd.size_mb} MB ${s.sd.type}`+(s.sd.fs_free_mb!=null?` · ${s.sd.fs_free_mb} MB free`:''):'not mounted ('+s.sd.error+')';
 const rows=[['Firmware',s.version+' · built '+s.built],['IDF',s.idf],['Uptime',fmtUp(s.uptime_s)],['IP',s.ip],['Wi-Fi',s.rssi+' dBm'],
  ['Battery',s.battery_level<0?'n/a':s.battery_level+'%'+(s.charging?' (charging)':'')],['Memory',`${s.heap_kb} KB internal · ${s.psram_kb} KB PSRAM free`],['SD card',sd]];
 $('st').innerHTML=rows.map(([k,v])=>`<dt>${k}</dt><dd>${esc(v)}</dd>`).join('');
 const ss=s.slideshow||{};current=ss.active?ss.current||'':'';
 $('sst').textContent=ss.active?('Running'+(ss.current?' · '+ss.current:'')):'Stopped';
 document.querySelectorAll('.pic').forEach(p=>p.classList.toggle('on',p.dataset.n===current));}catch(e){}}
['face','ink','bl'].forEach(id=>$(id).addEventListener('input',preview));
$('save').onclick=save;$('reset').onclick=()=>{fill(DEF);$('msg').textContent='Defaults loaded. Press Save to apply.';};
async function slideshow(action){const r=await fetch('/api/slideshow?action='+action,{method:'POST'});
 $('msg').textContent=r.ok?'':'Slideshow: '+await r.text();setTimeout(status,700);}
$('ring').onclick=async()=>{const r=await fetch('/api/chime/ring?bells='+$('rb').value,{method:'POST'});
 $('msg').textContent=r.ok?'Ringing.':'Ring failed: '+await r.text();};
$('ssStart').onclick=()=>slideshow('start');$('ssNext').onclick=()=>slideshow('next');$('ssStop').onclick=()=>slideshow('stop');

// ---- pictures: decode RGB565 for thumbnails, convert uploads in the browser
function draw565(bytes,cv){cv.width=cv.height=S;const g=cv.getContext('2d'),im=g.createImageData(S,S),d=im.data;
 for(let i=0,o=0;i<BYTES;i+=2,o+=4){const v=bytes[i]|bytes[i+1]<<8;d[o]=(v>>11)*255/31;d[o+1]=(v>>5&63)*255/63;d[o+2]=(v&31)*255/31;d[o+3]=255;}
 g.putImageData(im,0,0);}
async function decode(file){
 try{return await createImageBitmap(file,{imageOrientation:'from-image'});}catch(e){}
 return await new Promise((res,rej)=>{const i=new Image(),u=URL.createObjectURL(file);
  i.onload=()=>{URL.revokeObjectURL(u);res(i);};i.onerror=()=>{URL.revokeObjectURL(u);rej(new Error("this browser can't open it (HEIC? export as JPEG)"));};i.src=u;});}
// Cover-crop to 240x240, then Floyd-Steinberg dither to RGB565 little-endian (LVGL native).
function toRgb565(img){const c=document.createElement('canvas');c.width=c.height=S;const g=c.getContext('2d');
 const w=img.width,h=img.height,k=Math.max(S/w,S/h),dw=w*k,dh=h*k;
 g.imageSmoothingEnabled=true;g.imageSmoothingQuality='high';g.fillStyle='#000';g.fillRect(0,0,S,S);
 g.drawImage(img,(S-dw)/2,(S-dh)/2,dw,dh);
 const d=g.getImageData(0,0,S,S).data,f=new Float32Array(S*S*3),out=new Uint8Array(BYTES),L=[31,63,31],q=[0,0,0];
 for(let i=0,j=0;i<d.length;i+=4,j+=3){f[j]=d[i];f[j+1]=d[i+1];f[j+2]=d[i+2];}
 for(let y=0;y<S;y++)for(let x=0;x<S;x++){const p=(y*S+x)*3;
  for(let ch=0;ch<3;ch++){const v=Math.min(255,Math.max(0,f[p+ch])),l=L[ch],qv=Math.round(v*l/255),e=v-qv*255/l;q[ch]=qv;
   if(x+1<S)f[p+3+ch]+=e*7/16;
   if(y+1<S){const n=p+S*3+ch;if(x>0)f[n-3]+=e*3/16;f[n]+=e*5/16;if(x+1<S)f[n+3]+=e/16;}}
  const v=q[0]<<11|q[1]<<5|q[2],o=(y*S+x)*2;out[o]=v&255;out[o+1]=v>>8;}
 return out;}
function nameFor(file,taken){let b=file.name.replace(/\.[^.]*$/,'').toLowerCase().replace(/[^a-z0-9_-]+/g,'-').replace(/^[-_]+|[-_]+$/g,'').slice(0,28)||'picture';
 let n=b+'.rgb565',i=2;while(taken.has(n))n=b+'-'+(i++)+'.rgb565';return n;}

let names=[];const cache=new Map();
function card(img){const n=img.name,ok=n.endsWith('.rgb565')&&img.bytes===BYTES;
 const el=document.createElement('div');el.className='pic'+(n===current?' on':'');el.dataset.n=n;
 el.innerHTML=(ok?'<canvas></canvas>':'<div class="ph">not a clock picture</div>')+`<div class="nm" title="${esc(n)}">${esc(n)}</div><div class="b">`+
  (ok?'<button class="ghost sm" data-a="show">Show</button>':'')+'<button class="ghost sm del" data-a="del">Delete</button></div>';
 el.querySelectorAll('button').forEach(b=>b.onclick=()=>b.dataset.a==='show'?show(n):del(n,el));
 if(ok&&cache.has(n))draw565(cache.get(n),el.querySelector('canvas'));
 return el;}
async function thumbs(){for(const el of document.querySelectorAll('.pic')){const n=el.dataset.n,cv=el.querySelector('canvas');
 if(!cv||cache.has(n))continue;
 try{const r=await fetch('/api/sd/file?name='+encodeURIComponent(n));if(!r.ok)continue;const b=new Uint8Array(await r.arrayBuffer());
  if(b.length===BYTES){cache.set(n,b);draw565(b,cv);}}catch(e){}}}
async function images(){try{const r=await fetch('/api/sd/images');if(!r.ok)throw 0;const j=await r.json();
 const list=j.images.sort((a,b)=>a.name.localeCompare(b.name));names=list.map(i=>i.name);
 const g=$('grid');g.innerHTML='';
 if(!list.length)g.innerHTML='<small>No pictures yet. Add some above.</small>';
 list.forEach(i=>g.appendChild(card(i)));thumbs();}catch(e){$('grid').innerHTML='<small>SD card not available.</small>';}}
async function show(n){const r=await fetch('/api/display/show?secs=10&name='+encodeURIComponent(n),{method:'POST'});
 $('up').textContent=r.ok?`Showing ${n} on the clock for 10 s.`:'Show failed: '+await r.text();}
async function del(n,el){if(!confirm(`Delete ${n} from the SD card?`))return;
 const r=await fetch('/api/sd/delete?name='+encodeURIComponent(n),{method:'POST'});
 if(r.ok){cache.delete(n);el.remove();names=names.filter(x=>x!==n);$('up').textContent=`Deleted ${n}.`;
  if(!names.length)$('grid').innerHTML='<small>No pictures yet. Add some above.</small>';}
 else $('up').textContent='Delete failed: '+await r.text();}
let busy=false;
async function addFiles(files){files=[...files].filter(f=>f.type.startsWith('image/')||/\.(heic|heif|jpe?g|png|gif|webp|bmp|avif)$/i.test(f.name));
 if(!files.length||busy)return;busy=true;$('pick').disabled=true;
 const taken=new Set(names);let done=0,failed=[];
 for(const [k,f] of files.entries()){const tag=`${k+1}/${files.length}`;
  try{$('up').textContent=`Converting ${tag}: ${f.name}…`;const bytes=toRgb565(await decode(f));
   const n=nameFor(f,taken);taken.add(n);$('up').textContent=`Uploading ${tag}: ${n}…`;
   const r=await fetch('/api/sd/upload?name='+encodeURIComponent(n),{method:'POST',headers:{'Content-Type':'application/octet-stream'},body:bytes});
   if(!r.ok)throw new Error(await r.text());
   cache.set(n,bytes);done++;}catch(e){failed.push(`${f.name} (${e.message||e})`);}}
 $('up').textContent=`Added ${done} of ${files.length}.`+(failed.length?' Failed: '+failed.join('; '):'');
 busy=false;$('pick').disabled=false;$('file').value='';images();}
$('pick').onclick=()=>$('file').click();$('file').onchange=e=>addFiles(e.target.files);
const dz=$('drop');['dragenter','dragover'].forEach(t=>dz.addEventListener(t,e=>{e.preventDefault();dz.classList.add('over');}));
['dragleave','drop'].forEach(t=>dz.addEventListener(t,e=>{e.preventDefault();dz.classList.remove('over');}));
dz.addEventListener('drop',e=>addFiles(e.dataTransfer.files));
load();status();images();setInterval(status,5000);
</script></body></html>)HTML";

// ---------------------------------------------------------------- handlers
esp_err_t SendJson(httpd_req_t* req, cJSON* root) {
    char* s = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    esp_err_t err = httpd_resp_sendstr(req, s ? s : "{}");
    cJSON_free(s);
    cJSON_Delete(root);
    return err;
}

int CurrentBrightness() {
    Backlight* bl = Board::GetInstance().GetBacklight();
    return bl ? bl->brightness() : -1;
}

esp_err_t IndexHandler(httpd_req_t* req) {
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, kIndexHtml, sizeof(kIndexHtml) - 1);
}

esp_err_t GetSettingsHandler(httpd_req_t* req) {
    const ClockSettings::Values v = ClockSettings::Get().Load();
    cJSON* root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "face", v.face);
    cJSON_AddNumberToObject(root, "ink", v.ink);
    cJSON_AddNumberToObject(root, "day_start", v.day_start);
    cJSON_AddNumberToObject(root, "night_start", v.night_start);
    cJSON_AddBoolToObject(root, "show_seconds", v.show_seconds);
    cJSON_AddNumberToObject(root, "brightness", CurrentBrightness());
    cJSON_AddNumberToObject(root, "slide_secs", v.slide_secs);
    cJSON_AddBoolToObject(root, "slide_shuffle", v.slide_shuffle);
    cJSON_AddBoolToObject(root, "chime_enabled", v.chime_enabled);
    cJSON_AddNumberToObject(root, "quiet_from", v.quiet_from);
    cJSON_AddNumberToObject(root, "quiet_to", v.quiet_to);
    return SendJson(req, root);
}

esp_err_t PostSettingsHandler(httpd_req_t* req) {
    char body[512];
    if (req->content_len <= 0 || req->content_len >= static_cast<int>(sizeof(body))) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "body too large or empty");
        return ESP_FAIL;
    }
    int got = 0;
    while (got < static_cast<int>(req->content_len)) {
        int r = httpd_req_recv(req, body + got, req->content_len - got);
        if (r <= 0) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "read failed");
            return ESP_FAIL;
        }
        got += r;
    }
    body[got] = '\0';

    cJSON* root = cJSON_Parse(body);
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid JSON");
        return ESP_FAIL;
    }
    ClockSettings::Values v = ClockSettings::Get().Load();
    auto get_int = [&](const char* key, int& out) {
        cJSON* item = cJSON_GetObjectItem(root, key);
        if (cJSON_IsNumber(item)) out = item->valueint;
    };
    int face = static_cast<int>(v.face), ink = static_cast<int>(v.ink), brightness = -1;
    get_int("face", face);
    get_int("ink", ink);
    get_int("day_start", v.day_start);
    get_int("night_start", v.night_start);
    get_int("brightness", brightness);
    get_int("slide_secs", v.slide_secs);
    get_int("quiet_from", v.quiet_from);
    get_int("quiet_to", v.quiet_to);
    cJSON* chime = cJSON_GetObjectItem(root, "chime_enabled");
    if (cJSON_IsBool(chime)) v.chime_enabled = cJSON_IsTrue(chime);
    cJSON* secs = cJSON_GetObjectItem(root, "show_seconds");
    if (cJSON_IsBool(secs)) v.show_seconds = cJSON_IsTrue(secs);
    cJSON* shuffle = cJSON_GetObjectItem(root, "slide_shuffle");
    if (cJSON_IsBool(shuffle)) v.slide_shuffle = cJSON_IsTrue(shuffle);
    cJSON_Delete(root);

    if (face < 0 || ink < 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad colour");
        return ESP_FAIL;
    }
    v.face = static_cast<uint32_t>(face);
    v.ink = static_cast<uint32_t>(ink);
    if (!ClockSettings::Get().Save(v)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "out of range (hours 0-23 and different; colours 000000-FFFFFF; slideshow 2-3600 s)");
        return ESP_FAIL;
    }
    if (brightness >= 5 && brightness <= 100) {
        if (Backlight* bl = Board::GetInstance().GetBacklight()) {
            bl->SetBrightness(static_cast<uint8_t>(brightness), true);  // persisted, like the voice tool
        }
    }
    ESP_LOGI(TAG, "settings saved: face %06lx ink %06lx day %d night %d secs %d bl %d slide %d s%s",
             static_cast<unsigned long>(v.face), static_cast<unsigned long>(v.ink),
             v.day_start, v.night_start, v.show_seconds, brightness, v.slide_secs,
             v.slide_shuffle ? " shuffled" : "");
    return GetSettingsHandler(req);
}

esp_err_t StatusHandler(httpd_req_t* req) {
    const esp_app_desc_t* app = esp_app_get_description();
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "version", app->version);
    char built[40];
    snprintf(built, sizeof(built), "%s %s", app->date, app->time);
    cJSON_AddStringToObject(root, "built", built);
    cJSON_AddStringToObject(root, "idf", app->idf_ver);
    cJSON_AddNumberToObject(root, "uptime_s", static_cast<double>(esp_timer_get_time() / 1000000));
    cJSON_AddNumberToObject(root, "heap_kb", heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024);
    cJSON_AddNumberToObject(root, "psram_kb", heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024);

    char ip[16] = "?";
    if (esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF")) {
        esp_netif_ip_info_t info;
        if (esp_netif_get_ip_info(netif, &info) == ESP_OK) {
            snprintf(ip, sizeof(ip), IPSTR, IP2STR(&info.ip));
        }
    }
    cJSON_AddStringToObject(root, "ip", ip);
    wifi_ap_record_t ap;
    cJSON_AddNumberToObject(root, "rssi", esp_wifi_sta_get_ap_info(&ap) == ESP_OK ? ap.rssi : 0);

    int level = -1;
    bool charging = false, discharging = false;
    if (!Board::GetInstance().GetBatteryLevel(level, charging, discharging)) level = -1;
    cJSON_AddNumberToObject(root, "battery_level", level);
    cJSON_AddBoolToObject(root, "charging", charging);
    cJSON_AddNumberToObject(root, "brightness", CurrentBrightness());

    cJSON* sd = cJSON_Parse(clock_sd::StatusJson().c_str());
    cJSON_AddItemToObject(root, "sd", sd ? sd : cJSON_CreateObject());

    cJSON* ss = cJSON_AddObjectToObject(root, "slideshow");
    cJSON_AddBoolToObject(ss, "active", clock_slideshow::IsActive());
    cJSON_AddStringToObject(ss, "current", clock_slideshow::Current().c_str());
    return SendJson(req, root);
}

// ---------------------------------------------------------------- SD images
constexpr const char* kImageDir = clock_slideshow::kImageDir;
constexpr size_t kMaxUpload = 2 * 1024 * 1024;

bool ValidImageName(const std::string& n) {
    if (n.empty() || n.size() > 40 || n[0] == '.') return false;
    for (char c : n) {
        if (!(isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '_' || c == '-')) return false;
    }
    const auto dot = n.rfind('.');
    if (dot == std::string::npos) return false;
    const std::string ext = n.substr(dot);
    return ext == ".rgb565" || ext == ".png" || ext == ".jpg" || ext == ".bmp";
}

std::string QueryParam(httpd_req_t* req, const char* key) {
    char query[128];
    char val[64];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) return "";
    if (httpd_query_key_value(query, key, val, sizeof(val)) != ESP_OK) return "";
    return val;
}

esp_err_t Fail(httpd_req_t* req, httpd_err_code_t code, const char* msg) {
    httpd_resp_send_err(req, code, msg);
    return ESP_FAIL;
}

esp_err_t UploadHandler(httpd_req_t* req) {
    if (!clock_sd::IsMounted()) return Fail(req, HTTPD_500_INTERNAL_SERVER_ERROR, "SD card not mounted");
    const std::string name = QueryParam(req, "name");
    if (!ValidImageName(name)) return Fail(req, HTTPD_400_BAD_REQUEST, "name: [A-Za-z0-9._-], max 40, .rgb565/.png/.jpg/.bmp");
    if (req->content_len == 0 || req->content_len > kMaxUpload) return Fail(req, HTTPD_400_BAD_REQUEST, "empty or larger than 2 MB");

    mkdir(kImageDir, 0775);  // fine if it exists
    const std::string path = std::string(kImageDir) + "/" + name;
    const std::string tmp = path + ".part";
    FILE* f = fopen(tmp.c_str(), "wb");
    if (!f) return Fail(req, HTTPD_500_INTERNAL_SERVER_ERROR, "cannot create file");
    char* buf = static_cast<char*>(malloc(4096));
    size_t left = req->content_len;
    bool ok = buf != nullptr;
    while (ok && left > 0) {
        int r = httpd_req_recv(req, buf, left < 4096 ? left : 4096);
        if (r == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (r <= 0 || fwrite(buf, 1, r, f) != static_cast<size_t>(r)) ok = false;
        else left -= r;
    }
    free(buf);
    fclose(f);
    if (!ok) {
        remove(tmp.c_str());
        return Fail(req, HTTPD_500_INTERNAL_SERVER_ERROR, "upload interrupted or write failed");
    }
    int moved;
    {
        std::unique_lock<std::shared_mutex> files(clock_sd::FileMutex());  // nobody has `path` open
        remove(path.c_str());
        moved = rename(tmp.c_str(), path.c_str());
    }
    if (moved != 0) {
        remove(tmp.c_str());
        return Fail(req, HTTPD_500_INTERNAL_SERVER_ERROR, "could not rename upload");
    }
    ESP_LOGI(TAG, "uploaded %s (%u bytes)", path.c_str(), static_cast<unsigned>(req->content_len));
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "saved", path.c_str());
    cJSON_AddNumberToObject(root, "bytes", req->content_len);
    return SendJson(req, root);
}

esp_err_t ListImagesHandler(httpd_req_t* req) {
    cJSON* root = cJSON_CreateObject();
    cJSON* arr = cJSON_AddArrayToObject(root, "images");
    if (DIR* dir = opendir(kImageDir)) {
        while (dirent* e = readdir(dir)) {
            if (e->d_type == DT_DIR || !ValidImageName(e->d_name)) continue;
            cJSON* item = cJSON_CreateObject();
            cJSON_AddStringToObject(item, "name", e->d_name);
            struct stat st;
            const std::string p = std::string(kImageDir) + "/" + e->d_name;
            if (stat(p.c_str(), &st) == 0) cJSON_AddNumberToObject(item, "bytes", static_cast<double>(st.st_size));
            cJSON_AddItemToArray(arr, item);
        }
        closedir(dir);
    }
    return SendJson(req, root);
}

esp_err_t ShowHandler(httpd_req_t* req) {
    const std::string name = QueryParam(req, "name");
    if (!ValidImageName(name)) return Fail(req, HTTPD_400_BAD_REQUEST, "bad name");
    const std::string secs_s = QueryParam(req, "secs");
    const int secs = secs_s.empty() ? 10 : atoi(secs_s.c_str());
    const std::string err = clock_overlay::ShowRgb565File(std::string(kImageDir) + "/" + name, secs);
    if (!err.empty()) return Fail(req, HTTPD_400_BAD_REQUEST, err.c_str());
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "showing", name.c_str());
    cJSON_AddNumberToObject(root, "secs", secs);
    return SendJson(req, root);
}

// GET /api/sd/file?name=  Streams a picture (the page decodes .rgb565 for thumbnails).
esp_err_t FileHandler(httpd_req_t* req) {
    if (!clock_sd::IsMounted()) return Fail(req, HTTPD_500_INTERNAL_SERVER_ERROR, "SD card not mounted");
    const std::string name = QueryParam(req, "name");
    if (!ValidImageName(name)) return Fail(req, HTTPD_400_BAD_REQUEST, "bad name");
    const std::string path = std::string(kImageDir) + "/" + name;

    std::shared_lock<std::shared_mutex> files(clock_sd::FileMutex());
    struct stat st;
    if (stat(path.c_str(), &st) != 0) return Fail(req, HTTPD_404_NOT_FOUND, "not found");
    char etag[48];
    // newlib-nano printf has no %lld: keep both fields in unsigned long.
    snprintf(etag, sizeof(etag), "\"%lu-%lu\"", static_cast<unsigned long>(st.st_size),
             static_cast<unsigned long>(st.st_mtime));
    httpd_resp_set_hdr(req, "ETag", etag);
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");  // revalidate with the ETag
    char inm[48];
    if (httpd_req_get_hdr_value_str(req, "If-None-Match", inm, sizeof(inm)) == ESP_OK && strcmp(inm, etag) == 0) {
        httpd_resp_set_status(req, "304 Not Modified");
        return httpd_resp_send(req, nullptr, 0);
    }
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return Fail(req, HTTPD_500_INTERNAL_SERVER_ERROR, "open failed");
    const char* type = "application/octet-stream";
    if (name.size() > 4) {
        const std::string ext = name.substr(name.rfind('.'));
        if (ext == ".png") type = "image/png";
        else if (ext == ".jpg") type = "image/jpeg";
        else if (ext == ".bmp") type = "image/bmp";
    }
    httpd_resp_set_type(req, type);
    char* buf = static_cast<char*>(malloc(4096));
    esp_err_t err = buf ? ESP_OK : ESP_ERR_NO_MEM;
    while (err == ESP_OK) {
        const size_t n = fread(buf, 1, 4096, f);
        if (n == 0) break;
        err = httpd_resp_send_chunk(req, buf, n);
    }
    free(buf);
    fclose(f);
    if (err != ESP_OK) return err;  // client gone; httpd closes the socket
    return httpd_resp_send_chunk(req, nullptr, 0);
}

// POST /api/sd/delete?name=
esp_err_t DeleteHandler(httpd_req_t* req) {
    if (!clock_sd::IsMounted()) return Fail(req, HTTPD_500_INTERNAL_SERVER_ERROR, "SD card not mounted");
    const std::string name = QueryParam(req, "name");
    if (!ValidImageName(name)) return Fail(req, HTTPD_400_BAD_REQUEST, "bad name");
    const std::string path = std::string(kImageDir) + "/" + name;
    int r;
    {
        std::unique_lock<std::shared_mutex> files(clock_sd::FileMutex());  // waits for readers
        r = remove(path.c_str());
    }
    if (r != 0) return Fail(req, HTTPD_404_NOT_FOUND, "not found");
    ESP_LOGI(TAG, "deleted %s", path.c_str());
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "deleted", name.c_str());
    return SendJson(req, root);
}

// POST /api/chime/ring?bells=0..8  (0 = the count for the current half hour). Ignores quiet hours.
esp_err_t ChimeRingHandler(httpd_req_t* req) {
    const std::string b = QueryParam(req, "bells");
    const int bells = b.empty() ? 0 : atoi(b.c_str());
    if (bells < 0 || bells > 8) return Fail(req, HTTPD_400_BAD_REQUEST, "bells must be 0-8");
    clock_chime::RingNow(bells);
    cJSON* root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "bells", bells);
    return SendJson(req, root);
}

// POST /api/chime/preview  body = OGG/Opus clip (<= 128 KB). Plays once; nothing is stored.
esp_err_t ChimePreviewHandler(httpd_req_t* req) {
    constexpr size_t kMax = 128 * 1024;
    if (req->content_len < 64 || req->content_len > kMax) return Fail(req, HTTPD_400_BAD_REQUEST, "OGG clip, 64 B to 128 KB");
    std::string clip(req->content_len, '\0');
    size_t got = 0;
    while (got < clip.size()) {
        int r = httpd_req_recv(req, clip.data() + got, clip.size() - got);
        if (r == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (r <= 0) return Fail(req, HTTPD_400_BAD_REQUEST, "read failed");
        got += r;
    }
    if (clip.compare(0, 4, "OggS") != 0) return Fail(req, HTTPD_400_BAD_REQUEST, "not an OGG file");
    if (!clock_chime::PlayPreview(std::move(clip))) return Fail(req, HTTPD_500_INTERNAL_SERVER_ERROR, "busy: a preview is still queued");
    cJSON* root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "queued", true);
    return SendJson(req, root);
}

// POST /api/slideshow?action=start|stop|toggle|next
esp_err_t SlideshowHandler(httpd_req_t* req) {
    const std::string action = QueryParam(req, "action");
    if (action == "start") clock_slideshow::Start();
    else if (action == "stop") clock_slideshow::Stop();
    else if (action == "toggle") clock_slideshow::Toggle();
    else if (action == "next") clock_slideshow::Next();
    else return Fail(req, HTTPD_400_BAD_REQUEST, "action: start, stop, toggle or next");
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "action", action.c_str());
    return SendJson(req, root);
}

// GET /favicon.ico, /favicon.svg, /apple-touch-icon.png: Three Oak Woods site icons, from flash.
esp_err_t IconHandler(httpd_req_t* req) {
    const char* uri = req->uri;
    const unsigned char* data = kFaviconIco;
    size_t size = sizeof(kFaviconIco);
    const char* type = "image/x-icon";
    if (strcmp(uri, "/favicon.svg") == 0) {
        data = kFaviconSvg;
        size = sizeof(kFaviconSvg);
        type = "image/svg+xml";
    } else if (strcmp(uri, "/apple-touch-icon.png") == 0) {
        data = kAppleTouchIcon;
        size = sizeof(kAppleTouchIcon);
        type = "image/png";
    }
    httpd_resp_set_type(req, type);
    httpd_resp_set_hdr(req, "Cache-Control", "public, max-age=604800");  // a week; changes need a reflash
    return httpd_resp_send(req, reinterpret_cast<const char*>(data), size);
}

// ---------------------------------------------------------------- startup
void StartServices() {
    if (esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF")) {
        esp_netif_set_hostname(netif, kHostname);  // DHCP hostname; takes effect on the next lease
    }

    esp_err_t err = mdns_init();
    if (err == ESP_OK) {
        mdns_hostname_set(kHostname);
        mdns_instance_name_set("Round Box Clock");
        mdns_service_add("Round Box Clock", "_http", "_tcp", 80, nullptr, 0);
        ESP_LOGI(TAG, "mDNS: %s.local", kHostname);
    } else {
        ESP_LOGE(TAG, "mdns_init failed: %s", esp_err_to_name(err));
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.ctrl_port = 32769;      // keep clear of any other httpd instance's default ctrl port
    config.stack_size = 6144;
    config.lru_purge_enable = true;
    config.max_uri_handlers = 20;  // 15 registered; registration failures are logged below
    config.recv_wait_timeout = 15;  // uploads
    httpd_handle_t server = nullptr;
    err = httpd_start(&server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed: %s", esp_err_to_name(err));
        return;
    }
    const httpd_uri_t uris[] = {
        {.uri = "/", .method = HTTP_GET, .handler = IndexHandler, .user_ctx = nullptr},
        {.uri = "/api/settings", .method = HTTP_GET, .handler = GetSettingsHandler, .user_ctx = nullptr},
        {.uri = "/api/settings", .method = HTTP_POST, .handler = PostSettingsHandler, .user_ctx = nullptr},
        {.uri = "/api/status", .method = HTTP_GET, .handler = StatusHandler, .user_ctx = nullptr},
        {.uri = "/api/sd/upload", .method = HTTP_POST, .handler = UploadHandler, .user_ctx = nullptr},
        {.uri = "/api/sd/images", .method = HTTP_GET, .handler = ListImagesHandler, .user_ctx = nullptr},
        {.uri = "/api/display/show", .method = HTTP_POST, .handler = ShowHandler, .user_ctx = nullptr},
        {.uri = "/api/sd/file", .method = HTTP_GET, .handler = FileHandler, .user_ctx = nullptr},
        {.uri = "/api/sd/delete", .method = HTTP_POST, .handler = DeleteHandler, .user_ctx = nullptr},
        {.uri = "/api/slideshow", .method = HTTP_POST, .handler = SlideshowHandler, .user_ctx = nullptr},
        {.uri = "/api/chime/ring", .method = HTTP_POST, .handler = ChimeRingHandler, .user_ctx = nullptr},
        {.uri = "/api/chime/preview", .method = HTTP_POST, .handler = ChimePreviewHandler, .user_ctx = nullptr},
        {.uri = "/favicon.ico", .method = HTTP_GET, .handler = IconHandler, .user_ctx = nullptr},
        {.uri = "/favicon.svg", .method = HTTP_GET, .handler = IconHandler, .user_ctx = nullptr},
        {.uri = "/apple-touch-icon.png", .method = HTTP_GET, .handler = IconHandler, .user_ctx = nullptr},
    };
    for (const auto& u : uris) {
        if (httpd_register_uri_handler(server, &u) != ESP_OK) {
            ESP_LOGE(TAG, "could not register %s", u.uri);
        }
    }
    ESP_LOGI(TAG, "web settings at http://%s.local/", kHostname);
}

void WaitTask(void*) {
    for (;;) {
        esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        esp_netif_ip_info_t info = {};
        if (netif && esp_netif_get_ip_info(netif, &info) == ESP_OK && info.ip.addr != 0) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    StartServices();
    vTaskDelete(nullptr);
}

}  // namespace

void StartWhenNetworkReady() {
    static bool started = false;
    if (started) return;
    started = true;
    xTaskCreate(WaitTask, "clock_web_wait", 4096, nullptr, 3, nullptr);
}

}  // namespace clock_web
