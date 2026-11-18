#include "BoxIO.h"
#include <stdlib.h>
#include <string.h>

#if defined(__AVR__)
#include <avr/pgmspace.h>
#define FLASH_LIT(s) PSTR(s)
#else
#define FLASH_LIT(s) s
#endif

static BoxWriteHandler gHandlers[BOXIO_MAX_HANDLERS];

#define BOXIO_MAX3(a, b, c) ((a) > (b) ? ((a) > (c) ? (a) : (c)) : ((b) > (c) ? (b) : (c)))
// One request buffer instead of a raw copy, an encoded copy, and a path copy.
static char ioScratch[BOXIO_MAX3(BOXIO_VW_PATH, BOXIO_PROP_PATH, BOXIO_BUF)];

BoxIOWriteReg::BoxIOWriteReg(int pin, BoxWriteHandler handler) {
  if (pin >= 0 && pin < BOXIO_MAX_HANDLERS) {
    gHandlers[pin] = handler;
  }
}

BoxIOClass BoxIO;

void BoxIOClass::setAuth(const char* key) { _key = key ? key : ""; }

void BoxIOClass::setServer(const char* host, uint16_t port) {
  _host = host ? host : "127.0.0.1";
  _port = port;
  _usePort = port;
  _portRetryAt = 0;
}

void BoxIOClass::config(const char* key, const char* host, uint16_t port) {
  setAuth(key);
  setServer(host, port);
}

void BoxIOClass::begin(Client& client) {
  _client = &client;
  _alive = false;
  for (int i = 0; i < BOXIO_MAX_PENDING; i++) _pending[i].used = false;
  _windowStart = millis();
  _sentInWindow = 0;
  _netRetryAt = 0;
}

void BoxIOClass::begin(Client& client, const char* key, const char* host, uint16_t port) {
  config(key, host, port);
  begin(client);
}

#if defined(ESP32) || defined(ESP8266)
#if defined(ESP8266)
#include <ESP8266WiFi.h>
#else
#include <WiFi.h>
#endif
static WiFiClient gWifiClient;

void BoxIOClass::begin(const char* key, const char* ssid, const char* pass, const char* host, uint16_t port) {
  config(key, host, port);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, pass);
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 25000) {
    delay(250);
  }
  begin(gWifiClient);
}
#endif

void BoxIOClass::onWrite(int pin, BoxWriteHandler handler) {
  if (pin >= 0 && pin < BOXIO_MAX_HANDLERS) gHandlers[pin] = handler;
}

bool BoxIOClass::connected() { return _ok; }

bool BoxIOClass::ensureConnected() {
  if (!_client) return false;
  if (_client->connected()) return true;
  return openServer();
}

bool BoxIOClass::openServer() {
  if (!_client) return false;
  uint32_t now = millis();
  uint16_t preferred = _usePort ? _usePort : _port;
  if (preferred != _port && _portRetryAt != 0 && (int32_t)(now - _portRetryAt) >= 0) {
    if (_client->connect(_host, _port)) {
      _usePort = _port;
      _portRetryAt = 0;
      return true;
    }
    _portRetryAt = now + 60000;
  }
  if (_client->connect(_host, preferred)) return true;
  // Port 5923 is often closed by a router that still allows port 80.
  // The dashboard proxy serves /hw/ on port 80.
  if (preferred != 80 && _client->connect(_host, 80)) {
    _usePort = 80;
    _portRetryAt = now + 60000;
    return true;
  }
  return false;
}

static long headerContentLength(const char* line) {
  char tmp[40];
  size_t n = 0;
  for (; line[n] && line[n] != '\r' && n + 1 < sizeof(tmp); n++) {
    char c = line[n];
    if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    tmp[n] = c;
  }
  tmp[n] = 0;
#if defined(__AVR__)
  if (strncmp_P(tmp, PSTR("content-length:"), 15) != 0) return -1;
#else
  const char* key = "content-length:";
  if (strncmp(tmp, key, 15) != 0) return -1;
#endif
  const char* p = tmp + 15;
  while (*p == ' ') p++;
  if (*p < '0' || *p > '9') return -1;
  return atol(p);
}

static void noteNetFail(uint32_t* retryAt) {
  *retryAt = millis() + 3000;
}

static void lowerHeader(const char* line, char* tmp, size_t tmpLen) {
  size_t n = 0;
  for (; line[n] && line[n] != '\r' && n + 1 < tmpLen; n++) {
    char c = line[n];
    if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    tmp[n] = c;
  }
  tmp[n] = 0;
}

