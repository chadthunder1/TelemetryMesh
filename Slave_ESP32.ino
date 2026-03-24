#include <esp_now.h>
#include <WiFi.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <DHT.h>
#define SLAVE_ID        2       // Change to 2, 3 … for each slave

// ── Master MAC address ──────────────────────────────────────
// Upload Master_ESP32.ino first, open Serial Monitor at 115200,
// copy the "addr" value printed on boot, paste it here:
uint8_t masterMAC[] = { 0x88, 0x57, 0x21, 0x78, 0xB5, 0x6C };
//                       ^^^  replace with your master's MAC  ^^^

// ── Pin definitions ─────────────────────────────────────────
#define DHT_PIN         4       // GPIO4  → DHT22 data pin
#define DHT_TYPE        DHT22

// I2C pins (ESP32 default — usually don't need to change)
#define SDA_PIN         21
#define SCL_PIN         22

// ── Timing ──────────────────────────────────────────────────
#define SEND_INTERVAL   2000    // ms between transmissions

// ╚══════════════════════════════════════════════════════════╝

DHT dht(DHT_PIN, DHT_TYPE);
LiquidCrystal_I2C lcd(0x27, 16, 2);   // I2C address 0x27, 16 cols, 2 rows

// ── Packet structure (must match master exactly) ─────────────
typedef struct SensorPacket {
  uint8_t  nodeID;
  float    temperature;
  float    humidity;
  float    extra;         // unused on basic DHT22 slave — set to 0
  uint32_t timestamp;
  bool     valid;
} SensorPacket;

SensorPacket    packet;
esp_now_peer_info_t peerInfo;

bool espNowReady = false;

// ── ESP-NOW send callback (optional LED feedback) ────────────
void onDataSent(const uint8_t *mac, esp_now_send_status_t status) {
  // status == ESP_NOW_SEND_SUCCESS means master received it
  // You can blink an LED here if you want visual feedback
}

// ── Helper: print a right-padded string on LCD ───────────────
void lcdPrint(uint8_t row, String text) {
  while (text.length() < 16) text += ' ';  // pad to 16 chars
  lcd.setCursor(0, row);
  lcd.print(text.substring(0, 16));
}

// ── Setup ─────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);

  // ── I2C & LCD ───────────────────────────────────────────────
  Wire.begin(SDA_PIN, SCL_PIN);
  lcd.init();
  lcd.backlight();
  lcdPrint(0, "TelemetryMesh");
  lcdPrint(1, "Node " + String(SLAVE_ID) + " starting");
  delay(1500);
  lcd.clear();

  // ── DHT sensor ──────────────────────────────────────────────
  dht.begin();

  // ── Wi-Fi (STA mode required for ESP-NOW) ───────────────────
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  // ── ESP-NOW ─────────────────────────────────────────────────
  if (esp_now_init() != ESP_OK) {
    lcdPrint(0, "ESP-NOW FAIL");
    lcdPrint(1, "Check config");
    Serial.println("ESP-NOW init failed");
    while (1) delay(1000);
  }

  esp_now_register_send_cb(onDataSent);

  // Register master as peer
  memset(&peerInfo, 0, sizeof(peerInfo));
  memcpy(peerInfo.peer_addr, masterMAC, 6);
  peerInfo.channel = 0;      // 0 = same channel as Wi-Fi interface
  peerInfo.encrypt = false;

  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    lcdPrint(0, "Peer add FAIL");
    Serial.println("Failed to add master peer");
    while (1) delay(1000);
  }

  espNowReady = true;
  packet.nodeID = SLAVE_ID;

  lcdPrint(0, "Node " + String(SLAVE_ID) + " Ready");
  lcdPrint(1, "Waiting DHT22");
  delay(2000);   // DHT22 needs ~2 s after power-on before first read
}

// ── Loop ──────────────────────────────────────────────────────
void loop() {
  static uint32_t lastSend = 0;

  if (millis() - lastSend < SEND_INTERVAL) return;
  lastSend = millis();

  // ── Read sensor ─────────────────────────────────────────────
  float temp = dht.readTemperature();   // °C
  float hum  = dht.readHumidity();      // %

  bool ok = !isnan(temp) && !isnan(hum);

  // ── Build packet ────────────────────────────────────────────
  packet.valid       = ok;
  packet.temperature = ok ? temp : 0.0f;
  packet.humidity    = ok ? hum  : 0.0f;
  packet.extra       = 0.0f;            // extend for other sensors here
  packet.timestamp   = millis();

  // ── Send via ESP-NOW ────────────────────────────────────────
  if (espNowReady) {
    esp_now_send(masterMAC, (uint8_t*)&packet, sizeof(packet));
  }

  // ── Update LCD ──────────────────────────────────────────────
  if (ok) {
    // Row 0: T:28.5C  H:62%
    String row0 = "T:" + String(temp, 1) + "C H:" + String(hum, 0) + "%";
    lcdPrint(0, row0);
  } else {
    lcdPrint(0, "Sensor Error!");
  }

  // Row 1: N:01 | 00042s
  uint32_t upSec = millis() / 1000;
  String row1 = "N:" + String(SLAVE_ID, DEC);
  row1 += ok ? " |OK| " : " |ERR|";
  // pad uptime to 5 digits
  String upStr = String(upSec);
  while (upStr.length() < 5) upStr = "0" + upStr;
  row1 += upStr + "s";
  lcdPrint(1, row1);

  // ── Debug to Serial ─────────────────────────────────────────
  Serial.print("Node "); Serial.print(SLAVE_ID);
  if (ok) {
    Serial.print(" | T:"); Serial.print(temp, 1);
    Serial.print("C  H:"); Serial.print(hum, 0); Serial.println("%");
  } else {
    Serial.println(" | Sensor read failed");
  }
}
