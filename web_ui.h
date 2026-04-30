// web_ui.h — Soccer Wall web interface
// Auto-served by ESP32 at http://192.168.4.1
// Communicates with firmware via WebSocket at ws://192.168.4.1/ws

#pragma once
#include <pgmspace.h>

const char SOCCER_WALL_HTML[] PROGMEM = R"rawhtml(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1, maximum-scale=1, user-scalable=no">
<title>Soccer Wall</title>
<style>
*{box-sizing:border-box;margin:0;padding:0;-webkit-tap-highlight-color:transparent}
html,body{height:100%;background:#0f0f1a;color:#e2e8f0;font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;overflow:hidden}
body{display:flex;flex-direction:column;max-width:420px;margin:0 auto}

/* Status bar */
#status-bar{display:flex;justify-content:space-between;align-items:center;padding:10px 16px;font-size:12px;color:#94a3b8;flex-shrink:0}
#conn-indicator{display:flex;align-items:center;gap:6px}
#conn-dot{width:9px;height:9px;border-radius:50%;background:#ef4444;transition:background 0.3s}
#conn-dot.on{background:#22c55e}

/* E-stop (persistent small) */
#estop-small{background:#7f1d1d;color:#fca5a5;border:none;border-radius:8px;padding:6px 14px;font-size:12px;font-weight:600;cursor:pointer;letter-spacing:0.5px}
#estop-small:active{opacity:0.75}

/* Screen container */
#screens{flex:1;overflow:hidden;position:relative}
.screen{position:absolute;top:0;left:0;right:0;bottom:0;display:flex;flex-direction:column;padding:12px 16px;gap:10px;overflow-y:auto;opacity:0;pointer-events:none;transition:opacity 0.15s}
.screen.active{opacity:1;pointer-events:auto}

/* Screen title */
.screen-title{font-size:11px;font-weight:600;color:#64748b;text-transform:uppercase;letter-spacing:1.5px;margin-bottom:2px}

/* Back button */
.back-btn{background:none;border:0.5px solid #334155;color:#94a3b8;border-radius:9px;padding:7px 14px;font-size:13px;cursor:pointer;display:inline-flex;align-items:center;gap:5px;width:fit-content}
.back-btn:active{background:#1e293b}

/* Jump button */
#jump-btn{width:100%;aspect-ratio:1;max-height:220px;background:#1d4ed8;color:#fff;border:none;border-radius:22px;display:flex;flex-direction:column;align-items:center;justify-content:center;gap:6px;cursor:pointer;transition:transform 0.1s,background 0.2s}
#jump-btn:active{transform:scale(0.96)}
#jump-btn.disabled{background:#1e293b;color:#4b5563;cursor:default}
#jump-btn.running{background:#166534}
#jump-label{font-size:36px;font-weight:700;letter-spacing:3px}
#jump-sub{font-size:12px;opacity:0.65}

/* Big e-stop (jump screen) */
#estop-big{width:100%;background:#dc2626;color:#fff;border:none;border-radius:16px;padding:18px;font-size:18px;font-weight:600;cursor:pointer;display:none;margin-top:auto;flex-shrink:0}
#estop-big:active{opacity:0.85}

/* Banner cards */
.banner{border-radius:12px;padding:12px 14px;display:flex;align-items:flex-start;gap:10px;font-size:13px}
.banner.warn{background:#1c1917;border:0.5px solid #d97706;color:#fcd34d}
.banner.danger{background:#1c0a0a;border:0.5px solid #ef4444;color:#fca5a5}
.banner.info{background:#0d1b2a;border:0.5px solid #1e40af;color:#93c5fd}
.banner button{margin-left:auto;background:#1d4ed8;color:#fff;border:none;border-radius:8px;padding:6px 12px;font-size:12px;font-weight:600;cursor:pointer;white-space:nowrap;flex-shrink:0}

/* Menu buttons */
.menu-btn{width:100%;background:#1e293b;color:#e2e8f0;border:0.5px solid #334155;border-radius:14px;padding:14px 16px;font-size:14px;font-weight:500;cursor:pointer;text-align:left;display:flex;align-items:center;justify-content:space-between}
.menu-btn:active{background:#293f5c}
.menu-btn .title{font-size:15px;color:#e2e8f0}
.menu-btn .sub{font-size:12px;color:#64748b;margin-top:3px}

/* Cards */
.card{background:#0d1b2a;border:0.5px solid #1e3a5f;border-radius:12px;padding:12px 14px}
.card .lbl{font-size:11px;color:#64748b;margin-bottom:3px}
.card .val{font-size:14px;color:#93c5fd;font-weight:500}
.card .val.green{color:#22c55e}

/* Position display */
.pos-row{display:flex;gap:8px}
.pos-cell{flex:1;background:#0d1b2a;border:0.5px solid #1e3a5f;border-radius:10px;padding:10px;text-align:center}
.pos-cell .lbl{font-size:10px;color:#64748b;margin-bottom:2px}
.pos-cell .val{font-size:18px;color:#22c55e;font-weight:600;font-family:monospace}

/* Action buttons */
.action-btn{width:100%;background:#1e293b;color:#e2e8f0;border:0.5px solid #334155;border-radius:12px;padding:14px;font-size:15px;font-weight:500;cursor:pointer;text-align:center}
.action-btn:active{background:#334155}
.action-btn.primary{background:#1d4ed8;color:#fff;border-color:#2563eb}
.action-btn:disabled{opacity:0.4;cursor:default}

/* Motion control grid */
.move-grid{display:grid;grid-template-columns:1fr 1fr;gap:8px}
.move-btn{background:#1e293b;color:#e2e8f0;border:0.5px solid #334155;border-radius:12px;padding:14px 8px;font-size:14px;font-weight:600;cursor:pointer;text-align:center}
.move-btn.pos{color:#86efac;border-color:#166534}
.move-btn.neg{color:#fca5a5;border-color:#7f1d1d}
.move-btn:active{opacity:0.65}

/* Actuator section */
.actuator-section{background:#0d1b2a;border:0.5px solid #1e3a5f;border-radius:12px;padding:12px}
.section-title{font-size:11px;font-weight:600;color:#64748b;text-transform:uppercase;letter-spacing:1px;margin-bottom:10px}

/* Diagnostics */
.diag-row{display:flex;justify-content:space-between;align-items:center;padding:7px 0;border-bottom:0.5px solid #1e293b}
.diag-row:last-child{border-bottom:none}
.diag-lbl{font-size:12px;color:#64748b}
.diag-val{font-size:12px;color:#93c5fd;font-weight:500;font-family:monospace}

/* Progress bar */
.progress-wrap{background:#1e293b;border-radius:6px;height:8px;overflow:hidden;margin-top:8px}
.progress-fill{background:#22c55e;height:100%;border-radius:6px;width:0%;transition:width 0.3s}

/* E-stop screen */
#screen-estop{background:#0f0f1a;text-align:center;justify-content:center;align-items:center}

/* Toast */
#toast{position:fixed;bottom:16px;left:50%;transform:translateX(-50%);background:#1e293b;border:0.5px solid #334155;border-radius:10px;padding:9px 18px;font-size:13px;color:#e2e8f0;opacity:0;transition:opacity 0.25s;pointer-events:none;white-space:nowrap;z-index:100}
#toast.show{opacity:1}
</style>
</head>
<body>

<!-- Status bar (always visible) -->
<div id="status-bar">
  <div id="conn-indicator">
    <div id="conn-dot"></div>
    <span id="conn-label">Connecting...</span>
  </div>
  <button id="estop-small" onclick="sendCmd('estop')">&#9724; E-STOP</button>
</div>

<!-- Screens -->
<div id="screens">

  <!-- HOME SCREEN -->
  <div class="screen active" id="screen-home">
    <div id="banner-homing" class="banner warn" style="display:none">
      <div style="flex:1">System not homed — home before first jump</div>
      <button onclick="goTo('screen-home-mode')">Home now</button>
    </div>
    <div id="banner-estop" class="banner danger" style="display:none">
      <div>
        <div style="font-weight:600">E-stop activated</div>
        <div style="font-size:12px;margin-top:3px;color:#f87171">Re-home before jumping</div>
      </div>
      <button onclick="goTo('screen-home-mode')" style="background:#78350f;border:none;border-radius:8px;padding:6px 12px;font-size:12px;font-weight:600;cursor:pointer;color:#fcd34d">Home now</button>
    </div>
    <div id="banner-fault" class="banner danger" style="display:none">
      <div>
        <div style="font-weight:600">Drive fault detected</div>
        <div id="fault-detail" style="font-size:12px;margin-top:3px;color:#f87171"></div>
      </div>
    </div>
	<div id="banner-offline" class="banner warn" style="display:none">
      <div style="flex:1">
        <div style="font-weight:600">Right drive offline</div>
        <div style="font-size:12px;margin-top:3px">Only left actuator will move. Proceed?</div>
      </div>
      <button onclick="sendCmd('ack_offline')">Proceed</button>
    </div>

    <button id="jump-btn" class="disabled" onclick="handleJump()">
      <div id="jump-label">JUMP</div>
      <div id="jump-sub">home required</div>
    </button>

    <div class="pos-row">
      <div class="pos-cell"><div class="lbl">Left actuator</div><div class="val" id="pos-left">0 mm</div></div>
      <div class="pos-cell"><div class="lbl">Right actuator</div><div class="val" id="pos-right">0 mm</div></div>
    </div>

    <button class="menu-btn" onclick="goTo('screen-menu')">
      <div><div class="title">More options</div><div class="sub">Home, Manual, Calibrate</div></div>
      <span style="color:#475569;font-size:20px">&#8250;</span>
    </button>

    <button id="estop-big" onclick="sendCmd('estop')">&#9724; EMERGENCY STOP</button>
  </div>

  <!-- MENU SCREEN -->
  <div class="screen" id="screen-menu">
    <button class="back-btn" onclick="goTo('screen-home')">&#8249; Back</button>
    <div class="screen-title">Options</div>

    <button class="menu-btn" onclick="goTo('screen-home-mode')">
      <div><div class="title">Home</div><div class="sub">Return structure to home position</div></div>
      <span style="color:#475569;font-size:20px">&#8250;</span>
    </button>
    <button class="menu-btn" onclick="goTo('screen-manual')">
      <div><div class="title">Manual</div><div class="sub">Move to arbitrary position</div></div>
      <span style="color:#475569;font-size:20px">&#8250;</span>
    </button>
    <button class="menu-btn" onclick="goTo('screen-calibrate')">
      <div><div class="title">Calibrate</div><div class="sub">Independent adjustment and diagnostics</div></div>
      <span style="color:#475569;font-size:20px">&#8250;</span>
    </button>
  </div>

  <!-- HOME MODE SCREEN -->
  <div class="screen" id="screen-home-mode">
    <button class="back-btn" onclick="goTo('screen-menu')">&#8249; Back</button>
    <div class="screen-title">Home</div>

    <div class="card">
      <div class="lbl">Homing status</div>
      <div class="val" id="homing-status-text">Not homed</div>
    </div>
    <div class="card">
      <div class="lbl">Method</div>
      <div class="val">Hit-and-stop (torque surge)</div>
    </div>

    <div id="homing-progress-card" class="card" style="display:none">
      <div class="lbl">Progress</div>
      <div class="val" id="homing-progress-label">Driving to end of travel...</div>
      <div class="progress-wrap"><div class="progress-fill" id="homing-progress-fill"></div></div>
    </div>

    <div style="font-size:12px;color:#475569;line-height:1.6">
      Actuators drive to full retraction until a torque surge is detected. That position becomes home (zero).
    </div>

    <button class="action-btn primary" id="start-home-btn" onclick="sendCmd('home')">Start homing</button>
  </div>

  <!-- MANUAL SCREEN -->
  <div class="screen" id="screen-manual">
    <button class="back-btn" onclick="goTo('screen-menu')">&#8249; Back</button>
    <div class="screen-title">Manual</div>

    <div class="pos-row">
      <div class="pos-cell"><div class="lbl">Left</div><div class="val" id="pos-left-manual">0 mm</div></div>
      <div class="pos-cell"><div class="lbl">Right</div><div class="val" id="pos-right-manual">0 mm</div></div>
    </div>

    <div style="font-size:12px;color:#64748b">Both actuators move together</div>

    <div class="move-grid">
      <button class="move-btn pos" onclick="sendManual(50)">+50 mm</button>
      <button class="move-btn neg" onclick="sendManual(-50)">-50 mm</button>
      <button class="move-btn pos" onclick="sendManual(10)">+10 mm</button>
      <button class="move-btn neg" onclick="sendManual(-10)">-10 mm</button>
    </div>
  </div>

  <!-- CALIBRATE SCREEN -->
  <div class="screen" id="screen-calibrate">
    <button class="back-btn" onclick="goTo('screen-menu')">&#8249; Back</button>
    <div class="screen-title">Calibrate</div>

    <div class="actuator-section">
      <div class="section-title">Left actuator</div>
      <div class="pos-row" style="margin-bottom:10px">
        <div class="pos-cell"><div class="lbl">Position</div><div class="val" id="pos-left-cal">0 mm</div></div>
        <div class="pos-cell"><div class="lbl">Speed</div><div class="val" id="spd-left-cal">0 rpm</div></div>
      </div>
      <div class="move-grid">
        <button class="move-btn pos" onclick="sendCal('left',20)">+20 mm</button>
        <button class="move-btn neg" onclick="sendCal('left',-20)">-20 mm</button>
        <button class="move-btn pos" onclick="sendCal('left',1)">+1 mm</button>
        <button class="move-btn neg" onclick="sendCal('left',-1)">-1 mm</button>
      </div>
    </div>

    <div class="actuator-section">
      <div class="section-title">Right actuator</div>
      <div class="pos-row" style="margin-bottom:10px">
        <div class="pos-cell"><div class="lbl">Position</div><div class="val" id="pos-right-cal">0 mm</div></div>
        <div class="pos-cell"><div class="lbl">Speed</div><div class="val" id="spd-right-cal">0 rpm</div></div>
      </div>
      <div class="move-grid">
        <button class="move-btn pos" onclick="sendCal('right',20)">+20 mm</button>
        <button class="move-btn neg" onclick="sendCal('right',-20)">-20 mm</button>
        <button class="move-btn pos" onclick="sendCal('right',1)">+1 mm</button>
        <button class="move-btn neg" onclick="sendCal('right',-1)">-1 mm</button>
      </div>
    </div>

    <div class="actuator-section">
      <div class="section-title">Drive 1 (Left) diagnostics</div>
      <div class="diag-row"><span class="diag-lbl">Modbus address</span><span class="diag-val">1</span></div>
      <div class="diag-row"><span class="diag-lbl">Baud rate</span><span class="diag-val">57600</span></div>
      <div class="diag-row"><span class="diag-lbl">Data format</span><span class="diag-val">8N2</span></div>
      <div class="diag-row"><span class="diag-lbl">Status word</span><span class="diag-val" id="diag-status-l">--</span></div>
      <div class="diag-row"><span class="diag-lbl">Actual speed</span><span class="diag-val" id="diag-speed-l">--</span></div>
      <div class="diag-row"><span class="diag-lbl">Load rate</span><span class="diag-val" id="diag-load-l">--</span></div>
      <div class="diag-row"><span class="diag-lbl">Fault</span><span class="diag-val" id="diag-fault-l">--</span></div>
    </div>

    <div class="actuator-section">
      <div class="section-title">Drive 2 (Right) diagnostics</div>
      <div class="diag-row"><span class="diag-lbl">Modbus address</span><span class="diag-val">2</span></div>
      <div class="diag-row"><span class="diag-lbl">Baud rate</span><span class="diag-val">57600</span></div>
      <div class="diag-row"><span class="diag-lbl">Data format</span><span class="diag-val">8N2</span></div>
      <div class="diag-row"><span class="diag-lbl">Status word</span><span class="diag-val" id="diag-status-r">--</span></div>
      <div class="diag-row"><span class="diag-lbl">Actual speed</span><span class="diag-val" id="diag-speed-r">--</span></div>
      <div class="diag-row"><span class="diag-lbl">Load rate</span><span class="diag-val" id="diag-load-r">--</span></div>
      <div class="diag-row"><span class="diag-lbl">Fault</span><span class="diag-val" id="diag-fault-r">--</span></div>
    </div>
  </div>

  <!-- E-STOP SCREEN -->
  <div class="screen" id="screen-estop">
    <div style="font-size:56px;margin-bottom:12px">&#9940;</div>
    <div style="font-size:24px;color:#ef4444;font-weight:600;margin-bottom:8px">Emergency stop</div>
    <div style="font-size:14px;color:#94a3b8;line-height:1.6;margin-bottom:20px;max-width:280px">
      All motion stopped. Drive control released. The structure is safe to access.
    </div>
    <div class="banner warn" style="width:100%;margin-bottom:20px">
      <div>
        <div style="font-weight:600">Re-homing required</div>
        <div style="font-size:12px;margin-top:3px">Home the system before jumping again.</div>
      </div>
    </div>
    <button class="action-btn primary" style="width:280px" onclick="clearEstop()">Continue to home screen</button>
  </div>

</div><!-- /screens -->

<div id="toast"></div>

<script>
var ws = null;
var state = {
  connected: false,
  homed: false,
  estop: false,
  faultLeft: false,
  faultRight: false,
  posLeft: 0,
  posRight: 0,
  speedLeft: 0,
  speedRight: 0,
  loadLeft: 0,
  loadRight: 0,
  statusLeft: '--',
  statusRight: '--',
  motionState: 'idle',
  rightOnline: true,
  rightAcknowledged: true
};
var currentScreen = 'screen-home';
var toastTimer = null;
var homingAnim = null;
var homingPct = 0;

function goTo(id) {
  document.querySelectorAll('.screen').forEach(function(s) {
    s.classList.remove('active');
  });
  document.getElementById(id).classList.add('active');
  currentScreen = id;
}

function showToast(msg) {
  var t = document.getElementById('toast');
  t.textContent = msg;
  t.classList.add('show');
  if (toastTimer) clearTimeout(toastTimer);
  toastTimer = setTimeout(function() { t.classList.remove('show'); }, 2000);
}

function sendCmd(cmd, extra) {
  if (!ws || ws.readyState !== WebSocket.OPEN) {
    showToast('Not connected');
    return;
  }
  var msg = Object.assign({ cmd: cmd }, extra || {});
  ws.send(JSON.stringify(msg));
}

function handleJump() {
  if (state.estop || !state.homed) return;
  if (state.motionState !== 'idle') return;
  sendCmd('jump');
}

function sendManual(delta) {
  sendCmd('manual', { delta: delta });
  showToast((delta > 0 ? '+' : '') + delta + ' mm');
}

function sendCal(side, delta) {
  sendCmd('cal', { side: side, delta: delta });
  showToast(side + ': ' + (delta > 0 ? '+' : '') + delta + ' mm');
}

function clearEstop() {
  sendCmd('clear_estop');
  goTo('screen-home');
}

function updateUI() {
  // Connection indicator
  var dot = document.getElementById('conn-dot');
  var lbl = document.getElementById('conn-label');
  dot.className = state.connected ? 'on' : '';
  lbl.textContent = state.connected ? 'SOCCER_WALL' : 'Disconnected';

  // Positions
  document.getElementById('pos-left').textContent   = state.posLeft + ' mm';
  document.getElementById('pos-right').textContent  = state.posRight + ' mm';
  document.getElementById('pos-left-manual').textContent  = state.posLeft + ' mm';
  document.getElementById('pos-right-manual').textContent = state.posRight + ' mm';
  document.getElementById('pos-left-cal').textContent  = state.posLeft + ' mm';
  document.getElementById('pos-right-cal').textContent = state.posRight + ' mm';
  document.getElementById('spd-left-cal').textContent  = state.speedLeft + ' rpm';
  document.getElementById('spd-right-cal').textContent = state.speedRight + ' rpm';

  // Diagnostics
  document.getElementById('diag-status-l').textContent = state.statusLeft;
  document.getElementById('diag-status-r').textContent = state.statusRight;
  document.getElementById('diag-speed-l').textContent  = state.speedLeft + ' rpm';
  document.getElementById('diag-speed-r').textContent  = state.speedRight + ' rpm';
  document.getElementById('diag-load-l').textContent   = state.loadLeft + '%';
  document.getElementById('diag-load-r').textContent   = state.loadRight + '%';
  document.getElementById('diag-fault-l').textContent  = state.faultLeft  ? 'FAULT' : 'OK';
  document.getElementById('diag-fault-r').textContent  = state.faultRight ? 'FAULT' : 'OK';
  document.getElementById('diag-fault-l').style.color  = state.faultLeft  ? '#ef4444' : '#22c55e';
  document.getElementById('diag-fault-r').style.color  = state.faultRight ? '#ef4444' : '#22c55e';

  // Homing status
  document.getElementById('homing-status-text').textContent = state.homed ? 'Homed \u2713' : 'Not homed since power-on';
  document.getElementById('homing-status-text').style.color = state.homed ? '#22c55e' : '#fcd34d';

  // Banners
  document.getElementById('banner-homing').style.display =
    (!state.homed && !state.estop && !state.faultLeft && !state.faultRight) ? 'flex' : 'none';
  document.getElementById('banner-estop').style.display  = state.estop ? 'flex' : 'none';
  document.getElementById('banner-offline').style.display =
      (!state.rightOnline && !state.rightAcknowledged) ? 'flex' : 'none';
  var faultBanner = document.getElementById('banner-fault');
  if (state.faultLeft || state.faultRight) {
    faultBanner.style.display = 'flex';
    var parts = [];
    if (state.faultLeft)  parts.push('Left: ' + state.statusLeft);
    if (state.faultRight) parts.push('Right: ' + state.statusRight);
    document.getElementById('fault-detail').textContent = parts.join('  ');
  } else {
    faultBanner.style.display = 'none';
  }

  // Jump button
  var jumping = (state.motionState === 'jump_extend' || state.motionState === 'jump_retract');
  var jBtn = document.getElementById('jump-btn');
  var jLbl = document.getElementById('jump-label');
  var jSub = document.getElementById('jump-sub');
  var jBig = document.getElementById('estop-big');

  jBig.style.display = jumping ? 'block' : 'none';

  if (state.estop) {
    jBtn.className = 'disabled';
    jLbl.textContent = 'JUMP';
    jSub.textContent = 'e-stop active';
  } else if (!state.homed) {
    jBtn.className = 'disabled';
    jLbl.textContent = 'JUMP';
    jSub.textContent = 'home required';
  } else if (jumping) {
    jBtn.className = 'running';
    jLbl.textContent = state.motionState === 'jump_extend' ? 'JUMPING' : 'LANDING';
    jSub.textContent = 'sequence in progress\u2026';
  } else {
    jBtn.className = '';
    jLbl.textContent = 'JUMP';
    jSub.textContent = 'tap to activate';
  }

  // Homing progress animation
  if (state.motionState === 'homing') {
    document.getElementById('homing-progress-card').style.display = 'block';
    if (!homingAnim) {
      homingPct = 0;
      homingAnim = setInterval(function() {
        homingPct = Math.min(homingPct + 1.5, 95);
        document.getElementById('homing-progress-fill').style.width = homingPct + '%';
        var lbl = homingPct < 60 ? 'Driving to end of travel\u2026'
                : homingPct < 80 ? 'Stall detected \u2014 confirming\u2026'
                : 'Setting home position\u2026';
        document.getElementById('homing-progress-label').textContent = lbl;
      }, 200);
    }
  } else {
    if (homingAnim) {
      clearInterval(homingAnim);
      homingAnim = null;
    }
    if (state.homed) {
      document.getElementById('homing-progress-fill').style.width = '100%';
      document.getElementById('homing-progress-label').textContent = 'Complete \u2713';
    }
  }

  // Route to estop screen automatically
  if (state.estop && currentScreen !== 'screen-estop') {
    goTo('screen-estop');
  }
}

// WebSocket connection
function connect() {
  var host = window.location.hostname || '192.168.4.1';
  ws = new WebSocket('ws://' + host + '/ws');

  ws.onopen = function() {
    state.connected = true;
    updateUI();
  };

  ws.onclose = function() {
    state.connected = false;
    updateUI();
    setTimeout(connect, 2000);
  };

  ws.onerror = function() {
    state.connected = false;
    updateUI();
  };

  ws.onmessage = function(evt) {
    try {
      var msg = JSON.parse(evt.data);
      if (msg.type !== 'status') return;
      state.homed        = msg.homed;
      state.estop        = msg.estop;
      state.faultLeft    = msg.faultLeft;
      state.faultRight   = msg.faultRight;
      state.posLeft      = msg.posLeft  || 0;
      state.posRight     = msg.posRight || 0;
      state.speedLeft    = msg.speedLeft  || 0;
      state.speedRight   = msg.speedRight || 0;
      state.loadLeft     = msg.loadLeft   || 0;
      state.loadRight    = msg.loadRight  || 0;
      state.statusLeft   = msg.statusLeft  || '--';
      state.statusRight  = msg.statusRight || '--';
      state.motionState  = msg.state || 'idle';
	  state.rightOnline       = msg.rightOnline !== false;
      state.rightAcknowledged = msg.rightAcknowledged !== false;
      updateUI();
    } catch(e) {}
  };
}

connect();
</script>
</body>
</html>
)rawhtml";
