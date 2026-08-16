#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>

// ======== ПИНЫ ========
#define MIC_PIN     34   // MAX9814 OUT
#define GAIN_PIN    32   // MAX9814 GAIN
#define AR_PIN      33   // MAX9814 AR
#define ENC_CLK     18   // Энкодер CLK
#define ENC_DT      19   // Энкодер DT
#define ENC_SW      5    // Энкодер кнопка
#define BUZZER_PIN  25   // Пассивный зуммер
#define BTN_PIN     4    // KY-004
#define RGB_R       27
#define RGB_G       26
#define RGB_B       14
#define LED1        12
#define LED2        13

// ======== Wi-Fi ========
const char* ssid     = "ESP32_LAB";
const char* password = "12345678";

WebServer     server(80);
WebSocketsServer ws(81);

// ======== Состояние ========
int  currentMode   = 1;   // 1-5
int  gainMode      = 2;   // 0=40дБ 1=50дБ 2=60дБ
int  arMode        = 1;   // 0=быстрый 1=средний 2=медленный
int  genFreq       = 440; // Гц для генератора
int  genDuty       = 50;  // скважность %
int  ampGain       = 1;   // коэффициент усиления режим 5
bool oosEnabled    = false;

// ======== Счётчик (режим 4) ========
volatile int  counter     = 0;
volatile bool btnPressed  = false;
unsigned long btnLastTime = 0;
unsigned long btnHoldStart = 0;

// ======== Энкодер ========
volatile int  encValue    = 440;
volatile int  lastCLK     = HIGH;

// ======== ADC буфер ========
#define SAMPLES 100
int adcBuf[SAMPLES];

// ======== Прерывания ========
void IRAM_ATTR onButton() {
  unsigned long now = millis();
  if (now - btnLastTime < 50) return;
  btnLastTime = now;
  btnPressed = true;
}

void IRAM_ATTR onEncoder() {
  int clk = digitalRead(ENC_CLK);
  int dt  = digitalRead(ENC_DT);
  if (clk != lastCLK && clk == LOW) {
    if (dt != clk) encValue += (currentMode == 3 ? 10 : 1);
    else           encValue -= (currentMode == 3 ? 10 : 1);
    if (currentMode == 3) {
      encValue = constrain(encValue, 20, 20000);
      genFreq = encValue;
      ledcWriteTone(0, genFreq);
    }
  }
  lastCLK = clk;
}

// ======== Установка GAIN ========
void setGain(int mode) {
  gainMode = mode;
  if (mode == 0) { pinMode(GAIN_PIN, OUTPUT); digitalWrite(GAIN_PIN, HIGH); } // 40дБ
  if (mode == 1) { pinMode(GAIN_PIN, OUTPUT); digitalWrite(GAIN_PIN, LOW);  } // 50дБ
  if (mode == 2) { pinMode(GAIN_PIN, INPUT);  }                               // 60дБ
}

void setAR(int mode) {
  arMode = mode;
  if (mode == 0) { pinMode(AR_PIN, OUTPUT); digitalWrite(AR_PIN, LOW);  } // быстрый
  if (mode == 1) { pinMode(AR_PIN, INPUT);  }                             // средний
  if (mode == 2) { pinMode(AR_PIN, OUTPUT); digitalWrite(AR_PIN, HIGH); } // медленный
}

// ======== RGB по биту счётчика ========
void showCounter(int val) {
  digitalWrite(LED1, (val >> 0) & 1);
  digitalWrite(LED2, (val >> 1) & 1);
  digitalWrite(RGB_R, (val >> 2) & 1);
  digitalWrite(RGB_G, (val >> 3) & 1);
  digitalWrite(RGB_B, (val >> 4) & 1);
}

// ======== Цифровые фильтры ========
int lpfPrev = 2048;
int applyFilter(int raw, int type) {
  // 0=нет 1=ФНЧ 2=ВЧ
  if (type == 0) return raw;
  if (type == 1) { // ФНЧ — скользящее среднее
    lpfPrev = (lpfPrev * 7 + raw) / 8;
    return lpfPrev;
  }
  if (type == 2) { // ВЧ — вычитаем низкие
    int hp = raw - lpfPrev;
    lpfPrev = (lpfPrev * 7 + raw) / 8;
    return hp + 2048;
  }
  return raw;
}

