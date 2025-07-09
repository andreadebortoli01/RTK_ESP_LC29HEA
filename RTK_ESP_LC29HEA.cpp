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
float epe3d = -1.0;
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

String base64Encode(const String& input);
String ggaWithChecksum(const char* line);
float toDecimal(const char* val, const char* dir);
void parseNMEA_RMC(const char* line);
void parseNMEA_GGA(const char *line);
void parseNMEA_PQTMEPE(const char* line);
void handleRoot();


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

// --- Configurazione GNSS
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