static bool appendLiteral(char* out, size_t cap, size_t* o, const char* lit) {
  if (!lit) return false;
  size_t n = strlen(lit);
  if (*o + n >= cap) return false;
  memcpy(out + *o, lit, n);
  *o += n;
  out[*o] = 0;
  return true;
}

static bool appendFlash(char* out, size_t cap, size_t* o, const char* flash) {
#if defined(__AVR__)
  char tmp[24];
  strncpy_P(tmp, flash, sizeof(tmp) - 1);
  tmp[sizeof(tmp) - 1] = 0;
  return appendLiteral(out, cap, o, tmp);
#else
  return appendLiteral(out, cap, o, flash);
#endif
}

// Decimal only. snprintf pulls in vfprintf, which does not fit a Leonardo.
static bool appendUInt(char* out, size_t cap, size_t* o, unsigned long value) {
  char tmp[10];
  int n = 0;
  do {
    tmp[n++] = (char)('0' + (value % 10UL));
    value /= 10UL;
  } while (value && n < (int)sizeof(tmp));
  if (*o + (size_t)n >= cap) return false;
  while (n) out[(*o)++] = tmp[--n];
  out[*o] = 0;
  return true;
}

static bool appendLong(char* out, size_t cap, size_t* o, long value) {
  if (value < 0) {
    if (*o + 1 >= cap) return false;
    out[(*o)++] = '-';
    out[*o] = 0;
    unsigned long magnitude = 0UL - (unsigned long)value;
    return appendUInt(out, cap, o, magnitude);
  }
  return appendUInt(out, cap, o, (unsigned long)value);
}

static void formatLong(char* out, size_t cap, long value) {
  size_t o = 0;
  if (!cap) return;
  if (!appendLong(out, cap, &o, value)) out[0] = 0;
}

// Two digits after the point, same shape as "%.2f", without printf.
static void formatFixed2(char* out, size_t cap, float value) {
  size_t o = 0;
  if (!cap) return;
  out[0] = 0;
  if (value != value) {
    appendLiteral(out, cap, &o, "nan");
    return;
  }
  bool neg = value < 0;
  float mag = neg ? -value : value;
  if (mag > 1.0e8f) mag = 1.0e8f;
  float scaled = mag * 100.0f + 0.5f;
  unsigned long cents = (unsigned long)scaled;
  if (neg && cents != 0 && !appendLiteral(out, cap, &o, "-")) return;
  if (!appendUInt(out, cap, &o, cents / 100UL)) return;
  if (!appendLiteral(out, cap, &o, ".")) return;
  unsigned frac = (unsigned)(cents % 100UL);
  if (o + 2 >= cap) return;
  out[o++] = (char)('0' + frac / 10);
  out[o++] = (char)('0' + frac % 10);
  out[o] = 0;
}

static bool pinQuery(char* out, size_t cap, const char* head, int pin, const char* tail) {
  size_t o = 0;
  out[0] = 0;
  if (!appendFlash(out, cap, &o, head)) return false;
  if (!appendLong(out, cap, &o, pin)) return false;
  if (tail && !appendFlash(out, cap, &o, tail)) return false;
  return true;
}

static void quoteKey(char* pat, size_t cap, const char* key) {
  size_t o = 0;
  if (cap < 3) {
    if (cap) pat[0] = 0;
    return;
  }
  pat[o++] = '"';
  while (key && *key && o + 2 < cap) pat[o++] = *key++;
  pat[o++] = '"';
  pat[o] = 0;
}

// Encode into out at *o. Stops after maxEncoded output bytes, matching the old
// fixed encode buffers that cut a value instead of overflowing the path.
static bool appendEncoded(char* out, size_t cap, size_t* o, const char* in, size_t maxEncoded) {
  static const char hex[] = "0123456789ABCDEF";
  size_t enc = 0;
  if (!in) in = "";
  for (size_t i = 0; in[i] && enc < maxEncoded; i++) {
    unsigned char c = (unsigned char)in[i];
    bool plain = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' ||
                 c == '_' || c == '.' || c == '~';
    size_t need = plain ? 1 : 3;
    if (enc + need > maxEncoded || *o + need >= cap) break;
    if (plain) {
      out[(*o)++] = (char)c;
    } else if (c == ' ') {
      out[(*o)++] = '%';
      out[(*o)++] = '2';
      out[(*o)++] = '0';
    } else {
      out[(*o)++] = '%';
      out[(*o)++] = hex[c >> 4];
      out[(*o)++] = hex[c & 0x0F];
    }
    enc += need;
  }
  out[*o] = 0;
  return true;
}