// ======== Читаем ADC и отправляем ========
int filterType = 0;

void sendADC() {
  String json = "{\"mode\":" + String(currentMode) + ",\"data\":[";
  for (int i = 0; i < SAMPLES; i++) {
    int raw = analogRead(MIC_PIN);
    int val = applyFilter(raw, (currentMode == 2) ? filterType : 0);
    // Режим 5: усиление
    if (currentMode == 5) {
      int centered = val - 2048;
      centered = centered * ampGain;
      // ООС — если амплитуда большая, уменьшаем усиление
      if (oosEnabled && abs(centered) > 1500 && ampGain > 1) ampGain--;
      centered = constrain(centered, -2048, 2047);
      val = centered + 2048;
    }
    adcBuf[i] = val;
    delayMicroseconds(100);
  }
  for (int i = 0; i < SAMPLES; i++) {
    json += adcBuf[i];
    if (i < SAMPLES - 1) json += ",";
  }
  json += "]}";
  ws.broadcastTXT(json);
}

// ======== WebSocket события ========
void onWsEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t len) {
  if (type != WStype_TEXT) return;
  String msg = String((char*)payload);

  if (msg.startsWith("mode:")) {
    currentMode = msg.substring(5).toInt();
    // Выключаем зуммер если выходим из режима 3
    if (currentMode != 3) ledcWriteTone(0, 0);
    else { encValue = genFreq; ledcWriteTone(0, genFreq); }
  }
  else if (msg.startsWith("gain:"))   setGain(msg.substring(5).toInt());
  else if (msg.startsWith("ar:"))     setAR(msg.substring(3).toInt());
  else if (msg.startsWith("filter:")) filterType = msg.substring(7).toInt();
  else if (msg.startsWith("duty:")) {
    genDuty = msg.substring(5).toInt();
    ledcWrite(0, map(genDuty, 0, 100, 0, 255));
  }
  else if (msg.startsWith("ampgain:")) ampGain = msg.substring(8).toInt();
  else if (msg == "oos:on")  oosEnabled = true;
  else if (msg == "oos:off") oosEnabled = false;
  else if (msg == "reset")  { counter = 0; showCounter(0); }
}

