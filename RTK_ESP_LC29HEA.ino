#include <WiFi.h>
#include <WebServer.h>
#include <MPU9250_asukiaaa.h>
#include <Wire.h>


// ===== CONFIGURAZIONE =====
const char WIFI_SSID[] = "Clique SRL";
const char WIFI_PASS[] = "CliqueWiFi";
const char* NTRIP_CASTER = "194.105.50.232";
const int NTRIP_PORT = 2101;
const char* MOUNTPOINT = "ROVE04GGG";
const char* NTRIP_USERNAME = "Andrea_Debo01";
const char* NTRIP_PASSWORD = "NtripDebo";
#define GNSS_BAUD 460800
MPU9250_asukiaaa IMU;

WiFiClient client;
WebServer server(80);

char nmeaLine[120];
int nmeaPos = 0;
char lastGga[120] = "";
unsigned long lastGgaSent = 0;
int satelliteCount = -1;
double lastLat = 0.0, lastLon = 0.0, lastAlt = 0.0;
char fixType[32] = "No Fix";
char fixDetail[48] = "";
char statusMsg[80] = "GNSS non risponde";
bool gnssActive = false;
char rawLog[801];
int rawLogLen = 0;
unsigned long rtcmBytes = 0, lastRtcmTime = 0;
bool rtcmReceived = false, everRtcm = false;
byte rtcmSample[100];
int rtcmSampleLen = 0;
bool gnssCfgDone = false;
unsigned long gnssCfgStart = 0, gnssCfgEnd = 0;
String ntripStatus = "In attesa...";
bool headerEnded = false, sentFirstGGA = false;
String headerBuffer = "";
float ageDiff = -1.0;
float lastVel = 0.0;
#define CPU_AVG_LEN 100
float cpuUsage = 0;
float cpuUsageBuffer[CPU_AVG_LEN] = { 0 };
int cpuUsageIndex = 0;
unsigned long lastPrintAcc = 0;
unsigned long lastPrintPos = 0;

// --- Precisione 3D ---
float epe3d = -1.0;  // Valore aggiornato dal messaggio $PQTMEPE

#define RTCM_LOG_SIZE 4096
byte rtcmLog[RTCM_LOG_SIZE];
unsigned int rtcmLogLen = 0;

#define MAX_LOG_LINES 20
String logLines[MAX_LOG_LINES];
int logPos = 0;
void addLog(const String& msg) {
  logLines[logPos] = "[" + String(millis() / 1000) + "s] " + msg;
  logPos = (logPos + 1) % MAX_LOG_LINES;
}

// --- Base64 encode ---
String base64Encode(const String& input) {
  const char base64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  String output;
  int val = 0, valb = -6;
  for (uint8_t c : input) {
    val = (val << 8) + c;
    valb += 8;
    while (valb >= 0) {
      output += base64_table[(val >> valb) & 0x3F];
      valb -= 6;
    }
  }
  if (valb > -6) output += base64_table[((val << 8) >> (valb + 8)) & 0x3F];
  while (output.length() % 4) output += '=';
  return output;
}

// --- Configurazione GNSS SOLO RTK + Abilitazione PQTMEPE ---
void setupGNSS_RTK() {
  Serial2.println("$PQTMCFGCNST,1,1,1,1,1,1*3E");
  delay(200);
  Serial2.println("$PQTMCFGFIXRATE,100*27");
  delay(200);
  Serial2.println("$PQTMCFGNMEADP,W,3,8,3,3,3,3*39");
  delay(200);
  Serial2.println("$PQTMCFGMSGRATE,W,PQTMEPE,1,2*1D");
  delay(200);
  Serial2.println("$PQTMSAVEPAR*5A");
  delay(200);
}

String ggaWithChecksum(const char* line) {
  byte cs = 0;
  for (uint16_t i = 1; line[i] && line[i] != '*' && line[i] != '\r' && line[i] != '\n'; i++) cs ^= line[i];
  String out = line;
  out += '*';
  if (cs < 16) out += '0';
  String hex = String(cs, HEX);
  hex.toUpperCase();
  out += hex;
  out += "\r\n";
  return out;
}