bool BoxIOClass::httpGet(const char* pathAndQuery, char* resp, size_t respLen) {
  if (!_client || !pathAndQuery) return false;
  uint32_t now = millis();
  if (_netRetryAt != 0 && (int32_t)(_netRetryAt - now) > 0) return false;

  // A kept socket is reused. Leftover bytes mean the previous reply was short,
  // so drop it and dial again. W5100 only has four sockets; opening one per
  // poll fills them and the link drops a few seconds later.
  bool reuse = _alive && _client->connected() && !_client->available();
  if (!reuse) {
    if (_client->connected()) _client->stop();
    _alive = false;
    if (!openServer()) {
      _ok = false;
      noteNetFail(&_netRetryAt);
      return false;
    }
  }

  _client->print(F("GET "));
  _client->print(pathAndQuery);
  _client->print(F(" HTTP/1.1\r\nHost: "));
  _client->print(_host);
  _client->print(F("\r\nX-BoxIO-Key: "));
  _client->print(_key);
  _client->print(F("\r\nConnection: keep-alive\r\n\r\n"));
  _client->flush();

  uint32_t start = millis();
  while (!_client->available()) {
    if ((uint32_t)(millis() - start) >= 2000) {
      _client->stop();
      _alive = false;
      // The socket was up and then went quiet. Try a fresh connection once
      // before the 3 second backoff, so one stalled reply does not look offline.
      if (reuse) return httpGet(pathAndQuery, resp, respLen);
      _ok = false;
      noteNetFail(&_netRetryAt);
      return false;
    }
    delay(5);
  }

  // NativeEthernet keeps connected() true after the peer has sent the body.
  // Waiting for that flag never returns, and an 8s hardware watchdog resets
  // the board inside run(). Stop on Content-Length, or after a short idle.
  bool headersDone = false;
  bool skipLine = false;
  bool keep = false;
  bool statusSeen = false;
  long contentLength = -1;
  size_t bodyGot = 0;
  char line[40];
  uint8_t ln = 0;
  size_t n = 0;
  uint32_t lastByte = millis();
  if (resp && respLen) resp[0] = 0;
  while (true) {
    while (_client->available()) {
      int got = _client->read();
      if (got < 0) break;
      char c = (char)got;
      lastByte = millis();
      if (!headersDone) {
        if (c == '\n') {
          if (!skipLine) {
            line[ln] = 0;
            if (ln == 0 || (ln == 1 && line[0] == '\r')) headersDone = true;
            else {
              char low[40];
              lowerHeader(line, low, sizeof(low));
              if (!statusSeen) {
                statusSeen = true;
#if defined(__AVR__)
                keep = strncmp_P(low, PSTR("http/1.1"), 8) == 0;
              } else if (strncmp_P(low, PSTR("connection:"), 11) == 0) {
                if (strstr_P(low, PSTR("close"))) keep = false;
                else if (strstr_P(low, PSTR("keep-alive"))) keep = true;
#else
                keep = strncmp(low, "http/1.1", 8) == 0;
              } else if (strncmp(low, "connection:", 11) == 0) {
                if (strstr(low, "close")) keep = false;
                else if (strstr(low, "keep-alive")) keep = true;
#endif
              } else {
                long parsed = headerContentLength(line);
                if (parsed >= 0) contentLength = parsed;
              }
            }
          }
          ln = 0;
          skipLine = false;
        } else if (!skipLine) {
          if ((size_t)ln + 1 < sizeof(line)) line[ln++] = c;
          else skipLine = true;
        }
        if (headersDone && contentLength == 0) {
          if (!keep) _client->stop();
          _alive = keep;
          _ok = true;
          _netRetryAt = 0;
          return true;
        }
      } else {
        bodyGot++;
        if (resp && n + 1 < respLen) {
          resp[n++] = c;
          resp[n] = 0;
        }
        if (contentLength >= 0 && (long)bodyGot >= contentLength) {
          if (!keep) _client->stop();
          _alive = keep;
          _ok = true;
          _netRetryAt = 0;
          return true;
        }
      }
    }
    bool idle = headersDone && !_client->available() && contentLength < 0 &&
                (uint32_t)(millis() - lastByte) >= 300;
    bool closed = headersDone && !_client->available() && !_client->connected();
    if (idle || closed) {
      _client->stop();
      _alive = false;
      _ok = true;
      _netRetryAt = 0;
      return true;
    }
    if ((uint32_t)(millis() - start) >= 2000) {
      _client->stop();
      _alive = false;
      if (reuse) return httpGet(pathAndQuery, resp, respLen);
      _ok = false;
      noteNetFail(&_netRetryAt);
      return false;
    }
    delay(1);
  }
}

