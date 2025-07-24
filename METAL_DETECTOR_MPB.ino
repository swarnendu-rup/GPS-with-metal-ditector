/*
   ESP32 + A88 Metal Detector + NEO-7M GPS
   Single-file web map served directly from the ESP32
   NOW IN WIFI STATION (CLIENT) MODE
   2024-07-24
*/
#include <ElegantOTA.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <TinyGPS++.h>
#include <SPIFFS.h>
#include <TimeLib.h> // for makeTime / tmElements_t

// ------------- USER CONFIG -------------
// --- CHANGED: These are now the credentials for your WiFi ROUTER ---
const char* SSID = "MySpyCar"; // <-- CHANGE THIS
const char* PASSWORD = "123456789"; // <-- CHANGE THIS

const uint8_t DETECT_PIN = 15; // metal-detector digital output
const uint8_t GPS_TX = 17; // NEO-7M TX  → ESP32 RX2
const uint8_t GPS_RX = 16; // NEO-7M RX  → ESP32 TX2 (optional)
// ---------------------------------------

TinyGPSPlus gps;
AsyncWebServer server(80);

struct Hit {
  double lat, lon;
  uint32_t epoch;
};
std::vector<Hit> hits;

// Forward declaration for the HTML function
const char* index_html(); 

void setup() {
  Serial.begin(115200);
  Serial2.begin(9600, SERIAL_8N1, GPS_RX, GPS_TX);
  pinMode(DETECT_PIN, INPUT_PULLUP);

  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS Mount Failed");
    return;
  }

  loadHits(); // restore after reboot

  // --- CHANGED: Connect to WiFi as a Station (Client) ---
  WiFi.mode(WIFI_STA);
  WiFi.begin(SSID, PASSWORD);
  Serial.print("Connecting to WiFi...");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConnected!");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP()); // <-- CHANGED from softAPIP()

  // Serve root page (HTML + JS + CSS in one string)
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *req) {
    req->send(200, "text/html", index_html());
  });

  // Serve JSON list of hits
  server.on("/hits", HTTP_GET, [](AsyncWebServerRequest *req) {
    String json = "[";
    for (size_t i = 0; i < hits.size(); ++i) {
      json += "{\"lat\":" + String(hits[i].lat, 6) +
              ",\"lon\":" + String(hits[i].lon, 6) +
              ",\"ts\":"  + String(hits[i].epoch) + "}";
      if (i < hits.size() - 1) json += ",";
    }
    json += "]";
    req->send(200, "application/json", json);
  });
  
  ElegantOTA.begin(&server);    // Start ElegantOTA
  server.begin();
}

void loop() {
  while (Serial2.available()) gps.encode(Serial2.read());

  static bool lastState = HIGH;
  bool curState = digitalRead(DETECT_PIN);
  if (lastState == HIGH && curState == LOW) { // falling edge
    if (gps.location.isValid() && gps.date.isValid() && gps.time.isValid()) {
      uint32_t epoch = 0;
      {
        tmElements_t tm;
        tm.Year   = gps.date.year() - 1970;
        tm.Month  = gps.date.month();
        tm.Day    = gps.date.day();
        tm.Hour   = gps.time.hour();
        tm.Minute = gps.time.minute();
        tm.Second = gps.time.second();
        epoch = makeTime(tm);
      }
      Hit h { gps.location.lat(), gps.location.lng(), epoch };
      hits.push_back(h);
      saveHits();
      Serial.printf("Hit: %.6f,%.6f  %u\n", h.lat, h.lon, h.epoch);
    }
  }
  lastState = curState;
  ElegantOTA.loop();
}

// ---------- persistence ----------
void saveHits() {
  File f = SPIFFS.open("/hits.bin", FILE_WRITE);
  uint32_t n = hits.size();
  f.write((uint8_t*)&n, sizeof(n));
  f.write((uint8_t*)hits.data(), n * sizeof(Hit));
  f.close();
}

void loadHits() {
  if (!SPIFFS.exists("/hits.bin")) return;
  File f = SPIFFS.open("/hits.bin", FILE_READ);
  uint32_t n;
  f.read((uint8_t*)&n, sizeof(n));
  hits.resize(n);
  f.read((uint8_t*)hits.data(), n * sizeof(Hit));
  f.close();
}

// ---------- single-file web page ----------
const char* index_html() {
  return R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="utf-8"/>
  <title>Metal Log Map</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <link rel="stylesheet" href="https://unpkg.com/leaflet@1.9.4/dist/leaflet.css"/>
  <style> html,body,#map{margin:0;height:100%;font-family:sans-serif} </style>
</head>
<body>
<div id="map"></div>
<script src="https://unpkg.com/leaflet@1.9.4/dist/leaflet.js"></script>
<script>
(async function() {
  const map = L.map('map').setView([20, 0], 2);
  L.tileLayer('https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png', {
    attribution: '© OpenStreetMap contributors'
  }).addTo(map);

  const hits = await fetch('/hits').then(r => r.json());
  if (hits.length) {
    const g = L.layerGroup().addTo(map);
    hits.forEach(h => {
      L.marker([h.lat, h.lon])
       .addTo(g)
       .bindPopup(new Date(h.ts * 1000).toLocaleString());
    });
    map.fitBounds(g.getBounds().pad(0.05));
  } else {
    alert("No hits yet – go detect some metal!");
  }
})();
</script>
</body>
</html>
)rawliteral";
}