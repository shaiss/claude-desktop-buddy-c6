#include "manifest.h"
#include <Arduino.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <string.h>

const char* const MANIFEST_STATE_NAMES[MANIFEST_N_STATES] = {
  "sleep", "idle", "busy", "attention", "celebrate", "dizzy", "heart"
};

static uint16_t parseHexColor(const char* s, uint16_t fallback) {
  if (!s) return fallback;
  if (*s == '#') s++;
  uint32_t v = strtoul(s, nullptr, 16);
  return (uint16_t)(((v >> 19) & 0x1F) << 11 | ((v >> 10) & 0x3F) << 5 | ((v >> 3) & 0x1F));
}

bool manifestLoad(const char* dirName, Manifest* out) {
  char mpath[64];
  snprintf(mpath, sizeof(mpath), "/characters/%s/manifest.json", dirName);

  File mf = LittleFS.open(mpath, "r");
  if (!mf) {
    Serial.printf("[char] manifest not found: %s\n", mpath);
    return false;
  }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, mf);
  mf.close();
  if (err) {
    Serial.printf("[char] manifest parse: %s\n", err.c_str());
    return false;
  }

  const char* nm = doc["name"];
  strncpy(out->name, nm ? nm : dirName, sizeof(out->name) - 1);
  out->name[sizeof(out->name) - 1] = 0;

  JsonObject colors = doc["colors"];
  out->colors.body    = parseHexColor(colors["body"],    out->colors.body);
  out->colors.bg      = parseHexColor(colors["bg"],      out->colors.bg);
  out->colors.text    = parseHexColor(colors["text"],    out->colors.text);
  out->colors.textDim = parseHexColor(colors["textDim"], out->colors.textDim);
  out->colors.ink     = parseHexColor(colors["ink"],     out->colors.ink);

  const char* mode = doc["mode"];
  out->textMode = (mode && strcmp(mode, "text") == 0);

  JsonObject states = doc["states"];

  if (out->textMode) {
    for (uint8_t i = 0; i < MANIFEST_N_STATES; i++) {
      TextState& ts = out->textStates[i];
      ts.nFrames = 0;
      ts.delayMs = 200;
      JsonObject st = states[MANIFEST_STATE_NAMES[i]];
      if (st.isNull()) continue;
      ts.delayMs = st["delay"] | 200;
      JsonArray fr = st["frames"];
      for (JsonVariant v : fr) {
        if (ts.nFrames >= 8) break;
        const char* s = v.as<const char*>();
        strncpy(ts.frames[ts.nFrames], s ? s : "", 19);
        ts.frames[ts.nFrames][19] = 0;
        ts.nFrames++;
      }
    }
    return true;
  }

  out->gifTotal = 0;
  for (uint8_t i = 0; i < MANIFEST_N_STATES; i++) {
    out->stateStart[i] = out->gifTotal;
    out->stateCount[i] = 0;
    JsonVariant v = states[MANIFEST_STATE_NAMES[i]];
    if (v.is<JsonArray>()) {
      for (JsonVariant e : v.as<JsonArray>()) {
        if (out->gifTotal >= MANIFEST_MAX_GIFS) break;
        const char* fn = e.as<const char*>();
        if (fn) {
          snprintf(out->gifPaths[out->gifTotal], 32, "%s", fn);
          out->gifTotal++;
          out->stateCount[i]++;
        }
      }
    } else {
      const char* fn = v.as<const char*>();
      if (fn) {
        snprintf(out->gifPaths[out->gifTotal], 32, "%s", fn);
        out->gifTotal++;
        out->stateCount[i] = 1;
      }
    }
  }
  return true;
}