float toDecimal(const char* val, const char* dir) {
  if (strlen(val) < 6) return 0.0;
  float raw = atof(val);
  int deg = int(raw / 100);
  float min = raw - (deg * 100);
  float dec = deg + min / 60.0;
  return (dir[0] == 'S' || dir[0] == 'W') ? -dec : dec;
}

void parseNMEA_GGA(const char* line) {
  strncpy(lastGga, line, sizeof(lastGga) - 1);
  lastGga[sizeof(lastGga) - 1] = 0;
  char copy[120];
  strncpy(copy, line, sizeof(copy) - 1);
  copy[sizeof(copy) - 1] = 0;
  char* p = copy;
  char* parts[15];
  int n = 0;
  while (n < 15 && (parts[n] = strtok(n == 0 ? p : NULL, ","))) n++;
  if (n < 10) return;

  int fix = atoi(parts[6]);
  satelliteCount = atoi(parts[7]);
  if (n >= 14 && strlen(parts[13]) > 0) ageDiff = atof(parts[13]);

  if (fix == 5 || fix == 4) {
    if (strlen(parts[2]) >= 6) lastLat = toDecimal(parts[2], parts[3]);
    if (strlen(parts[4]) >= 6) lastLon = toDecimal(parts[4], parts[5]);
    lastAlt = atof(parts[9]);
  }

  if (!everRtcm) strncpy(fixDetail, "Mai ricevuto RTCM", sizeof(fixDetail));
  else if (!rtcmReceived) strncpy(fixDetail, "Attesa dati RTCM...", sizeof(fixDetail));
  else if (fix == 0) strncpy(fixDetail, "No Fix", sizeof(fixDetail));
  else if (fix == 1) strncpy(fixDetail, "Standalone GPS", sizeof(fixDetail));
  else if (fix == 2) strncpy(fixDetail, "Differenziale (DGPS/SBAS)", sizeof(fixDetail));
  else if (fix == 4) strncpy(fixDetail, "RTK FIXED", sizeof(fixDetail));
  else if (fix == 5) strncpy(fixDetail, "RTK Float", sizeof(fixDetail));
  else strncpy(fixDetail, "Altro stato GNSS", sizeof(fixDetail));

  switch (fix) {
    case 1: strncpy(fixType, "GPS FIX", sizeof(fixType)); break;
    case 2: strncpy(fixType, "DGPS", sizeof(fixType)); break;
    case 5: strncpy(fixType, "RTK FLOAT", sizeof(fixType)); break;
    case 4: strncpy(fixType, "RTK FIX", sizeof(fixType)); break;
    default: strncpy(fixType, "No Fix", sizeof(fixType));
  }
}

void parseNMEA_RMC(const char* line) {
  char copy[120];
  strncpy(copy, line, sizeof(copy) - 1);
  copy[sizeof(copy) - 1] = 0;
  char* p = copy;
  char* parts[13];
  int n = 0;
  while (n < 13 && (parts[n] = strtok(n == 0 ? p : NULL, ","))) n++;
  if (n < 8) return;
  if (strlen(parts[7]) > 0) {
    float sog = atof(parts[7]);
    lastVel = sog * 0.514444;
  }
}

void parseNMEA_PQTMEPE(const char* line) {
  char copy[120];
  strncpy(copy, line, sizeof(copy) - 1);
  copy[sizeof(copy) - 1] = 0;
  char* parts[10];
  int n = 0;
  char* p = strtok(copy, ",");
  while (p && n < 10) {
    parts[n++] = p;
    p = strtok(NULL, ",");
  }
  if (n >= 7) {
    float val = atof(parts[6]);
    if (val > 0 && val < 99) epe3d = val;
  }
}


void handleRoot();

