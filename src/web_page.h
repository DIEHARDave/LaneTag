// The device's single web page: scan status, printers and settings.
#pragma once

#include <pgmspace.h>

const char INDEX_HTML[] PROGMEM = R"HTML(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>LaneTag</title>
<style>
:root{--bg:#f4f5f7;--card:#fff;--text:#1d2129;--muted:#5f6673;--line:#dde0e5;--accent:#2563eb;--ok:#15803d;--bad:#b91c1c;--warn:#a16207;--chip:#eef0f3}
@media (prefers-color-scheme:dark){:root{--bg:#14161a;--card:#1d2026;--text:#e8eaed;--muted:#9aa1ad;--line:#2f333b;--accent:#5b8def;--ok:#4ade80;--bad:#f87171;--warn:#facc15;--chip:#2a2e36}}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--text);font:15px/1.45 system-ui,-apple-system,"Segoe UI",Roboto,sans-serif}
header{display:flex;flex-wrap:wrap;gap:8px 16px;align-items:center;padding:12px 16px;background:var(--card);border-bottom:1px solid var(--line)}
header h1{font-size:17px;margin:0;flex:1 1 auto}
.pill{font-size:12px;padding:2px 8px;border-radius:99px;background:var(--chip);color:var(--muted);white-space:nowrap}
.pill.ok{color:var(--ok)}.pill.bad{color:var(--bad)}
nav{display:flex;gap:4px;padding:8px 16px 0;max-width:760px;margin:0 auto}
nav button{border:0;background:none;color:var(--muted);padding:8px 12px;border-radius:8px 8px 0 0;font:inherit;cursor:pointer}
nav button.on{background:var(--card);color:var(--text);font-weight:600}
main{max-width:760px;margin:0 auto;padding:0 16px 32px}
section{display:none}section.on{display:block}
.card{background:var(--card);border:1px solid var(--line);border-radius:10px;padding:16px;margin-top:12px}
.card h2{font-size:15px;margin:0 0 10px}
.banner{background:var(--chip);border-left:4px solid var(--warn);padding:10px 12px;border-radius:6px;margin-top:12px}
.row{display:flex;gap:12px;align-items:center;flex-wrap:wrap}
.swatch{width:56px;height:56px;border-radius:10px;border:1px solid var(--line);flex:none;background:repeating-linear-gradient(45deg,var(--chip) 0 6px,transparent 6px 12px)}
.title{font-size:18px;font-weight:600}
.muted{color:var(--muted);font-size:13px}
.chips{display:flex;flex-wrap:wrap;gap:6px;margin-top:8px}
.chip{background:var(--chip);border-radius:6px;padding:2px 8px;font-size:13px}
.status{margin-top:12px;padding:8px 10px;border-radius:6px;background:var(--chip)}
.status.ok{color:var(--ok)}.status.bad{color:var(--bad)}
.slots{display:grid;grid-template-columns:repeat(auto-fill,minmax(130px,1fr));gap:8px;margin-top:12px}
.slot{display:flex;gap:8px;align-items:center;text-align:left;border:1px solid var(--line);background:var(--bg);color:var(--text);border-radius:8px;padding:8px;font:inherit;cursor:pointer}
.slot:disabled{opacity:.55;cursor:default}
.slot.next{border-color:var(--accent);box-shadow:0 0 0 2px var(--accent);opacity:1}
.slot.next span em{color:var(--accent);font-style:normal;font-weight:600}
.slot i.q{display:flex;align-items:center;justify-content:center;font-style:normal;font-weight:600;font-size:13px;color:var(--muted);background:var(--chip)}
.slot i{width:22px;height:22px;border-radius:50%;border:1px solid var(--line);flex:none}
.slot b{display:block;font-size:14px}.slot span{font-size:12px;color:var(--muted)}
button.btn{border:1px solid var(--line);background:var(--bg);color:var(--text);border-radius:8px;padding:7px 14px;font:inherit;cursor:pointer}
button.primary{background:var(--accent);border-color:var(--accent);color:#fff}
button.danger{color:var(--bad)}
label{display:block;font-size:13px;color:var(--muted);margin:10px 0 4px}
input,select{width:100%;padding:8px 10px;border:1px solid var(--line);border-radius:8px;background:var(--bg);color:var(--text);font:inherit}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(160px,1fr));gap:0 12px}
.printer{border:1px solid var(--line);border-radius:8px;padding:12px;margin-top:10px}
.actions{display:flex;gap:8px;flex-wrap:wrap;margin-top:14px}
.hint{font-size:13px;color:var(--muted);margin-top:6px}
.msg{margin-top:10px;font-size:14px}
.hidden{display:none}
</style>
</head>
<body>
<header>
  <h1 id="device">LaneTag</h1>
  <span class="pill" id="wifiPill">Wi-Fi</span>
  <span class="pill" id="readerPill">Reader</span>
</header>
<nav>
  <button data-tab="scan" class="on">Scan</button>
  <button data-tab="printers">Printers</button>
  <button data-tab="settings">Settings</button>
</nav>
<main>
<section id="scan" class="on">
  <div id="apBanner" class="banner hidden"></div>
  <div class="card">
    <label for="target" style="margin-top:0">Send scans to</label>
    <select id="target"></select>
    <div class="hint" id="targetHint"></div>
  </div>
  <div class="card">
    <div class="row">
      <div class="swatch" id="swatch"></div>
      <div style="flex:1;min-width:0">
        <div class="title" id="scanTitle">Waiting for a tag</div>
        <div class="muted" id="scanSub">Hold a spool's tag on the reader.</div>
      </div>
    </div>
    <div class="chips" id="chips"></div>
    <div class="status hidden" id="scanStatus"></div>
    <div class="slots" id="slots"></div>
    <div class="hint" id="slotsHint"></div>
    <div class="actions">
      <button class="btn hidden" id="resend">Send to printer again</button>
      <button class="btn hidden" id="cancel">Cancel</button>
      <button class="btn" id="rescan">Read the tag again</button>
    </div>
  </div>
</section>

<section id="printers">
  <div class="card">
    <h2>Printers</h2>
    <div class="hint">Pick a method per printer. <b>zmod_nfc</b> sends the whole tag to the zmod_nfc plugin, which shows its slot popup, matches Spoolman and fills lane data. <b>Zmod basic</b> sets slot color and material through Moonraker (no plugin needed). <b>Stock</b> uses Flashforge's local API: slot color and material only; it needs the printer's serial number and check code. A <b>Snapmaker U1</b> with paxx12's Extended firmware gets each scan as its head's own RFID read, and its SpoolLink matches the tag in Spoolman; each U1 reader is fixed to one head.</div>
    <div id="printerList"></div>
    <div class="actions">
      <button class="btn" id="addPrinter">Add printer</button>
      <button class="btn primary" id="savePrinters">Save</button>
    </div>
    <div class="msg" id="printersMsg"></div>
  </div>
</section>

<section id="settings">
  <div class="card">
    <h2>Wi-Fi and device</h2>
    <div class="grid">
      <div><label for="ssid">Wi-Fi network</label><input id="ssid" list="ssids" autocomplete="off"><datalist id="ssids"></datalist></div>
      <div><label for="wifiPass">Wi-Fi password</label><input id="wifiPass" type="password" autocomplete="new-password"></div>
      <div><label for="hostname">Device name (name.local)</label><input id="hostname"></div>
      <div><label for="uiPassword">Page password (user admin)</label><input id="uiPassword" type="password" autocomplete="new-password"></div>
      <div><label for="wifiPower">Wi-Fi transmit power</label><select id="wifiPower"><option value="0">Default</option><option value="19.5">19.5 dBm</option><option value="17">17 dBm</option><option value="15">15 dBm</option><option value="13">13 dBm</option><option value="11">11 dBm</option><option value="8.5">8.5 dBm</option><option value="7">7 dBm</option><option value="5">5 dBm</option></select></div>
    </div>
    <div class="hint">If the reader drops off the network (common on ESP32-C3 SuperMini boards), try other transmit powers; the change applies at once.</div>
    <div class="actions"><button class="btn" id="scanWifi">Find networks</button><button class="btn primary" id="joinWifi">Save and join</button></div>
    <div class="msg" id="wifiMsg"></div>
  </div>
  <div class="card">
    <h2>NFC reader</h2>
    <div class="grid">
      <div><label for="chip">Reader</label><select id="chip"><option value="pn532">PN532</option><option value="rc522">RC522 (SPI)</option><option value="pn5180">PN5180 (SPI)</option></select></div>
      <div class="pn532"><label for="bus">Connection</label><select id="bus"><option value="i2c">I2C</option><option value="uart">HSU (UART)</option></select></div>
      <div class="pn532 i2c"><label for="sda">SDA pin</label><input id="sda" type="number"></div>
      <div class="pn532 i2c"><label for="scl">SCL pin</label><input id="scl" type="number"></div>
      <div class="pn532 uart"><label for="rx">RX pin (to PN532 TX)</label><input id="rx" type="number"></div>
      <div class="pn532 uart"><label for="tx">TX pin (to PN532 RX)</label><input id="tx" type="number"></div>
      <div class="spi"><label for="sck">SCK pin</label><input id="sck" type="number"></div>
      <div class="spi"><label for="miso">MISO pin</label><input id="miso" type="number"></div>
      <div class="spi"><label for="mosi">MOSI pin</label><input id="mosi" type="number"></div>
      <div class="spi"><label for="ss" id="ssLabel">SDA (SS) pin</label><input id="ss" type="number"></div>
      <div class="pn5180"><label for="busy">BUSY pin</label><input id="busy" type="number"></div>
      <div class="spi"><label for="rst">RST pin (-1: not wired)</label><input id="rst" type="number"></div>
      <div><label for="ledPin">LED pin (-1: none)</label><input id="ledPin" type="number"></div>
      <div><label for="ledLow">LED lights when</label><select id="ledLow"><option value="0">high</option><option value="1">low</option></select></div>
      <div><label for="buzzerPin">Buzzer pin (-1: none)</label><input id="buzzerPin" type="number"></div>
      <div><label for="buttonPin">Slot button pin (-1: none)</label><input id="buttonPin" type="number"></div>
    </div>
    <div class="actions"><button class="btn primary" id="saveSettings">Save</button></div>
    <div class="msg" id="settingsMsg"></div>
  </div>
  <div class="card">
    <h2>Display</h2>
    <div class="grid">
      <div><label for="dispType">Screen</label><select id="dispType"><option value="none">None</option><option value="ssd1306">OLED SSD1306 (0.96", I2C)</option><option value="sh1106">OLED SH1106 (1.3", I2C)</option><option value="cyd">CYD 2.8" touch (ESP32-2432S028)</option><option value="cyd-st7789">CYD 2.8" touch, ST7789 (two USB ports)</option></select></div>
      <div class="disp oled"><label for="dispSda">SDA pin</label><input id="dispSda" type="number"></div>
      <div class="disp oled"><label for="dispScl">SCL pin</label><input id="dispScl" type="number"></div>
      <div class="disp oled"><label for="dispAddr">Address</label><select id="dispAddr"><option value="60">0x3C</option><option value="61">0x3D</option></select></div>
      <div class="disp"><label for="dispFlip">Orientation</label><select id="dispFlip"><option value="0">Normal</option><option value="1">Upside down</option></select></div>
      <div class="disp"><label for="dispSleep">Screen off after (minutes, 0: never)</label><input id="dispSleep" type="number" min="0"></div>
    </div>
    <div class="hint">Shows the active printer and slot, and the filament of the last scan. It can share the PN532's I2C pins. Boards with a built-in 0.96" OLED (such as the Ideaspark ESP32) usually have it on SDA 21 / SCL 22. The CYD touch screen (classic ESP32 builds only) also has buttons: tap the printer name for the next printer, the bottom row for the fixed slot, and a scan waiting for a slot can be put into one there.</div>
    <div class="msg" id="dispMsg"></div>
    <div class="actions"><button class="btn primary" id="saveDisplay">Save</button></div>
  </div>
  <div class="card">
    <h2>Firmware</h2>
    <div class="muted" id="version"></div>
    <label for="fw">Firmware file (.pio/build/&lt;board&gt;/firmware.bin)</label>
    <input id="fw" type="file" accept=".bin">
    <div class="actions"><button class="btn" id="upload">Upload</button><button class="btn danger" id="restart">Restart</button></div>
    <div class="msg" id="fwMsg"></div>
  </div>
</section>
</main>
<script>
const $ = id => document.getElementById(id);
const esc = s => String(s ?? "").replace(/[&<>"']/g, c => ({"&":"&amp;","<":"&lt;",">":"&gt;",'"':"&quot;","'":"&#39;"}[c]));
const METHODS = {zmod_nfc: "Zmod + zmod_nfc", zmod_basic: "Zmod basic", stock: "Stock firmware", snapmaker: "Extended firmware"};
const MODELS = {ad5x: "AD5X", creator5: "Creator 5", snapmaker_u1: "Snapmaker U1"};
// The U1 runs only the Snapmaker method, which is only for the U1; it cannot
// ask for the slot, so its readers are always fixed to a head.
const U1 = "snapmaker_u1";
let status = null, config = null, slotsFor = "", slots = null, busy = false;

async function api(path, body) {
  const opt = body === undefined ? {} : {method: "POST", headers: {"Content-Type": "application/json"}, body: JSON.stringify(body)};
  const r = await fetch(path, opt);
  let data = {};
  try { data = await r.json(); } catch (e) {}
  if (!r.ok && data.ok === undefined) data.ok = false;
  return data;
}

document.querySelectorAll("nav button").forEach(b => b.onclick = () => {
  document.querySelectorAll("nav button").forEach(x => x.classList.toggle("on", x === b));
  document.querySelectorAll("section").forEach(s => s.classList.toggle("on", s.id === b.dataset.tab));
});

function setStatus(el, text, ok) {
  el.textContent = text || "";
  el.className = (el.id === "scanStatus" ? "status " : "msg ") + (ok === true ? "ok" : ok === false ? "bad" : "");
  el.classList.toggle("hidden", !text);
}

function renderHeader() {
  $("device").textContent = status.device;
  document.title = status.device;
  const w = $("wifiPill");
  w.textContent = status.wifi ? `Wi-Fi ${status.ssid} · ${status.ip} · ${status.rssi} dBm` : status.ap ? "Setup access point" : "No Wi-Fi";
  w.title = status.wifi ? `transmit power ${status.tx_power} dBm` : "";
  w.className = "pill " + (status.wifi ? "ok" : "bad");
  const r = $("readerPill");
  r.textContent = status.reader.name;
  r.title = status.reader.error || "";
  r.className = "pill " + (status.reader.ok ? "ok" : "bad");
  const ap = $("apBanner");
  ap.classList.toggle("hidden", !status.ap);
  if (status.ap) ap.textContent = `Setup mode: this device is the "${status.ap_name}" Wi-Fi network (password lanetag1). Enter your Wi-Fi network under Settings.` +
    (status.wifi_error ? ` Could not join ${status.ssid}: ${status.wifi_error}.` : "");
  $("version").textContent = "Version " + status.version;
}

function renderTarget() {
  const sel = $("target");
  const opts = status.printers.map((p, i) => `<option value="${i}">${esc(p.name)} — ${esc(MODELS[p.model])}, ${esc(METHODS[p.method])}</option>`).join("");
  if (sel.dataset.opts !== opts) { sel.innerHTML = opts || "<option>No printers yet</option>"; sel.dataset.opts = opts; }
  sel.disabled = !status.printers.length;
  if (document.activeElement !== sel) sel.value = status.active;
  const p = status.printers[status.active];
  $("targetHint").textContent = !p ? "Add a printer on the Printers tab." :
    p.slot ? `This reader is fixed to ${slotName(p, p.slot)}: scans go straight into it.` :
    p.method === "zmod_nfc" ? "Scans go straight to the printer, which asks for the slot on its screen." :
    "Keep this page open: after each scan, pick the slot below. This printer's screen cannot ask, and nothing is set until you do. To skip this, click a slot below to send every scan there.";
}

function renderScan() {
  const s = status.scan, f = (s && s.filament) || {};
  const color = f.color ? "#" + f.color : "";
  $("swatch").style.background = color || "";
  if (!s) {
    $("scanTitle").textContent = "Waiting for a tag";
    $("scanSub").textContent = "Hold a spool's tag on the reader.";
    $("chips").innerHTML = "";
    setStatus($("scanStatus"), "");
  } else {
    $("scanTitle").textContent = s.printer_title || f.title || "Tag " + s.uid;
    $("scanSub").textContent = `${s.describe} · ${s.age < 60 ? s.age + " s" : Math.round(s.age / 60) + " min"} ago`;
    const chips = [];
    if (f.format) chips.push(f.format + " tag");
    else chips.push("Format decoded on the printer only");
    if (f.material) chips.push(f.material + (f.modifiers ? " " + f.modifiers : ""));
    if (color) chips.push(color.toUpperCase());
    if (f.nozzle_min || f.nozzle_max) chips.push("Nozzle " + [f.nozzle_min, f.nozzle_max].filter(Boolean).join("–") + " °C");
    if (f.bed_temp) chips.push("Bed " + f.bed_temp + " °C");
    if (f.weight) chips.push(f.weight + " g");
    if (s.printer_spool || f.spool_id) chips.push("Spoolman #" + (s.printer_spool || f.spool_id));
    $("chips").innerHTML = chips.map(c => `<span class="chip">${esc(c)}</span>`).join("");
    setStatus($("scanStatus"), s.status, s.status ? s.ok : undefined);
  }
  const p = status.printers[status.active];
  $("resend").classList.toggle("hidden", !(s && p && p.method === "zmod_nfc"));
  $("cancel").classList.toggle("hidden", !(s && s.waiting));
}

function renderSlots() {
  const el = $("slots"), hint = $("slotsHint");
  const s = status.scan;
  if (!slots) { el.innerHTML = ""; hint.textContent = status.printers.length ? "Loading slots…" : ""; return; }
  if (!slots.ok) { el.innerHTML = ""; hint.textContent = "Printer: " + (slots.error || "not reachable"); return; }
  // A scan waiting for its slot: a click puts it there. Otherwise a click
  // picks the slot the next scan goes into (the fixed slot, also set on the
  // Printers tab or with the board's button), or Ask.
  const waiting = s && s.waiting;
  const p = status.printers[status.active], next = p ? p.slot : 0;
  const nextName = next && slots.slots.find(x => x.slot === next);
  hint.textContent = waiting ? "Click a slot to put this spool in it." :
    nextName ? `The next scan goes into ${nextName.label}. Click another ${p.model === U1 ? "head" : "slot, or Ask,"} to change that.` :
    "The printer asks where each scan goes. Click a slot to send every scan there instead.";
  const mark = on => on && !waiting ? "<br><em>next scan</em>" : "";
  const ask = p && p.method === "zmod_nfc" ? "on the printer's screen" : "on this page";
  const tiles = waiting || (p && p.model === U1) ? [] : [`<button class="slot${next ? "" : " next"}" data-slot="0" ${busy ? "disabled" : ""}><i class="q">?</i><div><b>Ask</b><span>${ask}${mark(!next)}</span></div></button>`];
  for (const x of slots.slots) {
    const c = x.color ? `background:#${esc(x.color)}` : "";
    const what = [x.material && x.material !== "?" ? x.material : "", x.loaded ? "loaded" : "empty"].filter(Boolean).join(" · ");
    const on = x.slot === next;
    tiles.push(`<button class="slot${on && !waiting ? " next" : ""}" data-slot="${x.slot}" ${busy ? "disabled" : ""}><i style="${c}"></i><div><b>${esc(x.label)}</b><span>${esc(what)}${mark(on)}</span></div></button>`);
  }
  el.innerHTML = tiles.join("");
  el.querySelectorAll("button").forEach(b => b.onclick = () => waiting ? assign(+b.dataset.slot) : setFixed(+b.dataset.slot));
}

async function setFixed(slot) {
  busy = true; renderSlots();
  const r = await api("/api/fixed_slot", {printer: status.active, slot});
  if (!r.ok) setStatus($("scanStatus"), r.error, false);
  busy = false;
  await refresh();
}

async function loadSlots(force) {
  // Reload when the target, the scan or its outcome changes (slots change on assignment).
  const key = status.active + ":" + (status.scan ? status.scan.id + ":" + status.scan.status : 0);
  if (!force && key === slotsFor) return;
  slotsFor = key;
  if (!status.printers.length) { slots = null; renderSlots(); return; }
  slots = null; renderSlots();
  slots = await api("/api/slots?printer=" + status.active);
  renderSlots();
}

async function assign(slot) {
  busy = true; renderSlots();
  setStatus($("scanStatus"), "Sending…");
  const r = await api("/api/assign", {printer: status.active, slot});
  setStatus($("scanStatus"), r.message || r.error, r.ok);
  busy = false;
  await refresh();
  loadSlots(true);
}

async function refresh() {
  try {
    status = await api("/api/status");
    renderHeader(); renderTarget(); renderScan(); renderSlots();
    followSlots();
    loadSlots(false);
  } catch (e) {
    $("wifiPill").textContent = "Device not reachable"; $("wifiPill").className = "pill bad";
  }
}

// The board's button changes the active printer's slot: show it on the
// Printers tab too, so saving that tab does not undo it.
function followSlots() {
  if (!config) return;
  config.printers.forEach((c, i) => {
    const p = status.printers[c.keep];
    if (!p || c.savedSlot === p.slot) return;
    c.savedSlot = c.slot = p.slot;
    const card = document.querySelector(`.printer[data-i="${i}"]`);
    if (!card) return;
    card.querySelector('[data-k="slot"]').value = p.slot;
    showMethodFields(card);
  });
}

$("target").onchange = async e => {
  await api("/api/active", {printer: +e.target.value});
  await refresh();
};
$("resend").onclick = async () => {
  setStatus($("scanStatus"), "Sending…");
  const r = await api("/api/resend", {printer: status.active});
  setStatus($("scanStatus"), r.message || r.error, r.ok);
  refresh();
};
$("cancel").onclick = async () => { await api("/api/cancel", {printer: status.active}); refresh(); };
$("rescan").onclick = () => api("/api/rescan", {});

// --- Printers ---------------------------------------------------------------
function slotName(p, n) {
  return p.model === "creator5" ? `toolhead T${n - 1}` : p.model === U1 ? `head ${n}` : `slot ${n}`;
}

// Only zmod_nfc can ask on the printer's screen. With the other methods
// "ask" means picking the slot on this page after every scan.
function slotOptions(p) {
  const ask = p.method === "zmod_nfc" ? "Ask on the printer's screen" : "Ask on this web page";
  let html = p.model === U1 ? "" : `<option value="0" ${p.slot ? "" : "selected"}>${ask}</option>`;
  for (let n = 1; n <= 4; n++) html += `<option value="${n}" ${p.slot === n ? "selected" : ""}>Always ${slotName(p, n)}</option>`;
  return html;
}

function methodOptions(p) {
  return Object.entries(METHODS).filter(([k]) => (k === "snapmaker") === (p.model === U1))
    .map(([k, v]) => `<option value="${k}" ${p.method === k ? "selected" : ""}>${v}</option>`).join("");
}

function printerCard(p, i) {
  const ph = p.method === "stock" ? 8898 : 7125;
  return `<div class="printer" data-i="${i}">
  <div class="grid">
    <div><label>Name</label><input data-k="name" value="${esc(p.name)}" placeholder="AD5X"></div>
    <div><label>IP address</label><input data-k="host" value="${esc(p.host)}" placeholder="192.168.1.50"></div>
    <div><label>Printer</label><select data-k="model">${Object.entries(MODELS).map(([k, v]) => `<option value="${k}" ${p.model === k ? "selected" : ""}>${v}</option>`).join("")}</select></div>
    <div><label>Method</label><select data-k="method">${methodOptions(p)}</select></div>
    <div><label>Port</label><input data-k="port" type="number" value="${p.port || ""}" placeholder="${ph}"></div>
    <div><label>Slot for this reader</label><select data-k="slot">${slotOptions(p)}</select></div>
    <div class="m-stock"><label>Serial number</label><input data-k="serial" value="${esc(p.serial)}"></div>
    <div class="m-stock"><label>Check code</label><input data-k="check_code" type="password" autocomplete="new-password" placeholder="${p.check_code_set ? "unchanged" : ""}"></div>
    <div class="m-zmod"><label>Moonraker API key (if needed)</label><input data-k="api_key" type="password" autocomplete="new-password" placeholder="${p.api_key_set ? "unchanged" : "none"}"></div>
  </div>
  <div class="banner slot-warn hidden">The printer's screen cannot ask where this spool goes with this method. With "Ask on this web page", nothing is sent to the printer until you pick the slot on this reader's web page to send to. So the esp32 web page has to be open in a browser when you scan. Pick a fixed slot to have every scan fill that slot by itself.</div>
  <div class="actions"><button class="btn test">Test</button><button class="btn danger remove">Remove</button><span class="msg"></span></div>
</div>`;
}

function showMethodFields(card) {
  const model = card.querySelector('[data-k="model"]').value;
  const method = card.querySelector('[data-k="method"]');
  let m = method.value;
  if (model === U1) m = "snapmaker";
  else if (m === "snapmaker") m = "zmod_nfc";
  method.innerHTML = methodOptions({model, method: m});
  const slot = card.querySelector('[data-k="slot"]');
  let value = +slot.value;
  // Switching to a method whose printer cannot ask: default to a fixed slot,
  // so a scan fills it without the web page.
  if ((model === U1 || m !== "zmod_nfc" && card.dataset.method === "zmod_nfc") && !value) value = 1;
  card.dataset.method = m;
  slot.innerHTML = slotOptions({method: m, model, slot: value});
  card.querySelector(".slot-warn").classList.toggle("hidden", m === "zmod_nfc" || value !== 0);
  card.querySelectorAll(".m-stock").forEach(e => e.classList.toggle("hidden", m !== "stock"));
  card.querySelectorAll(".m-zmod").forEach(e => e.classList.toggle("hidden", m === "stock"));
  card.querySelector('[data-k="port"]').placeholder = m === "stock" ? 8898 : 7125;
}

function renderPrinters() {
  $("printerList").innerHTML = config.printers.map(printerCard).join("") || '<div class="hint">No printers yet.</div>';
  document.querySelectorAll(".printer").forEach(card => {
    showMethodFields(card);
    for (const k of ["method", "model", "slot"]) card.querySelector(`[data-k="${k}"]`).onchange = () => showMethodFields(card);
    card.querySelector(".remove").onclick = () => { collectPrinters(); config.printers.splice(+card.dataset.i, 1); renderPrinters(); };
    card.querySelector(".test").onclick = async () => {
      const msg = card.querySelector(".msg");
      if (config.printers[+card.dataset.i].keep === undefined) { msg.textContent = "Save first"; return; }
      msg.textContent = "Testing…";
      const r = await api("/api/test", {printer: config.printers[+card.dataset.i].keep});
      msg.textContent = r.message || r.error; msg.className = "msg " + (r.ok ? "ok" : "bad");
      msg.style.color = r.ok ? "var(--ok)" : "var(--bad)";
    };
  });
}

function collectPrinters() {
  document.querySelectorAll(".printer").forEach(card => {
    const p = config.printers[+card.dataset.i];
    card.querySelectorAll("[data-k]").forEach(e => {
      const k = e.dataset.k;
      if (k === "check_code" || k === "api_key") { if (e.value) p[k] = e.value; else delete p[k]; }
      else if (k === "port" || k === "slot") p[k] = +e.value || 0;
      else p[k] = e.value.trim();
    });
  });
}

$("addPrinter").onclick = () => {
  collectPrinters();
  config.printers.push({name: "", host: "", model: "ad5x", method: "zmod_nfc", port: 0, serial: ""});
  renderPrinters();
};
$("savePrinters").onclick = async () => {
  collectPrinters();
  const r = await api("/api/config", {printers: config.printers.map(p => {
    const o = {keep: p.keep ?? -1, name: p.name, host: p.host, model: p.model, method: p.method, port: p.port, slot: p.slot || 0, serial: p.serial};
    if (p.check_code) o.check_code = p.check_code;
    if (p.api_key) o.api_key = p.api_key;
    return o;
  })});
  setStatus($("printersMsg"), r.ok ? "Saved" : r.error, r.ok);
  if (r.ok) { await loadConfig(); slotsFor = ""; refresh(); }
};

// --- Settings ----------------------------------------------------------------
function showBus() {
  const chip = $("chip").value, spi = chip !== "pn532", uart = $("bus").value === "uart";
  document.querySelectorAll(".pn532").forEach(e => e.classList.toggle("hidden", spi));
  document.querySelectorAll(".spi").forEach(e => e.classList.toggle("hidden", !spi));
  document.querySelectorAll(".pn5180").forEach(e => e.classList.toggle("hidden", chip !== "pn5180"));
  document.querySelectorAll(".pn532.uart").forEach(e => e.classList.toggle("hidden", spi || !uart));
  document.querySelectorAll(".pn532.i2c").forEach(e => e.classList.toggle("hidden", spi || uart));
  $("ssLabel").textContent = chip === "pn5180" ? "NSS pin" : "SDA (SS) pin";
}
$("chip").onchange = showBus;
$("bus").onchange = showBus;

const READER_PINS = ["sda", "scl", "rx", "tx", "sck", "miso", "mosi", "ss", "rst", "busy"];
function renderSettings() {
  $("ssid").value = config.wifi_ssid;
  $("wifiPass").placeholder = config.wifi_pass_set ? "unchanged" : "";
  $("hostname").value = config.hostname;
  $("uiPassword").placeholder = config.ui_password_set ? "unchanged" : "none";
  $("wifiPower").value = String(config.wifi_power || 0);
  $("chip").value = config.reader.chip || "pn532";
  $("bus").value = config.reader.bus;
  for (const k of READER_PINS) $(k).value = config.reader[k];
  $("ledPin").value = config.led_pin;
  $("ledLow").value = config.led_active_low ? 1 : 0;
  $("buzzerPin").value = config.buzzer_pin;
  $("buttonPin").value = config.button_pin;
  const d = config.display || {};
  $("dispType").value = d.type || "none";
  $("dispSda").value = d.sda;
  $("dispScl").value = d.scl;
  $("dispAddr").value = String(d.address || 60);
  $("dispFlip").value = d.flip ? 1 : 0;
  $("dispSleep").value = d.sleep ?? 10;
  showDisplay();
  showBus();
}

function displaySettings() {
  return {type: $("dispType").value, sda: +$("dispSda").value, scl: +$("dispScl").value,
    address: +$("dispAddr").value, flip: $("dispFlip").value === "1", sleep: +$("dispSleep").value};
}
function showDisplay() {
  const type = $("dispType").value;
  document.querySelectorAll(".disp").forEach(e => e.classList.toggle("hidden", type === "none" || (e.classList.contains("oled") && type.startsWith("cyd"))));
  const d = status && status.display;
  setStatus($("dispMsg"), d && d.error ? d.error : "", d ? !d.error : undefined);
}
$("dispType").onchange = showDisplay;
$("saveDisplay").onclick = async () => {
  const r = await api("/api/config", {display: displaySettings()});
  setStatus($("dispMsg"), r.ok ? (r.restart ? "Saved; restarting" : "Saved") : r.error, r.ok);
};

$("wifiPower").onchange = async () => {
  const r = await api("/api/config", {wifi_power: +$("wifiPower").value});
  setStatus($("wifiMsg"), r.ok ? "Transmit power set" : r.error, r.ok);
};
async function saveSettings(msg, join) {
  const body = {
    wifi_ssid: $("ssid").value.trim(), hostname: $("hostname").value.trim(),
    reader: Object.assign({chip: $("chip").value, bus: $("bus").value}, ...READER_PINS.map(k => ({[k]: +$(k).value}))),
    led_pin: +$("ledPin").value, led_active_low: $("ledLow").value === "1", buzzer_pin: +$("buzzerPin").value,
    button_pin: +$("buttonPin").value, display: displaySettings(),
  };
  if ($("wifiPass").value) body.wifi_pass = $("wifiPass").value;
  if (join) body.join = true;
  if ($("uiPassword").value) body.ui_password = $("uiPassword").value;
  const r = await api("/api/config", body);
  setStatus(msg, r.ok ? (r.restart ? "Saved; restarting to join " + body.wifi_ssid + ". Reconnect to " + body.hostname + ".local or the new IP address. If it cannot join, the setup network comes back in about 20 seconds." : "Saved") : r.error, r.ok);
}
$("saveSettings").onclick = () => saveSettings($("settingsMsg"));
$("joinWifi").onclick = () => saveSettings($("wifiMsg"), true);
$("scanWifi").onclick = async () => {
  $("scanWifi").textContent = "Searching…";
  const r = await api("/api/networks");
  $("ssids").innerHTML = (r.networks || []).map(n => `<option value="${esc(n.ssid)}">${n.rssi} dBm${n.open ? ", open" : ""}</option>`).join("");
  $("scanWifi").textContent = `Find networks (${(r.networks || []).length} found)`;
  $("ssid").focus();
};
$("upload").onclick = async () => {
  const file = $("fw").files[0];
  if (!file) return;
  setStatus($("fwMsg"), "Uploading…");
  const form = new FormData();
  form.append("firmware", file, file.name);
  try {
    const r = await (await fetch("/update", {method: "POST", body: form})).json();
    setStatus($("fwMsg"), r.ok ? "Updated; restarting…" : r.error, r.ok);
  } catch (e) { setStatus($("fwMsg"), "Upload failed", false); }
};
$("restart").onclick = () => api("/api/restart", {});

async function loadConfig() {
  config = await api("/api/config");
  config.printers.forEach((p, i) => { p.keep = i; p.savedSlot = p.slot; });
  renderPrinters(); renderSettings();
}

loadConfig();
refresh();
setInterval(() => { if (!busy) refresh(); }, 1500);
</script>
</body>
</html>
)HTML";
