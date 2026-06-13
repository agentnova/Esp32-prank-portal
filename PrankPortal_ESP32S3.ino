/*
 * ============================================================
 *  Prank Captive Portal  -  ESP32-S3   (v7)
 * ------------------------------------------------------------
 *  Open WiFi AP -> captive "sign in" page -> fake connect
 *  sequence -> prank message + confetti.
 *
 *  Live dashboard lets you edit the SSID, prank messages and
 *  on/off state -- all saved to flash, no re-flash needed.
 *
 *  v7 adds:
 *   - PIN-LOCKED admin: the dashboard, /api/stats, /save, CSV
 *     export and clear all require a login (HTTP Basic Auth).
 *     Username "admin"; PIN editable in Settings (default "1234").
 *     The prank page itself stays open so victims aren't blocked.
 *   - mDNS: reach the dashboard at http://prank.local/dashboard
 *     (the IP http://4.3.2.1/dashboard always works as a fallback).
 *
 *  Connect to the AP, open a normal browser, go to
 *  http://prank.local/dashboard   (or  http://4.3.2.1/dashboard )
 *
 *  No passwords captured. No real network impersonated. Joke only.
 *
 *  Board:  ESP32S3 Dev Module   (Arduino-ESP32 core 3.x)
 *  Libs :  WiFi, DNSServer, WebServer, Preferences, ESPmDNS (bundled)
 * ============================================================
 */

#include <WiFi.h>
#include <DNSServer.h>
#include <WebServer.h>
#include <Preferences.h>
#include <ESPmDNS.h>

// ---- Fixed settings ----------------------------------------
const int   AP_CHANNEL = 1;            // 1, 6 or 11 are best
const int   AP_MAXCONN = 8;            // max simultaneous clients
const char* ADMIN_PATH = "/dashboard"; // your private view URL
const char* ADMIN_USER = "admin";      // dashboard login username
const char* MDNS_HOST  = "prank";      // -> http://prank.local
// ------------------------------------------------------------

// ---- Editable config (loaded from / saved to flash) --------
String apSsid       = "Free_Public_WiFi";   // default network name
bool   prankEnabled = true;                 // master on/off
String adminPin     = "1234";               // dashboard PIN (change it!)

#define MAX_MSGS 12
String messages[MAX_MSGS];
int    numMsgs = 0;

const char* DEFAULT_MSGS[] = {
  "ACCESS GRANTED to nothing. Enjoy your stay. &#128682;",
  "No internet, but here's a reminder: drink some water. &#128167;",
  "This whole portal lives on a chip smaller than your thumbnail. Cool, right? &#129299;",
  "Loading internet... loading internet... yeah, this isn't happening.",
  "You connected to a stranger's open WiFi without thinking. We should talk about your security habits. &#128274;",
  "This network exists purely to waste 8 seconds of your life. You're welcome. &#128521;"
};
const int NUM_DEFAULT = sizeof(DEFAULT_MSGS) / sizeof(DEFAULT_MSGS[0]);

Preferences prefs;
int msgIndex = 0;     // advances on each portal view

int getCount() { return numMsgs; }
String getMsg(int i) { return (numMsgs > 0) ? messages[i % numMsgs] : String(""); }
// ------------------------------------------------------------

// ---- Victim tracking ---------------------------------------
#define MAX_DEVICES 24
struct Device {
  bool      used;
  uint8_t   mac[6];
  IPAddress ip;
  bool      online;
  uint32_t  firstSeen;
  uint32_t  lastSeen;
  uint32_t  prankCount;
  String    lastMsg;
};
Device   devices[MAX_DEVICES];
uint32_t totalPranks = 0;
// ------------------------------------------------------------

const byte  DNS_PORT = 53;
IPAddress   apIP(4, 3, 2, 1);
IPAddress   netMask(255, 255, 255, 0);

DNSServer   dnsServer;
WebServer   server(80);

