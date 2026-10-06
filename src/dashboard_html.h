#pragma once

#include <Arduino.h>

static const char DASHBOARD_HTML[] PROGMEM = R"HTML(
<!doctype html>
<html lang="zh-CN">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>ESP32-S3 设备中心</title>
  <style>
    :root{color-scheme:dark;--bg:#09111f;--card:#111d30;--card2:#16243a;--text:#edf5ff;--muted:#93a4bc;--line:#24354d;--blue:#4ba3ff;--green:#39d98a;--red:#ff6577;--yellow:#ffc857}
    *{box-sizing:border-box} body{margin:0;background:radial-gradient(circle at 20% 0,#173253 0,transparent 32%),var(--bg);color:var(--text);font:14px/1.5 -apple-system,BlinkMacSystemFont,"Segoe UI",sans-serif}
    .wrap{max-width:1180px;margin:auto;padding:22px}.top{display:flex;gap:16px;align-items:center;justify-content:space-between;margin-bottom:18px}.title h1{font-size:25px;margin:0}.title p{color:var(--muted);margin:4px 0 0}.pill{display:inline-flex;align-items:center;gap:7px;padding:8px 12px;border:1px solid var(--line);background:#0d1727;border-radius:999px}.dot{width:9px;height:9px;border-radius:50%;background:var(--red);box-shadow:0 0 12px currentColor}.dot.on{background:var(--green)}
    .grid{display:grid;grid-template-columns:repeat(12,1fr);gap:14px}.card{grid-column:span 4;background:linear-gradient(145deg,var(--card2),var(--card));border:1px solid var(--line);border-radius:16px;padding:17px;box-shadow:0 12px 35px #0003}.wide{grid-column:span 8}.full{grid-column:1/-1}.card h2{font-size:15px;margin:0 0 14px}.stats{display:grid;grid-template-columns:repeat(2,1fr);gap:10px}.stat{background:#0c1727;border:1px solid #203149;border-radius:11px;padding:10px}.stat span{display:block;color:var(--muted);font-size:12px}.stat b{display:block;margin-top:4px;font-size:15px;word-break:break-all}.ok{color:var(--green)}.bad{color:var(--red)}.warn{color:var(--yellow)}
    label{display:block;color:var(--muted);font-size:12px;margin:9px 0 5px}input,select,button{width:100%;border-radius:9px;border:1px solid var(--line);background:#0b1626;color:var(--text);padding:10px 11px;font:inherit}button{cursor:pointer;background:#196cc1;border-color:#277fd7;font-weight:650}button:hover{filter:brightness(1.1)}button.secondary{background:#17273d}button.danger{background:#8f2e3a;border-color:#b44754}.row{display:flex;gap:9px}.row>*{flex:1}.notice{padding:10px 12px;background:#0b1727;border-left:3px solid var(--blue);border-radius:7px;color:var(--muted);margin-bottom:12px}.message{min-height:21px;color:var(--green);margin-top:8px}.networks{max-height:170px;overflow:auto;margin-top:10px}.network{display:flex;justify-content:space-between;padding:8px 3px;border-bottom:1px solid var(--line);cursor:pointer}.network:hover{color:var(--blue)}
    pre{height:310px;overflow:auto;margin:0;background:#050b13;border:1px solid #1d2a3a;border-radius:10px;padding:13px;color:#a9e6c5;font:12px/1.6 ui-monospace,SFMono-Regular,Menlo,monospace;white-space:pre-wrap}.actions{display:grid;grid-template-columns:repeat(3,1fr);gap:9px}.foot{text-align:center;color:var(--muted);padding:18px 0 4px;font-size:12px}
    @media(max-width:820px){.card,.wide{grid-column:1/-1}.wrap{padding:15px}.top{align-items:flex-start;flex-direction:column}.stats{grid-template-columns:repeat(2,1fr)}}
  </style>
</head>
<body>
<div class="wrap">
  <div class="top">
    <div class="title"><h1>ESP32-S3 设备中心</h1><p id="subtitle">正在读取开发板信息…</p></div>
    <div class="pill"><i id="liveDot" class="dot"></i><span id="liveText">正在连接</span></div>
  </div>
  <div class="grid">
    <section class="card wide"><h2>运行概览</h2><div class="stats" id="overview"></div></section>
    <section class="card"><h2>网络状态</h2><div class="stats" id="network"></div></section>
    <section class="card"><h2>芯片与存储</h2><div class="stats" id="hardware"></div></section>
    <section class="card"><h2>MQTT</h2><div class="stats" id="mqtt"></div></section>
    <section class="card"><h2>SOS 求助</h2><div class="stats" id="alert"></div><button class="danger" onclick="testAlert()">发送测试报警</button><div class="notice" style="margin-top:10px;margin-bottom:0">测试报警与设备操作受管理员认证保护。</div><div id="alertMsg" class="message"></div></section>
    <section class="card"><h2>设备标识</h2><div class="stats" id="identity"></div></section>

    <section class="card wide">
      <h2>Wi‑Fi 配网</h2>
      <div class="notice">可直接填写 2.4 GHz Wi‑Fi，也可以继续使用手机的 ESP BLE Provisioning 应用进行蓝牙配网。</div>
      <form id="wifiForm"><div class="row"><div><label>Wi‑Fi 名称</label><input id="ssid" name="ssid" required autocomplete="off"></div><div><label>Wi‑Fi 密码</label><input name="password" type="password" autocomplete="new-password"></div></div><div class="row" style="margin-top:10px"><button type="submit">连接 Wi‑Fi</button><button type="button" class="secondary" onclick="scanWifi()">扫描附近网络</button></div></form>
      <div id="networks" class="networks"></div><div id="wifiMsg" class="message"></div>
    </section>
    <section class="card">
      <h2>MQTT 设置</h2>
      <form id="mqttForm"><label>服务器地址</label><input name="host" id="mqttHost" required><div class="row"><div><label>端口</label><input name="port" id="mqttPort" type="number" min="1" max="65535"></div><div><label>用户名</label><input name="user" id="mqttUser"></div></div><label>密码（留空表示不修改）</label><input name="password" type="password" autocomplete="new-password"><button style="margin-top:10px">保存并重新连接</button></form><div id="mqttMsg" class="message"></div>
    </section>

    <section class="card full"><h2>实时设备日志 <small style="color:var(--muted);font-weight:400">本机时间 · 运行时间</small></h2><pre id="logs">等待日志…</pre></section>

    <section class="card wide"><h2>MQTT 命令测试</h2><form id="publishForm" class="row"><input name="message" value='{"action":"test"}'><button>发送到 command 主题</button></form><div id="publishMsg" class="message"></div></section>
    <section class="card"><h2>设备操作</h2><div class="actions"><button class="secondary" onclick="refreshNow()">立即刷新</button><button class="danger" onclick="restartDevice()">重新启动</button><button class="danger" onclick="forgetWifi()">清除配网</button></div><div id="actionMsg" class="message"></div></section>
  </div>
  <div class="foot">页面由 ESP32-S3 本机提供 · 无需互联网即可使用</div>
</div>
<script>
const $=s=>document.querySelector(s);
const esc=v=>String(v??'—').replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
const stat=(k,v,cls='')=>`<div class="stat"><span>${esc(k)}</span><b class="${cls}">${esc(v)}</b></div>`;
const size=n=>n>=1048576?(n/1048576).toFixed(2)+' MB':n>=1024?(n/1024).toFixed(1)+' KB':n+' B';
let initialized=false;
let estimatedBootMs=0;
function uptimeSeconds(value){
  const m=String(value||'').match(/(?:(\d+)天\s*)?(\d+):(\d+):(\d+)/);
  return m?((Number(m[1]||0)*86400)+(Number(m[2])*3600)+(Number(m[3])*60)+Number(m[4])):0;
}
async function status(){
  try{
    const r=await fetch('/api/status',{cache:'no-store'}); if(!r.ok)throw 0; const d=await r.json();
    estimatedBootMs=Date.now()-uptimeSeconds(d.system.uptime)*1000;
    $('#liveDot').className='dot on'; $('#liveText').textContent='设备在线'; $('#subtitle').textContent=`${d.chip.model} · ${d.deviceId}`;
    $('#overview').innerHTML=stat('运行时间',d.system.uptime)+stat('内部温度',d.system.temperature+' °C')+stat('空闲内存',size(d.memory.freeHeap))+stat('最低空闲内存',size(d.memory.minFreeHeap))+stat('BOOT 按钮',d.system.bootPressed?'按下':'未按下',d.system.bootPressed?'warn':'')+stat('重启原因',d.system.resetReason);
    $('#network').innerHTML=stat('Wi‑Fi',d.wifi.connected?'已连接':'未连接',d.wifi.connected?'ok':'bad')+stat('SSID',d.wifi.ssid)+stat('IP 地址',d.wifi.ip)+stat('信号',d.wifi.connected?d.wifi.rssi+' dBm':'—')+stat('配网页面热点',d.ap.ssid)+stat('热点 IP',d.ap.ip);
    $('#hardware').innerHTML=stat('芯片',d.chip.model)+stat('版本 / 核心',d.chip.revision+' / '+d.chip.cores)+stat('CPU',d.chip.cpuMHz+' MHz')+stat('Flash',size(d.memory.flash))+stat('PSRAM',size(d.memory.psram))+stat('程序大小',size(d.memory.sketch));
    $('#mqtt').innerHTML=stat('连接',d.mqtt.connected?'已连接':'未连接',d.mqtt.connected?'ok':'bad')+stat('服务器',d.mqtt.host+':'+d.mqtt.port)+stat('状态码',d.mqtt.state)+stat('状态主题',d.mqtt.statusTopic)+stat('遥测主题',d.mqtt.telemetryTopic)+stat('命令主题',d.mqtt.commandTopic);
    const alertClass=d.alert.state==='acked'?'ok':(d.alert.state==='failed'?'bad':(d.alert.state==='idle'?'':'warn'));
    $('#alert').innerHTML=stat('状态',d.alert.state,alertClass)+stat('当前事件',d.alert.activeEventId||'—')+stat('待处理数量',d.alert.pending)+stat('最近事件',d.alert.lastEventId||'—')+stat('发送次数',d.alert.attempts)+stat('ACK 时间',d.alert.lastAckUptime?d.alert.lastAckUptime+' s':'—')+stat('错误',d.alert.error||'—',d.alert.error?'bad':'');
    $('#identity').innerHTML=stat('设备 ID',d.deviceId)+stat('STA MAC',d.wifi.mac)+stat('AP MAC',d.ap.mac)+stat('SDK',d.system.sdk)+stat('固件编译',d.system.build)+stat('主机名',d.wifi.hostname);
    if(!initialized){$('#mqttHost').value=d.mqtt.host;$('#mqttPort').value=d.mqtt.port;$('#mqttUser').value=d.mqtt.user;initialized=true}
  }catch(e){$('#liveDot').className='dot';$('#liveText').textContent='连接中断'}
}
function renderLogs(text){
  return text.split(/\r?\n/).map(line=>{
    const m=line.match(/^\[(\d+)s\](.*)$/);
    if(!m||!estimatedBootMs)return line;
    const clock=new Date(estimatedBootMs+Number(m[1])*1000).toLocaleTimeString('zh-CN',{hour12:false});
    return `[${clock}] [${m[1]}s]${m[2]}`;
  }).join('\n');
}
async function logs(){try{const r=await fetch('/api/logs',{cache:'no-store'});const t=await r.text();const p=$('#logs');const bottom=p.scrollTop+p.clientHeight>=p.scrollHeight-30;p.textContent=t?renderLogs(t):'暂无日志';if(bottom)p.scrollTop=p.scrollHeight}catch(e){}}
async function scanWifi(){const box=$('#networks');box.textContent='正在扫描…';try{const d=await (await fetch('/api/wifi/scan')).json();box.innerHTML=d.networks.map(n=>`<div class="network" onclick="pickSsid(this)" data-ssid="${esc(n.ssid)}"><span>${esc(n.ssid||'(隐藏网络)')}</span><span>${n.rssi} dBm · ${n.secure?'🔒':'开放'}</span></div>`).join('')||'没有发现网络'}catch(e){box.textContent='扫描失败，请稍后重试'}}
function pickSsid(el){$('#ssid').value=el.dataset.ssid}
async function sendForm(form,url,msg){msg.textContent='正在处理…';try{const body=new URLSearchParams(new FormData(form));const r=await fetch(url,{method:'POST',body});msg.textContent=await r.text()}catch(e){msg.textContent='请求失败'}}
$('#wifiForm').onsubmit=e=>{e.preventDefault();sendForm(e.target,'/api/wifi',$('#wifiMsg'))};
$('#mqttForm').onsubmit=e=>{e.preventDefault();sendForm(e.target,'/api/mqtt',$('#mqttMsg'))};
$('#publishForm').onsubmit=e=>{e.preventDefault();sendForm(e.target,'/api/mqtt/publish',$('#publishMsg'))};
async function testAlert(){if(!confirm('确定发送一次测试 SOS 报警吗？'))return;$('#alertMsg').textContent='正在进入发送队列…';try{const r=await fetch('/api/alert/test',{method:'POST',body:new URLSearchParams({confirm:'SOS'})});$('#alertMsg').textContent=await r.text();status()}catch(e){$('#alertMsg').textContent='请求失败'}}
function refreshNow(){status();logs();$('#actionMsg').textContent='已刷新'}
async function restartDevice(){if(confirm('确定重新启动开发板？')){await fetch('/api/restart',{method:'POST'});$('#actionMsg').textContent='设备正在重启…'}}
async function forgetWifi(){if(confirm('确定清除保存的 Wi‑Fi 并重新配网？')){await fetch('/api/wifi/forget',{method:'POST'});$('#actionMsg').textContent='配网已清除，设备正在重启…'}}
status();logs();setInterval(status,2000);setInterval(logs,1000);
</script>
</body></html>
)HTML";