void BoxIOClass::dispatchCommands(const char* json) {
  if (!json) return;
  const char* p = json;
  while ((p = strstr(p, "\"pin\"")) != nullptr) {
    const char* colon = strchr(p, ':');
    if (!colon) break;
    int pin = atoi(colon + 1);
    const char* vkey = strstr(colon, "\"value\"");
    if (!vkey) break;
    const char* vcolon = strchr(vkey, ':');
    if (!vcolon) break;
    const char* q = vcolon + 1;
    while (*q == ' ') q++;
    char value[BOXIO_CMD_VALUE];
    size_t i = 0;
    if (*q == '"') {
      q++;
      while (*q && *q != '"' && i + 1 < sizeof(value)) {
        if (*q == '\\' && q[1]) q++;
        value[i++] = *q++;
      }
    } else {
      while (*q && *q != ',' && *q != '}' && i + 1 < sizeof(value)) {
        value[i++] = *q++;
      }
    }
    value[i] = 0;
    if (pin >= 0 && pin < BOXIO_MAX_HANDLERS && gHandlers[pin]) {
      BoxParam param(value);
      gHandlers[pin](param);
    }
    p = q;
  }
}

void BoxIOClass::setMaxSendsPerSecond(uint16_t times) {
  _perMinute = false;
  if (times > 10000) times = 10000;
  _sendLimit = times;
  _windowStart = millis();
  _sentInWindow = 0;
}

void BoxIOClass::setMaxSendsPerMinute(uint16_t times) {
  if (times < 1) times = 1;
  if (times > 10000) times = 10000;
  _perMinute = true;
  _sendLimit = times;
  _windowStart = millis();
  _sentInWindow = 0;
}

bool BoxIOClass::takeSendSlot() {
  if (!_perMinute && _sendLimit == 0) return true;
  uint32_t now = millis();
  uint32_t span = _perMinute ? 60000UL : 1000UL;
  if (now - _windowStart >= span) {
    _windowStart = now;
    _sentInWindow = 0;
  }
  if (_sentInWindow >= _sendLimit) return false;
  _sentInWindow++;
  return true;
}

void BoxIOClass::enqueueWrite(int pin, const char* value) {
  const char* src = value ? value : "";
  size_t len = strlen(src);
  if (len >= BOXIO_PENDING_LEN) return;
  int freeSlot = -1;
  int oldest = -1;
  uint32_t oldestSeq = 0xFFFFFFFFUL;
  for (int i = 0; i < BOXIO_MAX_PENDING; i++) {
    if (_pending[i].used && _pending[i].pin == pin) {
      memcpy(_pending[i].value, src, len + 1);
      return;
    }
    if (!_pending[i].used) {
      if (freeSlot < 0) freeSlot = i;
      continue;
    }
    if (_pending[i].seq < oldestSeq) {
      oldestSeq = _pending[i].seq;
      oldest = i;
    }
  }
  int slot = freeSlot >= 0 ? freeSlot : oldest;
  if (slot < 0) return;
  _pending[slot].used = true;
  _pending[slot].pin = pin;
  _pending[slot].seq = _pendingSeq++;
  if (_pendingSeq == 0) _pendingSeq = 1;
  memcpy(_pending[slot].value, src, len + 1);
}

void BoxIOClass::transmitVirtualWrite(int pin, const char* value) {
  if (!pinQuery(ioScratch, sizeof(ioScratch), FLASH_LIT("/hw/vw?pin="), pin, FLASH_LIT("&value="))) return;
  size_t o = strlen(ioScratch);
  appendEncoded(ioScratch, sizeof(ioScratch), &o, value ? value : "", BOXIO_VW_ENCODE);
  httpGet(ioScratch, _buf, sizeof(_buf));
}

void BoxIOClass::flushPending() {
  if (_flushing) return;
  _flushing = true;
  for (;;) {
    int slot = -1;
    uint32_t best = 0xFFFFFFFFUL;
    for (int i = 0; i < BOXIO_MAX_PENDING; i++) {
      if (_pending[i].used && _pending[i].seq < best) {
        best = _pending[i].seq;
        slot = i;
      }
    }
    if (slot < 0) break;
    if (!takeSendSlot()) break;
    int pin = _pending[slot].pin;
    char value[BOXIO_PENDING_LEN];
    memcpy(value, _pending[slot].value, BOXIO_PENDING_LEN);
    _pending[slot].used = false;
    transmitVirtualWrite(pin, value);
  }
  _flushing = false;
}

int BoxIOClass::setTimer(unsigned long intervalMs, BoxTimerHandler handler) {
  if (!handler || intervalMs == 0) return -1;
  for (int i = 0; i < BOXIO_MAX_TIMERS; i++) {
    if (_timers[i].active) continue;
    _timers[i].active = true;
    _timers[i].fn = handler;
    _timers[i].interval = (uint32_t)intervalMs;
    _timers[i].next = millis() + (uint32_t)intervalMs;
    return i;
  }
  return -1;
}

