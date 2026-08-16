#include <WiFi.h>
#include <WebServer.h>

WebServer server(80);

// История RSSI для графика (последние 30 точек)
int rssiHistory[30] = {0};
uint8_t histIdx = 0;
uint32_t uptimeSeconds = 0;

const char HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html><head>
<meta charset='utf-8'>
<meta name='viewport' content='width=device-width,initial-scale=1'>
<title>ESP32 Monitor</title>
<style>
  *{box-sizing:border-box;margin:0;padding:0}
  body{font-family:monospace;background:#0d0d0d;color:#ccc;padding:20px;max-width:480px}
  .header{display:flex;align-items:center;gap:12px;margin-bottom:24px;padding-bottom:16px;border-bottom:1px solid #1a1a1a}
  .dot{width:8px;height:8px;border-radius:50%;background:#1D9E75;animation:pulse 1.5s infinite;flex-shrink:0}
  @keyframes pulse{0%,100%{opacity:1}50%{opacity:0.3}}
  h1{font-size:14px;font-weight:500;color:#eee;letter-spacing:.08em}
  .sub{font-size:11px;color:#555;margin-top:2px}
  .lbl{font-size:10px;color:#444;letter-spacing:.08em;text-transform:uppercase;margin-bottom:8px}
  .stats{display:grid;grid-template-columns:repeat(4,1fr);gap:6px;margin-bottom:20px}

  .stat{background:#0a0a0a;border:1px solid #1a1a1a;border-radius:6px;padding:10px 12px}
  .stat-l{font-size:10px;color:#444;margin-bottom:4px}
  .stat-v{font-size:19px;font-weight:500;color:#eee;font-family:monospace}
  .stat-v.g{color:#1D9E75}
  .stat-v.r{color:#E24B4A}
  .chart-box{background:#0a0a0a;border:1px solid #1a1a1a;border-radius:8px;padding:12px;margin-bottom:20px}
  canvas{width:100%;height:120px;display:block}
  .statusbar{display:flex;align-items:center;gap:8px;padding:10px 14px;border-radius:6px;border:1px solid #1a3329;background:#0a1f18;font-size:12px;color:#5DCAA5}
</style>
</head><body>

<div class='header'>
  <div class='dot'></div>
  <div>
    <h1>ESP32 MONITOR</h1>
    <div class='sub'>192.168.1.47 &nbsp;·&nbsp; AP mode</div>
  </div>
</div>

<div class='stats'>
  <div class='stat'><div class='stat-l'>клиенты</div><div class='stat-v g' id='clients'>0</div></div>
  <div class='stat'><div class='stat-l'>uptime</div><div class='stat-v' id='uptime'>0s</div></div>
  <div class='stat'><div class='stat-l'>свобод. RAM</div><div class='stat-v' id='heap'>0</div></div>
  <div class='stat'><div class='stat-l'>WiFi канал</div><div class='stat-v g' id='wifich'>-</div></div>
</div>

<div class='lbl'>загрузка (обновления в сек)</div>
<div class='chart-box'>
  <canvas id='chart'></canvas>
</div>

<div class='lbl'>RAM история</div>
<div class='chart-box'>
  <canvas id='chart2'></canvas>
</div>

<div class='statusbar'>
  ▶ &nbsp;<span id='statustext'>мониторинг активен</span>
</div>

<script>
const MAX=30;
let loadHistory=new Array(MAX).fill(0);
let heapHistory=new Array(MAX).fill(0);
let lastUpdate=Date.now();

function drawChart(canvasId, data, color, minVal, maxVal, unit){
  const canvas=document.getElementById(canvasId);
  const ctx=canvas.getContext('2d');
  canvas.width=canvas.offsetWidth*2;
  canvas.height=240;
  ctx.clearRect(0,0,canvas.width,canvas.height);
  const w=canvas.width, h=canvas.height;
  const pad=10;
  const range=maxVal-minVal||1;

  // grid
  ctx.strokeStyle='#1a1a1a';
  ctx.lineWidth=1;
  for(let i=0;i<=4;i++){
    const y=pad+((h-pad*2)/4)*i;
    ctx.beginPath();ctx.moveTo(0,y);ctx.lineTo(w,y);ctx.stroke();
  }

  // line
  ctx.strokeStyle=color;
  ctx.lineWidth=2;
  ctx.beginPath();
  data.forEach((v,i)=>{
    const x=(i/(MAX-1))*w;
    const y=h-pad-((v-minVal)/range)*(h-pad*2);
    i===0?ctx.moveTo(x,y):ctx.lineTo(x,y);
  });
  ctx.stroke();

  // fill
  ctx.fillStyle=color+'22';
  ctx.lineTo(w,h);ctx.lineTo(0,h);ctx.closePath();ctx.fill();

  // last value
  const last=data[data.length-1];
  ctx.fillStyle=color;
  ctx.font='bold 20px monospace';
  ctx.fillText(last+unit, 8, 28);
}

function update(){
  fetch('/stats').then(r=>r.json()).then(d=>{
    document.getElementById('clients').textContent=d.clients;
    document.getElementById('heap').textContent=Math.round(d.heap/1024)+'K';
    document.getElementById('wifich').textContent = d.channel;

    // uptime красиво
    const s=d.uptime;
    const h=Math.floor(s/3600),m=Math.floor((s%3600)/60),sec=s%60;
    document.getElementById('uptime').textContent=
      h>0?h+'h'+(m<10?'0':'')+m+'m':m>0?m+'m'+sec+'s':sec+'s';

    // скорость обновлений
    const now=Date.now();
    const rate=Math.round(1000/(now-lastUpdate)*10)/10;
    lastUpdate=now;
    loadHistory.push(rate);
    if(loadHistory.length>MAX) loadHistory.shift();

    heapHistory.push(Math.round(d.heap/1024));
    if(heapHistory.length>MAX) heapHistory.shift();

    drawChart('chart', loadHistory, '#1D9E75', 0, 5, '/s');
    drawChart('chart2', heapHistory, '#378ADD', 0, 300, 'K');

    document.getElementById('statustext').textContent=
      'клиентов: '+d.clients+' | heap: '+Math.round(d.heap/1024)+'K | uptime: '+d.uptime+'s';
  });
}

update();
setInterval(update, 1000);
</script>
</body></html>
)rawliteral";

void setup() {
  Serial.begin(115200);
  IPAddress local_IP(192, 168, 1, 47);
  IPAddress gateway(192, 168, 1, 47);
  IPAddress subnet(255, 255, 255, 0);

  WiFi.softAP("ESP32-Monitor", "12345678", 6);

  WiFi.softAPConfig(local_IP, gateway, subnet);

  Serial.print("AP IP: ");
  Serial.println(WiFi.softAPIP());


  server.on("/", []() { server.send(200, "text/html", HTML); });

  server.on("/stats", []() {
    String json = "{";
    json += "\"clients\":" + String(WiFi.softAPgetStationNum()) + ",";
    json += "\"uptime\":" + String(uptimeSeconds) + ",";
    json += "\"heap\":" + String(ESP.getFreeHeap()) + ",";
    json += "\"channel\":" + String(WiFi.channel());
    json += "}";
   server.send(200, "application/json", json);
  });

  server.begin();
  Serial.println("СТААААРТ");
}

void loop() {
  server.handleClient();

  // Uptime каждую секунду
  static uint32_t lastSec = 0;
  if (millis() - lastSec >= 1000) {
    lastSec = millis();
    uptimeSeconds++;
    Serial.printf("Uptime: %ds | Клиенты: %d | Heap: %dK\n",
      uptimeSeconds,
      WiFi.softAPgetStationNum(),
      ESP.getFreeHeap() / 1024);
  }
}