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
.voice-row{display:flex;gap:8px;align-items:center;flex-wrap:wrap;padding:4px 0}.voice-row b{min-width:9em}
:root{--bg:#f4f5f7;--card:#fff;--text:#1d2129;--muted:#5f6673;--line:#dde0e5;--accent:#2563eb;--ok:#15803d;--bad:#b91c1c;--warn:#a16207;--chip:#eef0f3}
@media (prefers-color-scheme:dark){:root{--bg:#14161a;--card:#1d2026;--text:#e8eaed;--muted:#9aa1ad;--line:#2f333b;--accent:#5b8def;--ok:#4ade80;--bad:#f87171;--warn:#facc15;--chip:#2a2e36}}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--text);font:15px/1.45 system-ui,-apple-system,"Segoe UI",Roboto,sans-serif}
header{display:flex;flex-wrap:wrap;gap:8px 16px;align-items:center;padding:12px 16px;background:var(--card);border-bottom:1px solid var(--line)}
header h1{font-size:17px;margin:0;flex:1 1 auto}
header .logo{width:32px;height:32px;flex:none}
.brand{display:block;width:100%;max-width:340px;height:auto;margin:4px auto 0;color:var(--text)}
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
a{color:var(--accent)}
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
.methods{margin-top:16px;border-top:1px solid var(--line);padding-top:4px}
.methods p{margin:8px 0}
.msg{margin-top:10px;font-size:14px}
.found{display:flex;gap:10px;align-items:center;padding:8px 0;border-top:1px solid var(--line)}.found>div{flex:1 1 auto;min-width:0}.found b{display:block}
.hidden{display:none}
</style>
</head>
<body>
<header>
  <svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 400 400" class="logo" aria-hidden="true"><g transform="translate(36 0)"><g id="spool"><ellipse cx="230" cy="194" rx="112" ry="150" id="back-flange" fill="#243241" stroke="#1b2632" stroke-width="3"/><path id="filament" d="M151.01 70.72L218.21 65.68A96.32 129 0 0 1 228.99 323.28L161.79 328.32A96.32 129 0 0 1 151.01 70.72Z" fill="#f28a1e" stroke="#1b2632" stroke-width="3" stroke-linejoin="round"/><g id="filament-coils" fill="none" stroke="#c9650f" stroke-width="2.5" stroke-linecap="round"><path d="M167.08 69.52A96.32 129 0 0 1 177.86 327.11"/><path d="M177.31 68.75A96.32 129 0 0 1 188.08 326.35"/><path d="M187.54 67.98A96.32 129 0 0 1 198.31 325.58"/><path d="M197.76 67.22A96.32 129 0 0 1 208.53 324.81"/><path d="M207.99 66.45A96.32 129 0 0 1 218.76 324.05"/></g><ellipse cx="150" cy="200" rx="112" ry="150" id="front-rim" fill="#53697f" stroke="#1b2632" stroke-width="3" transform="translate(11 -1)"/><ellipse cx="150" cy="200" rx="112" ry="150" id="front-face" fill="#3a4e63" stroke="#1b2632" stroke-width="3"/><g id="windows"><clipPath id="mk-win0"><path d="M180.1 94.97A84 112.5 0 0 0 119.9 94.97L133.95 143.99A44.8 60 0 0 1 166.05 143.99Z"/></clipPath><path d="M180.1 94.97A84 112.5 0 0 0 119.9 94.97L133.95 143.99A44.8 60 0 0 1 166.05 143.99Z" fill="#243241" stroke="#243241" stroke-width="6" stroke-linejoin="round"/><g clip-path="url(#mk-win0)"><path d="M180.1 94.97A84 112.5 0 0 0 119.9 94.97L133.95 143.99A44.8 60 0 0 1 166.05 143.99Z" fill="#f28a1e" transform="translate(7 -0.5)"/><path d="M129 134.55A56 75 0 0 1 185 134.55" fill="none" stroke="#c9650f" stroke-width="2.5"/><path d="M122.28 118.96A69.44 93 0 0 1 191.72 118.96" fill="none" stroke="#c9650f" stroke-width="2.5"/></g><path d="M180.1 94.97A84 112.5 0 0 0 119.9 94.97L133.95 143.99A44.8 60 0 0 1 166.05 143.99Z" fill="none" stroke="#1b2632" stroke-width="2.5" stroke-linejoin="round"/><clipPath id="mk-win1"><path d="M228.42 240.32A84 112.5 0 0 0 228.42 159.68L191.82 178.5A44.8 60 0 0 1 191.82 221.5Z"/></clipPath><path d="M228.42 240.32A84 112.5 0 0 0 228.42 159.68L191.82 178.5A44.8 60 0 0 1 191.82 221.5Z" fill="#243241" stroke="#243241" stroke-width="6" stroke-linejoin="round"/><g clip-path="url(#mk-win1)"><path d="M228.42 240.32A84 112.5 0 0 0 228.42 159.68L191.82 178.5A44.8 60 0 0 1 191.82 221.5Z" fill="#f28a1e" transform="translate(7 -0.5)"/><path d="M205.5 162A56 75 0 0 1 205.5 237" fill="none" stroke="#c9650f" stroke-width="2.5"/><path d="M217.14 153A69.44 93 0 0 1 217.14 246" fill="none" stroke="#c9650f" stroke-width="2.5"/></g><path d="M228.42 240.32A84 112.5 0 0 0 228.42 159.68L191.82 178.5A44.8 60 0 0 1 191.82 221.5Z" fill="none" stroke="#1b2632" stroke-width="2.5" stroke-linejoin="round"/><clipPath id="mk-win2"><path d="M119.9 305.03A84 112.5 0 0 0 180.1 305.03L166.05 256.01A44.8 60 0 0 1 133.95 256.01Z"/></clipPath><path d="M119.9 305.03A84 112.5 0 0 0 180.1 305.03L166.05 256.01A44.8 60 0 0 1 133.95 256.01Z" fill="#243241" stroke="#243241" stroke-width="6" stroke-linejoin="round"/><g clip-path="url(#mk-win2)"><path d="M119.9 305.03A84 112.5 0 0 0 180.1 305.03L166.05 256.01A44.8 60 0 0 1 133.95 256.01Z" fill="#f28a1e" transform="translate(7 -0.5)"/><path d="M185 264.45A56 75 0 0 1 129 264.45" fill="none" stroke="#c9650f" stroke-width="2.5"/><path d="M191.72 280.04A69.44 93 0 0 1 122.28 280.04" fill="none" stroke="#c9650f" stroke-width="2.5"/></g><path d="M119.9 305.03A84 112.5 0 0 0 180.1 305.03L166.05 256.01A44.8 60 0 0 1 133.95 256.01Z" fill="none" stroke="#1b2632" stroke-width="2.5" stroke-linejoin="round"/><clipPath id="mk-win3"><path d="M71.58 159.68A84 112.5 0 0 0 71.58 240.32L108.18 221.5A44.8 60 0 0 1 108.18 178.5Z"/></clipPath><path d="M71.58 159.68A84 112.5 0 0 0 71.58 240.32L108.18 221.5A44.8 60 0 0 1 108.18 178.5Z" fill="#243241" stroke="#243241" stroke-width="6" stroke-linejoin="round"/><g clip-path="url(#mk-win3)"><path d="M71.58 159.68A84 112.5 0 0 0 71.58 240.32L108.18 221.5A44.8 60 0 0 1 108.18 178.5Z" fill="#f28a1e" transform="translate(7 -0.5)"/><path d="M108.5 237A56 75 0 0 1 108.5 162" fill="none" stroke="#c9650f" stroke-width="2.5"/><path d="M96.86 246A69.44 93 0 0 1 96.86 153" fill="none" stroke="#c9650f" stroke-width="2.5"/></g><path d="M71.58 159.68A84 112.5 0 0 0 71.58 240.32L108.18 221.5A44.8 60 0 0 1 108.18 178.5Z" fill="none" stroke="#1b2632" stroke-width="2.5" stroke-linejoin="round"/></g><clipPath id="mk-bore"><ellipse cx="150" cy="200" rx="22.4" ry="30" /></clipPath><g id="bore"><ellipse cx="150" cy="200" rx="22.4" ry="30" fill="#53697f"/><g clip-path="url(#mk-bore)"><ellipse cx="162" cy="199" rx="22.4" ry="30" fill="#141c25"/></g><ellipse cx="150" cy="200" rx="22.4" ry="30" fill="none" stroke="#1b2632" stroke-width="3"/></g><g id="nfc-sticker"><ellipse cx="102.48" cy="136.36" rx="21.28" ry="28.5" fill="#ffffff" stroke="#1b2632" stroke-width="2.5"/><g id="tag-waves" transform="translate(102.48 136.36) scale(17.02 22.8)" fill="none" stroke="#1e88e5" stroke-width="0.13" stroke-linecap="round"><circle r="0.13" fill="#1e88e5" stroke="none"/><path d="M-0.25 -0.28A0.38 0.38 0 0 0 -0.25 0.28M0.25 -0.28A0.38 0.38 0 0 1 0.25 0.28"/><path d="M-0.44 -0.49A0.66 0.66 0 0 0 -0.44 0.49M0.44 -0.49A0.66 0.66 0 0 1 0.44 0.49"/><path d="M-0.63 -0.7A0.94 0.94 0 0 0 -0.63 0.7M0.63 -0.7A0.94 0.94 0 0 1 0.63 0.7"/></g></g></g></g></svg>
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
  <svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 1018 400" class="brand" role="img" aria-label="LaneTag"><g id="lg-spool"><ellipse cx="292" cy="194" rx="112" ry="150" id="lg-back-flange" fill="#243241" stroke="#1b2632" stroke-width="3"/><path id="lg-filament" d="M213.01 70.72L280.21 65.68A96.32 129 0 0 1 290.99 323.28L223.79 328.32A96.32 129 0 0 1 213.01 70.72Z" fill="#f28a1e" stroke="#1b2632" stroke-width="3" stroke-linejoin="round"/><g id="lg-filament-coils" fill="none" stroke="#c9650f" stroke-width="2.5" stroke-linecap="round"><path d="M229.08 69.52A96.32 129 0 0 1 239.86 327.11"/><path d="M239.31 68.75A96.32 129 0 0 1 250.08 326.35"/><path d="M249.54 67.98A96.32 129 0 0 1 260.31 325.58"/><path d="M259.76 67.22A96.32 129 0 0 1 270.53 324.81"/><path d="M269.99 66.45A96.32 129 0 0 1 280.76 324.05"/></g><ellipse cx="212" cy="200" rx="112" ry="150" id="lg-front-rim" fill="#53697f" stroke="#1b2632" stroke-width="3" transform="translate(11 -1)"/><ellipse cx="212" cy="200" rx="112" ry="150" id="lg-front-face" fill="#3a4e63" stroke="#1b2632" stroke-width="3"/><g id="lg-windows"><clipPath id="lt-win0"><path d="M242.1 94.97A84 112.5 0 0 0 181.9 94.97L195.95 143.99A44.8 60 0 0 1 228.05 143.99Z"/></clipPath><path d="M242.1 94.97A84 112.5 0 0 0 181.9 94.97L195.95 143.99A44.8 60 0 0 1 228.05 143.99Z" fill="#243241" stroke="#243241" stroke-width="6" stroke-linejoin="round"/><g clip-path="url(#lt-win0)"><path d="M242.1 94.97A84 112.5 0 0 0 181.9 94.97L195.95 143.99A44.8 60 0 0 1 228.05 143.99Z" fill="#f28a1e" transform="translate(7 -0.5)"/><path d="M191 134.55A56 75 0 0 1 247 134.55" fill="none" stroke="#c9650f" stroke-width="2.5"/><path d="M184.28 118.96A69.44 93 0 0 1 253.72 118.96" fill="none" stroke="#c9650f" stroke-width="2.5"/></g><path d="M242.1 94.97A84 112.5 0 0 0 181.9 94.97L195.95 143.99A44.8 60 0 0 1 228.05 143.99Z" fill="none" stroke="#1b2632" stroke-width="2.5" stroke-linejoin="round"/><clipPath id="lt-win1"><path d="M290.42 240.32A84 112.5 0 0 0 290.42 159.68L253.82 178.5A44.8 60 0 0 1 253.82 221.5Z"/></clipPath><path d="M290.42 240.32A84 112.5 0 0 0 290.42 159.68L253.82 178.5A44.8 60 0 0 1 253.82 221.5Z" fill="#243241" stroke="#243241" stroke-width="6" stroke-linejoin="round"/><g clip-path="url(#lt-win1)"><path d="M290.42 240.32A84 112.5 0 0 0 290.42 159.68L253.82 178.5A44.8 60 0 0 1 253.82 221.5Z" fill="#f28a1e" transform="translate(7 -0.5)"/><path d="M267.5 162A56 75 0 0 1 267.5 237" fill="none" stroke="#c9650f" stroke-width="2.5"/><path d="M279.14 153A69.44 93 0 0 1 279.14 246" fill="none" stroke="#c9650f" stroke-width="2.5"/></g><path d="M290.42 240.32A84 112.5 0 0 0 290.42 159.68L253.82 178.5A44.8 60 0 0 1 253.82 221.5Z" fill="none" stroke="#1b2632" stroke-width="2.5" stroke-linejoin="round"/><clipPath id="lt-win2"><path d="M181.9 305.03A84 112.5 0 0 0 242.1 305.03L228.05 256.01A44.8 60 0 0 1 195.95 256.01Z"/></clipPath><path d="M181.9 305.03A84 112.5 0 0 0 242.1 305.03L228.05 256.01A44.8 60 0 0 1 195.95 256.01Z" fill="#243241" stroke="#243241" stroke-width="6" stroke-linejoin="round"/><g clip-path="url(#lt-win2)"><path d="M181.9 305.03A84 112.5 0 0 0 242.1 305.03L228.05 256.01A44.8 60 0 0 1 195.95 256.01Z" fill="#f28a1e" transform="translate(7 -0.5)"/><path d="M247 264.45A56 75 0 0 1 191 264.45" fill="none" stroke="#c9650f" stroke-width="2.5"/><path d="M253.72 280.04A69.44 93 0 0 1 184.28 280.04" fill="none" stroke="#c9650f" stroke-width="2.5"/></g><path d="M181.9 305.03A84 112.5 0 0 0 242.1 305.03L228.05 256.01A44.8 60 0 0 1 195.95 256.01Z" fill="none" stroke="#1b2632" stroke-width="2.5" stroke-linejoin="round"/><clipPath id="lt-win3"><path d="M133.58 159.68A84 112.5 0 0 0 133.58 240.32L170.18 221.5A44.8 60 0 0 1 170.18 178.5Z"/></clipPath><path d="M133.58 159.68A84 112.5 0 0 0 133.58 240.32L170.18 221.5A44.8 60 0 0 1 170.18 178.5Z" fill="#243241" stroke="#243241" stroke-width="6" stroke-linejoin="round"/><g clip-path="url(#lt-win3)"><path d="M133.58 159.68A84 112.5 0 0 0 133.58 240.32L170.18 221.5A44.8 60 0 0 1 170.18 178.5Z" fill="#f28a1e" transform="translate(7 -0.5)"/><path d="M170.5 237A56 75 0 0 1 170.5 162" fill="none" stroke="#c9650f" stroke-width="2.5"/><path d="M158.86 246A69.44 93 0 0 1 158.86 153" fill="none" stroke="#c9650f" stroke-width="2.5"/></g><path d="M133.58 159.68A84 112.5 0 0 0 133.58 240.32L170.18 221.5A44.8 60 0 0 1 170.18 178.5Z" fill="none" stroke="#1b2632" stroke-width="2.5" stroke-linejoin="round"/></g><clipPath id="lt-bore"><ellipse cx="212" cy="200" rx="22.4" ry="30" /></clipPath><g id="lg-bore"><ellipse cx="212" cy="200" rx="22.4" ry="30" fill="#53697f"/><g clip-path="url(#lt-bore)"><ellipse cx="224" cy="199" rx="22.4" ry="30" fill="#141c25"/></g><ellipse cx="212" cy="200" rx="22.4" ry="30" fill="none" stroke="#1b2632" stroke-width="3"/></g><g id="lg-nfc-sticker"><ellipse cx="164.48" cy="136.36" rx="21.28" ry="28.5" fill="#ffffff" stroke="#1b2632" stroke-width="2.5"/><g id="lg-tag-waves" transform="translate(164.48 136.36) scale(17.02 22.8)" fill="none" stroke="#1e88e5" stroke-width="0.13" stroke-linecap="round"><circle r="0.13" fill="#1e88e5" stroke="none"/><path d="M-0.25 -0.28A0.38 0.38 0 0 0 -0.25 0.28M0.25 -0.28A0.38 0.38 0 0 1 0.25 0.28"/><path d="M-0.44 -0.49A0.66 0.66 0 0 0 -0.44 0.49M0.44 -0.49A0.66 0.66 0 0 1 0.44 0.49"/><path d="M-0.63 -0.7A0.94 0.94 0 0 0 -0.63 0.7M0.63 -0.7A0.94 0.94 0 0 1 0.63 0.7"/></g></g></g><g id="hardware"><g id="cable" fill="none" stroke-linecap="round"><path d="M83.19 161.36C79.19 201.36 47.43 215.53 81.43 255.53" stroke="#1b2632" stroke-width="10"/><path d="M83.19 161.36C79.19 201.36 47.43 215.53 81.43 255.53" stroke="#c8d0d8" stroke-width="5"/></g><g id="esp32" transform="rotate(5 108 296)"><rect x="70" y="244" width="76" height="104" rx="8" fill="#243241" stroke="#1b2632" stroke-width="3"/><path d="M90 268V256H99V266H108V256H117V266H126V256" fill="none" stroke="#c8d0d8" stroke-width="2.5" stroke-linejoin="round"/><rect x="86" y="274" width="44" height="40" rx="3" fill="#c8d0d8" stroke="#1b2632" stroke-width="2"/><circle cx="78" cy="258" r="2.6" fill="#c8d0d8"/><circle cx="138" cy="258" r="2.6" fill="#c8d0d8"/><circle cx="78" cy="272" r="2.6" fill="#c8d0d8"/><circle cx="138" cy="272" r="2.6" fill="#c8d0d8"/><circle cx="78" cy="286" r="2.6" fill="#c8d0d8"/><circle cx="138" cy="286" r="2.6" fill="#c8d0d8"/><circle cx="78" cy="300" r="2.6" fill="#c8d0d8"/><circle cx="138" cy="300" r="2.6" fill="#c8d0d8"/><circle cx="78" cy="314" r="2.6" fill="#c8d0d8"/><circle cx="138" cy="314" r="2.6" fill="#c8d0d8"/><circle cx="78" cy="328" r="2.6" fill="#c8d0d8"/><circle cx="138" cy="328" r="2.6" fill="#c8d0d8"/><rect x="97" y="336" width="22" height="9" rx="2" fill="#c8d0d8" stroke="#1b2632" stroke-width="2"/></g><g id="nfc-card" transform="rotate(-8 74 96)"><rect x="68" y="148" width="12" height="14" rx="2" fill="#1b2632"/><rect x="18" y="40" width="112" height="112" rx="13" fill="#d93a2f" stroke="#1b2632" stroke-width="3"/><circle cx="50" cy="74" r="22" fill="#ffffff" stroke="#1b2632" stroke-width="2.5"/><g id="nfc-symbol" transform="translate(30.05 56) scale(0.15)" fill="none" stroke="#1e88e5" stroke-width="24" stroke-linecap="round" stroke-linejoin="round"><path d="M32 148L42 84L118 150L126 86"/><path d="M166 62A85 85 0 0 1 166 178"/><path d="M206 32A120 120 0 0 1 206 208"/></g><g id="rfid-mark" fill="none" stroke="#ffffff" stroke-width="5.5"><path d="M98 98A14 14 0 0 1 112 84"/><path d="M87 98A25 25 0 0 1 112 73"/><path d="M76 98A36 36 0 0 1 112 62"/></g><circle cx="112" cy="98" r="5.5" fill="#ffffff"/><path id="rfid-text" fill="#ffffff" d="M55.36 138 51.41 130.84H50.31V138H45.69V119.05H53.44Q55.68 119.05 57.26 119.83Q58.84 120.61 59.62 121.98Q60.41 123.34 60.41 125.01Q60.41 126.9 59.34 128.39Q58.27 129.87 56.19 130.49L60.57 138ZM50.31 127.58H53.17Q54.44 127.58 55.07 126.96Q55.71 126.34 55.71 125.2Q55.71 124.12 55.07 123.5Q54.44 122.88 53.17 122.88H50.31ZM75.63 119.05V122.75H67.91V126.74H73.69V130.33H67.91V138H63.29V119.05ZM82.68 119.05V138H78.06V119.05ZM103.07 128.52Q103.07 131.3 101.84 133.46Q100.61 135.62 98.35 136.81Q96.1 138 93.13 138H86.03V119.05H93.13Q96.13 119.05 98.37 120.23Q100.61 121.42 101.84 123.57Q103.07 125.72 103.07 128.52ZM98.37 128.52Q98.37 125.9 96.91 124.45Q95.45 122.99 92.83 122.99H90.65V134H92.83Q95.45 134 96.91 132.57Q98.37 131.14 98.37 128.52Z"/></g></g><g id="wordmark" aria-label="LaneTag"><path id="word-lane" fill="currentColor" d="M473.82 227.1H502.5V244H451.94V154.14H473.82ZM538.85 171.55Q546.4 171.55 552.1 174.62Q557.79 177.7 560.86 182.69V172.58H582.75V244H560.86V233.89Q557.66 238.88 551.97 241.95Q546.27 245.02 538.72 245.02Q530.02 245.02 522.85 240.48Q515.68 235.94 511.52 227.55Q507.36 219.17 507.36 208.16Q507.36 197.15 511.52 188.83Q515.68 180.51 522.85 176.03Q530.02 171.55 538.85 171.55ZM545.25 190.62Q538.72 190.62 534.18 195.3Q529.63 199.97 529.63 208.16Q529.63 216.35 534.18 221.15Q538.72 225.95 545.25 225.95Q551.78 225.95 556.32 221.22Q560.86 216.48 560.86 208.29Q560.86 200.1 556.32 195.36Q551.78 190.62 545.25 190.62ZM668.38 202.27V244H646.62V205.22Q646.62 198.05 642.91 194.08Q639.2 190.11 632.93 190.11Q626.66 190.11 622.94 194.08Q619.23 198.05 619.23 205.22V244H597.34V172.58H619.23V182.05Q622.56 177.31 628.19 174.56Q633.82 171.81 640.86 171.81Q653.41 171.81 660.9 179.94Q668.38 188.06 668.38 202.27ZM749.28 213.54H699.74Q700.26 220.19 704.03 223.71Q707.81 227.23 713.31 227.23Q721.5 227.23 724.7 220.32H748Q746.21 227.36 741.54 232.99Q736.86 238.62 729.82 241.82Q722.78 245.02 714.08 245.02Q703.58 245.02 695.39 240.54Q687.2 236.06 682.59 227.74Q677.98 219.42 677.98 208.29Q677.98 197.15 682.53 188.83Q687.07 180.51 695.26 176.03Q703.46 171.55 714.08 171.55Q724.45 171.55 732.51 175.9Q740.58 180.26 745.12 188.32Q749.66 196.38 749.66 207.14Q749.66 210.21 749.28 213.54ZM727.26 201.38Q727.26 195.74 723.42 192.42Q719.58 189.09 713.82 189.09Q708.32 189.09 704.54 192.29Q700.77 195.49 699.87 201.38Z"/><path id="word-tag" fill="#f28a1e" d="M824.54 154.14V171.68H800.74V244H778.85V171.68H755.04V154.14ZM861.41 171.55Q868.96 171.55 874.66 174.62Q880.35 177.7 883.42 182.69V172.58H905.31V244H883.42V233.89Q880.22 238.88 874.53 241.95Q868.83 245.02 861.28 245.02Q852.58 245.02 845.41 240.48Q838.24 235.94 834.08 227.55Q829.92 219.17 829.92 208.16Q829.92 197.15 834.08 188.83Q838.24 180.51 845.41 176.03Q852.58 171.55 861.41 171.55ZM867.81 190.62Q861.28 190.62 856.74 195.3Q852.19 199.97 852.19 208.16Q852.19 216.35 856.74 221.15Q861.28 225.95 867.81 225.95Q874.34 225.95 878.88 221.22Q883.42 216.48 883.42 208.29Q883.42 200.1 878.88 195.36Q874.34 190.62 867.81 190.62ZM969.06 182.69V172.58H990.94V243.87Q990.94 253.73 987.04 261.73Q983.14 269.73 975.14 274.46Q967.14 279.2 955.23 279.2Q939.36 279.2 929.5 271.71Q919.65 264.22 918.24 251.42H939.87Q940.9 255.52 944.74 257.89Q948.58 260.26 954.21 260.26Q960.99 260.26 965.02 256.35Q969.06 252.45 969.06 243.87V233.76Q965.86 238.75 960.22 241.89Q954.59 245.02 947.04 245.02Q938.21 245.02 931.04 240.48Q923.87 235.94 919.71 227.55Q915.55 219.17 915.55 208.16Q915.55 197.15 919.71 188.83Q923.87 180.51 931.04 176.03Q938.21 171.55 947.04 171.55Q954.59 171.55 960.29 174.62Q965.98 177.7 969.06 182.69ZM953.44 190.62Q946.91 190.62 942.37 195.3Q937.82 199.97 937.82 208.16Q937.82 216.35 942.37 221.15Q946.91 225.95 953.44 225.95Q959.97 225.95 964.51 221.22Q969.06 216.48 969.06 208.29Q969.06 200.1 964.51 195.36Q959.97 190.62 953.44 190.62Z"/></g></svg>
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
    <div id="printerList"></div>
    <div id="foundList"></div>
    <div class="actions">
      <button class="btn" id="findPrinters">Find printers</button>
      <button class="btn" id="addPrinter">Add printer</button>
      <button class="btn primary" id="savePrinters">Save</button>
    </div>
    <div class="msg" id="printersMsg"></div>
    <div class="hint methods">
      <p>Pick a method per printer:</p>
      <p><b>FF_NFC</b> sends the whole tag to the FF_NFC plugin (Zmod or Kiln OS), which shows its slot popup, matches Spoolman and fills lane data.</p>
      <p><b>Zmod basic</b> sets the slot's color and material through Moonraker; no plugin needed.</p>
      <p><b>Stock</b> (Flashforge) uses Flashforge's local API: slot color and material only. It needs the printer's serial number and check code.</p>
      <p><b>Snapmaker U1, Extended firmware</b> (paxx12): each scan arrives as the head's own RFID read, and SpoolLink matches the tag in Spoolman. Each U1 reader is fixed to one head.</p>
      <p><b>Snapmaker U1, stock firmware</b>: a scan sets the head's material, vendor and color the way an edit on the screen does (no Spoolman matching).</p>
      <p><b>Bambu Lab</b> (LAN mode) gets the tray's material, color and Generic preset over its MQTT broker. It needs the serial number and the LAN access code from the printer's network settings; newer firmware also needs Developer Mode.</p>
      <p><b>Klipper</b> printers go through Moonraker. <b>Happy Hare</b> sets the MMU gate map, <b>AFC</b> (Box Turtle, Night Owl, ...) sets a lane, and <b>lane data</b> fills Moonraker's lane_data for OrcaSlicer on any Klipper printer. Tags naming a Spoolman spool (by the spool ID on the tag, or by the tag's UID linked to a spool in the printer's Spoolman) are assigned through Spoolman.</p>
      <p><b>Flashforge AD5M</b> (Zmod or Forge-X) has no filament slots to set: use lane data.</p>
      <p><b>Creality K1 / K2 / Hi</b> with a CFS gets the slot's material and color the way Creality Print sets it (stock firmware, nothing to enter but the IP address). Slots holding a Creality RFID spool are left to the CFS.</p>
      <p><b>Anycubic Kobra</b> (Kobra 3 / S1 generation) with an ACE in LAN mode gets the slot's material and color over its own MQTT broker. Turn LAN mode on at the printer; nothing else to enter.</p>
      <p><b>Qidi</b> Plus4 / Q2 / Max4 with a Box gets the slot's filament, color and vendor from the printer's own lists, as its RFID reader sets them (stock firmware, through Moonraker).</p>
      <p><b>Spoolman (no printer)</b> makes this a stand-alone reader for Spoolman itself: enter the Spoolman server's IP address, port 7912. A scan finds the spool by the spool ID on the tag, by the tag's UID in the spool's Card UIDs, or as the only spool with the tag's brand, material and color (the tag is then bound to it), and it can add spools Spoolman does not have yet. Printers running FF_NFC, AFC-lite or SpoolLink find those spools by the same UIDs.</p>
    </div>
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
      <div><label for="ledType">LED</label><select id="ledType"><option value="single">Single LED</option><option value="rgb">RGB LED (3 pins)</option><option value="ws2812">WS2812 / NeoPixel</option></select></div>
      <div><label for="ledPin" id="ledPinLabel">LED pin (-1: none)</label><input id="ledPin" type="number"></div>
      <div class="led-rgb"><label for="ledGreen">Green pin (-1: none)</label><input id="ledGreen" type="number"></div>
      <div class="led-rgb"><label for="ledBlue">Blue pin (-1: none)</label><input id="ledBlue" type="number"></div>
      <div class="led-pins"><label for="ledLow">LED lights when</label><select id="ledLow"><option value="0">high</option><option value="1">low</option></select></div>
      <div><label for="buzzerPin">Buzzer pin (-1: none)</label><input id="buzzerPin" type="number"></div>
      <div><label for="buttonPin">Slot button pin (-1: none)</label><input id="buttonPin" type="number"></div>
      <div><label for="sleepMinutes">Sleep after minutes unused (0: never)</label><input id="sleepMinutes" type="number" min="0" max="1440"></div>
    </div>
    <div class="hint">Sleep is for a reader on a battery: after that long without a scan, button press, touch or use of this page, the reader, screen and Wi-Fi switch off and the board draws almost nothing. A press of the slot button wakes it; it is back on the network in a few seconds, then scans as usual. Only some pins can wake the board (C3: GPIO 0-5, so not its BOOT button; C6: 0-7; C5: 0-6; S3: 0-21; ESP32: 0, 2, 4, 12-15, 25-27, 32-39, and 34-39 need a 10k pull-up to 3.3 V). A PN5180 stays powered while the board sleeps, as do the power LEDs of the reader and board.</div>
    <div class="actions"><button class="btn primary" id="saveSettings">Save</button></div>
    <div class="msg" id="settingsMsg"></div>
  </div>
  <div class="card">
    <h2>Spoolman tag scanning</h2>
    <div class="grid">
      <div><label for="smReport">Report every scan to Spoolman at</label><input id="smReport" placeholder="none, e.g. 192.168.1.20:7912"></div>
    </div>
    <div class="hint">Spoolman 0.27 or newer: this reader then shows up in Spoolman's settings under Tag scanning, and a browser paired with it (or set to auto-navigate) opens the scanned spool, whichever printer the scan goes to. Spoolman finds the spool by tags linked in Spoolman; a Spoolman entry on the Printers tab links them as it finds or adds spools.</div>
    <div class="actions"><button class="btn primary" id="saveReport">Save</button></div>
    <div class="msg" id="reportMsg"></div>
  </div>
  <div class="card">
    <h2>Display</h2>
    <div class="grid">
      <div><label for="dispType">Screen</label><select id="dispType"><option value="none">None</option><option value="ssd1306">OLED SSD1306 (0.96", I2C)</option><option value="sh1106">OLED SH1106 (1.3", I2C)</option><option value="lcd1602">LCD 1602 (16x2, I2C backpack)</option><option value="cyd">CYD 2.8" touch (ESP32-2432S028)</option><option value="cyd-st7789">CYD 2.8" touch, ST7789 (two USB ports)</option><option value="c6-lcd147">Waveshare ESP32-C6/C5-LCD-1.47</option><option value="lcd19">ideaspark ESP32 1.9" LCD</option><option value="lcd114">ideaspark ESP32 1.14" LCD</option><option value="c6-lcd169">Waveshare ESP32-C6-LCD-1.69</option></select></div>
      <div class="disp oled"><label for="dispSda">SDA pin</label><input id="dispSda" type="number"></div>
      <div class="disp oled"><label for="dispScl">SCL pin</label><input id="dispScl" type="number"></div>
      <div class="disp oled"><label for="dispAddr">Address</label><select id="dispAddr"><option value="60">0x3C</option><option value="61">0x3D</option><option value="39">0x27</option><option value="63">0x3F</option></select></div>
      <div class="disp flip"><label for="dispFlip">Orientation</label><select id="dispFlip"><option value="0">Normal</option><option value="1">Upside down</option></select></div>
      <div class="disp"><label for="dispSleep">Screen off after (minutes, 0: never)</label><input id="dispSleep" type="number" min="0"></div>
      <div class="disp"><label for="dispLogo">Logo at start-up</label><select id="dispLogo"><option value="1">Show for 3 seconds</option><option value="0">Skip (fastest start)</option></select></div>
    </div>
    <div class="hint">Shows the active printer and slot, and the filament of the last scan. It can share the PN532's I2C pins. Boards with a built-in 0.96" OLED (such as the Ideaspark ESP32) usually have it on SDA 21 / SCL 22. The CYD touch screen (classic ESP32 builds only) also has buttons: tap the printer name for the next printer, the bottom row for the fixed slot, and a scan waiting for a slot can be put into one there.</div>
    <div class="msg" id="dispMsg"></div>
    <div class="actions"><button class="btn primary" id="saveDisplay">Save</button></div>
  </div>
  <div class="card hidden" id="voiceCard">
    <h2>Voice commands</h2>
    <div class="hint">Hold the board's PWR button, say a command, let go. The board knows your voice from three takes of each command, recorded here: click Train, then hold PWR and say the command three times (the screen shows how far it is). Add 3 more takes later (another day, or holding the board differently) and it gets surer: up to 6 per command. Any words work, in any language ("Lane 2", "T one", ...). Lane 1-4 sets the slot scans go to, or puts a scan that waits for its slot into that one. Everything is recognized on the board itself.</div>
    <div id="voiceList"></div>
    <div class="msg hidden" id="voiceMsg"></div>
    <div class="actions"><button class="btn hidden" id="voiceCancel">Stop training</button><button class="btn danger" id="voiceClear">Forget all</button> <a href="/api/voice/last.wav" target="_blank" rel="noopener">last recording</a></div>
  </div>
  <div class="card">
    <h2>Firmware</h2>
    <div class="muted" id="version"></div>
    <div class="muted"><a href="https://github.com/DIEHARDave/LaneTag" target="_blank" rel="noopener">LaneTag on GitHub</a> · <a href="https://github.com/DIEHARDave/LaneTag/releases" target="_blank" rel="noopener">all releases</a> (download a .bin there and upload it below)</div>
    <div class="muted" id="releaseLine"></div>
    <div class="banner hidden" id="newRelease"></div>
    <div class="actions"><button class="btn primary" id="checkUpdate">Check for updates</button></div>
    <div class="msg hidden" id="otaMsg"></div>
    <label for="fw">Or upload a firmware file (lanetag-&lt;version&gt;-<span id="fwBoard">&lt;board&gt;</span>.bin from the <a href="https://github.com/DIEHARDave/LaneTag/releases" target="_blank" rel="noopener">releases</a>, not the -factory one; or .pio/build/&lt;board&gt;/firmware.bin)</label>
    <input id="fw" type="file" accept=".bin">
    <div class="actions"><button class="btn" id="upload">Upload</button><button class="btn danger" id="restart">Restart</button></div>
    <div class="msg" id="fwMsg"></div>
  </div>
</section>
</main>
<script>
const $ = id => document.getElementById(id);
const esc = s => String(s ?? "").replace(/[&<>"']/g, c => ({"&":"&amp;","<":"&lt;",">":"&gt;",'"':"&quot;","'":"&#39;"}[c]));
const METHODS = {ff_nfc: "FF_NFC plugin", zmod_basic: "Zmod basic", stock: "Stock firmware", snapmaker: "Extended firmware", snapmaker_stock: "Stock firmware", bambu: "LAN mode (MQTT)", creality_cfs: "Stock firmware (CFS)", anycubic_ace: "LAN mode (ACE)", qidi_box: "Stock firmware (Box)", happy_hare: "Happy Hare MMU", afc: "AFC (Box Turtle, ...)", lane_data: "Moonraker lane data", spoolman: "Spoolman API"};
const MODELS = {ad5x: "Flashforge AD5X", creator5: "Flashforge Creator 5", snapmaker_u1: "Snapmaker U1", bambu: "Bambu Lab", ad5m: "Flashforge AD5M (Zmod / Forge-X)", creality: "Creality K1 / K2 / Hi", anycubic: "Anycubic Kobra (ACE)", qidi: "Qidi Plus4 / Q2 / Max4", klipper: "Klipper", spoolman: "Spoolman (no printer)"};
// The printer list is shown in alphabetical order.
const MODEL_ORDER = Object.entries(MODELS).sort((a, b) => a[1].localeCompare(b[1]));
// The U1 runs only the Snapmaker method, which is only for the U1; it cannot
// ask for the slot, so its readers are always fixed to a head. Bambu
// printers likewise have a method of their own.
const U1 = "snapmaker_u1";
const METHODS_FOR = {snapmaker_u1: ["snapmaker", "snapmaker_stock"], bambu: ["bambu"], klipper: ["happy_hare", "afc", "lane_data"], ad5m: ["lane_data"], creality: ["creality_cfs"], anycubic: ["anycubic_ace"], qidi: ["qidi_box"], spoolman: ["spoolman"]};
const FLASHFORGE_METHODS = ["ff_nfc", "zmod_basic", "stock"];
const methodsFor = model => METHODS_FOR[model] || FLASHFORGE_METHODS;
const AMS_NAMES = {1: "AMS A", 2: "AMS B", 3: "AMS C", 4: "AMS D", 0: "External spool"};
const ACE_NAMES = {1: "ACE 1", 2: "ACE 2", 3: "ACE 3", 4: "ACE 4"};
const BOX_NAMES = {1: "Box 1", 2: "Box 2", 3: "Box 3", 4: "Box 4"};
const CFS_NAMES = {1: "CFS 1", 2: "CFS 2", 3: "CFS 3", 4: "CFS 4", 0: "External spool"};
// Printers whose readers serve one unit (AMS, CFS) or the external spool.
const HAS_EXTERNAL = ["bambu", "creality"];
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
  $("version").textContent = `Version ${status.version} · ${status.board} build`;
  $("fwBoard").textContent = status.board;
}

// The newest release on GitHub, asked by the browser on "Check for updates".
// When it is newer, the button installs it: the reader downloads the file
// from GitHub itself (/api/ota), since the browser can't hand it over.
let update = null;
async function checkRelease() {
  const line = $("releaseLine"), b = $("newRelease"), btn = $("checkUpdate");
  const parse = v => (/^v?(\d+)\.(\d+)\.(\d+)/.exec(v || "") || []).slice(1).map(Number);
  update = null;
  btn.textContent = "Check for updates";
  setStatus($("otaMsg"), "");
  line.textContent = "Checking GitHub for releases…";
  b.classList.add("hidden");
  let rel;
  try {
    const r = await fetch("https://api.github.com/repos/DIEHARDave/LaneTag/releases/latest");
    if (r.status === 404) { line.textContent = "No releases on GitHub yet"; return; }
    rel = await r.json();
  } catch (e) {
    line.textContent = "Couldn't reach GitHub to check for releases";
    return;
  }
  const latest = parse(rel.tag_name), mine = parse(status.version);
  if (latest.length !== 3) { line.textContent = "Couldn't read the latest release from GitHub"; return; }
  const link = `<a href="${esc(rel.html_url)}" target="_blank" rel="noopener">${esc(rel.tag_name)}</a>`;
  if (mine.length !== 3) { line.innerHTML = `Latest release: ${link} (this is a development build, not compared)`; return; }
  const i = latest.findIndex((n, k) => n !== mine[k]);
  if (i < 0 || latest[i] < mine[i]) { line.innerHTML = `Latest release: ${link} · up to date`; return; }
  line.innerHTML = `Latest release: ${link}`;
  const asset = (rel.assets || []).find(a => a.name.endsWith(`-${status.board}.bin`));
  b.innerHTML = `Update available: ${link}` +
    (asset ? ` · or <a href="${esc(asset.browser_download_url)}">download ${esc(asset.name)}</a> and upload it below` : ` (no ${esc(status.board)} file in it)`);
  b.classList.remove("hidden");
  if (!asset) return;
  update = {tag: rel.tag_name, url: asset.browser_download_url, sha256: (asset.digest || "").replace(/^sha256:/, "")};
  btn.textContent = "Install update";
}

async function installUpdate() {
  if (!confirm(`Install ${update.tag}? The reader downloads it from GitHub (it needs internet access) and restarts; settings are kept.`)) return;
  const btn = $("checkUpdate"), msg = $("otaMsg");
  btn.disabled = true;
  let r = await api("/api/ota", {url: update.url, sha256: update.sha256});
  while (r.ok && r.running) {
    setStatus(msg, `Downloading ${update.tag}… ${r.progress}%`);
    await new Promise(res => setTimeout(res, 1000));
    // Gone after the whole file: it is already restarting.
    try { r = await api("/api/ota"); } catch (e) { r = r.progress >= 99 ? {ok: true, done: true} : {ok: false, error: "Lost the reader"}; }
  }
  if (r.ok && r.done) {
    setStatus(msg, "Updated; restarting…", true);
    setTimeout(() => location.reload(), 10000);
    return;
  }
  setStatus(msg, r.error || "The update failed", false);
  btn.disabled = false;
}
$("checkUpdate").onclick = () => update ? installUpdate() : checkRelease();

function renderTarget() {
  const sel = $("target");
  const opts = status.printers.map((p, i) => `<option value="${i}">${esc(p.name)} — ${esc(MODELS[p.model])}, ${esc(METHODS[p.method])}</option>`).join("");
  if (sel.dataset.opts !== opts) { sel.innerHTML = opts || "<option>No printers yet</option>"; sel.dataset.opts = opts; }
  sel.disabled = !status.printers.length;
  if (document.activeElement !== sel) sel.value = status.active;
  const p = status.printers[status.active];
  $("targetHint").textContent = !p ? "Add a printer on the Printers tab." :
    p.slot ? `This reader is fixed to ${slotName(p, p.slot)}: scans go straight into it.` :
    p.method === "ff_nfc" ? "Scans go straight to the printer, which asks for the slot on its screen." :
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
  $("resend").classList.toggle("hidden", !(s && p && p.method === "ff_nfc"));
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
    p && p.model === "spoolman" ? "Every scan is looked up in Spoolman." :
    nextName && slotCount(p) === 1 ? `Every scan goes into ${nextName.label}, the printer's only one.` :
    nextName ? `The next scan goes into ${nextName.label}. Click another ${p.model === U1 ? "head" : "slot, or Ask,"} to change that.` :
    "The printer asks where each scan goes. Click a slot to send every scan there instead.";
  const mark = on => on && !waiting ? "<br><em>next scan</em>" : "";
  const ask = p && p.method === "ff_nfc" ? "on the printer's screen" : "on this page";
  const tiles = waiting || (p && (p.model === U1 || slotCount(p) === 1)) ? [] : [`<button class="slot${next ? "" : " next"}" data-slot="0" ${busy ? "disabled" : ""}><i class="q">?</i><div><b>Ask</b><span>${ask}${mark(!next)}</span></div></button>`];
  for (const x of slots.slots) {
    const c = x.color ? `background:#${esc(x.color)}` : "";
    const what = p && p.model === "spoolman" ? "version " + x.material :
      [x.material && x.material !== "?" ? x.material : "", x.loaded ? "loaded" : "empty"].filter(Boolean).join(" · ");
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

let voiceSeen = 0;
async function loadVoice() {
  const v = await api("/api/voice");
  if (!v.commands) return;
  const list = $("voiceList");
  list.innerHTML = v.commands.map((c, i) => {
    const now = v.training === i;
    const state = now ? `training: take ${v.take + 1} of ${v.takes_needed}` : c.takes >= v.takes_needed ? `trained (${c.takes} takes)` : c.takes ? `${c.takes} of ${v.takes_needed} takes` : "not trained";
    const off = v.training >= 0 ? "disabled" : "";
    const more = c.takes >= v.takes_needed ? `<button class="btn" data-more="${i}" ${off}>Add ${v.takes_needed} more</button>` : "";
    return `<div class="voice-row"><b>${c.name}</b> <span class="muted">${state}</span> <button class="btn" data-train="${i}" ${off}>${c.takes ? "Train again" : "Train"}</button>${more}</div>`;
  }).join("");
  list.querySelectorAll("[data-train], [data-more]").forEach(b => b.onclick = async () => {
    const r = await api("/api/voice", b.dataset.more ? {command: +b.dataset.more, more: true} : {command: +b.dataset.train});
    if (!r.ok) setStatus($("voiceMsg"), r.error || "Could not start training", false);
    loadVoice();
  });
  $("voiceCancel").classList.toggle("hidden", v.training < 0);
  if (!v.ready) setStatus($("voiceMsg"), "Microphone: " + (v.error || "not ready"), false);
  else if (v.last && v.last.id !== voiceSeen) {
    voiceSeen = v.last.id;
    const d = v.last.distance >= 0 ? ` (distance ${v.last.distance}, next ${v.last.second}, ${v.last.frames} frames, peak ${v.last.peak})` : ` (${v.last.frames} frames, peak ${v.last.peak})`;
    setStatus($("voiceMsg"), (v.last.heard ? v.last.heard + ": " : "") + v.last.action + d, !!v.last.heard);
  }
}
$("voiceCancel").onclick = async () => { await api("/api/voice", {cancel: true}); loadVoice(); };
$("voiceClear").onclick = async () => { await api("/api/voice", {clear: -1}); loadVoice(); };

async function refresh() {
  try {
    status = await api("/api/status");
    $("voiceCard").classList.toggle("hidden", !status.voice);
    if (status.voice && $("settings").classList.contains("on")) loadVoice();
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
  if (p.model === "bambu") return +p.unit === 0 ? "external spool" : `tray ${"ABCD"[p.unit - 1]}${n}`;
  if (p.model === "ad5m") return "tool T0";
  if (p.model === "spoolman") return "Spoolman";
  if (p.model === "creality") return +p.unit === 0 ? "external spool" : `slot ${p.unit}${"ABCD"[n - 1]}`;
  if (p.model === "anycubic") return (+p.unit || 1) === 1 ? `slot ${n}` : `ACE ${p.unit} slot ${n}`;
  if (p.model === "qidi") return (+p.unit || 1) === 1 ? `slot ${n}` : `Box ${p.unit} slot ${n}`;
  if (p.model === "klipper") {
    const i = ((+p.unit || 1) - 1) * 4 + n - 1;
    return p.method === "happy_hare" ? `gate ${i}` : p.method === "afc" ? `lane${i + 1}` : `tool T${i}`;
  }
  return p.model === "creator5" ? `toolhead T${n - 1}` : p.model === U1 ? `head ${n}` : `slot ${n}`;
}

// A Bambu external spool is a single slot; a Klipper printer has what was
// found on it (its tools, or the gates/lanes of the reader's group).
function slotCount(p) {
  if ((HAS_EXTERNAL.includes(p.model) && +p.unit === 0) || p.model === "ad5m" || p.model === "spoolman") return 1;
  if (p.model === "klipper" && p.slots_found > 0) return p.slots_found;
  return 4;
}

// Only FF_NFC can ask on the printer's screen. With the other methods
// "ask" means picking the slot on this page after every scan.
function slotOptions(p) {
  const ask = p.method === "ff_nfc" ? "Ask on the printer's screen" : "Ask on this web page";
  let html = p.model === U1 || slotCount(p) === 1 ? "" : `<option value="0" ${p.slot ? "" : "selected"}>${ask}</option>`;
  for (let n = 1; n <= slotCount(p); n++) html += `<option value="${n}" ${p.slot === n ? "selected" : ""}>Always ${slotName(p, n)}</option>`;
  return html;
}

function methodOptions(p) {
  const allowed = methodsFor(p.model);
  return Object.entries(METHODS).filter(([k]) => allowed.includes(k))
    .map(([k, v]) => `<option value="${k}" ${p.method === k ? "selected" : ""}>${v}</option>`).join("");
}

// Which four trays, gates, lanes or tools a reader covers.
function unitOptions(p) {
  const names = p.model === "bambu" ? AMS_NAMES : p.model === "creality" ? CFS_NAMES : p.model === "anycubic" ? ACE_NAMES : p.model === "qidi" ? BOX_NAMES : Object.fromEntries([1, 2, 3, 4].map(u => {
    const a = (u - 1) * 4;
    return [u, p.method === "happy_hare" ? `Gates ${a}-${a + 3}` : p.method === "afc" ? `Lanes ${a + 1}-${a + 4} (in AFC's order)` : `Tools T${a}-T${a + 3}`];
  }));
  return Object.entries(names).map(([k, v]) => `<option value="${k}" ${+p.unit === +k ? "selected" : ""}>${v}</option>`).join("");
}

function printerCard(p, i) {
  const ph = {stock: 8898, bambu: 8883, creality_cfs: 9999, anycubic_ace: 18910, spoolman: 7912}[p.method] || 7125;
  if (p.unit === undefined) p.unit = 1;
  return `<div class="printer" data-i="${i}">
  <div class="grid">
    <div><label>Name</label><input data-k="name" value="${esc(p.name)}" placeholder="AD5X"></div>
    <div><label>IP address</label><input data-k="host" value="${esc(p.host)}" placeholder="192.168.1.50"></div>
    <div><label>Printer</label><select data-k="model">${MODEL_ORDER.map(([k, v]) => `<option value="${k}" ${p.model === k ? "selected" : ""}>${v}</option>`).join("")}</select></div>
    <div><label>Method</label><select data-k="method">${methodOptions(p)}</select></div>
    <div><label>Port</label><input data-k="port" type="number" value="${p.port || ""}" placeholder="${ph}"></div>
    <div class="m-slot"><label>Slot for this reader</label><select data-k="slot">${slotOptions(p)}</select></div>
    <div class="m-afc"><label>Tags naming a Spoolman spool</label><select data-k="next_lane"><option value="0">Go to the slot above</option><option value="1" ${p.next_lane ? "selected" : ""}>Go to the next lane loaded</option></select></div>
    <div class="m-spoolman"><label>Location for scanned spools</label><input data-k="location" value="${esc(p.location)}" placeholder="leave as it is"></div>
    <div class="m-spoolman"><label>Tags Spoolman does not know</label><select data-k="create"><option value="1">Add them as new spools</option><option value="0" ${p.create === false ? "selected" : ""}>Leave them out</option></select></div>
    <div class="m-stock"><label>Serial number</label><input data-k="serial" value="${esc(p.serial)}"></div>
    <div class="m-stock"><label class="code-label">Check code</label><input data-k="check_code" type="password" autocomplete="new-password" placeholder="${p.check_code_set ? "unchanged" : ""}"></div>
    <div class="m-unit"><label class="unit-label">AMS for this reader</label><select data-k="unit">${unitOptions(p)}</select></div>
    <div class="m-zmod"><label class="key-label">Moonraker API key (if needed)</label><input data-k="api_key" type="password" autocomplete="new-password" placeholder="${p.api_key_set ? "unchanged" : "none"}"></div>
  </div>
  <div class="banner slot-warn hidden">The printer's screen cannot ask where this spool goes with this method. With "Ask on this web page", nothing is sent to the printer until you pick the slot on this reader's web page to send to. So the esp32 web page has to be open in a browser when you scan. Pick a fixed slot to have every scan fill that slot by itself.</div>
  <div class="actions"><button class="btn test">Test</button><button class="btn danger remove">Remove</button><span class="msg"></span></div>
</div>`;
}

function showMethodFields(card) {
  const model = card.querySelector('[data-k="model"]').value;
  const method = card.querySelector('[data-k="method"]');
  let m = method.value;
  if (!methodsFor(model).includes(m)) m = methodsFor(model)[0];
  const unitSel = card.querySelector('[data-k="unit"]');
  let unit = +unitSel.value;
  if (!HAS_EXTERNAL.includes(model) && unit < 1) unit = 1;
  // What was found on the printer holds while it is the same printer, method
  // and group of four as saved.
  const saved = config.printers[+card.dataset.i] || {};
  const host = card.querySelector('[data-k="host"]').value.trim();
  const slots_found = saved.keep !== undefined && saved.model === model && saved.method === m &&
    +saved.unit === unit && saved.host === host ? saved.slots_found || 0 : 0;
  unitSel.innerHTML = unitOptions({model, method: m, unit});
  card.querySelector(".unit-label").textContent = model === "bambu" ? "AMS for this reader" : model === "creality" ? "CFS for this reader" : model === "anycubic" ? "ACE for this reader" : model === "qidi" ? "Box for this reader" :
    m === "happy_hare" ? "Gates for this reader" : m === "afc" ? "Lanes for this reader" : "Tools for this reader";
  method.innerHTML = methodOptions({model, method: m});
  const slot = card.querySelector('[data-k="slot"]');
  let value = +slot.value;
  // Switching to a method whose printer cannot ask: default to a fixed slot,
  // so a scan fills it without the web page.
  if ((model === U1 || m !== "ff_nfc" && card.dataset.method === "ff_nfc") && !value) value = 1;
  card.dataset.method = m;
  if (value > slotCount({model, unit, slots_found})) value = 1;
  slot.innerHTML = slotOptions({method: m, model, slot: value, unit, slots_found});
  value = +slot.value;
  card.querySelector(".slot-warn").classList.toggle("hidden", m === "ff_nfc" || value !== 0);
  card.querySelectorAll(".m-stock").forEach(e => e.classList.toggle("hidden", m !== "stock" && m !== "bambu"));
  card.querySelectorAll(".m-unit").forEach(e => e.classList.toggle("hidden", !["bambu", "creality", "anycubic", "qidi", "klipper"].includes(model)));
  card.querySelectorAll(".m-spoolman").forEach(e => e.classList.toggle("hidden", m !== "spoolman"));
  card.querySelectorAll(".m-slot").forEach(e => e.classList.toggle("hidden", m === "spoolman"));
  card.querySelectorAll(".m-afc").forEach(e => e.classList.toggle("hidden", m !== "afc"));
  card.querySelector(".key-label").textContent = m === "spoolman" ? "API key (only behind a proxy)" : "Moonraker API key (if needed)";
  card.querySelectorAll(".m-zmod").forEach(e => e.classList.toggle("hidden", ["stock", "bambu", "creality_cfs", "anycubic_ace"].includes(m)));
  card.querySelector(".code-label").textContent = m === "bambu" ? "Access code" : "Check code";
  card.querySelector('[data-k="port"]').placeholder = {stock: 8898, bambu: 8883, creality_cfs: 9999, anycubic_ace: 18910, spoolman: 7912}[m] || 7125;
}

function renderPrinters() {
  $("printerList").innerHTML = config.printers.map(printerCard).join("") || '<div class="hint">No printers yet.</div>';
  document.querySelectorAll(".printer").forEach(card => {
    showMethodFields(card);
    for (const k of ["method", "model", "slot", "unit"]) card.querySelector(`[data-k="${k}"]`).onchange = () => showMethodFields(card);
    card.querySelector(".remove").onclick = () => { collectPrinters(); config.printers.splice(+card.dataset.i, 1); renderPrinters(); };
    card.querySelector(".test").onclick = async () => {
      const msg = card.querySelector(".msg");
      if (config.printers[+card.dataset.i].keep === undefined) { msg.textContent = "Save first"; return; }
      msg.textContent = "Testing…";
      const r = await api("/api/test", {printer: config.printers[+card.dataset.i].keep});
      msg.textContent = r.message || r.error; msg.className = "msg " + (r.ok ? "ok" : "bad");
      msg.style.color = r.ok ? "var(--ok)" : "var(--bad)";
      if (r.slots_found !== undefined) { config.printers[+card.dataset.i].slots_found = r.slots_found; showMethodFields(card); }
    };
  });
}

function collectPrinters() {
  document.querySelectorAll(".printer").forEach(card => {
    const p = config.printers[+card.dataset.i];
    card.querySelectorAll("[data-k]").forEach(e => {
      const k = e.dataset.k;
      if (k === "check_code" || k === "api_key") { if (e.value) p[k] = e.value; else delete p[k]; }
      else if (k === "create" || k === "next_lane") p[k] = e.value === "1";
      else if (k === "port" || k === "slot" || k === "unit") p[k] = +e.value || 0;
      else p[k] = e.value.trim();
    });
  });
}

$("addPrinter").onclick = () => {
  collectPrinters();
  config.printers.push({name: "", host: "", model: "ad5x", method: "ff_nfc", port: 0, serial: ""});
  renderPrinters();
};
// Printers found on the network, each with a button that fills a new card.
let found = [];
function renderFound(d) {
  found = d.printers || [];
  const head = d.running ? `Searching ${esc(d.subnet || "the network")}… ${d.progress}%` :
    d.error ? esc(d.error) : found.length ? `Found on ${esc(d.subnet)}:` : `No printers found on ${esc(d.subnet)}. Printers that are off, asleep or on another network do not show up; add those by hand.`;
  $("foundList").innerHTML = `<div class="hint">${head}</div>` + found.map((f, i) => {
    const have = config.printers.some(p => p.host === f.host && (p.model === "spoolman") === (f.model === "spoolman"));
    const what = `${esc(MODELS[f.model])}${f.supported ? ", " + esc(METHODS[f.method]) : ""} · ${esc(f.host)}`;
    const btn = !f.supported ? "" : have ? '<span class="muted">added</span>' : `<button class="btn add-found" data-f="${i}">Add</button>`;
    return `<div class="found"><div><b>${esc(f.name)}</b><span class="muted">${what}</span>${f.note ? `<div class="hint">${esc(f.note)}</div>` : ""}</div>${btn}</div>`;
  }).join("");
  document.querySelectorAll(".add-found").forEach(b => b.onclick = () => {
    const f = found[+b.dataset.f];
    collectPrinters();
    config.printers.push({name: f.name, host: f.host, model: f.model, method: f.method, port: 0, serial: f.serial || "", unit: f.unit ?? 1, slot: 0});
    renderPrinters();
    renderFound(d);
    setStatus($("printersMsg"), `${f.name} added below the others: check it, then Save`, true);
  });
}
$("findPrinters").onclick = async () => {
  $("findPrinters").disabled = true;
  let d = await api("/api/discover", {});
  while (d.ok !== false && d.running) {
    renderFound(d);
    await new Promise(r => setTimeout(r, 1000));
    d = await api("/api/discover");
  }
  renderFound(d);
  $("findPrinters").disabled = false;
};
$("savePrinters").onclick = async () => {
  collectPrinters();
  const r = await api("/api/config", {printers: config.printers.map(p => {
    const o = {keep: p.keep ?? -1, name: p.name, host: p.host, model: p.model, method: p.method, port: p.port, slot: p.slot || 0, unit: p.unit ?? 1, serial: p.serial};
    if (p.check_code) o.check_code = p.check_code;
    if (p.api_key) o.api_key = p.api_key;
    if (p.model === "spoolman") { o.create = p.create !== false; o.location = p.location || ""; }
    if (p.method === "afc") o.next_lane = !!p.next_lane;
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

// An RGB LED or WS2812 shows blue for a read, green when the printer took
// it, red for a failure, yellow while a slot has to be picked.
function showLed() {
  const type = $("ledType").value;
  document.querySelectorAll(".led-rgb").forEach(e => e.classList.toggle("hidden", type !== "rgb"));
  document.querySelectorAll(".led-pins").forEach(e => e.classList.toggle("hidden", type === "ws2812"));
  $("ledPinLabel").textContent = type === "rgb" ? "Red pin (-1: none)" : type === "ws2812" ? "Data pin (-1: none)" : "LED pin (-1: none)";
}
$("ledType").onchange = showLed;
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
  $("ledType").value = config.led_type || "single";
  $("ledPin").value = config.led_pin;
  $("ledGreen").value = config.led_green_pin;
  $("ledBlue").value = config.led_blue_pin;
  $("ledLow").value = config.led_active_low ? 1 : 0;
  showLed();
  $("buzzerPin").value = config.buzzer_pin;
  $("buttonPin").value = config.button_pin;
  $("sleepMinutes").value = config.sleep_minutes;
  const d = config.display || {};
  $("dispType").value = d.type || "none";
  $("dispSda").value = d.sda;
  $("dispScl").value = d.scl;
  $("dispAddr").value = String(d.address || 60);
  $("dispFlip").value = d.flip ? 1 : 0;
  $("dispSleep").value = d.sleep ?? 10;
  $("dispLogo").value = d.boot_logo === false ? 0 : 1;
  $("smReport").value = config.spoolman_report || "";
  showDisplay();
  showBus();
}

function displaySettings() {
  return {type: $("dispType").value, sda: +$("dispSda").value, scl: +$("dispScl").value,
    address: +$("dispAddr").value, flip: $("dispFlip").value === "1", sleep: +$("dispSleep").value,
    boot_logo: $("dispLogo").value === "1"};
}
function showDisplay() {
  const type = $("dispType").value;
  document.querySelectorAll(".disp").forEach(e => e.classList.toggle("hidden", type === "none" ||
    (e.classList.contains("oled") && (type.startsWith("cyd") || type === "c6-lcd147" || type === "lcd19" || type === "lcd114" || type === "c6-lcd169")) ||
    (e.classList.contains("flip") && type === "lcd1602")));
  const d = status && status.display;
  setStatus($("dispMsg"), d && d.error ? d.error : "", d ? !d.error : undefined);
}
$("dispType").onchange = () => {
  // OLEDs answer at 0x3C/0x3D, LCD backpacks at 0x27/0x3F.
  const lcd = $("dispType").value === "lcd1602", addr = +$("dispAddr").value;
  if (lcd && addr >= 60) $("dispAddr").value = "39";
  if (!lcd && addr < 60) $("dispAddr").value = "60";
  showDisplay();
};
$("saveReport").onclick = async () => {
  const r = await api("/api/config", {spoolman_report: $("smReport").value.trim()});
  setStatus($("reportMsg"), r.ok ? "Saved" : r.error, r.ok);
};
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
    led_type: $("ledType").value, led_pin: +$("ledPin").value, led_green_pin: +$("ledGreen").value,
    led_blue_pin: +$("ledBlue").value, led_active_low: $("ledLow").value === "1", buzzer_pin: +$("buzzerPin").value,
    button_pin: +$("buttonPin").value, sleep_minutes: +$("sleepMinutes").value, display: displaySettings(),
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