void BoxIOClass::deleteTimer(int id) {
  if (id < 0 || id >= BOXIO_MAX_TIMERS) return;
  _timers[id].active = false;
  _timers[id].fn = nullptr;
}

static uint32_t clampInterval(uint32_t seconds) {
  if (seconds < 5) return 5;
  if (seconds > 86400) return 86400;
  return seconds;
}

static uint8_t clampMisses(uint8_t misses) {
  if (misses < 1) return 1;
  if (misses > 50) return 50;
  return misses;
}

static uint32_t clampTransition(uint32_t seconds) {
  if (seconds < 1) return 1;
  if (seconds > 3600) return 3600;
  return seconds;
}

static bool parseWatchHost(const char* raw, char* host, size_t hostLen, uint16_t* port) {
  if (!raw || !host || hostLen < 2 || !port) return false;
  while (*raw == ' ') raw++;
  const char* p = raw;
  *port = 80;
  if (strncmp(p, "https://", 8) == 0) {
    *port = 443;
    p += 8;
  } else if (strncmp(p, "http://", 7) == 0) {
    p += 7;
  }
  size_t n = 0;
  int colons = 0;
  int colonAt = -1;
  while (*p && *p != '/' && *p != '?' && *p != ' ' && n + 1 < hostLen) {
    if (*p == ':') {
      colons++;
      colonAt = (int)n;
    }
    host[n++] = *p++;
  }
  if (*p == ' ') return false;
  host[n] = 0;
  if (!n || colons > 1) return false;
  if (colons == 1) {
    if (colonAt <= 0 || colonAt >= (int)n - 1) return false;
    int parsed = atoi(host + colonAt + 1);
    if (parsed <= 0 || parsed > 65535) return false;
    host[colonAt] = 0;
    *port = (uint16_t)parsed;
  }
  return host[0] != 0;
}

static int jsonInt(const char* json, const char* key, int fallback) {
  char pat[24];
  quoteKey(pat, sizeof(pat), key);
  const char* p = strstr(json, pat);
  if (!p) return fallback;
  const char* colon = strchr(p, ':');
  if (!colon) return fallback;
  return atoi(colon + 1);
}

static bool jsonQuoted(const char* json, const char* key, char* out, size_t outLen) {
  char pat[24];
  quoteKey(pat, sizeof(pat), key);
  const char* p = strstr(json, pat);
  if (!p) return false;
  const char* colon = strchr(p, ':');
  if (!colon) return false;
  const char* v = strchr(colon, '"');
  if (!v) return false;
  v++;
  size_t i = 0;
  while (*v && *v != '"' && i + 1 < outLen) {
    out[i++] = *v++;
  }
  out[i] = 0;
  return true;
}

bool BoxIOClass::setPingWatchdog(const char* host, uint32_t intervalSec, uint8_t misses, int boardPin,
                                 BoxWatchDir direction, uint32_t transitionSec) {
  char parsed[BOXIO_WATCH_HOST];
  uint16_t port = 80;
  if (!parseWatchHost(host, parsed, sizeof(parsed), &port)) return false;
  if (boardPin < 0 || boardPin > 127) return false;
  applyWatchdog(parsed, port, intervalSec, misses, boardPin, direction, transitionSec, false);
  return true;
}

void BoxIOClass::clearPingWatchdog() {
  _wd.active = false;
  _wd.fromDash = false;
  _wd.pulsing = false;
  _wd.tripped = false;
  _wd.misses = 0;
  _wd.host[0] = 0;
  _wd.status[0] = 0;
}

void BoxIOClass::applyWatchdog(const char* host, uint16_t port, uint32_t intervalSec, uint8_t misses, int boardPin,
                               BoxWatchDir direction, uint32_t transitionSec, bool fromDash) {
  uint32_t interval = clampInterval(intervalSec);
  uint8_t need = clampMisses(misses);
  uint32_t gap = clampTransition(transitionSec);
  bool same = _wd.active && _wd.port == port && _wd.intervalSec == interval && _wd.missesNeeded == need &&
              _wd.gpio == (uint8_t)boardPin && _wd.direction == direction && _wd.transitionSec == gap &&
              strcmp(_wd.host, host ? host : "") == 0;
  _wd.fromDash = fromDash;
  if (same) return;
  _wd.active = true;
  _wd.tripped = false;
  _wd.pulsing = false;
  _wd.misses = 0;
  _wd.missesNeeded = need;
  _wd.gpio = (uint8_t)boardPin;
  _wd.direction = direction;
  _wd.secondLevel = direction == BOX_LOW_TO_HIGH ? HIGH : LOW;
  _wd.port = port;
  _wd.intervalSec = interval;
  _wd.transitionSec = gap;
  _wd.nextCheck = 0;
  _wd.status[0] = 0;
  strncpy(_wd.host, host ? host : "", sizeof(_wd.host) - 1);
  _wd.host[sizeof(_wd.host) - 1] = 0;
  pinMode(boardPin, OUTPUT);
}

