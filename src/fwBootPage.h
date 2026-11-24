#pragma once
// Status page of an install from GitHub: GET /update?to=<tag>&lang=nl|en.
//
// The web app opens it after "Install". It is served twice: by the running firmware before the
// restart (routesBasic / main.cpp) and by the minimal server of fwBootInstall() during the
// download after the restart (fwUpdate.h), when nothing else answers. It follows
// GET /api/firmware and shows the steps:
//   1. the controller restarts - the firmware is downloaded and installed after the restart
//   2. restarted (the minimal server answers: "bootInstall": true)
//   3. downloading, with a progress bar
//   4. installing and restarting
//   5. done: back to the web app (the new firmware answers with "current" = the tag)
// The API key comes from the web app's localStorage (same origin); the minimal server does not
// ask for it (it only shows the progress).
static const char FW_BOOT_PAGE[] = R"HTML(<!doctype html><html><head><meta charset=utf-8>
<meta name=viewport content="width=device-width,initial-scale=1"><title>Firmware update</title>
<style>
:root{--bg:#f4f5f7;--card:#fff;--ink:#1d2433;--mute:#667085;--acc:#e8590c;--ok:#2f9e44;--bad:#c92a2a;--line:#e4e7ec}
@media (prefers-color-scheme:dark){:root{--bg:#14161a;--card:#1d2026;--ink:#e6e8eb;--mute:#9aa1ab;--line:#2c3038}}
body{margin:0;background:var(--bg);color:var(--ink);font:15px/1.45 system-ui,-apple-system,sans-serif}
main{max-width:480px;margin:8vh auto;padding:0 16px}
.c{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:20px}
h1{font-size:18px;margin:0 0 4px}.s{color:var(--mute);margin:0 0 14px;font-size:14px}
ol{list-style:none;margin:0;padding:0}li{padding:7px 0;color:var(--mute)}
li>div{display:flex;gap:10px;align-items:flex-start}
li i{flex:0 0 20px;height:20px;border-radius:50%;border:2px solid var(--line);box-sizing:border-box;font:700 12px/16px system-ui;text-align:center;color:#fff}
li.a{color:var(--ink);font-weight:600}li.a i{border-color:var(--acc);border-top-color:transparent;animation:r 1s linear infinite}
li.d{color:var(--ink)}li.d i{background:var(--ok);border-color:var(--ok)}li.d i:after{content:"\2713"}
li.x{color:var(--bad);font-weight:600}li.x i{background:var(--bad);border-color:var(--bad)}li.x i:after{content:"!"}
@keyframes r{to{transform:rotate(360deg)}}
.b{height:8px;background:var(--line);border-radius:4px;margin:8px 0 2px 30px;overflow:hidden}
.b div{height:100%;width:0;background:var(--acc);transition:width .5s}
.m{margin-left:30px;font-size:13px;color:var(--mute);font-weight:400}
#e{color:var(--bad);margin:14px 0 0}#e:empty{display:none}a{color:var(--acc)}#k{margin:14px 0 0;font-size:14px}
</style></head><body><main><div class=c><h1 id=h></h1><p class=s id=s></p><ol id=l></ol><p id=e></p><p id=k></p></div></main>
<script>
const q=new URLSearchParams(location.search),to=q.get("to")||"";
const nl=(q.get("lang")||navigator.language||"").startsWith("nl");
const T=nl?{h:"Firmware-update"+(to?" naar "+to:""),s:"De kachel werkt gewoon verder. Laat deze pagina open; ze gaat aan het eind zelf terug naar de app.",
st:["Controller herstart – na de herstart wordt de firmware gedownload en geïnstalleerd","Herstart","Firmware downloaden","Installeren en herstarten","Klaar – terug naar de app"],
con:"verbinden met GitHub…",fail:"Update mislukt: ",back:"Terug naar de app",slow:"Nog geen antwoord van de controller. Wacht nog even of ga terug naar de app."}
:{h:"Firmware update"+(to?" to "+to:""),s:"The stove carries on as usual. Keep this page open; it returns to the app by itself at the end.",
st:["Controller restarts – after the restart the firmware is downloaded and installed","Restarted","Downloading firmware","Installing and restarting","Done – back to the app"],
con:"connecting to GitHub…",fail:"Update failed: ",back:"Back to the app",slow:"No answer from the controller yet. Wait a little longer or go back to the app."};
document.getElementById("h").textContent=T.h;document.getElementById("s").textContent=T.s;
const L=document.getElementById("l"),li=T.st.map(t=>{const e=document.createElement("li"),r=document.createElement("div"),s=document.createElement("span");
r.append(document.createElement("i"),s);s.textContent=t;e.append(r);L.append(e);return e});
const bar=document.createElement("div");bar.className="b";bar.hidden=true;bar.append(document.createElement("div"));
const pm=document.createElement("div");pm.className="m";li[2].append(bar,pm);
let key="";try{key=localStorage.getItem("apiKey")||""}catch(e){}
let step=-1,seenBoot=false,away=false,over=false;const t0=Date.now();
function set(n,p){step=Math.max(step,n);li.forEach((e,i)=>e.className=i<step||(step==4&&i==4)?"d":i==step?"a":"");
 if(p&&p.total){bar.hidden=false;const pc=Math.round(p.done/p.total*100);bar.firstChild.style.width=pc+"%";pm.textContent=pc+" % – "+Math.round(p.done/1024)+" / "+Math.round(p.total/1024)+" KB"}
 else if(step==2&&!p)pm.textContent=T.con;if(step>2){bar.firstChild.style.width="100%";pm.textContent=""}}
function fail(m){over=true;li.forEach((e,i)=>{if(i==Math.max(step,0))e.className="x"});document.getElementById("e").textContent=T.fail+(m||"?");backLink()}
function backLink(){const k=document.getElementById("k");k.textContent="";const a=document.createElement("a");a.href="/";a.textContent=T.back;k.append(a)}
async function get(){const c=new AbortController(),t=setTimeout(()=>c.abort(),4000);
 try{const r=await fetch("/api/firmware",{cache:"no-store",headers:key?{"X-API-Key":key}:{},signal:c.signal});return await r.json()}finally{clearTimeout(t)}}
async function tick(){let f=null;try{f=await get()}catch(e){}
 if(!f){away=true;set(seenBoot?3:0)}
 else if(f.bootInstall){seenBoot=true;const p=f.phase;
  if(p=="failed")return fail(f.lastError);if(p=="installing"||p=="restarting")set(3);else set(2,p=="downloading"?f.progress:null)}
 else if(f.state=="checking"||f.state=="restarting"){set(0)}
 else if(away||seenBoot){
  if((to&&f.current==to)||(!to&&seenBoot)){set(4);over=true;setTimeout(()=>location.href="/",2500);return}
  return fail(f.lastError||(f.lastAttempt||"").replace(/^failed: /,"")||(nl?"de controller draait nog "+f.current:"the controller still runs "+f.current))}
 else if(f.state=="error"&&f.lastError)return fail(f.lastError);
 else set(0);
 if(Date.now()-t0>360000&&!document.getElementById("e").textContent){document.getElementById("e").textContent=T.slow;backLink()}
 if(!over)setTimeout(tick,1500)}
set(0);tick();
</script></body></html>)HTML";