// ======== HTML страница ========
const char HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html><head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>ESP32 Живой учебник РЭА</title>
<style>
*{box-sizing:border-box;margin:0;padding:0;font-family:sans-serif}
body{background:#f4f4f4;color:#222;padding:12px}
h1{font-size:18px;margin-bottom:12px;text-align:center}
.tabs{display:flex;gap:6px;flex-wrap:wrap;margin-bottom:12px}
.tab{padding:8px 14px;border:1px solid #ccc;border-radius:8px;cursor:pointer;background:#fff;font-size:13px}
.tab.active{background:#222;color:#fff;border-color:#222}
.panel{display:none;background:#fff;border-radius:10px;padding:16px;border:1px solid #ddd}
.panel.active{display:block}
canvas{width:100%;height:200px;background:#111;border-radius:8px;display:block}
.row{display:flex;align-items:center;gap:10px;margin-top:12px;flex-wrap:wrap}
label{font-size:13px;color:#555}
select,input[type=range]{font-size:13px;padding:4px 8px;border-radius:6px;border:1px solid #ccc}
button{padding:7px 14px;border-radius:8px;border:1px solid #ccc;background:#fff;cursor:pointer;font-size:13px}
button.on{background:#222;color:#fff;border-color:#222}
.val{font-size:20px;font-weight:bold;margin-top:10px}
.bits{display:flex;gap:8px;margin-top:10px;flex-wrap:wrap}
.bit{width:44px;height:44px;border-radius:8px;background:#eee;display:flex;align-items:center;justify-content:center;font-size:11px;flex-direction:column;border:1px solid #ccc}
.bit.on{background:#222;color:#fff}
.stat{font-size:13px;color:#555;margin-top:6px}
</style></head><body>
<h1>ESP32 Живой учебник РЭА</h1>
<div class="tabs">
  <div class="tab active" onclick="setMode(1,this)">1. Осциллограф</div>
  <div class="tab" onclick="setMode(2,this)">2. Фильтр</div>
  <div class="tab" onclick="setMode(3,this)">3. Генератор</div>
  <div class="tab" onclick="setMode(4,this)">4. Счётчик</div>
  <div class="tab" onclick="setMode(5,this)">5. Усилитель</div>
</div>

<div id="p1" class="panel active">
  <canvas id="c1"></canvas>
  <div class="row">
    <label>Усиление MAX9814:</label>
    <select onchange="send('gain:'+this.value)"><option value="2">60 дБ</option><option value="1">50 дБ</option><option value="0">40 дБ</option></select>
    <label>AGC:</label>
    <select onchange="send('ar:'+this.value)"><option value="1">Средний</option><option value="0">Быстрый</option><option value="2">Медленный</option></select>
  </div>
  <div class="stat" id="stat1">Амплитуда: — | Среднее: —</div>
</div>

<div id="p2" class="panel">
  <canvas id="c2"></canvas>
  <div class="row">
    <label>Фильтр:</label>
    <select onchange="send('filter:'+this.value)">
      <option value="0">Без фильтра</option>
      <option value="1">ФНЧ (низкие частоты)</option>
      <option value="2">ВЧ (высокие частоты)</option>
    </select>
  </div>
  <div class="stat">Говори в микрофон — видишь разницу между фильтрами</div>
</div>

<div id="p3" class="panel">
  <div class="val" id="freqVal">440 Гц</div>
  <div class="row">
    <label>Скважность:</label>
    <input type="range" min="10" max="90" value="50" step="5" oninput="setDuty(this.value)">
    <span id="dutyVal">50%</span>
  </div>
  <div class="stat">Крути энкодер → меняется частота зуммера (20–20000 Гц)</div>
  <div class="stat">Кнопка энкодера → переключает скважность 10/50/90%</div>
</div>

<div id="p4" class="panel">
  <div class="val" id="cntDec">0</div>
  <div class="bits" id="bits"></div>
  <div class="row">
    <button onclick="send('reset')">Сброс (RS-триггер)</button>
  </div>
  <div class="stat">Нажимай KY-004 — счётчик растёт. Удержи 1 сек — сброс.</div>
  <div class="stat" id="cntHex">HEX: 0x00 | BIN: 00000</div>
</div>

<div id="p5" class="panel">
  <canvas id="c5"></canvas>
  <div class="row">
    <label>Коэфф. усиления K:</label>
    <input type="range" min="1" max="20" value="1" step="1" oninput="setAmpGain(this.value)">
    <span id="gainVal">K=1</span>
  </div>
  <div class="row">
    <button id="oosBtn" onclick="toggleOOS()">ООС: выкл</button>
  </div>
  <div class="stat">Включи ООС — амплитуда стабилизируется автоматически</div>
</div>

<script>
let ws, mode=1, oosOn=false;
const canvases={1:'c1',2:'c2',5:'c5'};

function connect(){
  ws=new WebSocket('ws://'+location.hostname+':81');
  ws.onmessage=e=>{
    const d=JSON.parse(e.data);
    if(d.mode!==mode) return;
    if(d.data) drawWave(d.data, d.mode);
    if(d.counter!==undefined) updateCounter(d.counter);
    if(d.freq!==undefined) document.getElementById('freqVal').textContent=d.freq+' Гц';
  };
  ws.onclose=()=>setTimeout(connect,1000);
}

function drawWave(data, m){
  const id=canvases[m];
  if(!id) return;
  const cv=document.getElementById(id);
  const ctx=cv.getContext('2d');
  cv.width=cv.offsetWidth; cv.height=200;
  ctx.fillStyle='#111'; ctx.fillRect(0,0,cv.width,cv.height);
  ctx.strokeStyle='#00ff88'; ctx.lineWidth=1.5;
  ctx.beginPath();
  const w=cv.width, h=cv.height, n=data.length;
  let mn=Math.min(...data), mx=Math.max(...data);
  let amp=mx-mn;
  for(let i=0;i<n;i++){
    const x=i/n*w;
    const y=h-(data[i]-mn)/(amp||1)*(h-10)-5;
    i===0?ctx.moveTo(x,y):ctx.lineTo(x,y);
  }
  ctx.stroke();
  // Статистика режим 1
  if(m===1){
    const mid=data.reduce((a,b)=>a+b,0)/n;
    document.getElementById('stat1').textContent=
      'Амплитуда: '+amp+' | Среднее: '+Math.round(mid);
  }
}

function updateCounter(val){
  document.getElementById('cntDec').textContent=val;
  document.getElementById('cntHex').textContent=
    'HEX: 0x'+val.toString(16).toUpperCase().padStart(2,'0')+
    ' | BIN: '+val.toString(2).padStart(8,'0');
  const bits=document.getElementById('bits');
  bits.innerHTML='';
  for(let i=7;i>=0;i--){
    const b=document.createElement('div');
    b.className='bit'+((val>>i&1)?' on':'');
    b.innerHTML='<span>б'+i+'</span><span>'+((val>>i)&1)+'</span>';
    bits.appendChild(b);
  }
}

function send(msg){ if(ws&&ws.readyState===1) ws.send(msg); }
function setMode(m,el){
  mode=m;
  document.querySelectorAll('.tab').forEach(t=>t.classList.remove('active'));
  el.classList.add('active');
  document.querySelectorAll('.panel').forEach(p=>p.classList.remove('active'));
  document.getElementById('p'+m).classList.add('active');
  send('mode:'+m);
}
function setDuty(v){
  document.getElementById('dutyVal').textContent=v+'%';
  send('duty:'+v);
}
function setAmpGain(v){
  document.getElementById('gainVal').textContent='K='+v;
  send('ampgain:'+v);
}
function toggleOOS(){
  oosOn=!oosOn;
  const btn=document.getElementById('oosBtn');
  btn.textContent='ООС: '+(oosOn?'вкл':'выкл');
  btn.className=oosOn?'on':'';
  send(oosOn?'oos:on':'oos:off');
}

connect();
</script></body></html>
)rawliteral";

void setup() {
  Serial.begin(115200);

  // Пины
  pinMode(MIC_PIN, INPUT);
  pinMode(ENC_CLK, INPUT_PULLUP);
  pinMode(ENC_DT,  INPUT_PULLUP);
  pinMode(ENC_SW,  INPUT_PULLUP);
  pinMode(BTN_PIN, INPUT_PULLUP);
  pinMode(RGB_R, OUTPUT); pinMode(RGB_G, OUTPUT); pinMode(RGB_B, OUTPUT);
  pinMode(LED1, OUTPUT); pinMode(LED2, OUTPUT);

  // PWM для зуммера
  ledcSetup(0, 440, 8);
  ledcAttachPin(BUZZER_PIN, 0);

  // GAIN и AR по умолчанию — средние (60дБ, средний AGC)
  setGain(2);
  setAR(1);

  // Прерывания
  attachInterrupt(digitalPinToInterrupt(BTN_PIN), onButton, FALLING);
  attachInterrupt(digitalPinToInterrupt(ENC_CLK), onEncoder, CHANGE);

  // Wi-Fi точка доступа
  WiFi.softAP(ssid, password);
  Serial.println("AP IP: " + WiFi.softAPIP().toString());

  // Веб-сервер
  server.on("/", [](){
    server.send_P(200, "text/html", HTML);
  });
  server.begin();

  // WebSocket
  ws.begin();
  ws.onEvent(onWsEvent);

  Serial.println("Готово! Открой: http://192.168.4.1");
}

unsigned long lastSend = 0;
unsigned long lastCounter = 0;

void loop() {
  server.handleClient();
  ws.loop();

  // Кнопка — счётчик (режим 4)
  if (btnPressed) {
    btnPressed = false;
    if (currentMode == 4) {
      counter++;
      if (counter > 255) counter = 0;
      showCounter(counter);
      // Отправляем счётчик
      String msg = "{\"mode\":4,\"counter\":" + String(counter) + "}";
      ws.broadcastTXT(msg);
    }
  }

  // Отправка ADC данных каждые 50мс (режимы 1, 2, 5)
  if (millis() - lastSend > 50) {
    lastSend = millis();
    if (currentMode == 1 || currentMode == 2 || currentMode == 5) {
      sendADC();
    }
    // Режим 3 — шлём текущую частоту
    if (currentMode == 3) {
      String msg = "{\"mode\":3,\"freq\":" + String(genFreq) + "}";
      ws.broadcastTXT(msg);
    }
  }
}
