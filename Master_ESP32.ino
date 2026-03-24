#include <esp_now.h>
#include <WiFi.h>

// ── Packet structure (must match slave exactly) ──────────────
typedef struct SensorPacket {
  uint8_t  nodeID;        // Slave ID (1, 2, 3 …)
  float    temperature;   // °C
  float    humidity;      // %
  float    extra;         // spare field
  uint32_t timestamp;     // millis() on slave at send time
  bool     valid;         // false if sensor read failed
} SensorPacket;

// ── Node store (supports up to 9 slaves) ─────────────────────
SensorPacket nodeData[10];
bool         nodeActive[10]   = {false};
uint32_t     nodeLastSeen[10] = {0};

// ── ESP-NOW receive callback (2.0.x signature) ───────────────
void onDataReceived(const uint8_t *mac_addr,
                    const uint8_t *data, int len) {

  if (len != sizeof(SensorPacket)) return;

  SensorPacket pkt;
  memcpy(&pkt, data, sizeof(pkt));

  uint8_t id = pkt.nodeID;
  if (id < 1 || id > 9) return;

  nodeData[id]     = pkt;
  nodeActive[id]   = true;
  nodeLastSeen[id] = millis();

  // Emit one JSON line per received packet
  Serial.print(F("{"));
  Serial.print(F("\"node\":")); Serial.print(id);
  Serial.print(F(",\"temp\":")); Serial.print(pkt.temperature, 2);
  Serial.print(F(",\"hum\":")); Serial.print(pkt.humidity, 2);
  Serial.print(F(",\"extra\":")); Serial.print(pkt.extra, 2);
  Serial.print(F(",\"valid\":")); Serial.print(pkt.valid ? F("true") : F("false"));
  Serial.print(F(",\"ms\":")); Serial.print(pkt.timestamp);
  Serial.println(F("}"));
}

// ── Setup ─────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(200);

  Serial.println(F("{\"event\":\"boot\",\"role\":\"master\"}"));

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  // Print MAC — copy this into every slave's masterMAC[]
  Serial.print(F("{\"event\":\"mac\",\"addr\":\""));
  Serial.print(WiFi.macAddress());
  Serial.println(F("\"}"));

  if (esp_now_init() != ESP_OK) {
    Serial.println(F("{\"event\":\"error\",\"msg\":\"ESP-NOW init failed\"}"));
    while (1) delay(1000);
  }

  esp_now_register_recv_cb(onDataReceived);

  Serial.println(F("{\"event\":\"ready\",\"msg\":\"Waiting for slaves\"}"));
}

// ── Loop ──────────────────────────────────────────────────────
void loop() {
  // Heartbeat every 5 s
  static uint32_t lastHB = 0;
  if (millis() - lastHB >= 5000) {
    lastHB = millis();

    Serial.print(F("{\"event\":\"heartbeat\",\"uptime_s\":"));
    Serial.print(millis() / 1000);
    Serial.print(F(",\"active_nodes\":["));
    bool first = true;
    for (int i = 1; i <= 9; i++) {
      if (nodeActive[i]) {
        if (!first) Serial.print(',');
        Serial.print(i);
        first = false;
      }
    }
    Serial.println(F("]}"));
  }
}