void setup() {
  // ================================
  Serial.begin(115200);
  Serial2.begin(GNSS_BAUD, SERIAL_8N1, 16, 17);
  delay(1800);
  addLog("Boot...");
  if (!gnssCfgDone) {
    setupGNSS_RTK();
    gnssCfgDone = true;
    delay(2500);
    addLog("GNSS coldstart (2.5s)");
  }

  addLog("Connessione WiFi...");
  IPAddress ip(192, 168, 178, 96);
  IPAddress gateway(192, 168, 178, 1);
  IPAddress subnet(255, 255, 255, 0);
  WiFi.config(ip, gateway, subnet);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    addLog("Attesa WiFi...");
  }

  addLog("WiFi connesso.");
  delay(2000);

  if (!client.connect(NTRIP_CASTER, NTRIP_PORT)) {
    strncpy(statusMsg, "Errore connessione caster", sizeof(statusMsg));
    ntripStatus = "Errore di connessione caster NTRIP";
    addLog("Errore connessione caster NTRIP");
    delay(5000);
    ESP.restart();
  } else {
    strncpy(statusMsg, "Connesso al caster", sizeof(statusMsg));
    ntripStatus = "Connesso al caster NTRIP";
    addLog("Connesso al caster NTRIP");
  }

  String credentials = String(NTRIP_USERNAME) + ":" + String(NTRIP_PASSWORD);
  String base64Creds = base64Encode(credentials);

  client.print(String("GET /") + MOUNTPOINT + " HTTP/1.0\r\n" + "User-Agent: NTRIP GNSS ESP32\r\n" + "Authorization: Basic " + base64Creds + "\r\n" + "Ntrip-Version: Ntrip/2.0\r\n\r\n");
  addLog("Richiesta NTRIP/RTCM...");

  server.on("/", handleRoot);
  server.on("/api", []() {
    String json = "{";
    json += "\"lat\":" + String(lastLat, 8);
    json += ",\"lon\":" + String(lastLon, 8);
    json += ",\"alt\":" + String(lastAlt, 2);
    json += ",\"sats\":" + String(satelliteCount);
    json += ",\"fix\":\"" + String(fixType) + "\"";
    json += ",\"rtk\":\"" + String(fixDetail) + "\"";
    json += ",\"epe3d\":" + String(epe3d, 3);
    json += ",\"agediff\":" + String(ageDiff, 1);
    json += ",\"rtcm\":" + String(rtcmBytes);
    json += ",\"gnssact\":" + String(gnssActive ? "true" : "false");
    json += ",\"status\":\"" + String(statusMsg) + "\"";
    json += ",\"ntrip\":\"" + ntripStatus + "\"";
    json += ",\"rssi\":" + String(WiFi.RSSI());
    json += ",\"vel\":" + String(lastVel, 2);
    // --- AGGIUNTA CPU USAGE ---
    json += ",\"cpuusage\":" + String(cpuUsage, 1);
    // --------------------------
    json += "}";
    server.send(200, "application/json", json);
  });
  server.begin();
  addLog("Webserver pronto su porta 80");


  Wire.begin(21, 22);  // SDA, SCL (cambia se usi altri pin)
  IMU.beginAccel();
  IMU.beginGyro();
}

