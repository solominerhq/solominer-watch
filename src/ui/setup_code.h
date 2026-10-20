#pragma once
#include <stddef.h>
#include <stdio.h>
namespace ui {
// Wi-Fi QR escaping is separate from display-font sanitization: scanners need
// the exact network bytes, including punctuation and UTF-8.
inline bool setupQrPayload(char* out, size_t capacity, const char* ssid, const char* password) {
  size_t used = 0;
  auto append = [&](const char* text, bool escape) {
    for (; *text; ++text) {
      const bool special = escape && (*text == ';' || *text == ':' || *text == ',' || *text == '"' || *text == '\\');
      if (used + (special ? 2u : 1u) >= capacity) return false;
      if (special) out[used++] = '\\';
      out[used++] = *text;
    }
    return true;
  };
  if (!out || !capacity || !ssid || !password) return false;
  bool ok = append("WIFI:T:WPA;S:", false) && append(ssid, true) && append(";P:", false) && append(password, true) && append(";;", false);
  out[ok ? used : 0] = 0;
  return ok;
}
}