// ============================================================
//  PORTAL PAGE  (HEAD -> message -> </p> -> TAIL)
// ============================================================
const char PAGE_HEAD[] PROGMEM = R"=====(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1, maximum-scale=1">
<title>Free Public WiFi</title>
<style>
  :root{ --accent:#7c5cff; }
  *{box-sizing:border-box;margin:0;padding:0;-webkit-tap-highlight-color:transparent}
  html,body{height:100%}
  body{font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",system-ui,sans-serif;
    min-height:100vh;display:flex;align-items:center;justify-content:center;
    padding:24px;color:#fff;text-align:center;overflow:hidden;position:relative;background:#0f0c29}
  .bg{position:fixed;inset:0;z-index:-2;
    background:
      radial-gradient(45% 55% at 18% 18%, rgba(124,92,255,.55), transparent 60%),
      radial-gradient(45% 55% at 82% 28%, rgba(195,55,100,.50), transparent 60%),
      radial-gradient(55% 65% at 50% 92%, rgba(29,38,113,.65), transparent 60%),
      linear-gradient(135deg,#0f0c29,#24243e);
    animation:drift 16s ease-in-out infinite alternate}
  @keyframes drift{0%{transform:scale(1) translateY(0)}100%{transform:scale(1.18) translateY(-12px)}}
  .spark{position:fixed;border-radius:50%;background:rgba(255,255,255,.55);z-index:-1;
    animation:rise-spark linear infinite}
  @keyframes rise-spark{from{transform:translateY(112vh)}to{transform:translateY(-12vh)}}
  .card{position:relative;width:100%;max-width:380px;
    background:rgba(255,255,255,.07);border:1px solid rgba(255,255,255,.16);
    border-radius:26px;padding:38px 26px;
    -webkit-backdrop-filter:blur(14px);backdrop-filter:blur(14px);
    box-shadow:0 24px 60px rgba(0,0,0,.45);
    animation:rise .6s cubic-bezier(.2,.8,.2,1) both}
  @keyframes rise{from{opacity:0;transform:translateY(20px) scale(.96)}to{opacity:1;transform:none}}
  h1{font-size:27px;font-weight:800;letter-spacing:-.5px;margin-bottom:4px}
  .sub{font-size:14px;opacity:.72;min-height:20px;margin-top:2px}
  .wifi{width:78px;height:62px;margin:0 auto 18px;display:block}
  .wifi path{animation:wave 1.7s ease-in-out infinite}
  .wifi path:nth-of-type(1){animation-delay:0s}
  .wifi path:nth-of-type(2){animation-delay:.22s}
  .wifi path:nth-of-type(3){animation-delay:.44s}
  @keyframes wave{0%,100%{opacity:.2}50%{opacity:1}}
  .bar{height:12px;border-radius:20px;margin:24px 0 8px;
    background:rgba(255,255,255,.14);overflow:hidden;position:relative}
  .fill{height:100%;width:0%;border-radius:20px;position:relative;
    background:linear-gradient(90deg,#4ade80,#22d3ee);
    transition:width .3s cubic-bezier(.4,0,.2,1)}
  .fill::after{content:'';position:absolute;inset:0;
    background:linear-gradient(90deg,transparent,rgba(255,255,255,.55),transparent);
    transform:translateX(-100%);animation:shimmer 1.2s infinite}
  @keyframes shimmer{to{transform:translateX(100%)}}
  .pct{font-size:13px;opacity:.75;font-variant-numeric:tabular-nums}
  .hidden{display:none}
  .emoji{font-size:70px;line-height:1}
  .pop{animation:pop .55s cubic-bezier(.2,1.4,.4,1) both}
  @keyframes pop{0%{transform:scale(.3) rotate(-12deg);opacity:0}
    70%{transform:scale(1.15) rotate(6deg)}100%{transform:scale(1) rotate(0);opacity:1}}
  .shake{animation:shake .5s .25s both}
  @keyframes shake{10%,90%{transform:translateX(-2px)}20%,80%{transform:translateX(4px)}
    30%,50%,70%{transform:translateX(-7px)}40%,60%{transform:translateX(7px)}}
  .gtitle{font-size:34px;font-weight:900;letter-spacing:1px;margin:8px 0 4px;
    background:linear-gradient(90deg,#fef08a,#fb7185,#a78bfa);
    -webkit-background-clip:text;background-clip:text;color:transparent}
  .msg{font-size:18px;line-height:1.55;margin:14px 4px 0;opacity:.95}
  .btn{margin-top:22px;border:none;cursor:pointer;font-size:15px;font-weight:600;color:#fff;
    padding:13px 22px;border-radius:14px;background:linear-gradient(135deg,var(--accent),#c33764);
    box-shadow:0 8px 22px rgba(124,92,255,.4);transition:transform .15s,box-shadow .15s}
  .btn:active{transform:scale(.95);box-shadow:0 4px 12px rgba(124,92,255,.35)}
  .foot{margin-top:22px;font-size:11.5px;opacity:.55;line-height:1.5}
  canvas#cfx{position:fixed;inset:0;pointer-events:none;z-index:5}
</style>
</head>
<body>
<div class="bg"></div>
<canvas id="cfx"></canvas>
<div class="card">

  <div id="loading">
    <svg class="wifi" viewBox="0 0 100 80" aria-hidden="true">
      <g fill="none" stroke="#fff" stroke-width="7" stroke-linecap="round">
        <path d="M8 30 A62 62 0 0 1 92 30"/>
        <path d="M24 45 A40 40 0 0 1 76 45"/>
        <path d="M38 58 A22 22 0 0 1 62 58"/>
      </g>
      <circle cx="50" cy="69" r="5" fill="#fff"/>
    </svg>
    <h1>Free Public WiFi</h1>
    <p class="sub" id="status">Initializing secure tunnel</p>
    <div class="bar"><div class="fill" id="fill"></div></div>
    <p class="pct" id="pct">0%</p>
  </div>

  <div id="gotcha" class="hidden">
    <div class="emoji pop">&#128520;</div>
    <h1 class="gtitle shake">GOTCHA!</h1>
    <p class="msg" id="msg">)=====";

// --- the rotating message is streamed in here, then </p> ---

const char PAGE_TAIL[] PROGMEM = R"=====(
    <button class="btn" id="again">Get me real WiFi &#8594;</button>
    <p class="foot">Powered by ESP32-S3 &bull; Nothing was kept &bull; It's a joke &#128578;</p>
  </div>

</div>
<script>
(function(){
  for(var i=0;i<14;i++){
    var s=document.createElement('div');s.className='spark';
    var sz=3+Math.random()*5;
    s.style.width=sz+'px';s.style.height=sz+'px';
    s.style.left=Math.random()*100+'vw';
    s.style.animationDuration=(8+Math.random()*10)+'s';
    s.style.animationDelay=(-Math.random()*12)+'s';
    s.style.opacity=0.2+Math.random()*0.5;
    document.body.appendChild(s);
  }
  var steps=['Initializing secure tunnel','Acquiring IP address',
             'Authenticating device','Optimizing your speed','Almost there'];
  var p=0,si=0;
  var fill=document.getElementById('fill'),
      pct=document.getElementById('pct'),
      status=document.getElementById('status');
  var t=setInterval(function(){
    p+=Math.floor(Math.random()*11)+4;
    if(p>=100){p=100;clearInterval(t);setTimeout(reveal,450);}
    fill.style.width=p+'%';
    pct.textContent=p+'%';
    var ns=Math.min(steps.length-1,Math.floor(p/100*steps.length));
    if(ns!==si){si=ns;status.textContent=steps[si];}
  },280);
  function reveal(){
    document.getElementById('loading').classList.add('hidden');
    document.getElementById('gotcha').classList.remove('hidden');
    confetti();
  }
  document.getElementById('again').addEventListener('click',function(){location.reload();});
  function confetti(){
    var c=document.getElementById('cfx'),x=c.getContext('2d');
    c.width=innerWidth;c.height=innerHeight;
    var cols=['#fef08a','#fb7185','#a78bfa','#4ade80','#22d3ee','#ffffff'],P=[];
    for(var i=0;i<150;i++)P.push({
      x:Math.random()*c.width, y:-20-Math.random()*c.height*0.4,
      r:4+Math.random()*6, c:cols[i%cols.length],
      vy:2+Math.random()*4, vx:-2.5+Math.random()*5,
      a:Math.random()*6.28, va:-0.2+Math.random()*0.4});
    var n=0;
    (function loop(){
      x.clearRect(0,0,c.width,c.height);n++;
      for(var i=0;i<P.length;i++){var q=P[i];
        q.y+=q.vy;q.x+=q.vx;q.a+=q.va;q.vy+=0.03;
        x.save();x.translate(q.x,q.y);x.rotate(q.a);x.fillStyle=q.c;
        x.fillRect(-q.r/2,-q.r/2,q.r,q.r*1.6);x.restore();}
      if(n<280)requestAnimationFrame(loop);else x.clearRect(0,0,c.width,c.height);
    })();
  }
})();
</script>
</body>
</html>)=====";

// ---- "prank off" page --------------------------------------
const char PAGE_OFF[] PROGMEM = R"=====(<!DOCTYPE html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Free Public WiFi</title>
<style>body{font-family:-apple-system,system-ui,sans-serif;min-height:100vh;margin:0;
  display:flex;align-items:center;justify-content:center;background:#0f0c29;color:#fff;
  text-align:center;padding:24px}.c{max-width:340px}.e{font-size:64px}
  h1{font-weight:800;margin:10px 0 6px;font-size:24px}p{opacity:.78;line-height:1.5}</style>
</head><body><div class="c"><div class="e">&#128246;</div>
<h1>No internet connection</h1>
<p>This network has no internet access right now. Please try another network.</p>
</div></body></html>)=====";

// ============================================================
//  DASHBOARD PAGE
// ============================================================
const char DASH_TOP[] PROGMEM = R"=====(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Victim Dashboard</title>
<style>
  *{box-sizing:border-box;margin:0;padding:0}
  body{font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",system-ui,sans-serif;
    background:#0f0c29;color:#e7e7f0;padding:20px;min-height:100vh;
    background-image:radial-gradient(50% 40% at 10% 0%,rgba(124,92,255,.25),transparent 60%),
      radial-gradient(50% 40% at 100% 0%,rgba(195,55,100,.22),transparent 60%)}
  .wrap{max-width:880px;margin:0 auto}
  h1{font-size:22px;font-weight:800;display:flex;align-items:center;gap:10px;flex-wrap:wrap}
  .live{width:9px;height:9px;border-radius:50%;background:#4ade80;
    box-shadow:0 0 0 0 rgba(74,222,128,.7);animation:pulse 1.6s infinite}
  @keyframes pulse{0%{box-shadow:0 0 0 0 rgba(74,222,128,.6)}
    70%{box-shadow:0 0 0 9px rgba(74,222,128,0)}100%{box-shadow:0 0 0 0 rgba(74,222,128,0)}}
  .badge{font-size:11px;font-weight:700;letter-spacing:.5px;padding:3px 10px;border-radius:20px;
    text-transform:uppercase;border:1px solid transparent}
  .badge.on{background:rgba(74,222,128,.18);color:#86efac;border-color:rgba(74,222,128,.4)}
  .badge.off{background:rgba(148,163,184,.18);color:#cbd5e1;border-color:rgba(148,163,184,.35)}
  .tag{font-size:12px;opacity:.6;margin:4px 0 16px}
  .bar2{display:flex;flex-wrap:wrap;align-items:center;gap:8px;margin-bottom:18px}
  .acts{margin-left:auto;display:flex;gap:8px}
  .btn2{padding:8px 14px;font-size:13px;border-radius:10px;text-decoration:none;cursor:pointer;
    border:1px solid rgba(255,255,255,.18);color:#e7e7f0;background:transparent}
  .btn2.danger{border-color:rgba(248,113,113,.5);color:#fca5a5}
  .stats{display:grid;grid-template-columns:repeat(4,1fr);gap:12px;margin-bottom:20px}
  .stat{background:rgba(255,255,255,.06);border:1px solid rgba(255,255,255,.12);
    border-radius:14px;padding:14px 16px}
  .stat .n{font-size:26px;font-weight:800;line-height:1.1}
  .stat .l{font-size:11px;opacity:.6;text-transform:uppercase;letter-spacing:.5px;margin-top:4px}
  .scroll{overflow-x:auto;border-radius:16px;border:1px solid rgba(255,255,255,.12)}
  table{width:100%;border-collapse:collapse;font-size:13px;min-width:680px}
  thead th{background:rgba(255,255,255,.07);text-align:left;padding:12px 14px;
    font-weight:600;font-size:11px;text-transform:uppercase;letter-spacing:.5px;opacity:.75;
    white-space:nowrap}
  tbody td{padding:12px 14px;border-top:1px solid rgba(255,255,255,.07);white-space:nowrap}
  .mac{font-family:ui-monospace,Menlo,Consolas,monospace;letter-spacing:.5px}
  .dot{display:inline-block;width:8px;height:8px;border-radius:50%;margin-right:7px;vertical-align:middle}
  .on{background:#4ade80;box-shadow:0 0 7px #4ade80}
  .off{background:#6b7280}
  .pill{display:inline-block;min-width:26px;text-align:center;padding:2px 9px;border-radius:20px;
    background:rgba(124,92,255,.28);border:1px solid rgba(124,92,255,.5);font-weight:700}
  .lastmsg{max-width:230px;overflow:hidden;text-overflow:ellipsis;opacity:.85}
  .empty{padding:34px;text-align:center;opacity:.6}
  .panel{margin-top:24px;background:rgba(255,255,255,.05);border:1px solid rgba(255,255,255,.12);
    border-radius:16px;padding:20px}
  .panel h2{font-size:15px;font-weight:800;margin-bottom:4px;display:flex;align-items:center;gap:8px}
  .panel .hint{font-size:12px;opacity:.55;margin-bottom:16px}
  .field{margin-bottom:16px}
  .field label{display:block;font-size:12px;opacity:.7;margin-bottom:6px;letter-spacing:.3px}
  input[type=text],textarea{width:100%;background:rgba(0,0,0,.28);color:#fff;
    border:1px solid rgba(255,255,255,.18);border-radius:10px;padding:11px 13px;font-size:14px;
    font-family:inherit}
  textarea{resize:vertical;line-height:1.5}
  input[type=text]:focus,textarea:focus{outline:none;border-color:#7c5cff}
  .toggle{display:flex;align-items:center;gap:10px;cursor:pointer;font-size:14px}
  .toggle input{width:18px;height:18px;accent-color:#7c5cff}
  .save{margin-top:6px;border:none;cursor:pointer;font-size:14px;font-weight:700;color:#fff;
    padding:12px 24px;border-radius:11px;background:linear-gradient(135deg,#7c5cff,#c33764)}
  .warn{font-size:11.5px;opacity:.6;margin-top:10px;line-height:1.5}
  .foot{margin-top:16px;font-size:11px;opacity:.45;text-align:center;line-height:1.6}
</style>
</head>
<body>
<div class="wrap">
  <h1><span class="live"></span>Victim Dashboard <span id="badge" class="badge off">&hellip;</span></h1>
  <p class="tag">Live view &middot; updates every 2s, no page reload</p>

  <div class="bar2"><div class="acts">
    <a href="/export.csv" class="btn2">&#11015; Export CSV</a>
    <a href="/clear" class="btn2 danger" onclick="return confirm('Clear all logged victims?')">Clear</a>
  </div></div>

  <div class="stats">
    <div class="stat"><div class="n" id="s_total">&hellip;</div><div class="l">Devices seen</div></div>
    <div class="stat"><div class="n" id="s_online">&hellip;</div><div class="l">Online now</div></div>
    <div class="stat"><div class="n" id="s_pranks">&hellip;</div><div class="l">Pranks served</div></div>
    <div class="stat"><div class="n" id="s_uptime" style="font-size:15px">&hellip;</div><div class="l">Uptime</div></div>
  </div>

  <div class="scroll"><table>
    <thead><tr>
      <th>Status</th><th>MAC address</th><th>IP</th>
      <th>Connected</th><th>Last seen</th><th>Pranks</th><th>Last message</th>
    </tr></thead>
    <tbody id="tb"><tr><td colspan="7" class="empty">Loading&hellip;</td></tr></tbody>
  </table></div>

  <div class="panel">
    <h2>&#9881;&#65039; Settings</h2>
    <p class="hint">Saved to flash &mdash; survives reboots. No re-flashing needed.</p>
    <form method="POST" action="/save">
)=====";

// --- settings form fields are streamed in here (prefilled) ---

const char DASH_BOTTOM[] PROGMEM = R"=====(
    </form>
  </div>
  <p class="foot">Times shown in your device's local timezone &bull; computed from chip uptime</p>
</div>
<script>
  var tb=document.getElementById('tb');
  function g(i){return document.getElementById(i);}
  function fmtUp(ms){var s=Math.floor(ms/1000),d=Math.floor(s/86400);s%=86400;
    var h=Math.floor(s/3600);s%=3600;var m=Math.floor(s/60);s%=60;
    return (d?d+'d ':'')+h+'h '+m+'m '+s+'s';}
  function fmtTime(ms,off){if(ms<0)return '\u2014';
    return new Date(ms+off).toLocaleString([],{month:'short',day:'numeric',hour:'2-digit',minute:'2-digit',second:'2-digit'});}
  function ago(last,now){var s=Math.floor((now-last)/1000);if(s<5)return 'just now';
    if(s<60)return s+'s ago';var m=Math.floor(s/60);if(m<60)return m+'m ago';
    var h=Math.floor(m/60);return h+'h '+(m%60)+'m ago';}
  async function poll(){
    try{
      var r=await fetch('/api/stats',{cache:'no-store'});
      var d=await r.json();
      var off=Date.now()-d.now;
      var b=g('badge');
      if(d.enabled){b.textContent='Prank ON';b.className='badge on';}
      else{b.textContent='Prank OFF';b.className='badge off';}
      g('s_total').textContent=d.total;
      g('s_online').textContent=d.online;
      g('s_pranks').textContent=d.pranks;
      g('s_uptime').textContent=fmtUp(d.uptime);
      if(!d.devices.length){
        tb.innerHTML='<tr><td colspan="7" class="empty">No one connected yet. Waiting for victims\u2026</td></tr>';
        return;
      }
      var h='';
      d.devices.forEach(function(v){
        h+='<tr><td><span class="dot '+(v.online?'on':'off')+'"></span>'+(v.online?'Online':'Offline')+'</td>'
         +'<td class="mac">'+v.mac+'</td>'
         +'<td>'+(v.ip||'\u2014')+'</td>'
         +'<td>'+fmtTime(v.first,off)+'</td>'
         +'<td>'+ago(v.last,d.now)+'</td>'
         +'<td><span class="pill">'+v.pranks+'</span></td>'
         +'<td class="lastmsg">'+(v.msg||'\u2014')+'</td></tr>';
      });
      tb.innerHTML=h;
    }catch(e){}
  }
  setInterval(poll,2000);poll();
</script>
</body>
</html>)=====";

// ============================================================
//  Config persistence
// ============================================================
String messagesAsText() {
  String t = "";
  for (int i = 0; i < numMsgs; i++) { t += messages[i]; if (i < numMsgs - 1) t += "\n"; }
  return t;
}

void applyMessagesFromText(const String& text) {
  numMsgs = 0;
  int start = 0;
  while (start <= text.length() && numMsgs < MAX_MSGS) {
    int nl = text.indexOf('\n', start);
    String line = (nl < 0) ? text.substring(start) : text.substring(start, nl);
    line.replace("\r", "");
    line.trim();
    if (line.length() > 0) messages[numMsgs++] = line;
    if (nl < 0) break;
    start = nl + 1;
  }
  if (numMsgs == 0)                                  // never leave it empty
    for (int i = 0; i < NUM_DEFAULT; i++) messages[numMsgs++] = String(DEFAULT_MSGS[i]);
  msgIndex = 0;
}

void loadConfig() {
  prefs.begin("prank", false);
  apSsid       = prefs.getString("ssid", "Free_Public_WiFi");
  prankEnabled = prefs.getBool("enabled", true);
  adminPin     = prefs.getString("pin", "1234");
  String defText = "";
  for (int i = 0; i < NUM_DEFAULT; i++) { defText += DEFAULT_MSGS[i]; if (i < NUM_DEFAULT - 1) defText += "\n"; }
  String msgs = prefs.getString("msgs", defText);
  prefs.end();
  applyMessagesFromText(msgs);
}

void saveConfig() {
  prefs.begin("prank", false);
  prefs.putString("ssid", apSsid);
  prefs.putBool("enabled", prankEnabled);
  prefs.putString("pin", adminPin);
  prefs.putString("msgs", messagesAsText());
  prefs.end();
}

// ============================================================
//  Tracking helpers
// ============================================================
bool macEq(const uint8_t* a, const uint8_t* b) {
  for (int i = 0; i < 6; i++) if (a[i] != b[i]) return false;
  return true;
}
String macStr(const uint8_t* m) {
  char b[18];
  snprintf(b, sizeof(b), "%02X:%02X:%02X:%02X:%02X:%02X", m[0], m[1], m[2], m[3], m[4], m[5]);
  return String(b);
}
bool macUnknown(const uint8_t* m) { return (m[0]|m[1]|m[2]|m[3]|m[4]|m[5]) == 0; }
int findByMac(const uint8_t* mac) {
  for (int i = 0; i < MAX_DEVICES; i++) if (devices[i].used && macEq(devices[i].mac, mac)) return i;
  return -1;
}
int findByIp(IPAddress ip) {
  for (int i = 0; i < MAX_DEVICES; i++) if (devices[i].used && devices[i].ip == ip) return i;
  return -1;
}
int allocSlot() {
  for (int i = 0; i < MAX_DEVICES; i++) if (!devices[i].used) return i;
  int best = 0; uint32_t oldest = 0xFFFFFFFF;
  for (int i = 0; i < MAX_DEVICES; i++)
    if (!devices[i].online && devices[i].lastSeen < oldest) { oldest = devices[i].lastSeen; best = i; }
  if (oldest == 0xFFFFFFFF) {
    oldest = 0xFFFFFFFF;
    for (int i = 0; i < MAX_DEVICES; i++)
      if (devices[i].lastSeen < oldest) { oldest = devices[i].lastSeen; best = i; }
  }
  return best;
}
int addByMac(const uint8_t* mac) {
  int i = allocSlot();
  devices[i].used = true;
  memcpy(devices[i].mac, mac, 6);
  devices[i].ip = IPAddress(0, 0, 0, 0);
  devices[i].online = true;
  devices[i].firstSeen = millis();
  devices[i].lastSeen = millis();
  devices[i].prankCount = 0;
  devices[i].lastMsg = "";
  return i;
}
int pendingIpSlot() {
  int best = -1; uint32_t newest = 0;
  for (int i = 0; i < MAX_DEVICES; i++)
    if (devices[i].used && devices[i].online &&
        devices[i].ip == IPAddress(0, 0, 0, 0) &&
        devices[i].lastSeen >= newest) { newest = devices[i].lastSeen; best = i; }
  return best;
}
void clearLog() {
  for (int i = 0; i < MAX_DEVICES; i++) { devices[i].used = false; devices[i].lastMsg = ""; }
  totalPranks = 0;
  msgIndex = 0;
}

// HTML-escape for prefilling form fields
String htmlEsc(const String& s) {
  String o; o.reserve(s.length() + 8);
  for (unsigned int i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '&') o += "&amp;";
    else if (c == '<') o += "&lt;";
    else if (c == '>') o += "&gt;";
    else if (c == '"') o += "&quot;";
    else o += c;
  }
  return o;
}
// JSON-escape for /api/stats
String jsonEsc(const String& s) {
  String o; o.reserve(s.length() + 8);
  for (unsigned int i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '"' || c == '\\') { o += '\\'; o += c; }
    else if (c == '\n') o += "\\n";
    else if (c == '\r') { }
    else if ((unsigned char)c < 0x20) { char b[8]; snprintf(b, sizeof(b), "\\u%04x", c); o += b; }
    else o += c;
  }
  return o;
}

// ============================================================
//  HTTP handlers
// ============================================================
// Gate every admin endpoint behind HTTP Basic Auth. Returns true
// when authenticated; otherwise sends a 401 and returns false.
bool requireAuth() {
  if (server.authenticate(ADMIN_USER, adminPin.c_str())) return true;
  server.requestAuthentication(BASIC_AUTH, "Prank Portal Admin",
                               "Authentication required.");
  return false;
}

void noCache() {
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "0");
}

void sendPortal() {
  noCache();

  if (!prankEnabled) {                       // prank disabled -> plain page
    server.send_P(200, "text/html", PAGE_OFF);
    return;
  }

  int n = getCount();
  int shown = (n > 0) ? (msgIndex % n) : 0;
  IPAddress ip = server.client().remoteIP();

  int i = findByIp(ip);
  if (i < 0) {
    i = allocSlot();
    devices[i].used = true;
    memset(devices[i].mac, 0, 6);
    devices[i].ip = ip;
    devices[i].online = true;
    devices[i].firstSeen = millis();
  }
  devices[i].prankCount++;
  devices[i].lastMsg = getMsg(shown);
  devices[i].lastSeen = millis();
  totalPranks++;

  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/html", "");
  server.sendContent_P(PAGE_HEAD);
  server.sendContent(getMsg(shown));
  server.sendContent("</p>");
  server.sendContent_P(PAGE_TAIL);
  server.sendContent("");

  if (n > 0) msgIndex = (msgIndex + 1) % n;
}

void sendDashboard() {
  if (!requireAuth()) return;
  noCache();
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/html", "");
  server.sendContent_P(DASH_TOP);

  // prefilled settings form fields
  String form = "<div class=\"field\"><label>Network name (SSID)</label>";
  form += "<input type=\"text\" name=\"ssid\" value=\"" + htmlEsc(apSsid) + "\" maxlength=\"32\"></div>";
  form += "<div class=\"field\"><label class=\"toggle\"><input type=\"checkbox\" name=\"enabled\"";
  form += (prankEnabled ? " checked" : "");
  form += "> Prank enabled</label></div>";
  form += "<div class=\"field\"><label>Dashboard PIN (login: user \"admin\" &mdash; leave blank to keep current)</label>";
  form += "<input type=\"text\" name=\"pin\" value=\"\" placeholder=\"&bull;&bull;&bull;&bull;\" maxlength=\"32\" autocomplete=\"off\"></div>";
  form += "<div class=\"field\"><label>Prank messages (one per line, max " + String(MAX_MSGS) + ")</label>";
  form += "<textarea name=\"msgs\" rows=\"8\">" + htmlEsc(messagesAsText()) + "</textarea></div>";
  form += "<button class=\"save\" type=\"submit\">Save settings</button>";
  form += "<p class=\"warn\">Changing the SSID restarts the network &mdash; you'll need to reconnect to the new name. Changing the PIN signs you out, so log back in with the new one. HTML entities like <code>&amp;#128521;</code> render as emoji.</p>";
  server.sendContent(form);

  server.sendContent_P(DASH_BOTTOM);
  server.sendContent("");
}

void sendStats() {
  if (!requireAuth()) return;
  noCache();
  uint32_t now = millis();
  int total = 0, online = 0;
  for (int i = 0; i < MAX_DEVICES; i++) if (devices[i].used) { total++; if (devices[i].online) online++; }

  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "application/json", "");
  String head = "{\"now\":" + String(now) + ",\"uptime\":" + String(now) +
                ",\"ssid\":\"" + jsonEsc(apSsid) + "\",\"enabled\":" + (prankEnabled ? "true" : "false") +
                ",\"total\":" + String(total) + ",\"online\":" + String(online) +
                ",\"pranks\":" + String(totalPranks) + ",\"devices\":[";
  server.sendContent(head);

  bool first = true;
  for (int i = 0; i < MAX_DEVICES; i++) {
    if (!devices[i].used) continue;
    Device& d = devices[i];
    String o = first ? "{" : ",{";
    first = false;
    o += "\"mac\":\"" + (macUnknown(d.mac) ? String("unknown") : macStr(d.mac)) + "\",";
    o += "\"ip\":\"" + (d.ip == IPAddress(0, 0, 0, 0) ? String("") : d.ip.toString()) + "\",";
    o += "\"online\":" + String(d.online ? "true" : "false") + ",";
    o += "\"first\":" + String(d.firstSeen) + ",";
    o += "\"last\":" + String(d.lastSeen) + ",";
    o += "\"pranks\":" + String(d.prankCount) + ",";
    o += "\"msg\":\"" + jsonEsc(d.lastMsg) + "\"}";
    server.sendContent(o);
  }
  server.sendContent("]}");
  server.sendContent("");
}

void sendCSV() {
  if (!requireAuth()) return;
  noCache();
  server.sendHeader("Content-Disposition", "attachment; filename=victims.csv");
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/csv", "");
  server.sendContent("mac,ip,online,first_seen_ms,last_seen_ms,pranks,last_message\n");
  for (int i = 0; i < MAX_DEVICES; i++) {
    if (!devices[i].used) continue;
    Device& d = devices[i];
    String line = (macUnknown(d.mac) ? String("unknown") : macStr(d.mac)) + ",";
    line += (d.ip == IPAddress(0, 0, 0, 0) ? String("") : d.ip.toString()) + ",";
    line += String(d.online ? "yes" : "no") + ",";
    line += String(d.firstSeen) + "," + String(d.lastSeen) + "," + String(d.prankCount) + ",";
    String m = d.lastMsg; m.replace("\"", "\"\"");
    line += "\"" + m + "\"\n";
    server.sendContent(line);
  }
  server.sendContent("");
}

String portalURL() { return String("http://") + apIP.toString() + "/"; }
void redirectToPortal() {
  noCache();
  server.sendHeader("Location", portalURL(), true);
  server.send(302, "text/plain", "");
}
void redirectToDash() {
  noCache();
  server.sendHeader("Location", ADMIN_PATH, true);
  server.send(302, "text/plain", "");
}

void startAP() {
  WiFi.softAP(apSsid.c_str(), NULL, AP_CHANNEL, 0, AP_MAXCONN);
}

void startMDNS() {
  MDNS.end();
  if (MDNS.begin(MDNS_HOST)) {
    MDNS.addService("http", "tcp", 80);
    Serial.print("mDNS       -> http://"); Serial.print(MDNS_HOST);
    Serial.print(".local"); Serial.println(ADMIN_PATH);
  } else {
    Serial.println("mDNS failed to start (IP still works).");
  }
}

void handleSave() {
  if (!requireAuth()) return;
  String newSsid = server.hasArg("ssid") ? server.arg("ssid") : apSsid;
  newSsid.trim();
  if (newSsid.length() == 0) newSsid = apSsid;     // never allow blank

  bool   newEnabled = server.hasArg("enabled");    // checkbox present => on
  String newMsgs    = server.hasArg("msgs") ? server.arg("msgs") : messagesAsText();

  // PIN: blank field = keep current
  if (server.hasArg("pin")) {
    String p = server.arg("pin"); p.trim();
    if (p.length() > 0) adminPin = p;
  }

  bool ssidChanged = (newSsid != apSsid);

  apSsid = newSsid;
  prankEnabled = newEnabled;
  applyMessagesFromText(newMsgs);
  saveConfig();

  if (ssidChanged) {
    String html = "<!DOCTYPE html><html><head><meta charset=utf-8>"
                  "<meta name=viewport content=\"width=device-width,initial-scale=1\">"
                  "<style>body{font-family:-apple-system,system-ui,sans-serif;background:#0f0c29;"
                  "color:#fff;min-height:100vh;margin:0;display:flex;align-items:center;"
                  "justify-content:center;text-align:center;padding:24px}.c{max-width:340px}"
                  "h1{font-size:22px;margin:8px 0}p{opacity:.8;line-height:1.5}"
                  "b{color:#a78bfa}</style></head><body><div class=c><div style=font-size:54px>&#128246;</div>"
                  "<h1>Network updated</h1><p>Reconnect to <b>" + htmlEsc(apSsid) +
                  "</b>, then reopen <b>http://4.3.2.1" + String(ADMIN_PATH) + "</b></p></div></body></html>";
    server.send(200, "text/html", html);
    delay(300);
    WiFi.softAPdisconnect(false);
    delay(150);
    startAP();
    startMDNS();
  } else {
    redirectToDash();
  }
}

void onWiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
  switch (event) {
    case ARDUINO_EVENT_WIFI_AP_STACONNECTED: {
      uint8_t* mac = info.wifi_ap_staconnected.mac;
      int i = findByMac(mac);
      if (i < 0) i = addByMac(mac);
      devices[i].online = true;
      devices[i].ip = IPAddress(0, 0, 0, 0);
      devices[i].lastSeen = millis();
      Serial.print("[+] connected    "); Serial.println(macStr(mac));
      break;
    }
    case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED: {
      uint8_t* mac = info.wifi_ap_stadisconnected.mac;
      int i = findByMac(mac);
      if (i >= 0) { devices[i].online = false; devices[i].lastSeen = millis(); }
      Serial.print("[-] disconnected "); Serial.println(macStr(mac));
      break;
    }
    case ARDUINO_EVENT_WIFI_AP_STAIPASSIGNED: {
      IPAddress ip(info.wifi_ap_staipassigned.ip.addr);
      int i = pendingIpSlot();
      if (i >= 0) { devices[i].ip = ip; devices[i].lastSeen = millis(); }
      break;
    }
    default: break;
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);

  for (int i = 0; i < MAX_DEVICES; i++) { devices[i].used = false; devices[i].lastMsg = ""; }
  loadConfig();

  WiFi.onEvent(onWiFiEvent);
  WiFi.mode(WIFI_AP);
  WiFi.setSleep(false);
  WiFi.softAPConfig(apIP, apIP, netMask);
  startAP();

  Serial.print("AP started -> "); Serial.println(apSsid);
  Serial.print("Portal     -> "); Serial.println(portalURL());
  Serial.print("Dashboard  -> http://"); Serial.print(apIP); Serial.println(ADMIN_PATH);

  startMDNS();

  dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
  dnsServer.start(DNS_PORT, "*", apIP);

  server.on("/", sendPortal);
  server.on(ADMIN_PATH, sendDashboard);
  server.on("/api/stats", sendStats);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/export.csv", sendCSV);
  server.on("/clear", []() { if (!requireAuth()) return; clearLog(); redirectToDash(); });
  server.on("/favicon.ico", []() { server.send(204); });

  server.on("/generate_204", redirectToPortal);       // Android
  server.on("/gen_204", redirectToPortal);            // Android
  server.on("/hotspot-detect.html", redirectToPortal);// iOS / macOS
  server.on("/ncsi.txt", redirectToPortal);           // Windows
  server.on("/connecttest.txt", redirectToPortal);    // Windows
  server.on("/fwlink", redirectToPortal);             // Windows
  server.on("/canonical.html", redirectToPortal);     // Firefox
  server.onNotFound(redirectToPortal);

  server.begin();
  Serial.println("Web server ready.");
}

void loop() {
  dnsServer.processNextRequest();
  server.handleClient();
  delay(1);
}
