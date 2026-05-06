'use strict';
import {Component, h, html, render, useEffect, useState, useRef} from './preact.min.js';

const MSG_COLORS = {
  sent:    '#5af',   // 发送
  estop:   '#e74c3c',// 急停
  paused:  '#e67e22',// 暂停
  resumed: '#1abc9c',// 恢复
  reject:  '#ff6b6b',// 拒绝
  ok:      '#2ecc71',// 成功
  recv:    '#888',   // 一般接收
};
const Message = m => html`<span
  style="color: ${MSG_COLORS[m.message.type] || '#aaa'}; display:block;">
  ${m.message.uart ? '← ' : '→ '}${m.message.data}
</span>`;

// 根据响应文本判断颜色类型
function classifyResponse(line) {
  if (!line) return 'recv';
  // STATE 行（依状态关键词着色）
  if (/STATE:Stoping/.test(line))  return 'estop';
  if (/STATE:Pauseing/.test(line)) return 'paused';
  if (/STATE:Normal/.test(line))   return 'resumed';
  // 失败类
  if (/\bError\b/.test(line))      return 'reject';
  // 成功类：'<Cmd> OK' / 'Move OK' / 'Q:' 等
  if (/\bOK\b/.test(line))         return 'ok';
  return 'recv';
}

// 模块级稳定组件（引用不变，Preact 不会卸载/重建）。
// 关键：slider 使用 defaultValue 而非 value —— 不受控 input，Preact
// 初始化后不会再写 DOM.value。拖动期间完全由浏览器管理，不会因为父组件
// 重渲染而把滑块"弹回"到陈旧的 state 值。
// 外部需要同步（例如 !RESET）时通过 document.getElementById(id).value 直写。
const JointSlider = ({label, idx, id, min, max, initial, onInput, onCommit}) => html`
  <div class="joint-row">
    <label class="joint-label">${label}</label>
    <input type="range" step="1" min=${min} max=${max} defaultValue=${initial} class="slider"
      id=${id}
      oninput=${ev => onInput(idx, ev)}
      onchange=${ev => onCommit(idx, ev)}/>
    <span class="joint-val" id=${id + '-val'}>${initial}\u00b0</span>
  </div>`;