void BoxIOClass::noteWatchdog(const char* text) {
  if (!text) text = "";
  if (strcmp(_wd.status, text) == 0) return;
  strncpy(_wd.status, text, sizeof(_wd.status) - 1);
  _wd.status[sizeof(_wd.status) - 1] = 0;
  if (!_client) return;
  setProperty(_wd.gpio, "watchdogStatus", _wd.status);
}

bool BoxIOClass::probeReachable(const char* host, uint16_t port) {
  if (!_client || !host || !host[0]) return false;
#if defined(ESP32) || defined(ESP8266)
  if (WiFi.status() != WL_CONNECTED) return false;
#endif
  _alive = false;
  if (_client->connected()) _client->stop();
  bool ok = _client->connect(host, port);
  if (_client->connected()) _client->stop();
  return ok;
}

void BoxIOClass::serviceWatchdog() {
  if (!_wd.active) return;
  uint32_t now = millis();
  if (_wd.pulsing && (int32_t)(now - _wd.pulseAt) >= 0) {
    digitalWrite(_wd.gpio, _wd.secondLevel);
    _wd.pulsing = false;
  }
  if (_wd.nextCheck != 0 && (int32_t)(now - _wd.nextCheck) < 0) return;
  _wd.nextCheck = now + (uint32_t)_wd.intervalSec * 1000UL;
  if (!_wd.host[0]) {
    noteWatchdog("Set a site or IP");
    return;
  }
  bool ok = probeReachable(_wd.host, _wd.port);
  char shown[25];
  strncpy(shown, _wd.host, 24);
  shown[24] = 0;
  if (ok) {
    _wd.misses = 0;
    _wd.tripped = false;
    char text[48];
    size_t o = 0;
    text[0] = 0;
    appendFlash(text, sizeof(text), &o, FLASH_LIT("ok "));
    appendLiteral(text, sizeof(text), &o, shown);
    noteWatchdog(text);
    return;
  }
  if (_wd.misses < 255) _wd.misses++;
  if (_wd.misses >= _wd.missesNeeded && !_wd.tripped) {
    _wd.tripped = true;
    uint8_t first = _wd.direction == BOX_LOW_TO_HIGH ? LOW : HIGH;
    digitalWrite(_wd.gpio, first);
    _wd.pulsing = true;
    _wd.pulseAt = now + _wd.transitionSec * 1000UL;
    char text[48];
    size_t o = 0;
    text[0] = 0;
    appendFlash(text, sizeof(text), &o, FLASH_LIT("fail "));
    appendFlash(text, sizeof(text), &o,
                _wd.direction == BOX_LOW_TO_HIGH ? FLASH_LIT("low-to-high ") : FLASH_LIT("high-to-low "));
    appendLiteral(text, sizeof(text), &o, shown);
    noteWatchdog(text);
    return;
  }
  char text[48];
  size_t o = 0;
  text[0] = 0;
  appendFlash(text, sizeof(text), &o, FLASH_LIT("miss "));
  appendUInt(text, sizeof(text), &o, _wd.misses);
  appendFlash(text, sizeof(text), &o, FLASH_LIT("/"));
  appendUInt(text, sizeof(text), &o, _wd.missesNeeded);
  appendFlash(text, sizeof(text), &o, FLASH_LIT(" "));
  appendLiteral(text, sizeof(text), &o, shown);
  noteWatchdog(text);
}

