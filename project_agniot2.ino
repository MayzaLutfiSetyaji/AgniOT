/*
  AGNIOT - Sistem Deteksi & Pemadaman Kebakaran Dini Gudang Tekstil
  Firmware NodeMCU ESP-12E (ESP8266)

  Library yang dibutuhkan (Library Manager):
    - PubSubClient            (Nick O'Leary)
    - DHT sensor library      (Adafruit) + Adafruit Unified Sensor
    - Servo                   (bawaan core ESP8266)

  Board: NodeMCU 1.0 (ESP-12E Module), Upload speed 115200
*/

#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>
#include <PubSubClient.h>
#include <Servo.h>
#include <DHT.h>

// ======================= KONFIGURASI =======================
// Daftar Wi-Fi (boleh lebih dari satu). ESP akan memilih yang tersedia
// dengan sinyal terbaik. Tambah/hapus baris di fungsi connectWifi() sesuai kebutuhan.

const char* MQTT_HOST     = "broker.hivemq.com";
const uint16_t MQTT_PORT  = 1883;
const char* TOPIC_TELEMETRY = "agniot/kelompok2/telemetry";
const char* TOPIC_CONTROL   = "agniot/kelompok2/control";

// ---- Pin (sesuai roadmap) ----
#define PIN_MQ2_AO    A0
#define PIN_FLAME_DO  D1   // LOW = api terdeteksi
#define PIN_DHT       D2
#define PIN_RELAY     D0   // Active LOW
#define PIN_SERVO     D4
#define PIN_LED_NORMAL  D7 // hijau
#define PIN_LED_WARN    D6 // kuning
#define PIN_LED_DANGER  D5 // merah
#define PIN_BUZZER    D8   // HIGH = bunyi

// ---- Sensor DHT: ganti ke DHT11 jika memakai DHT11 ----
#define DHT_TYPE DHT22
// #define DHT_TYPE DHT11

// ---- Ambang batas (sesuai roadmap) ----
const float TEMP_HIGH_C   = 40.0;
const int   SMOKE_THRESH  = 400;

// ---- Servo scanning ----
const int  SERVO_MIN_ANGLE = 0;
const int  SERVO_MAX_ANGLE = 180;
const unsigned long SERVO_STEP_MS = 25;     // kecepatan sapuan (makin besar makin pelan)

// ---- Flame debounce & servo locking ----
const int  FLAME_CONFIRM_COUNT   = 3;       // jumlah pembacaan LOW beruntun untuk dianggap api
const unsigned long FLAME_RELEASE_MS = 3000; // api hilang selama ini -> servo lanjut scan

// ---- Pompa ----
const unsigned long OVERRIDE_MAX_MS = 60000; // override PUMP_ON otomatis mati setelah 60 detik

// ---- Relay: OFF dengan melepas pin (INPUT), sesuai hasil uji laporan ----
// true  : OFF = pinMode(INPUT)  (terbukti bekerja pada modul 5V + NodeMCU 3.3V)
// false : OFF = digitalWrite(HIGH)
const bool RELAY_OFF_BY_RELEASE = true;

// ---- Interval ----
const unsigned long DHT_INTERVAL_MS       = 2000;
const unsigned long PUBLISH_INTERVAL_MS   = 1000;
const unsigned long MQTT_RETRY_MS         = 5000;
const unsigned long WIFI_RETRY_MS         = 10000;

// ======================= OBJEK & STATE =======================
WiFiClient espClient;
PubSubClient mqtt(espClient);
Servo scanServo;
DHT dht(PIN_DHT, DHT_TYPE);

enum AiStatus {
  ST_NORMAL,
  ST_HIGH_TEMP,
  ST_LIGHT,
  ST_SMOLDERING,
  ST_FIRE
};

float temperature = 0.0;
int   smokeValue  = 0;
bool  flameDetected = false;       // sudah terdebounce
int   flameLowCount = 0;
unsigned long lastFlameSeen = 0;

