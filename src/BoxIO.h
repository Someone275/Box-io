/*
  BoxIO — Arduino client for a self-hosted Box IO server.

  Works with any board that provides an Arduino Client (WiFiClient,
  EthernetClient, WiFiNINA, etc.), including ESP32, ESP8266 (NodeMCU),
  Teensy 4.1 (QNEthernet), SAM, SAMD, MegaAVR, and AVR.
*/

#ifndef BOXIO_H
#define BOXIO_H

#include <Arduino.h>
#include <Client.h>

// Sizes in a sketch-folder BoxIOConfig.h apply when that folder is on the
// include path for BOTH this header and BoxIO.cpp. The Arduino library build
// does not always add it, so the defaults below are what an Uno actually uses.
#if defined(__has_include)
#  if __has_include("BoxIOConfig.h")
#    include "BoxIOConfig.h"
#  endif
#endif

// About 2.5 KB of SRAM or less: Uno, Nano, Leonardo, Micro, Leonardo ETH.
// Mega (RAMEND 0x21FF) and larger AVR boards keep the medium buffers.
#ifndef BOXIO_AVR_SMALL
#if (defined(__AVR__) && defined(RAMEND) && (RAMEND < 0x1000)) \
    || defined(__AVR_ATmega328P__) || defined(__AVR_ATmega168__) \
    || defined(__AVR_ATmega328PB__) || defined(__AVR_ATmega168P__) \
    || defined(__AVR_ATmega168PB__)
#define BOXIO_AVR_SMALL 1
#else
#define BOXIO_AVR_SMALL 0
#endif
#endif

#ifndef BOXIO_MAX_HANDLERS
#define BOXIO_MAX_HANDLERS 32
#endif

#ifndef BOXIO_BUF
#if BOXIO_AVR_SMALL
// Shared with the request scratch. An email URL has to fit in this many
// characters. 160 leaves room for Ethernet on a 2 KB Uno.
#define BOXIO_BUF 160
#else
#define BOXIO_BUF 512
#endif
#endif

#ifndef BOXIO_VW_ENCODE
#if BOXIO_AVR_SMALL
#define BOXIO_VW_VALUE 64
#define BOXIO_VW_ENCODE 96
#define BOXIO_VW_PATH 128
#define BOXIO_PROP_VALUE 80
#define BOXIO_PROP_PATH 160
#define BOXIO_CMD_VALUE 48
#elif defined(__AVR__)
#define BOXIO_VW_VALUE 384
#define BOXIO_VW_ENCODE 384
#define BOXIO_VW_PATH 424
#define BOXIO_PROP_VALUE 280
#define BOXIO_PROP_PATH 360
#define BOXIO_CMD_VALUE 192
#else
// One 16 by 16 grid. Each cell can be an 11-digit integer, and each comma
// is sent as %2C, so the encoded query is longer than the raw list.
#define BOXIO_VW_VALUE 3200
#define BOXIO_VW_ENCODE 4096
#define BOXIO_VW_PATH 4224
#define BOXIO_PROP_VALUE 280
#define BOXIO_PROP_PATH 360
#define BOXIO_CMD_VALUE 192
#endif
#endif

#ifndef BOXIO_MAX_TIMERS
#if BOXIO_AVR_SMALL
#define BOXIO_MAX_TIMERS 4
#else
#define BOXIO_MAX_TIMERS 16
#endif
#endif

#ifndef BOXIO_MAX_PENDING
#if BOXIO_AVR_SMALL
#define BOXIO_MAX_PENDING 2
#define BOXIO_PENDING_LEN 24
#else
#define BOXIO_MAX_PENDING 12
#define BOXIO_PENDING_LEN 96
#endif
#endif

#ifndef V0
#define V0 0
#define V1 1
#define V2 2
#define V3 3
#define V4 4
#define V5 5
#define V6 6
#define V7 7
#define V8 8
#define V9 9
#define V10 10
#define V11 11
#define V12 12
#define V13 13
#define V14 14
#define V15 15
#define V16 16
#define V17 17
#define V18 18
#define V19 19
#define V20 20
#define V21 21
#define V22 22
#define V23 23
#define V24 24
#define V25 25
#define V26 26
#define V27 27
#define V28 28
#define V29 29
#define V30 30
#define V31 31
#endif

class BoxParam {
 public:
  explicit BoxParam(const char* value) : _value(value ? value : "") {}
  int asInt() const { return atoi(_value); }
  long asLong() const { return atol(_value); }
  float asFloat() const { return atof(_value); }
  bool asBool() const { return asInt() != 0; }
  const char* asStr() const { return _value; }

 private:
  const char* _value;
};

typedef void (*BoxWriteHandler)(const BoxParam& param);
typedef void (*BoxTimerHandler)();

// Pulse on the Arduino GPIO when the board cannot reach the internet target.
enum BoxWatchDir : uint8_t { BOX_HIGH_TO_LOW = 0, BOX_LOW_TO_HIGH = 1 };

#ifndef BOXIO_WATCH_HOST
#if BOXIO_AVR_SMALL
#define BOXIO_WATCH_HOST 32
#else
#define BOXIO_WATCH_HOST 64
#endif
#endif

class BoxIOWriteReg {
 public:
  BoxIOWriteReg(int pin, BoxWriteHandler handler);
};

class BoxIOClass {
 public:
  void setAuth(const char* key);
  void setServer(const char* host, uint16_t port = 5923);
  void config(const char* key, const char* host, uint16_t port = 5923);

  void begin(Client& client);
  void begin(Client& client, const char* key, const char* host, uint16_t port = 5923);

