#include <Wire.h>
#include <QMC5883LCompass.h>
#include <WiFi.h>
#include <WebServer.h>

QMC5883LCompass compass;
WebServer server(80);

const char* ssid = "ESP32-Compass";
const char* password = "12345678";

void setup() {
  Serial.begin(115200);

  Wire.begin(8, 9); // SDA=GPIO8, SCL=GPIO9

  compass.init();
  compass.setMode(0x01, 0x0C, 0x10, 0xC0);

  WiFi.softAP(ssid, password);
  Serial.println(WiFi.softAPIP());

  server.on("/data", []() {
    compass.read();
    int x = compass.getX();
    int y = compass.getY();
    int z = compass.getZ();
    int az = compass.getAzimuth();
    String json = "{\"x\":" + String(x) +
                  ",\"y\":" + String(y) +
                  ",\"z\":" + String(z) +
                  ",\"azimuth\":" + String(az) + "}";
    server.send(200, "application/json", json);
  });

server.on("/", []() {
    server.send(200, "text/html", R"(
<!DOCTYPE html><html><head><meta charset='utf-8'>
<title>Компас и дрон</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:sans-serif;background:#0f0f13;color:#fff;padding:1rem}
.grid{display:grid;grid-template-columns:1fr 1fr;gap:1rem}

canvas{display:block;background:#161620;border-radius:12px}
.label{font-size:11px;color:#fff;text-align:center;margin-top:4px} /* Мелкий текст подписей*/
.stats{display:grid;grid-template-columns:1fr 1fr;gap:8px;margin-top:1rem}
.stat{background:#1e1e2a;border-radius:8px;padding:10px;text-align:center}
.stat .lbl{font-size:11px;color:#fff;margin-bottom:4px}
.stat .val{font-size:20px;font-weight:500;color:#fff}
.legend{margin-top:1rem;background:#1e1e2a;border-radius:8px;padding:12px;font-size:12px;display:grid;grid-template-columns:1fr 1fr;gap:6px}
.li{display:flex;align-items:center;gap:8px;color:#fff} /* цвет мелкого текста*/
.dot{width:10px;height:10px;border-radius:50%;flex-shrink:0}
</style></head>
<body>
<div class='grid'>
  <div>
    <canvas id='drone' width='300' height='300'></canvas>
    <div class='label'>Вид сверху. Вращается по азимуту</div>
  </div>
  <div>
    <canvas id='plane' width='300' height='300'></canvas>
    <div class='label'>Магнитное поле X/Y + наклон Z</div>
  </div>
</div>
<div class='stats'>
  <div class='stat'><div class='lbl'>X (мГс)</div><div class='val' id='vx'>—</div></div>
  <div class='stat'><div class='lbl'>Y (мГс)</div><div class='val' id='vy'>—</div></div>
  <div class='stat'><div class='lbl'>Z (мГс)</div><div class='val' id='vz'>—</div></div>
  <div class='stat'><div class='lbl'>Азимут</div><div class='val' style='color:#4af' id='azVal'>—°</div></div>
</div>
<div class='legend'>
  <div class='li'><div class='dot' style='background:#4af'></div>Передние моторы (север)</div>
  <div class='li'><div class='dot' style='background:#f84'></div>Задние моторы (юг)</div>
  <div class='li'><div class='dot' style='background:#4af'></div>Стрелка азимута</div>
  <div class='li'><div class='dot' style='background:#4af'></div>Точка на плоскости (X/Y)</div>
  <div class='li'><div class='dot' style='background:#4f8;border-radius:50%;border:2px solid #4f8'></div>Кружок зелёный — Z↑ (наклон вверх)</div>
  <div class='li'><div class='dot' style='background:transparent;border:2px solid #f84;border-radius:50%'></div>Кружок оранж — Z↓ (наклон вниз)</div>
</div>
<script>
const dc=document.getElementById('drone').getContext('2d');
const pc=document.getElementById('plane').getContext('2d');

let curAz=0,curX=0,curY=0,curZ=0;
let tAz=0,tX=0,tY=0,tZ=0;
const LERP=0.12;

function lerp(a,b,t){return a+(b-a)*t;}
function lerpAngle(a,b,t){
  let d=b-a;
  while(d>180)d-=360;while(d<-180)d+=360;
  return a+d*t;
}

function drawDrone(a){
  const cx=150,cy=150;
  dc.clearRect(0,0,300,300); //размер

  dc.strokeStyle='#1a1a2a';dc.lineWidth=1;dc.setLineDash([3,3]);
  dc.beginPath();dc.arc(cx,cy,90,0,Math.PI*2);dc.stroke();
  dc.beginPath();dc.arc(cx,cy,50,0,Math.PI*2);dc.stroke();
  dc.setLineDash([]);

  dc.fillStyle='#fff';dc.font='10px sans-serif';dc.textAlign='center';
  dc.fillText('N',cx,cy-95);dc.fillText('S',cx,cy+105);
  dc.fillText('E',cx+97,cy+4);dc.fillText('W',cx-97,cy+4);

  const tickAngles=[0,45,90,135,180,225,270,315];
  tickAngles.forEach(ta=>{
    const tr=ta*Math.PI/180;
    dc.strokeStyle='#2a2a3a';dc.lineWidth=1;
    dc.beginPath();
    dc.moveTo(cx+85*Math.sin(tr),cy-85*Math.cos(tr));
    dc.lineTo(cx+95*Math.sin(tr),cy-95*Math.cos(tr));
    dc.stroke();
  });

  dc.save();dc.translate(cx,cy);dc.rotate(a*Math.PI/180);

  dc.strokeStyle='#2a2a3a';dc.lineWidth=3;
  dc.beginPath();dc.moveTo(-62,-62);dc.lineTo(62,62);dc.stroke();
  dc.beginPath();dc.moveTo(62,-62);dc.lineTo(-62,62);dc.stroke();

  dc.strokeStyle='#333';dc.lineWidth=1.5;
  dc.beginPath();dc.moveTo(-62,-62);dc.lineTo(62,62);dc.stroke();
  dc.beginPath();dc.moveTo(62,-62);dc.lineTo(-62,62);dc.stroke();

  const arms=[[-62,-62],[62,-62],[62,62],[-62,62]];
  const cols=['#4af','#4af','#f84','#f84'];
  arms.forEach(([x,y],i)=>{
    dc.beginPath();dc.arc(x,y,20,0,Math.PI*2);
    dc.fillStyle='#12121e';dc.fill();
    dc.strokeStyle=cols[i];dc.lineWidth=2;dc.stroke();

    dc.beginPath();
    const start=-Math.PI/2;
    dc.arc(x,y,15,start,start+Math.PI*1.5);
    dc.strokeStyle=cols[i]+'99';dc.lineWidth=1.5;dc.stroke();
  });

  dc.beginPath();dc.arc(0,0,24,0,Math.PI*2);
  dc.fillStyle='#1e1e30';dc.fill();
  dc.strokeStyle='#4af';dc.lineWidth=2;dc.stroke();
  dc.beginPath();dc.arc(0,0,10,0,Math.PI*2);
  dc.fillStyle='#4af33';dc.fill();
  dc.strokeStyle='#4af';dc.lineWidth=1;dc.stroke();

  dc.restore();

  const ar=(a-90)*Math.PI/180;
  dc.strokeStyle='#4af';dc.lineWidth=2;
  dc.beginPath();dc.moveTo(cx,cy);
  dc.lineTo(cx+75*Math.cos(ar),cy+75*Math.sin(ar));
  dc.stroke();
  dc.beginPath();dc.arc(cx+75*Math.cos(ar),cy+75*Math.sin(ar),3,0,Math.PI*2);
  dc.fillStyle='#4af';dc.fill();
}

function drawPlane(x,y,z){
  const cx=150,cy=150,scale=0.08; /* центр*/
  pc.clearRect(0,0,300,300); /*размер всего*/

  for(let i=0;i<=10;i++){
    const gx=20+i*26,gy=20+i*26;
    pc.strokeStyle='#1c1c28';pc.lineWidth=1;
    pc.beginPath();pc.moveTo(gx,20);pc.lineTo(gx,280);pc.stroke();
    pc.beginPath();pc.moveTo(20,gy);pc.lineTo(280,gy);pc.stroke();
  }

  pc.strokeStyle='#333';pc.lineWidth=1.5;
  pc.beginPath();pc.moveTo(cx,20);pc.lineTo(cx,280);pc.stroke();
  pc.beginPath();pc.moveTo(20,cy);pc.lineTo(280,cy);pc.stroke();

  pc.fillStyle='#fff';pc.font='10px sans-serif';pc.textAlign='center';
  pc.fillText('+X',272,cy-6);pc.fillText('-X',30,cy-6);
  pc.textAlign='left';
  pc.fillText('+Y',cx+4,28);pc.fillText('-Y',cx+4,276);

  const px=Math.max(25,Math.min(275,cx+x*scale));
  const py=Math.max(25,Math.min(275,cy-y*scale));

  pc.strokeStyle='#4af44';pc.lineWidth=1;pc.setLineDash([3,4]);
  pc.beginPath();pc.moveTo(cx,cy);pc.lineTo(px,py);pc.stroke();
  dc.setLineDash([]);
  pc.setLineDash([]);

  const zn=Math.max(-2000,Math.min(2000,z));
  const zr=Math.abs(zn)/2000*20+6;
  pc.beginPath();pc.arc(px,py,zr,0,Math.PI*2);
  pc.strokeStyle=zn>=0?'#4f8':'#f84';pc.lineWidth=1.5;pc.stroke();

  pc.beginPath();pc.arc(px,py,6,0,Math.PI*2);
  pc.fillStyle='#4af';pc.fill();

  pc.fillStyle='#fff';pc.font='10px sans-serif';pc.textAlign='left';
  const zDir=zn>=0?'↑ вверх':'↓ вниз';
  pc.fillText('Z: '+Math.round(z)+' '+zDir,8,292);
}

function animate(){
  curAz=lerpAngle(curAz,tAz,LERP);
  curX=lerp(curX,tX,LERP);
  curY=lerp(curY,tY,LERP);
  curZ=lerp(curZ,tZ,LERP);
  drawDrone(curAz);
  drawPlane(curX,curY,curZ);
  requestAnimationFrame(animate);
}

async function update(){
  try{
    const r=await fetch('/data');
    const d=await r.json();
    let a=d.azimuth;if(a<0)a+=360;
    tAz=a;tX=d.x;tY=d.y;tZ=d.z;
    document.getElementById('vx').textContent=d.x;
    document.getElementById('vy').textContent=d.y;
    document.getElementById('vz').textContent=d.z;
    document.getElementById('azVal').textContent=a+'°';
  }catch(e){}
  setTimeout(update,300);
}

animate();update();
</script></body></html>
    )");
  });

  server.begin();
}

void loop() {
  server.handleClient();

  static unsigned long lastPrint = 0;
  if (millis() - lastPrint >= 1000) {
    lastPrint = millis();

    compass.read();
    int x = compass.getX();
    int y = compass.getY();
    int z = compass.getZ();
    int az = compass.getAzimuth();
    if (az < 0) az += 360; 
    Serial.print("X: "); Serial.print(x);
    Serial.print(" | Y: "); Serial.print(y);
    Serial.print(" | Z: "); Serial.print(z);
    Serial.print(" | Азимут: "); Serial.print(az);
    Serial.println("°");
  }
}