int   servoAngle = 90;
int   servoDir   = 1;
bool  servoLocked = false;
unsigned long lastServoStep = 0;

AiStatus aiStatus = ST_NORMAL;
bool  pumpOn = false;
bool  overridePumpOn = false;
unsigned long overrideStart = 0;

unsigned long lastDht = 0, lastPublish = 0, lastMqttTry = 0, lastWifiTry = 0;
unsigned long lastBlink = 0;
bool blinkState = false;

// ======================= RELAY =======================
void relaySet(bool on) {
  if (on) {
    pinMode(PIN_RELAY, OUTPUT);
    digitalWrite(PIN_RELAY, LOW);
  } else {
    if (RELAY_OFF_BY_RELEASE) {
      pinMode(PIN_RELAY, INPUT);
    } else {
      pinMode(PIN_RELAY, OUTPUT);
      digitalWrite(PIN_RELAY, HIGH);
    }
  }
}

// ======================= BUZZER =======================
// Modul buzzer 3 kaki umumnya aktif LOW (LOW = bunyi).
// Agar HIGH 3.3V benar-benar mematikan modul, sambungkan VCC buzzer ke 3V3 (bukan 5V).
// Jika buzzer Anda ternyata aktif HIGH, ubah menjadi false.
const bool BUZZER_ACTIVE_LOW = true;

void buzzerSet(bool on) {
  pinMode(PIN_BUZZER, OUTPUT);
  digitalWrite(PIN_BUZZER, (on == BUZZER_ACTIVE_LOW) ? LOW : HIGH);
}

// ======================= MQTT =======================
const char* statusToString(AiStatus s) {
  switch (s) {
    case ST_NORMAL:     return "NORMAL";
    case ST_HIGH_TEMP:  return "HIGH_TEMP_WARNING";
    case ST_LIGHT:      return "LIGHT_WARNING";
    case ST_SMOLDERING: return "SMOLDERING_WARNING";
    case ST_FIRE:       return "BERBAHAYA";
  }
  return "NORMAL";
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  // Abaikan pesan dari topic lain.
  if (strcmp(topic, TOPIC_CONTROL) != 0) {
    Serial.printf("[MQTT] Topic tidak dikenal, diabaikan: %s\n", topic);
    return;
  }

  // Salin payload dengan batas ukuran agar selalu diakhiri null terminator.
  char msg[96];
  unsigned int n = (length < sizeof(msg) - 1) ? length : sizeof(msg) - 1;
  memcpy(msg, payload, n);
  msg[n] = '\0';

  Serial.printf("[MQTT] Control diterima: %s\n", msg);

  // Mendukung perintah plain text dan JSON sederhana, misalnya:
  // PUMP_ON atau {\"command\":\"PUMP_ON\"}
  // PUMP_OFF diperiksa lebih dahulu agar tidak salah terbaca sebagai PUMP_ON.
  if (strstr(msg, "PUMP_OFF") != nullptr) {
    overridePumpOn = false;
    Serial.println("[PUMP] Override manual OFF dilepas; status otomatis tetap berlaku.");
  } else if (strstr(msg, "PUMP_ON") != nullptr) {
    overridePumpOn = true;
    overrideStart = millis();
    Serial.println("[PUMP] Override manual ON diterima (maksimal 60 detik).");
  } else {
    Serial.println("[MQTT] Perintah tidak dikenali.");
  }
}

ESP8266WiFiMulti wifiMulti;

void connectWifi() {
  WiFi.mode(WIFI_STA);

  // ---- Daftarkan semua Wi-Fi di sini ----
  wifiMulti.addAP("JAMS", "Tanyaibukos");
  wifiMulti.addAP("JAMS 5G", "Tanyaibukos");
  wifiMulti.addAP("NAMA_HOTSPOT_HP", "PASSWORD_HOTSPOT_HP");
  // ----------------------------------------

  Serial.println("[WiFi] Mencari jaringan yang terdaftar ...");
  lastWifiTry = 0;  // paksa percobaan pertama segera
}

