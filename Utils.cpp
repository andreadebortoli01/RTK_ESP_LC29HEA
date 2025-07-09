#include <Arduino.h>

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