const App = function(props) {
  const [cfg, setCfg] = useState({tcp: {}, ws: {}, mqtt: {}});
  const [messages, setMessages] = useState([]);
  const [connected, setConnected] = useState(false);
  const [txt, setTxt] = useState('');
  // 滑块值 = 相对 HOME 的偏移（0 = HOME 位姿）
  const [j1txt, setj1Txt] = useState('0');
  const [j2txt, setj2Txt] = useState('0');
  const [j3txt, setj3Txt] = useState('0');
  const [j4txt, setj4Txt] = useState('0');
  const [j5txt, setj5Txt] = useState('0');
  const [j6txt, setj6Txt] = useState('0');
  const [velocity, setVelocity] = useState('10');
  const [handOpen, setHandOpen] = useState(false);
  // 机器人状态：'unknown' | 'normal' | 'estop' | 'paused'
  const [robotState, setRobotState] = useState('unknown');
  const [queueDepth, setQueueDepth] = useState(null);   // 队列剩余空位
  const [lastReject, setLastReject] = useState('');     // 最近一次拒绝信息
  const [curPos, setCurPos] = useState(null);
  const [cartPos, setCartPos] = useState(null);
  const [posMode, setPosMode] = useState('joint');
  const [curSpeed, setCurSpeed] = useState(null);
  const [handPct, setHandPct] = useState('0');
  const [handCur, setHandCur] = useState('1.0');
  const [rgbMode, setRgbMode] = useState('11');
  const [rgbColor, setRgbColor] = useState('#64ff64');  // 自定义模式颜色
  const [cartIn, setCartIn] = useState({x:'0', y:'0', z:'0', a:'0', b:'0', c:'0'});
  const [tuneNode, setTuneNode] = useState('1');
  const [tuneVals, setTuneVals] = useState({kp:'', kv:'', ki:'', kd:'', acc:'', speed:'', iLimit:''});
  const [diagNode, setDiagNode] = useState('all');
  const [cmdModeVal, setCmdModeVal] = useState('0');
  const [ws, setWs] = useState(null);
  const [showConfig, setShowConfig] = useState(false);
  const consoleRef = useRef(null);
  const posTimerRef = useRef(null);

  const refresh = () =>
      fetch('/api/config/get').then(r => r.json()).then(r => setCfg(r));

  const getport = (url, v) => ((url || '').match(/.*:(\d+)/) || ['', v])[1];

  const wsRef = useRef(null);

  const watchWebsocket = function() {
    var reconnect = function() {
      var port = getport(cfg.ws.url, 4002);
      var l = window.location, proto = l.protocol.replace('http', 'ws');
      var tid, url = `${proto}//${l.hostname}:${port}/ws`;
      var sock = new WebSocket(url);
      sock.onopen = () => {
        setConnected(true);
        setWs(sock);
        wsRef.current = sock;
      };
      sock.onmessage = ev => {
        // STM32 以 \r\n 终结每行；一次 onmessage 可能含多行
        var lines = String(ev.data).split(/\r?\n/).filter(s => s.length > 0);
        // Push every parsed line into the buffer; the 100ms flusher will
        // commit them in one setState batch.
        for (var i = 0; i < lines.length; i++) {
          msgBufRef.current.push({data: lines[i], uart: true, type: classifyResponse(lines[i])});
        }
        if (msgBufRef.current.length && msgFlushTimerRef.current == null) {
          msgFlushTimerRef.current = setTimeout(() => {
            msgFlushTimerRef.current = null;
            flushMessages();
          }, 100);
        }
        for (var i = 0; i < lines.length; i++) parseResponseLine(lines[i]);
      };
      sock.onclose = function() {
        clearTimeout(tid);
        tid = setTimeout(reconnect, 1000);
        setConnected(false);
        setWs(null);
        wsRef.current = null;
      };
    };
    reconnect();
  };

  // 解析单行响应，更新机器人状态 / 队列深度 / 位置
  const STATE_MAP = {
    Normal:   'normal',
    Stoping:  'estop',
    Pauseing: 'paused',
  };
  const parseResponseLine = (line) => {
    if (!line) return;

    // 位置查询: "GetJPos OK 0.00 -71.00 180.00 0.00 0.00 0.00 H:0"
    //            / "GetLPos OK x y z a b c H:0"
    var posMatch = line.match(/^(GetJPos|GetLPos) OK\s+(.+)$/);
    if (posMatch) {
      var parts = posMatch[2].trim().split(/\s+/);
      if (parts.length >= 6) {
        var hp = parts.find(p => p.startsWith('H:'));
        var pos = {
          v1: parseFloat(parts[0]), v2: parseFloat(parts[1]),
          v3: parseFloat(parts[2]), v4: parseFloat(parts[3]),
          v5: parseFloat(parts[4]), v6: parseFloat(parts[5]),
          hand: hp ? parseInt(hp.substring(2)) : -1,
        };
        if (posMatch[1] === 'GetJPos') setCurPos(pos);
        else setCartPos(pos);
      }
      return;
    }

    // 速度: "GetSpeed OK SPEED:45.00" 或子串 "SPEED:..."
    var spMatch = line.match(/SPEED:([\-\d.]+)/);
    if (spMatch) setCurSpeed(parseFloat(spMatch[1]));

    // 队列入队成功: "Move OK Q:15"
    var qMatch = line.match(/Q:(\d+)/);
    if (qMatch) {
      var qv = parseInt(qMatch[1]);
      setQueueDepth(prev => prev === qv ? prev : qv);  // 去重，避免 20Hz 重渲染
    }

    // STATE 行: "STATE:Normal QUEUE:15"
    var stateMatch = line.match(/STATE:(\w+)/);
    var queueMatch = line.match(/QUEUE:(\d+)/);
    if (queueMatch) {
      var qv2 = parseInt(queueMatch[1]);
      setQueueDepth(prev => prev === qv2 ? prev : qv2);
    }
    if (stateMatch) {
      var mapped = STATE_MAP[stateMatch[1]];
      if (mapped) setRobotState(mapped);
    }

    // 旧固件兼容：STATE:0/1/2 数字 + ESTOP|/PAUSED/RESUMED 关键字
    var legacyState = line.match(/STATE:(\d+)/);
    if (legacyState) {
      var n = parseInt(legacyState[1]);
      setRobotState(n === 0 ? 'normal' : (n === 1 ? 'estop' : (n === 2 ? 'paused' : 'unknown')));
    }
    if (/^ESTOP\|/.test(line))    setRobotState('estop');
    else if (/^PAUSED\|/.test(line)) setRobotState('paused');
    else if (/^RESUMED/.test(line))  setRobotState('normal');

    // 单独的 ack（没有后续 STATE 行的）
    if (line.startsWith('Start OK'))       { setRobotState('normal'); setLastReject(''); }
    else if (line.startsWith('Home OK'))   { setRobotState('normal'); setLastReject(''); }
    else if (line.startsWith('Reset OK'))  { setRobotState('normal'); }
    else if (line.startsWith('Close OK'))  { setRobotState('normal'); }

    // 失败行
    if (/\bError\b/.test(line))
      setLastReject(line);
  };

  // Buffered messages flush: many WS frames (esp. during slider drag) collapse
  // into ONE setMessages per 100ms, capping Preact re-renders at 10 Hz.
  const msgBufRef = useRef([]);
  const msgFlushTimerRef = useRef(null);
  const flushMessages = () => {
    var buf = msgBufRef.current;
    if (!buf.length) return;
    msgBufRef.current = [];
    setMessages(x => {
      var arr = x.concat(buf);
      return arr.length > 200 ? arr.slice(-150) : arr;
    });
  };
  const queueMessage = (m) => {
    msgBufRef.current.push(m);
    if (msgFlushTimerRef.current == null) {
      msgFlushTimerRef.current = setTimeout(() => {
        msgFlushTimerRef.current = null;
        flushMessages();
      }, 100);
    }
  };

  const wsSend = (msg) => {
    var s = wsRef.current;
    if (s && s.readyState === 1) s.send(msg + '\n');
    queueMessage({data: msg, uart: false, type: 'sent'});
  };

  useEffect(() => {
    refresh();
    watchWebsocket();
  }, []);

  // Auto-scroll console
  useEffect(() => {
    if (consoleRef.current) consoleRef.current.scrollTop = consoleRef.current.scrollHeight;
  }, [messages]);

  const sendmessage = ev => {
    wsSend(txt);
    setTxt('');
  };

  // HOME = 工作姿态（!HOME 后机械臂的目标），来自固件 DummyRobot::Homing() → MoveJ(0,0,90,0,0,0)
  // 注：[0,-75,180,0,0,0] 是 REST 零点姿态（!RESET），不是 HOME
  const HOME = [0, 0, 90, 0, 0, 0];
  // 硬件关节极限（绝对角度）
  const JOINT_LIMITS = [
    [-170, 170], [-75, 90], [35, 180],
    [-180, 180], [-100, 100], [-180, 180],
  ];
  // 偏移空间极限 = 硬件极限 - HOME
  const OFFSET_LIMITS = JOINT_LIMITS.map(([lo, hi], i) => [lo - HOME[i], hi - HOME[i]]);

  // 拖动时用 ref 跟踪每个关节的最新偏移，避免 setState 异步导致闭包读到旧值
  const lastOffsetsRef = useRef([0, 0, 0, 0, 0, 0]);

  const sendJointPos = () => {
    var offs = lastOffsetsRef.current;
    var abs  = offs.map((o, i) => (isFinite(o) ? o : 0) + HOME[i]);
    wsSend('&' + abs.join(',') + ',' + velocity);
  };

  const resetAllJoints = ev => {
    setj1Txt('0'); setj2Txt('0'); setj3Txt('0');
    setj4Txt('0'); setj5Txt('0'); setj6Txt('0');
    setVelocity('10');
    lastOffsetsRef.current = [0, 0, 0, 0, 0, 0];
    // Sliders are uncontrolled — imperatively write DOM to reflect HOME offsets
    for (var i = 1; i <= 6; i++) {
      var sl = document.getElementById('j' + i);
      var sp = document.getElementById('j' + i + '-val');
      if (sl) sl.value = '0';
      if (sp) sp.textContent = '0\u00b0';
    }
    wsSend('&' + HOME.join(',') + ',10');
  };

  const sendmessageJoint = ev => sendJointPos();

  // 节流 50ms = 20Hz。固件 MoveJ 队列大小有限（Q ≤ 15），高于此速率会
  // 导致队列爆满 + RX 端 Move OK 应答洪水，反而拖累 UI 渲染。
  // ref 里始终存的是"用户拖到的最新整数"，所以即使中间帧被丢弃，
  // 每 50ms 发出的命令仍然是当前真实位置 → 关节平滑追随。
  const jointThrottleRef = useRef({ timer: null, pending: false });
  const sendJointThrottled = () => {
    var r = jointThrottleRef.current;
    if (r.timer) { r.pending = true; return; }
    sendJointPos();
    r.timer = setTimeout(() => {
      r.timer = null;
      if (r.pending) { r.pending = false; sendJointPos(); }
    }, 50);
  };

  // Quick commands
  const cmdStart  = () => wsSend('!START');
  const cmdHome   = () => wsSend('!HOME');
  const cmdStop   = () => wsSend('!STOP');
  const cmdClose  = () => wsSend('!CLOSE');   // 原 !DISABLE 不存在于固件，改用 !CLOSE
  const cmdReset  = () => wsSend('!RESET');   // 回零点姿态（保持使能）
  const cmdGetPos = () => wsSend('#GETJPOS');
  const cmdHandOpen  = () => { wsSend('!HD_O'); setHandOpen(true); };
  const cmdHandClose = () => { wsSend('!HD_C'); setHandOpen(false); };
  const cmdPause  = () => wsSend('!PAUSE');
  const cmdResume = () => wsSend('!RESUME');

  // Hand advanced
  const cmdHandEn   = () => wsSend('!HD_EN');
  const cmdHandDis  = () => wsSend('!HD_DIS');
  const cmdHandZero = () => wsSend('!HD_ZERO');
  const cmdHandPos  = () => wsSend('!HD_POS ' + parseInt(handPct || '0'));
  const cmdHandCur  = () => wsSend('!HD_I ' + parseFloat(handCur || '1.0'));
  // RGB
  const cmdRgbOn   = () => wsSend('!RGB_ON');
  const cmdRgbOff  = () => wsSend('!RGB_OFF');
  const cmdRgbMode = () => wsSend('!RGB_MODE ' + parseInt(rgbMode || '0'));
  const cmdRgbCustom = () => {
    var h = rgbColor.replace('#', '');
    var r = parseInt(h.substring(0,2), 16) || 0;
    var g = parseInt(h.substring(2,4), 16) || 0;
    var b = parseInt(h.substring(4,6), 16) || 0;
    wsSend('!RGB_CUSTOM ' + r + ' ' + g + ' ' + b);
    setRgbMode('8');
  };
  // Queries
  const cmdGetLPos  = () => wsSend('#GETLPOS');
  const cmdGetSpeed = () => wsSend('#GETSPEED');
  // Diagnostics / node management
  const cmdCalib   = () => wsSend('!CALIBRATION');
  const cmdOnline  = () => wsSend(diagNode === 'all' ? '#ONLINE'  : ('#ONLINE '  + diagNode));
  const cmdGetDce  = () => wsSend(diagNode === 'all' ? '#GET_DCE' : ('#GET_DCE ' + diagNode));
  const cmdReboot  = () => wsSend(diagNode === 'all' ? '#REBOOT'  : ('#REBOOT '  + diagNode));
  const cmdOffsetJ = () => wsSend('#OFFSET_J ' + diagNode);
  const cmdSetMode = () => wsSend('#CMDMODE ' + parseInt(cmdModeVal || '0'));
  // Cartesian / interpolated joint
  const cmdMoveL = () => {
    var p = cartIn;
    wsSend('@' + p.x + ',' + p.y + ',' + p.z + ',' + p.a + ',' + p.b + ',' + p.c + ',' + velocity);
  };
  const cmdMoveInterp = () => {
    var offs = [j1txt, j2txt, j3txt, j4txt, j5txt, j6txt].map(parseFloat);
    var abs  = offs.map((o, i) => (isFinite(o) ? o : 0) + HOME[i]);
    wsSend('>' + abs.join(',') + ',' + velocity);
  };
  // Tuning
  const sendTune = (cmd, key) => {
    var v = tuneVals[key];
    if (v === '' || v == null) return;
    wsSend(cmd + ' ' + tuneNode + ' ' + v);
  };

  // 状态门控：ESTOP/PAUSED 下仅允许 STOP/RESUME/查询
  const isHalted = (robotState === 'estop' || robotState === 'paused');
  const stateBadge = {
    normal:  {text: 'NORMAL',  color: '#2ecc71'},
    estop:   {text: 'E-STOP',  color: '#e74c3c'},
    paused:  {text: 'PAUSED',  color: '#e67e22'},
    unknown: {text: '?',       color: '#888'},
  }[robotState] || {text: '?', color: '#888'};

  // Auto position polling
  const [autoQuery, setAutoQuery] = useState(false);
  useEffect(() => {
    if (posTimerRef.current) clearInterval(posTimerRef.current);
    if (autoQuery) {
      posTimerRef.current = setInterval(() => {
        var s = wsRef.current;
        if (s && s.readyState === 1) s.send('#GETJPOS\n');
      }, 500);
    }
    return () => { if (posTimerRef.current) clearInterval(posTimerRef.current); };
  }, [autoQuery]);

  const onchange = ev => fetch('/api/config/set', {
                           method: 'POST',
                           headers: {'Content-Type': 'application/json'},
                           body: JSON.stringify(cfg),
                         }).then(r => ws && ws.close());

  const set = obj => setCfg(x => Object.assign(x, obj));
  const nset = (n,obj) => setCfg(x => Object.assign(x, {[n]: Object.assign(x[n],obj)}));
  const setTx = ev => set({tx: parseInt(ev.target.value)});
  const setRx = ev => set({rx: parseInt(ev.target.value)});
  const setBaud = ev => set({baud: parseInt(ev.target.value)});
  const setTcpUrl = ev => nset('tcp', {url: `tcp://0.0.0.0:${ev.target.value}`});
  const setWsUrl = ev => nset('ws',{url: `ws://0.0.0.0:${ev.target.value}`});
  const setMqttUrl = ev => nset('mqtt',{url: ev.target.value});
  const setTcpEna = ev => (nset('tcp',{enable: ev.target.checked}), onchange());
  const setWsEna = ev => (nset('ws',{enable: ev.target.checked}), onchange());
  const setMqttEna = ev =>(nset('mqtt', {enable: ev.target.checked}), onchange());

  // 拖动期间 0 次 Preact 渲染：只更新 ref + 直接 DOM 写显示 span。
  // 鼠标松开（onchange）时才 setValue 同步 React 状态。
  const jointSetters = [setj1Txt, setj2Txt, setj3Txt, setj4Txt, setj5Txt, setj6Txt];

  const onJointSlideInput = (idx, ev) => {
    var raw = ev.target.value;
    var target = Math.round(parseFloat(raw));
    if (!isFinite(target)) return;
    // Direct DOM update for display — bypasses Preact entirely
    var span = ev.target.parentElement && ev.target.parentElement.querySelector('.joint-val');
    if (span) span.textContent = target + '\u00b0';
    var prev = lastOffsetsRef.current[idx];
    if (target === prev) return;
    var dir = target > prev ? 1 : -1;
    for (var cur = prev + dir; (dir > 0 ? cur <= target : cur >= target); cur += dir) {
      lastOffsetsRef.current[idx] = cur;
      sendJointThrottled();
    }
  };

  const onJointSlideChange = (idx, ev) => {
    // Drag ended: sync React state once so Reset / external updates still work.
    var target = Math.round(parseFloat(ev.target.value));
    if (isFinite(target)) jointSetters[idx](String(target));
    sendJointPos();   // 保险：确保最终一帧落地
  };

  return html`
<div class="container">
  <div class="header">
    <h1>Dummy Robot Controller</h1>
    <div style="display:flex; align-items:center; gap:0.6em;">
      <span style="font-size:12px; padding:2px 8px; border-radius:10px;
          background:${stateBadge.color}; color:#fff; font-weight:600;">
        ${stateBadge.text}
      </span>
      ${queueDepth !== null ? html`
        <span style="font-size:12px; color:#888;">Q:${queueDepth}</span>
      ` : ''}
      <span class="status-dot" style="color: ${connected ? '#0a0' : '#d00'};">
        \u25cf ${connected ? 'Online' : 'Offline'}
      </span>
    </div>
  </div>

  ${lastReject ? html`
    <div onclick=${() => setLastReject('')}
         style="position:fixed; inset:0; background:rgba(0,0,0,0.55); z-index:9999;
                display:flex; align-items:center; justify-content:center;">
      <div onclick=${ev => ev.stopPropagation()}
           style="background:#2a1a1f; border:1px solid #7a3030; border-radius:10px;
                  min-width:280px; max-width:90vw; padding:1.2em 1.4em;
                  box-shadow:0 8px 32px rgba(0,0,0,0.6); color:#ffd6d6;">
        <div style="font-size:15px; font-weight:600; color:#ff8a8a; margin-bottom:0.6em;">
          \u26A0 \u9519\u8BEF / Error
        </div>
        <div style="font-size:13px; margin-bottom:1em; white-space:pre-wrap; word-break:break-word;">
          ${lastReject}
        </div>
        <div style="text-align:right;">
          <button class="btn btn-sm btn-blue" onclick=${() => setLastReject('')}
                  style="min-width:5em;">\u786E\u5B9A OK</button>
        </div>
      </div>
    </div>
  ` : ''}

  <!-- Quick Control Buttons -->
  <div class="btn-bar">
    <button class="btn btn-green btn-2line" onclick=${cmdStart} disabled=${isHalted}><span>START</span><small>\u542F\u52A8</small></button>
    <button class="btn btn-blue btn-2line"  onclick=${cmdHome}  disabled=${isHalted}><span>HOME</span><small>\u5F52\u96F6</small></button>
    <button class="btn btn-red btn-2line"   onclick=${cmdStop}><span>E-STOP</span><small>\u6025\u505C</small></button>
    <button class="btn btn-orange btn-2line" onclick=${cmdPause}  disabled=${isHalted}><span>PAUSE</span><small>\u6682\u505C</small></button>
    <button class="btn btn-teal btn-2line"   onclick=${cmdResume}><span>RESUME</span><small>\u6062\u590D</small></button>
    <button class="btn btn-gray btn-2line"   onclick=${cmdReset}  disabled=${isHalted}><span>RESET</span><small>\u96F6\u70B9</small></button>
    <button class="btn btn-gray btn-2line"   onclick=${cmdClose}  disabled=${isHalted}><span>CLOSE</span><small>\u5173\u673A</small></button>
  </div>

  <!-- Row: Hand (merged) + RGB + Diagnostics -->
  <div class="grid-auto">
    <div class="section">
      <div class="section-title"><b>Hand Claw / \u624B\u722A</b></div>
      <div class="d-flex" style="gap:0.4em; flex-wrap:wrap;">
        <button class="btn ${handOpen ? 'btn-outline' : 'btn-green'}" onclick=${cmdHandClose}>Close</button>
        <button class="btn ${handOpen ? 'btn-green' : 'btn-outline'}" onclick=${cmdHandOpen}>Open</button>
        <button class="btn btn-sm btn-green" onclick=${cmdHandEn}>EN</button>
        <button class="btn btn-sm btn-gray"  onclick=${cmdHandDis}>DIS</button>
        <button class="btn btn-sm btn-blue"  onclick=${cmdHandZero}>Zero</button>
      </div>
      <div class="d-flex my-1" style="gap:0.4em; align-items:center; margin-top:0.5em;">
        <label class="addon" style="min-width:4em;">Pos %</label>
        <input type="range" min="0" max="100" value=${handPct} class="slider" style="flex:1;"
          oninput=${ev => setHandPct(ev.target.value)} onchange=${cmdHandPos} />
        <span style="min-width:3em; text-align:right;">${handPct}%</span>
      </div>
      <div class="d-flex my-1" style="gap:0.4em; align-items:center;">
        <label class="addon" style="min-width:4em;">Current</label>
        <input style="width:5em;" value=${handCur} oninput=${ev => setHandCur(ev.target.value)} />
        <span style="font-size:12px; color:#999;">A</span>
        <button class="btn btn-sm" onclick=${cmdHandCur}>Set</button>
      </div>
    </div>

  <!-- Current Position Display -->
  <div class="section">
    <div class="section-title">
      <b>Current Position / \u5F53\u524D\u4F4D\u7F6E</b>
      <div>
        <button class="btn btn-sm" onclick=${cmdGetPos}>Query</button>
        <label class="ml-1" style="font-size:13px;">
          <input type="checkbox" checked=${autoQuery}
            onchange=${ev => setAutoQuery(ev.target.checked)} /> Auto
        </label>
      </div>
    </div>
    <div style="display:flex; gap:0.4em; margin:0.4em 0; font-size:12px; align-items:center; flex-wrap:wrap;">
      <button class="btn btn-sm" style="background:${posMode==='joint'?'#5af':'#444'};"
        onclick=${() => { setPosMode('joint'); cmdGetPos(); }}>Joint</button>
      <button class="btn btn-sm" style="background:${posMode==='cart'?'#5af':'#444'};"
        onclick=${() => { setPosMode('cart'); cmdGetLPos(); }}>Cartesian</button>
      <button class="btn btn-sm" onclick=${cmdGetSpeed}>Speed</button>
      ${curSpeed !== null ? html`<span style="color:#9af;">Speed: <b>${curSpeed.toFixed(1)}</b>\u00b0/s</span>` : ''}
    </div>
    ${posMode === 'joint' ? html`
    <div class="pos-grid">
      <span class="pos-item">J1: <b>${curPos ? curPos.v1.toFixed(1)+'\u00b0' : '\u2014'}</b></span>
      <span class="pos-item">J2: <b>${curPos ? curPos.v2.toFixed(1)+'\u00b0' : '\u2014'}</b></span>
      <span class="pos-item">J3: <b>${curPos ? curPos.v3.toFixed(1)+'\u00b0' : '\u2014'}</b></span>
      <span class="pos-item">J4: <b>${curPos ? curPos.v4.toFixed(1)+'\u00b0' : '\u2014'}</b></span>
      <span class="pos-item">J5: <b>${curPos ? curPos.v5.toFixed(1)+'\u00b0' : '\u2014'}</b></span>
      <span class="pos-item">J6: <b>${curPos ? curPos.v6.toFixed(1)+'\u00b0' : '\u2014'}</b></span>
      <span class="pos-item">Hand: <b>${curPos && curPos.hand >= 0 ? (curPos.hand ? 'Open' : 'Closed') : '\u2014'}</b></span>
    </div>`
    : html`
    <div class="pos-grid">
      <span class="pos-item">X: <b>${cartPos ? cartPos.v1.toFixed(1) : '\u2014'}</b></span>
      <span class="pos-item">Y: <b>${cartPos ? cartPos.v2.toFixed(1) : '\u2014'}</b></span>
      <span class="pos-item">Z: <b>${cartPos ? cartPos.v3.toFixed(1) : '\u2014'}</b></span>
      <span class="pos-item">A: <b>${cartPos ? cartPos.v4.toFixed(1)+'\u00b0' : '\u2014'}</b></span>
      <span class="pos-item">B: <b>${cartPos ? cartPos.v5.toFixed(1)+'\u00b0' : '\u2014'}</b></span>
      <span class="pos-item">C: <b>${cartPos ? cartPos.v6.toFixed(1)+'\u00b0' : '\u2014'}</b></span>
      <span class="pos-item">Hand: <b>${cartPos && cartPos.hand >= 0 ? (cartPos.hand ? 'Open' : 'Closed') : '\u2014'}</b></span>
    </div>`}
  </div>
  </div><!-- /grid-auto row: Hand + Position -->

  <!-- Row: Joint Control + RGB + Diagnostics (4:1:1, equal height) -->
  <div style="display:grid; grid-template-columns: 3fr 1fr 1.3fr; gap:0.6em; align-items:stretch; margin-bottom:0.8em;">
  <div class="section" style="margin:0; min-width:0;">
    <div class="d-flex" style="justify-content: space-between; align-items: center;">
      <b>Joint Control / \u5173\u8282\u63A7\u5236</b>
      <div>
        <span style="font-size:13px;">Speed:</span>
        <input style="width:4em; font-size:13px;" value=${velocity}
          oninput=${ev => setVelocity(ev.target.value)} onchange=${sendmessageJoint} />
        <button class="btn btn-sm ml-1" onclick=${resetAllJoints}>Reset / \u590D\u4F4D</button>
      </div>
    </div>
    <${JointSlider} label="J1" idx=${0} id="j1" min=${OFFSET_LIMITS[0][0]} max=${OFFSET_LIMITS[0][1]} initial=${0} onInput=${onJointSlideInput} onCommit=${onJointSlideChange} />
    <${JointSlider} label="J2" idx=${1} id="j2" min=${OFFSET_LIMITS[1][0]} max=${OFFSET_LIMITS[1][1]} initial=${0} onInput=${onJointSlideInput} onCommit=${onJointSlideChange} />
    <${JointSlider} label="J3" idx=${2} id="j3" min=${OFFSET_LIMITS[2][0]} max=${OFFSET_LIMITS[2][1]} initial=${0} onInput=${onJointSlideInput} onCommit=${onJointSlideChange} />
    <${JointSlider} label="J4" idx=${3} id="j4" min=${OFFSET_LIMITS[3][0]} max=${OFFSET_LIMITS[3][1]} initial=${0} onInput=${onJointSlideInput} onCommit=${onJointSlideChange} />
    <${JointSlider} label="J5" idx=${4} id="j5" min=${OFFSET_LIMITS[4][0]} max=${OFFSET_LIMITS[4][1]} initial=${0} onInput=${onJointSlideInput} onCommit=${onJointSlideChange} />
    <${JointSlider} label="J6" idx=${5} id="j6" min=${OFFSET_LIMITS[5][0]} max=${OFFSET_LIMITS[5][1]} initial=${0} onInput=${onJointSlideInput} onCommit=${onJointSlideChange} />
  </div>

    <div class="section" style="margin:0; min-width:0;">
      <!-- Row 1: title + On/Off -->
      <div class="section-title" style="display:flex; align-items:center; gap:0.4em;">
        <b style="flex:1;">RGB Effect / \u706F\u6548</b>
        <button class="btn btn-sm btn-green" onclick=${cmdRgbOn}>On</button>
        <button class="btn btn-sm btn-gray"  onclick=${cmdRgbOff}>Off</button>
      </div>
      <!-- Row 2: Model dropdown + Apply -->
      <div class="d-flex my-1" style="gap:0.4em; align-items:center;">
        <select value=${rgbMode} onchange=${ev => setRgbMode(ev.target.value)} style="flex:1; min-width:0;">
          <option value="0">0 \u2014 \u5173\u95ED</option>
          <option value="1">1 \u2014 \u7EA2</option>
          <option value="2">2 \u2014 \u6A59</option>
          <option value="3">3 \u2014 \u9EC4</option>
          <option value="4">4 \u2014 \u7EFF</option>
          <option value="5">5 \u2014 \u9752</option>
          <option value="6">6 \u2014 \u84DD</option>
          <option value="7">7 \u2014 \u7D2B</option>
          <option value="8">8 \u2014 \u81EA\u5B9A\u4E49</option>
          <option value="9">9 \u2014 \u5F69\u8679</option>
          <option value="10">10 \u2014 \u547C\u5438\u706F</option>
          <option value="11">11 \u2014 \u81EA\u52A8</option>
        </select>
        <button class="btn btn-sm" onclick=${cmdRgbMode}>Apply</button>
      </div>
      <!-- Row 3: Color picker + hex + Set -->
      <div class="d-flex my-1" style="gap:0.4em; align-items:center;">
        <label class="addon" style="min-width:4em;">Color</label>
        <input type="color" value=${rgbColor}
          oninput=${ev => setRgbColor(ev.target.value)}
          style="width:3em; height:2em; padding:0; border:1px solid #444; background:transparent;" />
        <input style="flex:1; min-width:0; font-family:monospace; font-size:12px;" value=${rgbColor}
          oninput=${ev => setRgbColor(ev.target.value)} />
        <button class="btn btn-sm btn-blue" onclick=${cmdRgbCustom}>Set</button>
      </div>
    </div>

    <div class="section" style="margin:0; min-width:0;">
      <!-- Row 1: Title + Node + CmdMode (all in header line) -->
      <div class="section-title" style="display:flex; align-items:center; gap:0.6em; flex-wrap:wrap;">
        <b style="flex:0 0 auto;">Diagnostics / \u8BCA\u65AD</b>
        <div style="display:flex; align-items:center; gap:0.3em;">
          <label class="addon" style="font-size:12px;">Node</label>
          <select value=${diagNode} onchange=${ev => setDiagNode(ev.target.value)} style="font-size:12px;">
            <option value="all">all</option>
            <option value="0">0 (ref)</option>
            <option value="1">1 (J1)</option>
            <option value="2">2 (J2)</option>
            <option value="3">3 (J3)</option>
            <option value="4">4 (J4)</option>
            <option value="5">5 (J5)</option>
            <option value="6">6 (J6)</option>
            <option value="7">7 (Hand)</option>
          </select>
        </div>
        <div style="display:flex; align-items:center; gap:0.3em;">
          <label class="addon" style="font-size:12px;">CmdMode</label>
          <input style="width:3em; font-size:12px;" value=${cmdModeVal}
            oninput=${ev => setCmdModeVal(ev.target.value)} />
          <button class="btn btn-sm" onclick=${cmdSetMode}>Set</button>
        </div>
      </div>
      <!-- Row 2a: query/reboot buttons -->
      <div class="d-flex my-1" style="gap:0.4em; flex-wrap:wrap;">
        <button class="btn btn-sm" onclick=${cmdOnline}>Online</button>
        <button class="btn btn-sm" onclick=${cmdGetDce}>Get DCE</button>
        <button class="btn btn-sm btn-orange" onclick=${cmdReboot}>Reboot</button>
      </div>
      <!-- Row 2b: calibration pair -->
      <div class="d-flex my-1" style="gap:0.4em; flex-wrap:wrap;">
        <button class="btn btn-sm btn-red"  onclick=${cmdOffsetJ}>OffsetJ</button>
        <button class="btn btn-sm btn-blue" onclick=${cmdCalib}>Calibrate HOME</button>
      </div>
      <div style="font-size:11px; color:#999; margin-top:0.3em;">CmdMode: 0\u53EF\u6253\u65AD / 1\u987A\u5E8F / 2\u8FDE\u7EED</div>
    </div>

  </div><!-- /row: Joint + RGB + Diagnostics -->

  <!-- Row: DCE Tuning (2fr) + Network Config (1fr), equal height -->
  <div style="display:grid; grid-template-columns: 2fr 1fr; gap:0.6em; align-items:stretch; margin-bottom:0.8em;">
    <div class="section" style="margin:0; min-width:0;">
      <div class="section-title">
        <b>DCE Tuning / \u8C03\u53C2</b>
        <span class="hint">KP/KI/KD/KV \u4ec5 1\u20136</span>
      </div>
      <div class="d-flex my-1" style="gap:0.4em; align-items:center;">
        <label class="addon" style="min-width:4em;">Node</label>
        <select value=${tuneNode} onchange=${ev => setTuneNode(ev.target.value)}>
          <option value="1">1 (J1)</option>
          <option value="2">2 (J2)</option>
          <option value="3">3 (J3)</option>
          <option value="4">4 (J4)</option>
          <option value="5">5 (J5)</option>
          <option value="6">6 (J6)</option>
          <option value="7">7 (Hand)</option>
        </select>
      </div>
      <div style="display:grid; grid-template-columns:repeat(auto-fit, minmax(220px, 1fr)); gap:0.3em 0.6em;">
      ${[
        {key:'kp', label:'KP',     cmd:'#SET_DCE_KP'},
        {key:'kv', label:'KV',     cmd:'#SET_DCE_KV'},
        {key:'ki', label:'KI',     cmd:'#SET_DCE_KI'},
        {key:'kd', label:'KD',     cmd:'#SET_DCE_KD'},
        {key:'acc',label:'ACC_J',  cmd:'#ACC_J'},
        {key:'speed',label:'SPEED_J',cmd:'#SPEED_J'},
        {key:'iLimit',label:'I_LIMIT',cmd:'#I_LIMIT_J'},
      ].map(p => html`
        <div class="d-flex" style="gap:0.3em; align-items:center;">
          <label class="addon" style="min-width:4em; font-size:12px;">${p.label}</label>
          <input style="flex:1; min-width:3em;" value=${tuneVals[p.key]}
            oninput=${ev => setTuneVals(s => Object.assign({}, s, {[p.key]: ev.target.value}))} />
          <button class="btn btn-sm" onclick=${() => sendTune(p.cmd, p.key)}>Set</button>
        </div>
      `)}
      </div>
    </div>

    <!-- Network Config (always expanded, right 1fr) -->
    <div class="section" style="margin:0; min-width:0;">
      <div class="section-title"><b>Network Config / \u7F51\u7EDC\u914D\u7F6E</b></div>
      <div style="margin-top:0.5em;">
        <div class="d-flex my-1">
          <label class="addon">TCP port</label>
          <input style="width:5em;" value=${getport(cfg.tcp.url, 4001)} onchange=${onchange} oninput=${setTcpUrl} />
          <label class="ml-1 d-flex label"><input type="checkbox" checked=${cfg.tcp.enable} onchange=${setTcpEna} /> on</label>
        </div>
        <div class="d-flex my-1">
          <label class="addon">WS port</label>
          <input style="width:5em;" value=${getport(cfg.ws.url, 4002)} onchange=${onchange} oninput=${setWsUrl} />
          <label class="ml-1 d-flex label"><input type="checkbox" checked=${cfg.ws.enable} onchange=${setWsEna} /> on</label>
        </div>
        <div class="d-flex my-1">
          <label class="addon">MQTT</label>
          <input style="flex:1 1 auto; min-width:0;" value=${cfg.mqtt.url} onchange=${onchange} oninput=${setMqttUrl} />
          <label class="ml-1 d-flex label"><input type="checkbox" checked=${cfg.mqtt.enable} onchange=${setMqttEna} /> on</label>
        </div>
        <div class="d-flex my-1">
          <label class="addon">Baud</label>
          <input style="width:5em;" value=${cfg.baud} onchange=${onchange} oninput=${setBaud} />
          <span class="ml-1" style="color:#999; font-size:12px;">TX:${cfg.tx} RX:${cfg.rx}</span>
        </div>
      </div>
    </div>
  </div><!-- /row: DCE + Config -->

  <!-- UART Console -->
  <div class="section">
    <div class="d-flex" style="justify-content: space-between; align-items: center;">
      <b>UART Console / \u4E32\u53E3\u7EC8\u7AEF</b>
      <button class="btn btn-sm" onclick=${ev => setMessages([])}>Clear / \u6E05\u9664</button>
    </div>
    <div style="margin: 0.5em 0; display: flex">
      <input placeholder="Type command and press Enter..." style="flex: 1 100%; font-size:13px;"
        value=${txt} onchange=${sendmessage}
        oninput=${ev => setTxt(ev.target.value)} />
    </div>
    <pre ref=${consoleRef} class="console-box">
      ${messages.map(message => h(Message, {message}))}
    </pre>
  </div>


</div>`;
};

window.onload = () => render(h(App), document.body);
