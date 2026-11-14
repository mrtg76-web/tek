#pragma once
// The touch controller page served to the iPad (or any browser). Self-contained: no external assets.
// Layout is designed on a 1366 x 1024 canvas and scaled to the screen.
static const char* kTekWebPage = R"TEK(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1, maximum-scale=1, user-scalable=no, viewport-fit=cover">
<meta name="apple-mobile-web-app-capable" content="yes">
<meta name="mobile-web-app-capable" content="yes">
<meta name="apple-mobile-web-app-status-bar-style" content="black">
<meta name="apple-mobile-web-app-title" content="TEK">
<title>TEK</title>
<style>
  * { touch-action: none; -webkit-tap-highlight-color: transparent; }
  html, body { margin: 0; height: 100%; background: #121314; overflow: hidden; overscroll-behavior: none;
    -webkit-user-select: none; user-select: none; -webkit-touch-callout: none; touch-action: none;
    font-family: -apple-system, "Helvetica Neue", Arial, sans-serif; }
  #stage { position: absolute; left: 0; top: 0; width: 1366px; height: 1024px; transform-origin: 0 0; }
  .panel { position: absolute; left: 6px; top: 6px; width: 1354px; height: 1012px; background: #1b1c1e; border-radius: 18px; }
  .el { position: absolute; box-sizing: border-box; }
  .pad { border-radius: 8px; background: #26282b; }
  .btn { border-radius: 9px; background: #2b2d30; color: #fff; text-align: center; overflow: hidden; }
  .btn .l { font-weight: 700; font-size: 15px; margin-top: 14px; }
  .btn .s { font-size: 12px; color: #46d6c6; margin-top: 2px; }
  .btn .led { position: absolute; left: 10px; right: 10px; top: 5px; height: 5px; border-radius: 3px; }
  .btn.shift { background: #1f6e66; }
  .btn.latched { outline: 3px solid #30d0c0; outline-offset: -3px; }
  .seq { border-radius: 10px; background: #2b2d30; color: #fff; font-weight: 800; font-size: 26px;
    display: flex; align-items: center; justify-content: center; }
  .enc { border-radius: 50%; background: #0d0d0e; border: 3px solid #3a3c40; }
  .enc .in { position: absolute; left: 14px; top: 14px; right: 14px; bottom: 14px; border-radius: 50%; background: #2a2c2f; }
  .enc .mark { position: absolute; left: 50%; top: 6px; width: 4px; height: 18px; margin-left: -2px; background: #cfefff; border-radius: 2px; transform-origin: 2px 39px; }
  .enc .num { position: absolute; width: 100%; top: 36px; text-align: center; color: #fff; font-weight: 700; font-size: 20px; }
  #disp { background: #050708; border: 2px solid #2a3035; border-radius: 10px; color: #cfefff;
    font-family: Menlo, "SF Mono", monospace; padding: 10px 14px; }
  #disp .row { white-space: nowrap; overflow: hidden; }
  #hdr { font-size: 15px; display: flex; justify-content: space-between; }
  #hdrR { font-weight: 700; font-size: 18px; }
  #prog { display: flex; gap: 3px; margin: 6px 0 8px; }
  #prog div { flex: 1; height: 5px; background: #1a2226; }
  #l1 { font-size: 19px; font-weight: 700; height: 25px; }
  #l2 { font-size: 15px; color: #9fbfcf; height: 22px; }
  #kv { display: flex; margin-top: 10px; }
  #kv div { flex: 1; text-align: center; }
  #kv .v { font-size: 18px; font-weight: 700; }
  #kv .k { font-size: 13px; color: #6f8f9f; }
  #msg { position: absolute; left: 30px; right: 30px; top: 58px; padding: 12px; background: #cfefff; color: #000;
    font-weight: 700; font-size: 17px; text-align: center; border-radius: 6px; display: none; }
  #status { color: #8a8e94; font-size: 14px; }
  #status b { color: #30d070; }
  #status b.off { color: #ff5050; }
</style>
</head>
<body>
<div id="stage"><div class="panel"></div></div>
<script>
(function () {
  const W = 1366, H = 1024;
  const stage = document.getElementById('stage');
  const BTN = ["SHIFT","BACK","MUTE","PERF","GEN","MOD","FLOW","ARRANGER","KEYBOARD","ARP","LFO","STEP","PAGE",
    "TRACK","RANDOM","LOAD","SAVE","COPY","PASTE","INIT","END","16","32","48","64","UNDO","PLAY","STOP","REC",
    "1","2","3","4","5","6","7","8"];
  const SUB = {BACK:"config",MUTE:"nudge",PERF:"project",GEN:"cv gate",MOD:"ext mod",FLOW:"groove",ARRANGER:"on/off",
    KEYBOARD:"preview",ARP:"hold",LFO:"condense",STEP:"chord",PAGE:"expand",TRACK:"follow",RANDOM:"generate",LOAD:"reload",
    SAVE:"quick",COPY:"duplicate",PASTE:"clear",INIT:"x2 div",END:"/2 div","16":"<","32":"^","48":"v","64":">",UNDO:"redo",
    PLAY:"tap",STOP:"sync",REC:"settings"};
  // function button layout: 7 columns x 5 rows (names -> enum index)
  const LAYOUT = [
    ["SHIFT","BACK","PERF","GEN","MOD","FLOW","MUTE"],
    ["ARRANGER","KEYBOARD","ARP","LFO","RANDOM","STEP","PAGE"],
    ["TRACK","LOAD","SAVE","COPY","PASTE","INIT","END"],
    ["16","32","48","64","REC","STOP","PLAY"],
    ["UNDO"]];
  const pads = [], leds = [], btnEls = [], encMarks = [];
  const encAngle = [0, 0, 0, 0];

  function mk(cls, x, y, w, h, parent) {
    const d = document.createElement('div');
    d.className = 'el ' + cls;
    d.style.left = x + 'px'; d.style.top = y + 'px'; d.style.width = w + 'px'; d.style.height = h + 'px';
    (parent || stage).appendChild(d);
    return d;
  }

  // display
  const disp = mk('', 16, 16, 380, 210); disp.id = 'disp';
  disp.innerHTML = '<div id="hdr" class="row"><span id="hdrL"></span><span id="hdrR"></span></div><div id="prog"></div>' +
    '<div id="l1" class="row"></div><div id="l2" class="row"></div><div id="kv"></div><div id="msg"></div>';
  const prog = disp.querySelector('#prog');
  for (let i = 0; i < 16; i++) prog.appendChild(document.createElement('div'));
  const kv = disp.querySelector('#kv');
  const kvV = [], kvK = [];
  for (let i = 0; i < 4; i++) {
    const c = document.createElement('div');
    c.innerHTML = '<div class="v"></div><div class="k"></div>';
    kv.appendChild(c); kvV.push(c.firstChild); kvK.push(c.lastChild);
  }
  const status = mk('', 16, 236, 800, 24); status.id = 'status';

  // encoders
  for (let i = 0; i < 4; i++) {
    const e = mk('enc', 412 + i * 105, 46, 96, 96);
    e.innerHTML = '<div class="in"></div><div class="mark"></div><div class="num">' + (i + 1) + '</div>';
    e.dataset.kind = 'enc'; e.dataset.a = i;
    encMarks.push(e.querySelector('.mark'));
  }

  // function buttons
  LAYOUT.forEach((row, r) => row.forEach((name, c) => {
    const idx = BTN.indexOf(name);
    const b = mk('btn' + (name === 'SHIFT' ? ' shift' : ''), 842 + c * 73, 16 + r * 70, 68, 64);
    b.innerHTML = '<div class="led"></div><div class="l"' + (name.length > 6 ? ' style="font-size:12px"' : '') + '>' + name + '</div><div class="s">' + (SUB[name] || '') + '</div>';
    b.dataset.kind = 'btn'; b.dataset.a = idx;
    btnEls[idx] = b; leds[idx] = b.querySelector('.led');
  }));
  // sequencer buttons + grid
  for (let r = 0; r < 8; r++) {
    const idx = 29 + r;
    const b = mk('seq', 16, 390 + r * 78, 70, 70);
    b.textContent = String(r + 1);
    b.dataset.kind = 'btn'; b.dataset.a = idx;
    btnEls[idx] = b;
  }
  for (let y = 0; y < 8; y++) for (let x = 0; x < 16; x++) {
    const p = mk('pad', 100 + x * 78, 390 + y * 78, 70, 70);
    p.dataset.kind = 'pad'; p.dataset.a = x; p.dataset.b = y;
    pads.push(p);
  }

  function fit() {
    const s = Math.min(window.innerWidth / W, window.innerHeight / H);
    stage.style.transform = 'scale(' + s + ')';
    stage.style.left = ((window.innerWidth - W * s) / 2) + 'px';
    stage.style.top = ((window.innerHeight - H * s) / 2) + 'px';
  }
  window.addEventListener('resize', fit); fit();

)TEK" R"TEK(  // ---------------------------------------------------------------- connection
  let ws = null, connected = false;
  const url = location.host;
  let rtt = -1, worst = 0, worstAt = 0;
  function setStatus() {
    const net = rtt < 0 ? '' : ' &nbsp; ping ' + Math.round(rtt) + ' ms (max ' + Math.round(worst) + ')';
    status.innerHTML = 'TEK touch &nbsp; ' + (connected ? '<b>connected</b>' : '<b class="off">connecting…</b>') +
      ' &nbsp; ' + url + net + ' &nbsp;·&nbsp; hold a pad + turn a knob, SHIFT + tap';
  }
  // keep-alive: small upstream packets keep the Wi-Fi radios awake and measure the round trip
  setInterval(() => send('k ' + Math.round(performance.now())), 100);
  setInterval(() => { if (performance.now() - worstAt > 10000) worst = rtt; setStatus(); }, 1000);
  // clock sync: offset = server clock - local clock, taken from the fastest recent round trip
  let offset = null; const syncs = [];
  function pong(t) {
    const now = performance.now(), a = t.split(' '), sent = parseFloat(a[0]);
    rtt = now - sent;
    if (rtt >= worst) { worst = rtt; worstAt = now; }
    if (a.length > 1) {
      syncs.push([rtt, parseFloat(a[1]) - (sent + rtt / 2)]);
      if (syncs.length > 50) syncs.shift();
      let b = syncs[0]; for (const x of syncs) if (x[0] < b[0]) b = x;
      offset = b[1];
    }
  }
  function send(m) { if (ws && ws.readyState === 1) ws.send(m); }
  function connect() {
    ws = new WebSocket('ws://' + location.host + '/ws');
    ws.onopen = () => { connected = true; setStatus(); };
    ws.onclose = () => { connected = false; setStatus(); releaseAll(); setTimeout(connect, 1000); };
    ws.onerror = () => { try { ws.close(); } catch (e) {} };
    ws.onmessage = (ev) => { if (ev.data.charCodeAt(0) === 75) { pong(ev.data.substr(2)); return; } pending = ev.data; if (!raf) raf = requestAnimationFrame(flush); };
  }
  setStatus(); connect();

  const hex = (s) => '#' + s;
  let pending = null, raf = 0;
  function flush() { raf = 0; const t = pending; pending = null; if (t) draw(t); } // newest frame only, once per screen refresh
  const prev = {};
  const txt = (id, v) => { if (prev[id] !== v) { prev[id] = v; document.getElementById(id).textContent = v; } };
  const padC = new Array(128).fill(''), ledC = new Array(37).fill(''), progC = new Array(16).fill('');
  let base = '', baseProg = 0, pred = null, animating = false;
  const lit = (c) => {
    if (c === '000000' || c === '3b3e43' || c === '34373b') return '5a5d63';
    let o = ''; for (let k = 0; k < 3; k++) { const v = parseInt(c.substr(k * 2, 2), 16); o += Math.round(v + (255 - v) * 0.45).toString(16).padStart(2, '0'); }
    return o;
  };
  function paint() { // base pads + predicted playheads, progress bar from the synced clock
    const now = performance.now();
    const cols = new Array(128);
    for (let i = 0; i < 128; i++) cols[i] = base.substr(i * 6, 6);
    let pr = baseProg;
    if (pred && offset !== null) {
      const mpb = 60000 / pred.bpm, ref = pred.ref - offset;
      pr = Math.floor((((now - ref) / mpb) % 4 + 4) % 4 * 4);
      for (const r of pred.rows) {
        const lenMs = r.len * mpb;
        const n = Math.max(0, Math.min(32, Math.floor((now - (ref + r.ppq * mpb)) / lenMs)));
        const span = r.end - r.start + 1;
        const idx = r.start + (r.idx - r.start + n) % span;
        if (Math.floor(idx / 16) !== r.page) continue;
        const x = idx % 16;
        for (let y = (r.row < 0 ? 0 : r.row); y <= (r.row < 0 ? 7 : r.row); y++) cols[y * 16 + x] = lit(cols[y * 16 + x]);
      }
    }
    for (let i = 0; i < 16; i++) {
      const c = i <= pr ? (i % 4 === 0 ? '#cfefff' : '#86a0ad') : '#1a2226';
      if (progC[i] !== c) { progC[i] = c; prog.children[i].style.background = c; }
    }
    for (let i = 0; i < 128; i++) {
      const c = cols[i];
      if (padC[i] === c) continue; // touch only pads that changed
      padC[i] = c;
      pads[i].style.background = c === '000000' ? '#26282b' : hex(c);
      pads[i].style.boxShadow = c === '000000' ? 'none' : '0 0 10px ' + hex(c) + '55';
    }
  }
  function tick() { if (!pred) { animating = false; return; } paint(); requestAnimationFrame(tick); }
  function draw(t) {
    const f = t.split('\n');
    if (f[0] !== 'F') return;
    txt('hdrL', f[1]); txt('hdrR', f[2]); txt('l1', f[3]); txt('l2', f[4]);
    for (let i = 0; i < 4; i++) {
      if (prev['k' + i] !== f[5 + i]) { prev['k' + i] = f[5 + i]; kvK[i].textContent = f[5 + i]; }
      if (prev['v' + i] !== f[9 + i]) { prev['v' + i] = f[9 + i]; kvV[i].textContent = f[9 + i]; }
    }
    if (prev.msg !== f[13]) {
      prev.msg = f[13];
      const m = document.getElementById('msg');
      if (f[13]) { m.textContent = f[13]; m.style.display = 'block'; } else m.style.display = 'none';
    }
    baseProg = parseInt(f[14] || '0', 10);
    base = f[15] || '';
    pred = null;
    if (f[17] && f[17].charAt(0) === 'P') {
      const p = f[17].split(' ');
      pred = { ref: parseFloat(p[1]), bpm: parseFloat(p[2]), rows: [] };
      if (f[18]) for (const e of f[18].split(';')) {
        const v = e.split(',').map(Number);
        if (v.length === 7 && v[6] > 0) pred.rows.push({ row: v[0], idx: v[1], start: v[2], end: v[3], page: v[4], ppq: v[5], len: v[6] });
      }
    }
    paint();
    if (pred && !animating) { animating = true; requestAnimationFrame(tick); }
    const ls = f[16] || '';
    for (let i = 0; i < 37; i++) {
      const c = ls.substr(i * 6, 6);
      if (ledC[i] === c) continue;
      ledC[i] = c;
      if (i >= 29) {
        const b = btnEls[i];
        b.style.background = c === '000000' ? '#2b2d30' : hex(c);
        const v = parseInt(c, 16), lum = ((v >> 16) * 0.3 + ((v >> 8) & 255) * 0.59 + (v & 255) * 0.11);
        b.style.color = lum > 150 ? '#000' : '#fff';
      } else if (leds[i]) leds[i].style.background = c === '000000' ? 'transparent' : hex(c);
    }
  }

  // ---------------------------------------------------------------- multi-touch input
  const touches = new Map(); // pointerId -> state
  function target(x, y) {
    let el = document.elementFromPoint(x, y);
    while (el && el !== document.body && !(el.dataset && el.dataset.kind)) el = el.parentElement;
    return el && el.dataset && el.dataset.kind ? el : null;
  }
  function releaseAll() {
    touches.forEach((st) => end(st));
    touches.clear();
  }
  function end(st) {
    if (st.kind === 'pad') st.pads.slice().reverse().forEach(([x, y]) => send('pu ' + x + ' ' + y));
    else if (st.kind === 'btn') send('bu ' + st.a);
    else if (st.kind === 'enc') send('eu ' + st.a);
  }
  stage.addEventListener('pointerdown', (e) => {
    e.preventDefault();
    const el = target(e.clientX, e.clientY);
    if (!el) return;
    const kind = el.dataset.kind, a = parseInt(el.dataset.a, 10), b = parseInt(el.dataset.b || '0', 10);
    const st = { kind, a, b, pads: [], lastY: e.clientY, acc: 0 };
    if (kind === 'pad') { st.pads.push([a, b]); send('pd ' + a + ' ' + b); }
    else if (kind === 'btn') send('bd ' + a);
    else if (kind === 'enc') send('ed ' + a);
    touches.set(e.pointerId, st);
  }, { passive: false });
  stage.addEventListener('pointermove', (e) => {
    const st = touches.get(e.pointerId);
    if (!st) return;
    e.preventDefault();
    if (st.kind === 'enc') {
      const scale = window.innerHeight / H;
      st.acc += (st.lastY - e.clientY) / Math.max(0.3, scale);
      st.lastY = e.clientY;
      const step = 14;
      while (Math.abs(st.acc) >= step) {
        const d = st.acc > 0 ? 1 : -1;
        st.acc -= d * step;
        send('et ' + st.a + ' ' + d);
        encAngle[st.a] += d * 12;
        encMarks[st.a].style.transform = 'rotate(' + encAngle[st.a] + 'deg)';
      }
    } else if (st.kind === 'pad') { // slide to another pad of the same row = tie (first + last)
      const el = target(e.clientX, e.clientY);
      if (el && el.dataset.kind === 'pad') {
        const x = parseInt(el.dataset.a, 10), y = parseInt(el.dataset.b, 10);
        if (y === st.pads[0][1] && !st.pads.some(([px, py]) => px === x && py === y)) { st.pads.push([x, y]); send('pd ' + x + ' ' + y); }
      }
    }
  }, { passive: false });
  function up(e) {
    const st = touches.get(e.pointerId);
    if (!st) return;
    e.preventDefault();
    end(st);
    touches.delete(e.pointerId);
  }
  stage.addEventListener('pointerup', up, { passive: false });
  stage.addEventListener('pointercancel', up, { passive: false });
  document.addEventListener('gesturestart', (e) => e.preventDefault());
  document.addEventListener('visibilitychange', () => { if (document.hidden) releaseAll(); });
})();
</script>
</body>
</html>
)TEK";
