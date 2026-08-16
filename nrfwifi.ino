/*
ESP32              nRF24L01

3.3V      ------- VCC
GND       ------- GND
GPIO16    ------- CE
GPIO17    ------- CSN
GPIO18    ------- SCK
GPIO23    ------- MOSI
GPIO19    ------- MISO
*/

#include <SPI.h>
#include <RF24.h>
#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>

RF24 radio(16, 17);
WebServer server(80);
WebSocketsServer ws(81);

#define CHANNELS 126
int yanaidytebyablya[CHANNELS];
TaskHandle_t scanTask;

void scanLoop(void* param) {
  while(1) {
    for (int rep = 0; rep < 35; rep++) {
      for (int ch = 0; ch < CHANNELS; ch++) {
        radio.setChannel(ch);
        radio.startListening();
        delayMicroseconds(200);
        if (radio.testCarrier()) yanaidytebyablya[ch]++;
        radio.stopListening();
      }
      vTaskDelay(1); 
    }

    String json = "{\"channels\":[";
    for (int ch = 0; ch < CHANNELS; ch++) {
      json += yanaidytebyablya[ch];
      if (ch < CHANNELS - 1) json += ",";
      yanaidytebyablya[ch] = 0;
    }
    json += "]}";
    ws.broadcastTXT(json);
    vTaskDelay(10);
  }
}