void loop() {
  unsigned long loopStart = micros(); 
  unsigned long currentMillis = millis();
  IMU.accelUpdate();
  IMU.gyroUpdate();

  // Accelerometro
  float ax = IMU.accelX();
  float ay = IMU.accelY();
  float az = IMU.accelZ();
  // Giroscopio
  float gx = IMU.gyroX();
  float gy = IMU.gyroY();
  float gz = IMU.gyroZ();

  server.handleClient();

  while (client.available()) {
    char b = client.read();
    if (!headerEnded) {
      headerBuffer += b;
      if (headerBuffer.indexOf("ICY 200 OK") >= 0 || headerBuffer.endsWith("\r\n\r\n") || headerBuffer.endsWith("\n\n")) {
        headerEnded = true;
        addLog("Inizio ricezione dati RTCM dal caster");
      }
      continue;
    }
    if (headerEnded && !sentFirstGGA && strlen(lastGga) > 0) {
      client.print(ggaWithChecksum(lastGga));
      lastGgaSent = millis();
      sentFirstGGA = true;
      addLog("Invio primo GGA al caster");
    }
    Serial2.write(b);
    if (rtcmSampleLen < 100) rtcmSample[rtcmSampleLen++] = (byte)b;
    if (rtcmLogLen < RTCM_LOG_SIZE) rtcmLog[rtcmLogLen++] = (byte)b;
    rtcmBytes++;
    rtcmReceived = true;
    everRtcm = true;
    lastRtcmTime = millis();
  }

  if (headerEnded && millis() - lastGgaSent > 300 && strlen(lastGga) > 0) {
    client.print(ggaWithChecksum(lastGga));
    lastGgaSent = millis();
  }

  while (Serial2.available()) {
    gnssActive = true;
    char c = Serial2.read();
    if (rawLogLen < 800) rawLog[rawLogLen++] = c;
    else {
      memmove(rawLog, rawLog + 1, 799);
      rawLog[799] = c;
    }
    if (c == '\n') {
      nmeaLine[nmeaPos] = 0;
      if (strncmp(nmeaLine, "$GNGGA", 6) == 0)
        parseNMEA_GGA(nmeaLine);
      if (strncmp(nmeaLine, "$GNRMC", 6) == 0)
        parseNMEA_RMC(nmeaLine);
      if (strncmp(nmeaLine, "$PQTMEPE", 8) == 0)
        parseNMEA_PQTMEPE(nmeaLine);
      nmeaPos = 0;
    } else if (c != '\r' && nmeaPos < sizeof(nmeaLine) - 1) {
      nmeaLine[nmeaPos++] = c;
    }
  }

  static int prevFix = -1;
  if (rtcmReceived && (millis() - lastRtcmTime > 3500)) {
    strncpy(statusMsg, "Nessun dato RTCM recente (timeout)", sizeof(statusMsg));
    if (prevFix != 0) addLog("Nessun dato RTCM recente (timeout)");
    rtcmReceived = false;
    prevFix = 0;
  } else if (rtcmReceived) {
    strncpy(statusMsg, "RTCM in ricezione (ok)", sizeof(statusMsg));
    if (prevFix != 1) addLog("RTCM in ricezione (ok)");
    prevFix = 1;
  } else if (everRtcm) {
    strncpy(statusMsg, "RTCM non aggiornato", sizeof(statusMsg));
    if (prevFix != 2) addLog("RTCM non aggiornato");
    prevFix = 2;
  } else {
    strncpy(statusMsg, "Mai ricevuto RTCM!", sizeof(statusMsg));
    if (prevFix != 3) addLog("Mai ricevuto RTCM!");
    prevFix = 3;
  }
  yield();

  if (currentMillis - lastPrintAcc >= 100) {
    lastPrintAcc = currentMillis;
    Serial.print("Acc");
    Serial.print(",");
    Serial.print(ax, 4);
    Serial.print(",");
    Serial.print(ay, 4);
    Serial.print(",");
    Serial.print(az, 4);
    Serial.print(",");
    Serial.print(gx, 4);
    Serial.print(",");
    Serial.print(gy, 4);
    Serial.print(",");
    Serial.println(gz, 4);
  }

  if (currentMillis - lastPrintPos >= 1000) {
    lastPrintPos = currentMillis;
    Serial.print("Pos");
    Serial.print(",");
    Serial.print(lastLat, 6);
    Serial.print(",");
    Serial.print(lastLon, 6);
    Serial.print(",");
    Serial.println(lastAlt, 2);
  }

  unsigned long loopElapsed = micros() - loopStart;
  float loopMs = loopElapsed / 1000.0;
  float usage = (loopMs > 100.0) ? 100.0 : (loopMs < 0 ? 0.0 : loopMs);

  // Salva nel buffer circolare
  cpuUsageBuffer[cpuUsageIndex] = usage;
  cpuUsageIndex = (cpuUsageIndex + 1) % CPU_AVG_LEN;

  // Calcola media
  float sum = 0;
  for (int i = 0; i < CPU_AVG_LEN; i++) {
    sum += cpuUsageBuffer[i];
  }
  cpuUsage = sum / CPU_AVG_LEN;
}