void maintainConnections() {
  unsigned long now = millis();

  if (WiFi.status() != WL_CONNECTED) {
    if (now - lastWifiTry > WIFI_RETRY_MS || lastWifiTry == 0) {
      lastWifiTry = now;
      if (wifiMulti.run(5000) == WL_CONNECTED) {
        Serial.printf("[WiFi] Tersambung ke %s, IP %s\n",
                      WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
      }
    }
    return;
  }

  if (!mqtt.connected() && now - lastMqttTry > MQTT_RETRY_MS) {
    lastMqttTry = now;
    String clientId = "agniot-esp-" + String(ESP.getChipId(), HEX);
    Serial.print("[MQTT] Menghubungkan ... ");
    if (mqtt.connect(clientId.c_str())) {
      Serial.println("tersambung");
      if (mqtt.subscribe(TOPIC_CONTROL)) {
        Serial.printf("[MQTT] Subscribe berhasil: %s\n", TOPIC_CONTROL);
      } else {
        Serial.printf("[MQTT] Subscribe gagal: %s\n", TOPIC_CONTROL);
      }
    } else {
      Serial.printf("gagal, rc=%d\n", mqtt.state());
    }
  }
}

void publishTelemetry() {
  // Pertahankan nama field agar kompatibel dengan dashboard Lutfi.
  // flame: 0 = api terdeteksi (output sensor LOW), 1 = tidak terdeteksi.
  char buf[200];
  int written = snprintf(buf, sizeof(buf),
    "{\"temp\":%.1f,\"smoke\":%d,\"flame\":%d,\"servo_angle\":%d,"
    "\"ai_status\":\"%s\",\"pump_status\":\"%s\"}",
    temperature, smokeValue, flameDetected ? 0 : 1, servoAngle,
    statusToString(aiStatus), pumpOn ? "ON" : "OFF");

  if (written < 0 || written >= (int)sizeof(buf)) {
    Serial.println("[MQTT] Gagal membentuk JSON telemetry (buffer tidak cukup).");
    return;
  }

  if (mqtt.publish(TOPIC_TELEMETRY, buf)) {
    Serial.printf("[MQTT] Telemetry terkirim ke %s: %s\n", TOPIC_TELEMETRY, buf);
  } else {
    Serial.println("[MQTT] Publish telemetry gagal.");
  }
}

// ======================= SENSOR =======================
void readSensors() {
  unsigned long now = millis();

  // Suhu (DHT lambat -> baca periodik, pertahankan nilai terakhir jika gagal)
  if (now - lastDht >= DHT_INTERVAL_MS) {
    lastDht = now;
    float t = dht.readTemperature();
    if (!isnan(t)) temperature = t;
    else Serial.println("[DHT] Gagal membaca sensor");
  }

  // MQ-2: nilai ADC mentah ESP8266 (0-1023), bukan ppm/CO2 terkalibrasi.
  smokeValue = analogRead(PIN_MQ2_AO);

  // Flame + debounce
  if (digitalRead(PIN_FLAME_DO) == LOW) {
    if (flameLowCount < FLAME_CONFIRM_COUNT) flameLowCount++;
    if (flameLowCount >= FLAME_CONFIRM_COUNT) {
      flameDetected = true;
      lastFlameSeen = now;
    }
  } else {
    flameLowCount = 0;
    if (flameDetected && now - lastFlameSeen > FLAME_RELEASE_MS) {
      flameDetected = false;
    }
  }
}

// ======================= LOGIKA AI (RULE-BASED SENSOR FUSION) =======================
void evaluateAi() {
  bool isTempHigh    = (temperature >= TEMP_HIGH_C);
  bool isSmokePekat  = (smokeValue >= SMOKE_THRESH);
  bool isFlameExist  = flameDetected;

  if (isFlameExist && (isSmokePekat || isTempHigh)) {
    aiStatus = ST_FIRE;                 // api + asap ATAU api + suhu tinggi
  } else if (isFlameExist) {
    aiStatus = ST_LIGHT;                // api saja -> kemungkinan pantulan cahaya
  } else if (isSmokePekat) {
    aiStatus = ST_SMOLDERING;           // asap pekat tanpa api (termasuk + suhu tinggi)
  } else if (isTempHigh) {
    aiStatus = ST_HIGH_TEMP;
  } else {
    aiStatus = ST_NORMAL;
  }
}

// ======================= SERVO =======================
void updateServo() {
  // Servo locking: berhenti di posisi api saat status FIRE
  servoLocked = (aiStatus == ST_FIRE);
  if (servoLocked) return;

  unsigned long now = millis();
  if (now - lastServoStep < SERVO_STEP_MS) return;
  lastServoStep = now;

  servoAngle += servoDir;
  if (servoAngle >= SERVO_MAX_ANGLE) { servoAngle = SERVO_MAX_ANGLE; servoDir = -1; }
  if (servoAngle <= SERVO_MIN_ANGLE) { servoAngle = SERVO_MIN_ANGLE; servoDir = 1; }
  scanServo.write(servoAngle);
}

// ======================= AKTUATOR =======================
void updateActuators() {
  unsigned long now = millis();

  // Override otomatis dibatalkan setelah batas waktu (lindungi pompa dari kerja kering)
  if (overridePumpOn && now - overrideStart > OVERRIDE_MAX_MS) {
    overridePumpOn = false;
    Serial.println("[PUMP] Override habis waktu, dimatikan");
  }

  // Pump ON = (AI == BERBAHAYA) OR (Override == PUMP_ON)
  bool wantPump = (aiStatus == ST_FIRE) || overridePumpOn;
  if (wantPump != pumpOn) {
    pumpOn = wantPump;
    relaySet(pumpOn);
    Serial.printf("[PUMP] %s\n", pumpOn ? "ON" : "OFF");
  }

  // Blink 500 ms
  if (now - lastBlink >= 500) {
    lastBlink = now;
    blinkState = !blinkState;
  }

  bool led1 = false, led2 = false, led3 = false, buzzer = false;
  switch (aiStatus) {
    case ST_NORMAL:
      led1 = true;
      break;
    case ST_HIGH_TEMP:
      led2 = blinkState;               // LED 2 berkedip
      break;
    case ST_LIGHT:
      led2 = true;
      break;
    case ST_SMOLDERING:
      led2 = true;
      buzzer = blinkState;             // bip-bip lambat
      break;
    case ST_FIRE:
      led3 = true;
      buzzer = true;
      break;
  }

  digitalWrite(PIN_LED_NORMAL, led1 ? HIGH : LOW);
  digitalWrite(PIN_LED_WARN,   led2 ? HIGH : LOW);
  digitalWrite(PIN_LED_DANGER, led3 ? HIGH : LOW);
  buzzerSet(buzzer);
}

// ======================= SETUP & LOOP =======================
void setup() {
  // Relay dipastikan OFF paling awal
  relaySet(false);

  Serial.begin(115200);
  delay(100);
  Serial.println("\n=== AGNIOT Firmware ===");

  pinMode(PIN_FLAME_DO, INPUT);
  pinMode(PIN_LED_NORMAL, OUTPUT);
  pinMode(PIN_LED_WARN, OUTPUT);
  pinMode(PIN_LED_DANGER, OUTPUT);
  buzzerSet(false);

  dht.begin();
  scanServo.attach(PIN_SERVO);
  scanServo.write(servoAngle);

  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(mqttCallback);

  connectWifi();
}

void loop() {
  maintainConnections();
  if (mqtt.connected()) mqtt.loop();

  readSensors();
  evaluateAi();
  updateServo();
  updateActuators();

  unsigned long now = millis();
  if (mqtt.connected() && now - lastPublish >= PUBLISH_INTERVAL_MS) {
    lastPublish = now;
    publishTelemetry();
  }

  delay(10);  // beri waktu untuk WiFi stack & debounce flame
}