void BoxIOClass::syncWatchdogConfig() {
  uint32_t now = millis();
  if (_lastWatchSync != 0 && (int32_t)(now - _lastWatchSync) < 15000) return;
  _lastWatchSync = now;
  if (!httpGet("/hw/watchdog", _buf, sizeof(_buf))) return;
  int active = jsonInt(_buf, "active", 0);
  if (!active) {
    if (_wd.fromDash) clearPingWatchdog();
    return;
  }
  char target[BOXIO_WATCH_HOST];
  target[0] = 0;
  jsonQuoted(_buf, "target", target, sizeof(target));
  char parsed[BOXIO_WATCH_HOST];
  uint16_t port = 80;
  int pin = jsonInt(_buf, "pin", -1);
  if (pin < 0 || pin > 127 || !target[0] || !parseWatchHost(target, parsed, sizeof(parsed), &port)) {
    _wd.active = true;
    _wd.fromDash = true;
    _wd.pulsing = false;
    _wd.tripped = false;
    _wd.misses = 0;
    _wd.host[0] = 0;
    _wd.nextCheck = 0;
    if (pin >= 0 && pin <= 127) _wd.gpio = (uint8_t)pin;
    return;
  }
  BoxWatchDir dir = jsonInt(_buf, "dir", 0) == 1 ? BOX_LOW_TO_HIGH : BOX_HIGH_TO_LOW;
  applyWatchdog(parsed, port, (uint32_t)jsonInt(_buf, "interval", 30), (uint8_t)jsonInt(_buf, "misses", 3), pin, dir,
                (uint32_t)jsonInt(_buf, "transition", 5), true);
}

void BoxIOClass::runTimers() {
  uint32_t now = millis();
  for (int i = 0; i < BOXIO_MAX_TIMERS; i++) {
    if (!_timers[i].active || !_timers[i].fn) continue;
    if ((int32_t)(now - _timers[i].next) < 0) continue;
    BoxTimerHandler fn = _timers[i].fn;
    uint32_t interval = _timers[i].interval;
    _timers[i].next += interval;
    if ((int32_t)(now - _timers[i].next) >= 0) {
      _timers[i].next = now + interval;
    }
    fn();
  }
}

void BoxIOClass::run() {
  if (_inRun) return;
  _inRun = true;
  runTimers();
  serviceWatchdog();
  flushPending();
  if (_client) {
    uint32_t now = millis();
    if (now - _lastPing > 10000) {
      _lastPing = now;
      httpGet("/hw/ping", _buf, sizeof(_buf));
    }
    if (now - _lastPoll >= _pollMs) {
      _lastPoll = now;
      if (httpGet("/hw/pull", _buf, sizeof(_buf))) {
        dispatchCommands(_buf);
      }
    }
    syncWatchdogConfig();
  }
  _inRun = false;
}

void BoxIOClass::virtualWrite(int pin, int value) {
  char tmp[16];
  formatLong(tmp, sizeof(tmp), value);
  virtualWrite(pin, tmp);
}

void BoxIOClass::virtualWrite(int pin, long value) {
  char tmp[24];
  formatLong(tmp, sizeof(tmp), value);
  virtualWrite(pin, tmp);
}

void BoxIOClass::virtualWrite(int pin, float value) {
  char tmp[16];
  formatFixed2(tmp, sizeof(tmp), value);
  virtualWrite(pin, tmp);
}

void BoxIOClass::virtualWrite(int pin, const String& value) {
  virtualWrite(pin, value.c_str());
}

static bool buildIntCsv(const int* values, size_t count, char* out, size_t cap) {
  size_t o = 0;
  if (cap == 0) return false;
  out[0] = 0;
  for (size_t i = 0; i < count; i++) {
    char num[16];
    formatLong(num, sizeof(num), values[i]);
    size_t n = strlen(num);
    size_t comma = o ? 1 : 0;
    if (o + comma + n + 1 > cap) return false;
    if (comma) out[o++] = ',';
    memcpy(out + o, num, n);
    o += n;
    out[o] = 0;
  }
  return true;
}

void BoxIOClass::virtualWrite(int pin, const int* values, size_t count) {
  char small[BOXIO_PENDING_LEN];
  if (buildIntCsv(values, count, small, sizeof(small))) {
    virtualWrite(pin, small);
    return;
  }
  flushPending();
  if (!takeSendSlot()) return;
  if (!pinQuery(ioScratch, sizeof(ioScratch), FLASH_LIT("/hw/vw?pin="), pin, FLASH_LIT("&value="))) return;
  size_t o = strlen(ioScratch);
  for (size_t i = 0; i < count; i++) {
    char num[16];
    formatLong(num, sizeof(num), values[i]);
    if (i && !appendLiteral(ioScratch, sizeof(ioScratch), &o, "%2C")) break;
    appendEncoded(ioScratch, sizeof(ioScratch), &o, num, 16);
  }
  httpGet(ioScratch, _buf, sizeof(_buf));
}