// --- Pagina web con logging e zoom mappa ---
void handleRoot() {
  String html = "<!DOCTYPE html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += R"rawliteral(
  <link rel="stylesheet" href="https://unpkg.com/leaflet@1.9.4/dist/leaflet.css"/>
  <script src="https://unpkg.com/leaflet@1.9.4/dist/leaflet.js"></script>
  <script src="https://cdn.jsdelivr.net/npm/chart.js"></script>
  <style>
    @import url('https://fonts.googleapis.com/css2?family=Montserrat:wght@600;900&family=Fira+Mono&display=swap');
    body { margin: 0; padding: 0; min-height: 100vh;
      background: linear-gradient(120deg,#161e2a 0%, #13151a 100%);
      color: #f6fcff; font-family: 'Montserrat', Arial, sans-serif;
      display: flex; flex-direction: column; align-items: center;}
    .container { width: 100%; max-width: 780px; margin: 0 auto;
      display: flex; flex-direction: column; align-items: center; }
    h1 { font-size: 2.3em; font-weight: 900; color: #6ee8f9;
      margin: 28px 0 10px 0; letter-spacing: 1.5px;
      text-shadow: 0 3px 18px #239ddb40;}
    .datagrid { width: 100%; max-width: 700px; background: #181b22ee;
      border-radius: 20px; box-shadow: 0 0 30px #132b4488;
      margin-bottom: 30px; overflow: hidden;}
    table { border-collapse: collapse; width: 100%; font-size: 1.18em;}
    th, td { padding: 16px 12px; text-align: center;}
    th { background: #222634; color: #56ffe8; font-size: 1em;
      letter-spacing: 0.5px; font-weight: 700;
      text-shadow: 0 2px 10px #22fdff13;
      border-bottom: 2px solid #5ad5ff22;}
    td { background: #191c25bb; color: #fff; font-size: 1em; font-weight: 700;
      border-bottom: 1px solid #2c2e3a40;}
    tr:last-child td, tr:last-child th { border-bottom: none;}
    .value { font-weight: 800; font-size: 1.09em;}
    .fix { color: #ffe877; text-shadow: 0 1px 10px #ffc20055;}
    .rtk { color: #57ffd7;}
    .badge { display: inline-block; padding: 2px 10px; border-radius: 16px;
      font-size: 0.98em; margin-left: 6px; background: #191f26; color: #a3faff; box-shadow: 0 1px 8px #29e0e533;}
    #map { width: 99vw; max-width: 760px; height: 320px; border-radius: 22px; margin: 0 auto 18px auto;
      border: 3px solid #20b9c6; box-shadow: 0 2px 30px #0ae6ff40; background: #16222e;}
    #epe3dChart { background: #191c24; border-radius: 17px; box-shadow: 0 0 10px #29f3fc55; margin-top: 7px;}
    .button-row { margin: 22px 0 16px 0; display: flex; justify-content: center; gap: 16px;}
    .btnlog { font-family: 'Montserrat', sans-serif; padding: 12px 28px; font-size: 1.13em;
      border-radius: 11px; border: none; font-weight: 800; cursor: pointer;
      box-shadow: 0 3px 16px #24aaf322; background: #2a3043; color: #aaffee;
      transition: .16s; outline: none; border: 2px solid #2aebb3; position: relative; z-index: 2;}
    .btnlog.active { background: #29f1bc; color: #181a20; border-color: #4ef3c6; box-shadow: 0 2px 17px #24e3e970;}
    .btncsv { background: linear-gradient(90deg,#52dfff 60%,#0f8bf9 100%); color: #1b1b23; border: 0; font-weight: 900; text-shadow: 0 2px 8px #1cc2ff55;}
    .btnlog:active { transform: scale(0.98);}
    @media (max-width:700px){ .datagrid { font-size: 0.99em; } #map { height: 160px;} h1 { font-size: 1.2em; } }
  </style>
  )rawliteral";
  html += "<body><div class='container'><h1>RTK GNSS LIVE</h1>";
  html += "<div class='datagrid'><table id='gnsstable'>";
  html += "<tr><th>Latitudine</th><td class='value' id='lat'>...</td></tr>";
  html += "<tr><th>Longitudine</th><td class='value' id='lon'>...</td></tr>";
  html += "<tr><th>Altitudine</th><td class='value' id='alt'>...</td></tr>";
  html += "<tr><th>Satelliti</th><td class='value' id='sats'>...</td></tr>";
  html += "<tr><th>Fix</th><td class='value fix' id='fix'>...</td></tr>";
  html += "<tr><th>Fase RTK</th><td class='value rtk' id='rtk'>...</td></tr>";
  html += "<tr><th>Precisione 3D</th><td class='value' id='epe3d'>...</td></tr>";
  html += "<tr><th>Età correzione</th><td class='value' id='agediff'>...</td></tr>";
  html += "<tr><th>Bytes RTCM</th><td class='value' id='rtcm'>...</td></tr>";
  html += "<tr><th>Stato GNSS</th><td id='status'>...</td></tr>";
  html += "<tr><th>Stato NTRIP</th><td id='ntrip'>...</td></tr>";
  html += "<tr><th>Stato WiFi</th><td class='value' id='wifi'>...</td></tr>";
  html += "<tr><th>Velocità</th><td class='value' id='velocita'>...</td></tr>";
  // ---- AGGIUNTA RIGA CPU USAGE ----
  html += "<tr><th>Utilizzo CPU</th><td class='value' id='cpuusage'>...</td></tr>";
  // ---------------------------------
  html += "</table></div>";

  html += "<div class='button-row'><button class='btnlog' id='btnStart'>⏺ Avvia Log</button>";
  html += "<button class='btnlog btncsv' id='btnDownload' style='display:none'>⬇ Scarica CSV</button>";
  html += "<button class='btnlog' id='btnClear' style='display:none'>🧹 Reset Log</button></div>";

  html += "<div id='map'></div>";
  html += "<h3>Grafico Precisione 3D</h3><canvas id='epe3dChart' width='720' height='155'></canvas>";

  html += R"rawliteral(
<script>
let map, marker, poly;
let logData = JSON.parse(localStorage.getItem('gnssLog') || '[]');
let logActive = localStorage.getItem('gnssLogActive')==="1";
let logStartTime = parseInt(localStorage.getItem('gnssLogStartTime')||"0");
let btnStart, btnDownload, btnClear, ctx, epe3dChart;
let epe3dHist = JSON.parse(localStorage.getItem('epe3dHist')||'[]');
let lastCoords = null;

function updateButtons() {
  btnStart.textContent = logActive ? "⏸ Ferma Log" : "⏺ Avvia Log";
  btnStart.classList.toggle("active", logActive);
  btnDownload.style.display = logData.length>0 ? "" : "none";
  btnClear.style.display = logData.length>0 ? "" : "none";
}

function updateMap(lat,lon){
  if(!map){
    map = L.map('map').setView([lat,lon], 20);
    L.tileLayer('https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png',{maxZoom:22, attribution: ''}).addTo(map);
    map.on('moveend', function() { });
  }
  lastCoords = [lat,lon];
  if(marker){ marker.remove(); }
  marker = L.circleMarker([lat, lon],{radius:13, fillColor:'#13ffe2',color:'#0cc',weight:3,fillOpacity:0.9}).addTo(map);
}

function updateChart(){
  if(!ctx){ ctx = document.getElementById('epe3dChart').getContext('2d'); }
  if(epe3dChart){ epe3dChart.destroy(); }
  let labels = [];
  for(let i=0;i<epe3dHist.length;i++) labels.push('');
  epe3dChart = new Chart(ctx, {
    type: 'line',
    data: {
      labels: labels,
      datasets: [{
        label: 'Precisione 3D (m)',
        data: epe3dHist,
        borderColor: 'rgba(13,255,240,1)',
        backgroundColor: 'rgba(13,255,240,0.12)',
        fill: true,
        tension: 0.21,
        pointRadius: 0,
        borderWidth: 3,
      }]
    },
    options: {
      plugins: { legend: {display: false} },
      animation: false,
      scales: {
        x: {display:false},
        y: {
          beginAtZero:true,
          ticks: {color:'#aefcfe'},
          grid:{color:'#24494c66'}
        }
      }
    }
  });
}

function updateTable(d){
  document.getElementById('lat').textContent = (d.lat!==undefined ? d.lat.toFixed(8)+" °" : "...");
  document.getElementById('lon').textContent = (d.lon!==undefined ? d.lon.toFixed(8)+" °" : "...");
  document.getElementById('alt').textContent = (d.alt!==undefined ? d.alt.toFixed(2)+" m" : "...");
  document.getElementById('sats').textContent = d.sats!==undefined ? d.sats : "...";
  document.getElementById('fix').textContent = d.fix ? d.fix : "...";
  document.getElementById('rtk').textContent = d.rtk||"...";
  document.getElementById('epe3d').textContent = (d.epe3d!==undefined && d.epe3d>=0 ? d.epe3d.toFixed(3)+" m" : "N/D");
  document.getElementById('agediff').textContent = (d.agediff!==undefined && d.agediff>=0 ? d.agediff.toFixed(1)+" s" : "N/D");
  document.getElementById('rtcm').textContent = d.rtcm!==undefined ? d.rtcm + " B" : "...";
  document.getElementById('status').textContent = d.status||"...";
  document.getElementById('ntrip').textContent = d.ntrip||"...";
  document.getElementById('wifi').textContent = d.rssi!==undefined ? (d.rssi + " dBm") : "...";
  document.getElementById('velocita').textContent = (d.vel!==undefined ? d.vel.toFixed(2) + " m/s" : "...");
  // ---- AGGIUNTA JS: mostra CPU usage ----
  document.getElementById('cpuusage').textContent = (d.cpuusage!==undefined ? d.cpuusage.toFixed(1)+" %" : "...");
  // ---------------------------------------
}

function fetchData(){
  fetch('/api').then(r=>r.json()).then(d=>{
    if(!d || d.lat===undefined || Math.abs(d.lat)<0.01 || Math.abs(d.lon)<0.01){
      document.getElementById('map').style.display='none';
      return;
    } else {
      document.getElementById('map').style.display='';
    }
    updateTable(d);
    if(logActive){
      let now = Math.round(Date.now()/1000);
      if(!logStartTime){logStartTime=now;localStorage.setItem('gnssLogStartTime',logStartTime);}
      // log: tempo, lat, lon, alt, epe3d
      logData.push([now-logStartTime, d.lat, d.lon, d.alt, d.epe3d]);
      localStorage.setItem('gnssLog', JSON.stringify(logData));
    }
    if(d.epe3d!==undefined && !isNaN(d.epe3d)){
      epe3dHist.push(d.epe3d);
      if(epe3dHist.length>100) epe3dHist = epe3dHist.slice(epe3dHist.length-100);
      localStorage.setItem('epe3dHist',JSON.stringify(epe3dHist));
    }
    updateMap(d.lat, d.lon);
    updateChart();
  });
}
window.onload = function(){
  btnStart = document.getElementById('btnStart');
  btnDownload = document.getElementById('btnDownload');
  btnClear = document.getElementById('btnClear');
  updateButtons();
  fetchData();
  setInterval(fetchData, 100);
  btnStart.onclick = function(){
    logActive = !logActive;
    localStorage.setItem('gnssLogActive', logActive ? "1":"0");
    if(!logActive) localStorage.setItem('gnssLogStartTime', "");
    updateButtons();
  };
  btnDownload.onclick = function(){
    if(logData.length==0) return;
    let csv = "t_s,lat,lon,alt,epe3d\n";
    logData.forEach(e=>{
      csv += e[0]+","+e[1]+","+e[2]+","+e[3]+","+e[4]+"\n";
    });
    let blob = new Blob([csv], {type: "text/csv"});
    let url = URL.createObjectURL(blob);
    let a = document.createElement('a');
    a.href = url; a.download = "gnss_log.csv"; document.body.appendChild(a);
    a.click(); setTimeout(()=>{URL.revokeObjectURL(url);a.remove();},300);
  };
  btnClear.onclick = function(){
    localStorage.removeItem('gnssLog');
    localStorage.removeItem('gnssLogStartTime');
    logData = [];
    updateButtons();
    location.reload();
  };
};
</script>
</body></html>
)rawliteral";
  server.send(200, "text/html", html);
}


