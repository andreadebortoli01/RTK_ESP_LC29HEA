#include <Arduino.h>

extern char lastGga[120];
extern int satelliteCount;
extern float ageDiff;
extern double lastLat, lastLon, lastAlt;
extern bool everRtcm;
extern char fixDetail[48];
extern bool rtcmReceived;
extern char fixType[32];
extern float lastVel;
extern float epe3d;

String base64Encode(const String &input)
{
    const char base64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    String output;
    int val = 0, valb = -6;
    for (uint8_t c : input)
    {
        val = (val << 8) + c;
        valb += 8;
        while (valb >= 0)
        {
            output += base64_table[(val >> valb) & 0x3F];
            valb -= 6;
        }
    }
    if (valb > -6)
        output += base64_table[((val << 8) >> (valb + 8)) & 0x3F];
    while (output.length() % 4)
        output += '=';
    return output;
}

String ggaWithChecksum(const char *line)
{
    byte cs = 0;
    for (uint16_t i = 1; line[i] && line[i] != '*' && line[i] != '\r' && line[i] != '\n'; i++)
        cs ^= line[i];
    String out = line;
    out += '*';
    if (cs < 16)
        out += '0';
    String hex = String(cs, HEX);
    hex.toUpperCase();
    out += hex;
    out += "\r\n";
    return out;
}

float toDecimal(const char *val, const char *dir)
{
    if (strlen(val) < 6)
        return 0.0;
    float raw = atof(val);
    int deg = int(raw / 100);
    float min = raw - (deg * 100);
    float dec = deg + min / 60.0;
    return (dir[0] == 'S' || dir[0] == 'W') ? -dec : dec;
}

void parseNMEA_GGA(const char *line)
{
    strncpy(lastGga, line, sizeof(lastGga) - 1);
    lastGga[sizeof(lastGga) - 1] = 0;
    char copy[120];
    strncpy(copy, line, sizeof(copy) - 1);
    copy[sizeof(copy) - 1] = 0;
    char *p = copy;
    char *parts[15];
    int n = 0;
    while (n < 15 && (parts[n] = strtok(n == 0 ? p : NULL, ",")))
        n++;
    if (n < 10)
        return;

    int fix = atoi(parts[6]);
    satelliteCount = atoi(parts[7]);
    if (n >= 14 && strlen(parts[13]) > 0)
        ageDiff = atof(parts[13]);

    if (fix == 5 || fix == 4)
    {
        if (strlen(parts[2]) >= 6)
            lastLat = toDecimal(parts[2], parts[3]);
        if (strlen(parts[4]) >= 6)
            lastLon = toDecimal(parts[4], parts[5]);
        lastAlt = atof(parts[9]);
    }

    if (!everRtcm)
        strncpy(fixDetail, "Mai ricevuto RTCM", sizeof(fixDetail));
    else if (!rtcmReceived)
        strncpy(fixDetail, "Attesa dati RTCM...", sizeof(fixDetail));
    else if (fix == 0)
        strncpy(fixDetail, "No Fix", sizeof(fixDetail));
    else if (fix == 1)
        strncpy(fixDetail, "Standalone GPS", sizeof(fixDetail));
    else if (fix == 2)
        strncpy(fixDetail, "Differenziale (DGPS/SBAS)", sizeof(fixDetail));
    else if (fix == 4)
        strncpy(fixDetail, "RTK FIXED", sizeof(fixDetail));
    else if (fix == 5)
        strncpy(fixDetail, "RTK Float", sizeof(fixDetail));
    else
        strncpy(fixDetail, "Altro stato GNSS", sizeof(fixDetail));

    switch (fix)
    {
    case 1:
        strncpy(fixType, "GPS FIX", sizeof(fixType));
        break;
    case 2:
        strncpy(fixType, "DGPS", sizeof(fixType));
        break;
    case 5:
        strncpy(fixType, "RTK FLOAT", sizeof(fixType));
        break;
    case 4:
        strncpy(fixType, "RTK FIX", sizeof(fixType));
        break;
    default:
        strncpy(fixType, "No Fix", sizeof(fixType));
    }
}

void parseNMEA_RMC(const char *line)
{
    char copy[120];
    strncpy(copy, line, sizeof(copy) - 1);
    copy[sizeof(copy) - 1] = 0;
    char *p = copy;
    char *parts[13];
    int n = 0;
    while (n < 13 && (parts[n] = strtok(n == 0 ? p : NULL, ",")))
        n++;
    if (n < 8)
        return;
    if (strlen(parts[7]) > 0)
    {
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