  // EthernetClient::setConnectionTimeout is not on the Client base. Call it
  // when the concrete client has it, so a dead server cannot sit in connect()
  // longer than a typical hardware watchdog.
  template <typename ClientT>
  void begin(ClientT& client, const char* key, const char* host, uint16_t port = 5923) {
    applyConnectTimeout(client, 0);
    begin(static_cast<Client&>(client), key, host, port);
  }

#if defined(ESP32) || defined(ESP8266)
  void begin(const char* key, const char* ssid, const char* pass, const char* host, uint16_t port = 5923);
#endif

  void run();
  bool connected();

  void virtualWrite(int pin, int value);
  void virtualWrite(int pin, long value);
  void virtualWrite(int pin, float value);
  void virtualWrite(int pin, const char* value);
  void virtualWrite(int pin, const String& value);
  void virtualWrite(int pin, const int* values, size_t count);
  void virtualWrite(int pin, const float* values, size_t count);

  bool virtualRead(int pin, char* out, size_t outLen);

  void setProperty(int pin, const char* prop, const char* value);
  void setProperty(int pin, const char* prop, const String& value);

  bool email(const char* to, const char* subject, const char* body);
  bool sms(const char* to, const char* body);

  void onWrite(int pin, BoxWriteHandler handler);
  void setPollInterval(uint16_t ms) { _pollMs = ms; }

  // Cap virtualWrite traffic so a tight loop cannot flood the server.
  // Default is 30 sends per second. Pass 0 to remove the cap.
  // setMaxSendsPerMinute replaces that cap with a per-minute limit.
  void setMaxSendsPerSecond(uint16_t times);
  void setMaxSendsPerMinute(uint16_t times);

  // Call handler every intervalMs. Each call adds another timer.
  // Returns an id for deleteTimer, or -1 when the table is full.
  int setTimer(unsigned long intervalMs, BoxTimerHandler handler);
  void deleteTimer(int id);

  // From the board, check host on intervalSec. After misses failures in a row,
  // drive boardPin (a GPIO on this Arduino) high then low, or low then high.
  // transitionSec is the wait between those two levels. A hostname or IP uses
  // TCP port 80. https:// uses port 443. host:port overrides that.
  // Returns false when the host or pin is not usable.
  // A saved Ping watchdog widget replaces this the next time run() syncs.
  bool setPingWatchdog(const char* host, uint32_t intervalSec, uint8_t misses, int boardPin,
                       BoxWatchDir direction, uint32_t transitionSec);
  void clearPingWatchdog();

 private:
  Client* _client = nullptr;
  const char* _key = "";
  const char* _host = "127.0.0.1";
  uint16_t _port = 5923;
  uint16_t _usePort = 5923;
  uint32_t _portRetryAt = 0;
  uint32_t _lastPoll = 0;
  uint32_t _lastPing = 0;
  uint32_t _netRetryAt = 0;
  uint16_t _pollMs = 400;
  bool _ok = false;
  // Reply bytes only. The request is built in a separate scratch buffer so a
  // write handler can transmit while a pull response is still being read.
  char _buf[BOXIO_BUF];
  bool _alive = false;

  struct TimerSlot {
    BoxTimerHandler fn;
    uint32_t interval;
    uint32_t next;
    bool active;
  };

  struct PendingWrite {
    int pin;
    uint32_t seq;
    bool used;
    char value[BOXIO_PENDING_LEN];
  };

  uint16_t _sendLimit = 30;
  bool _perMinute = false;
  uint32_t _windowStart = 0;
  uint16_t _sentInWindow = 0;
  uint32_t _pendingSeq = 1;
  bool _inRun = false;
  bool _flushing = false;
  TimerSlot _timers[BOXIO_MAX_TIMERS] = {};
  PendingWrite _pending[BOXIO_MAX_PENDING] = {};

  struct Watchdog {
    bool active;
    bool fromDash;
    bool tripped;
    bool pulsing;
    uint8_t misses;
    uint8_t missesNeeded;
    uint8_t gpio;
    uint8_t secondLevel;
    BoxWatchDir direction;
    uint16_t port;
    uint32_t intervalSec;
    uint32_t transitionSec;
    uint32_t nextCheck;
    uint32_t pulseAt;
    char host[BOXIO_WATCH_HOST];
    char status[32];
  };

  Watchdog _wd = {};
  uint32_t _lastWatchSync = 0;

  template <typename T>
  static auto applyConnectTimeout(T& client, int) -> decltype(client.setConnectionTimeout((uint16_t)1500), void()) {
    client.setConnectionTimeout(1500);
  }
  template <typename T>
  static void applyConnectTimeout(T&, long) {}

  bool ensureConnected();
  bool openServer();
  bool httpGet(const char* pathAndQuery, char* resp, size_t respLen);
  void dispatchCommands(const char* json);
  bool takeSendSlot();
  void enqueueWrite(int pin, const char* value);
  void flushPending();
  void transmitVirtualWrite(int pin, const char* value);
  void runTimers();
  void serviceWatchdog();
  void syncWatchdogConfig();
  void applyWatchdog(const char* host, uint16_t port, uint32_t intervalSec, uint8_t misses, int boardPin,
                     BoxWatchDir direction, uint32_t transitionSec, bool fromDash);
  void noteWatchdog(const char* text);
  bool probeReachable(const char* host, uint16_t port);
};

extern BoxIOClass BoxIO;

#define BOX_WRITE(PIN)                                                       \
  void BOXIO_WRITE_FN_##PIN(const BoxParam& param);                          \
  static BoxIOWriteReg BOXIO_WRITE_REG_##PIN(PIN, BOXIO_WRITE_FN_##PIN);     \
  void BOXIO_WRITE_FN_##PIN(const BoxParam& param)

#endif