void BoxIOClass::virtualWrite(int pin, const float* values, size_t count) {
  char small[BOXIO_PENDING_LEN];
  size_t o = 0;
  bool fits = true;
  small[0] = 0;
  for (size_t i = 0; i < count; i++) {
    char num[16];
    formatFixed2(num, sizeof(num), values[i]);
    size_t n = strlen(num);
    size_t comma = o ? 1 : 0;
    if (o + comma + n + 1 > sizeof(small)) {
      fits = false;
      break;
    }
    if (comma) small[o++] = ',';
    memcpy(small + o, num, n);
    o += (size_t)n;
    small[o] = 0;
  }
  if (fits) {
    virtualWrite(pin, small);
    return;
  }
  flushPending();
  if (!takeSendSlot()) return;
  if (!pinQuery(ioScratch, sizeof(ioScratch), FLASH_LIT("/hw/vw?pin="), pin, FLASH_LIT("&value="))) return;
  o = strlen(ioScratch);
  for (size_t i = 0; i < count; i++) {
    char num[16];
    formatFixed2(num, sizeof(num), values[i]);
    if (i && !appendLiteral(ioScratch, sizeof(ioScratch), &o, "%2C")) break;
    appendEncoded(ioScratch, sizeof(ioScratch), &o, num, 16);
  }
  httpGet(ioScratch, _buf, sizeof(_buf));
}

void BoxIOClass::virtualWrite(int pin, const char* value) {
  for (int i = 0; i < BOXIO_MAX_PENDING; i++) {
    if (_pending[i].used && _pending[i].pin == pin) _pending[i].used = false;
  }
  flushPending();
  if (!takeSendSlot()) {
    enqueueWrite(pin, value);
    return;
  }
  transmitVirtualWrite(pin, value);
}

bool BoxIOClass::virtualRead(int pin, char* out, size_t outLen) {
  char path[40];
  if (!pinQuery(path, sizeof(path), FLASH_LIT("/hw/vr?pin="), pin, nullptr)) return false;
  if (!httpGet(path, _buf, sizeof(_buf))) return false;
  const char* vkey = strstr(_buf, "\"value\"");
  if (!vkey || !out || !outLen) return false;
  const char* q = strchr(vkey, ':');
  if (!q) return false;
  q++;
  while (*q == ' ') q++;
  size_t i = 0;
  if (*q == '"') {
    q++;
    while (*q && *q != '"' && i + 1 < outLen) out[i++] = *q++;
  } else {
    while (*q && *q != ',' && *q != '}' && i + 1 < outLen) out[i++] = *q++;
  }
  out[i] = 0;
  return true;
}

void BoxIOClass::setProperty(int pin, const char* prop, const String& value) {
  setProperty(pin, prop, value.c_str());
}

void BoxIOClass::setProperty(int pin, const char* prop, const char* value) {
  if (!pinQuery(ioScratch, sizeof(ioScratch), FLASH_LIT("/hw/property?pin="), pin, FLASH_LIT("&prop="))) return;
  size_t o = strlen(ioScratch);
  appendEncoded(ioScratch, sizeof(ioScratch), &o, prop ? prop : "", 40);
  if (!appendFlash(ioScratch, sizeof(ioScratch), &o, FLASH_LIT("&value="))) return;
  appendEncoded(ioScratch, sizeof(ioScratch), &o, value ? value : "", BOXIO_PROP_VALUE);
  httpGet(ioScratch, _buf, sizeof(_buf));
}

bool BoxIOClass::email(const char* to, const char* subject, const char* body) {
  size_t o = 0;
  if (!appendFlash(ioScratch, sizeof(ioScratch), &o, FLASH_LIT("/hw/email?to="))) return false;
  appendEncoded(ioScratch, sizeof(ioScratch), &o, to ? to : "", 80);
  if (!appendFlash(ioScratch, sizeof(ioScratch), &o, FLASH_LIT("&subject="))) return false;
  appendEncoded(ioScratch, sizeof(ioScratch), &o, subject ? subject : "", 80);
  if (!appendFlash(ioScratch, sizeof(ioScratch), &o, FLASH_LIT("&body="))) return false;
  appendEncoded(ioScratch, sizeof(ioScratch), &o, body ? body : "", 120);
  if (o >= BOXIO_BUF) return false;
  return httpGet(ioScratch, _buf, sizeof(_buf));
}

bool BoxIOClass::sms(const char* to, const char* body) {
  size_t o = 0;
  if (!appendFlash(ioScratch, sizeof(ioScratch), &o, FLASH_LIT("/hw/sms?to="))) return false;
  appendEncoded(ioScratch, sizeof(ioScratch), &o, to ? to : "", 40);
  if (!appendFlash(ioScratch, sizeof(ioScratch), &o, FLASH_LIT("&body="))) return false;
  appendEncoded(ioScratch, sizeof(ioScratch), &o, body ? body : "", 140);
  return httpGet(ioScratch, _buf, sizeof(_buf));
}