const char HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html><head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Сканер эфира 2.4 ГГц</title>
<style>
*{box-sizing:border-box;margin:0;padding:0;font-family:sans-serif}
body{background:#111;color:#eee;padding:12px}
h1{font-size:16px;margin-bottom:10px;color:#00ff88}
canvas{width:100%;height:200px;background:#000;border-radius:8px;display:block;border:1px solid #333}
#info{margin-top:12px;font-size:13px;line-height:1.8}
.row{display:flex;justify-content:space-between;padding:4px 8px;border-radius:6px;margin-bottom:3px}
.row:hover{background:#1a1a1a}
.freq{color:#00ff88}
.type{color:#aaa;font-size:12px}
.dist{color:#ffaa00}
.bar{color:#00aaff;font-size:11px}
.wifi{color:#4af}
.bt{color:#a4f}
.unknown{color:#888}
#status{font-size:12px;color:#555;margin-bottom:8px}
</style></head><body>
<h1>Сканер радиоэфира 2.4 ГГц — ESP32 + nRF24L01</h1>
<div id="status">Подключение...</div>
<canvas id="cv"></canvas>
<div id="info"></div>

<script>
let ws, data = new Array(126).fill(0);

const wifiCh = {
  1:{name:"Wi-Fi канал 1",center:1},
  2:{name:"Wi-Fi канал 1",center:1},
  3:{name:"Wi-Fi канал 1",center:1},
  4:{name:"Wi-Fi канал 1",center:1},
  5:{name:"Wi-Fi канал 1",center:1},
  6:{name:"Wi-Fi канал 2",center:6},
  11:{name:"Wi-Fi канал 3",center:11},
  16:{name:"Wi-Fi канал 4",center:16},
  21:{name:"Wi-Fi канал 5",center:21},
  26:{name:"Wi-Fi канал 6",center:26},
  31:{name:"Wi-Fi канал 7",center:31},
  36:{name:"Wi-Fi канал 8",center:36},
  41:{name:"Wi-Fi канал 9",center:41},
  46:{name:"Wi-Fi канал 10",center:46},
  51:{name:"Wi-Fi канал 11",center:51},
  56:{name:"Wi-Fi канал 12",center:56},
  61:{name:"Wi-Fi канал 13",center:61},
};

function getType(ch) {
  // Wi-Fi каналы 1,6,11 (самые популярные)
  if (ch >= 0  && ch <= 13) return {label:"Wi-Fi кан.1",  css:"wifi"};
  if (ch >= 24 && ch <= 29) return {label:"Wi-Fi кан.6",  css:"wifi"};
  if (ch >= 49 && ch <= 55) return {label:"Wi-Fi кан.11", css:"wifi"};
  if (ch >= 62 && ch <= 68) return {label:"Wi-Fi кан.13", css:"wifi"};
  // Bluetooth advertising
  if (ch === 2)  return {label:"BT реклама (37)", css:"bt"};
  if (ch === 26) return {label:"BT реклама (38)", css:"bt"};
  if (ch === 80) return {label:"BT реклама (39)", css:"bt"};
  // Bluetooth диапазон
  if (ch >= 0 && ch <= 79) return {label:"Bluetooth",     css:"bt"};
  return {label:"Неизвестно", css:"unknown"};
}

function getDist(strength) {
  // strength = количество обнаружений из 100 проходов
  if (strength >= 80) return "очень близко (<2м)";
  if (strength >= 50) return "близко (2–5м)";
  if (strength >= 20) return "рядом (5–10м)";
  if (strength >= 5)  return "далеко (10–20м)";
  return "очень далеко (>20м)";
}

function draw(d) {
  const cv = document.getElementById('cv');
  const ctx = cv.getContext('2d');
  cv.width = cv.offsetWidth;
  cv.height = 200;
  ctx.fillStyle = '#000';
  ctx.fillRect(0, 0, cv.width, cv.height);

  const w = cv.width, h = cv.height;
  const barW = w / 126;
  const max = Math.max(...d, 1);

  // Подписи частот
  ctx.fillStyle = '#333';
  ctx.font = '10px sans-serif';
  ['2.412\nWiFi1','2.437\nWiFi6','2.462\nWiFi11'].forEach((label, i) => {
    const freqs = [12, 37, 62];
    const x = freqs[i] * barW;
    ctx.fillStyle = '#1a3a1a';
    ctx.fillRect(x - barW*5, 0, barW*10, h);
  });

  for (let ch = 0; ch < 126; ch++) {
    const x = ch * barW;
    const bh = (d[ch] / max) * (h - 20);
    const t = getType(ch);
    ctx.fillStyle = t.css === 'wifi' ? '#00aa44' :
                    t.css === 'bt'   ? '#8844ff' : '#444';
    ctx.fillRect(x, h - bh, barW - 1, bh);
  }

  // Подписи осей
  ctx.fillStyle = '#555';
  ctx.font = '10px sans-serif';
  ctx.fillText('2.400', 2, h - 2);
  ctx.fillText('2.525', w - 35, h - 2);
  ctx.fillStyle = '#00aa44'; ctx.fillText('■ Wi-Fi', w/2 - 60, 14);
  ctx.fillStyle = '#8844ff'; ctx.fillText('■ BT',    w/2,      14);
}

function updateInfo(d) {
  let html = '';
  for (let ch = 0; ch < 126; ch++) {
    if (d[ch] > 0) {
      const freq = (2.400 + ch * 0.001).toFixed(3);
      const t = getType(ch);
      const dist = getDist(d[ch]);
      const bars = '#'.repeat(Math.min(d[ch], 20));
      html += `<div class="row">
        <span class="freq">${freq} ГГц</span>
        <span class="${t.css} type">${t.label}</span>
        <span class="dist">${dist}</span>
        <span class="bar">${bars}</span>
      </div>`;
    }
  }
  document.getElementById('info').innerHTML = html || '<div style="color:#555;padding:8px">Эфир чист</div>';
}

function connect() {
  ws = new WebSocket('ws://' + location.hostname + ':81');
  ws.onopen = () => document.getElementById('status').textContent = 'Подключено — сканирование...';
  ws.onclose = () => {
    document.getElementById('status').textContent = 'Отключено — переподключение...';
    setTimeout(connect, 1000);
  };
  ws.onmessage = e => {
    const d = JSON.parse(e.data).channels;
    draw(d);
    updateInfo(d);
  };
}
connect();
function scanWifi() {
  fetch('/wifi').then(r=>r.json()).then(nets => {
    let html = '<h2 style="color:#00ff88;margin:12px 0 6px">Wi-Fi сети:</h2>';
    nets.forEach(n => {
      const dist = n.rssi > -50 ? '<2м' : n.rssi > -65 ? '2-10м' : n.rssi > -80 ? '10-30м' : '>30м';
      html += `<div class="row"><span class="freq">${n.ssid}</span><span class="wifi">канал ${n.ch}</span><span class="dist">${dist}</span><span class="bar">${n.rssi} дБм</span></div>`;
    });
    document.getElementById('info').innerHTML += html;
  });
}
setInterval(scanWifi, 5000);
scanWifi();
</script></body></html>
)rawliteral";

void setup() {
  Serial.begin(115200);

  if (!radio.begin()) {
    Serial.println("nRF24L01 не найден!");
    while(1);
  }
  radio.setAutoAck(false);
  radio.setPALevel(RF24_PA_MIN);
  radio.setDataRate(RF24_2MBPS);
  radio.startListening();

  WiFi.softAP("ESP32_SCANNER", "12345678");
  server.on("/wifi", [](){
  int n = WiFi.scanComplete();
  if (n == WIFI_SCAN_RUNNING) {
    server.send(200, "application/json", "[]");
    return;
  }
  if (n <= 0) {
    WiFi.scanNetworks(true); // async
    server.send(200, "application/json", "[]");
    return;
  }
  String json = "[";
  for (int i = 0; i < n; i++) {
    if (i > 0) json += ",";
    json += "{\"ssid\":\"" + WiFi.SSID(i) + "\",";
    json += "\"rssi\":" + String(WiFi.RSSI(i)) + ",";
    json += "\"ch\":" + String(WiFi.channel(i)) + "}";
  }
  json += "]";
  WiFi.scanDelete();
  WiFi.scanNetworks(true); // сразу запускаем следующее
  server.send(200, "application/json", json);
  });
  Serial.println("Точка доступа: ESP32_SCANNER");
  Serial.println("Пароль: 12345678");
  Serial.println("Открой: http://192.168.4.1");

  server.on("/", [](){
    server.send_P(200, "text/html", HTML);
  });
  server.begin();
  ws.begin();
  ws.onEvent([](uint8_t n, WStype_t t, uint8_t* p, size_t l){});
  xTaskCreatePinnedToCore(scanLoop, "scan", 8192, NULL, 1, &scanTask, 1);
  WiFi.scanNetworks(true); // первый запуск сканирования
}

void loop() {
  server.handleClient();
  ws.loop();
  delay(10);
}
