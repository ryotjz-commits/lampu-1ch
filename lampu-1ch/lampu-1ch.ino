
/*
  ==========================================
       NOVIA HOME V2
       PREMIUM SMART HOME SYSTEM
  ==========================================
  Board       : ESP32
  Controller  : Blynk IoT
  Web Server  : Local Network
  Relay       : Active LOW
  ==========================================
*/

// ========== BLYNK CONFIG ==========

#define BLYNK_TEMPLATE_ID   "ISI_TEMPLATE_ID"
#define BLYNK_TEMPLATE_NAME "Novia Home"
#define BLYNK_AUTH_TOKEN    "ISI_AUTH_TOKEN"

#define BLYNK_PRINT Serial

// ========== LIBRARY ==========

#include <WiFi.h>
#include <WebServer.h>
#include <BlynkSimpleEsp32.h>

// ========== WIFI CONFIG ==========

char ssid[] = "Tenda";
char pass[] = "Noke0710";

// ========== SERVER ==========

WebServer server(80);

// ========== RELAY PIN ==========

#define RELAY_LUAR   26
#define RELAY_TENGAH 27
#define RELAY_KAMAR  14
#define RELAY_KIPAS  13

const uint8_t relayPins[] = {
  RELAY_LUAR,
  RELAY_TENGAH,
  RELAY_KAMAR,
  RELAY_KIPAS
};

bool relayStatus[4] = {
  false, false, false, false
};

// ========== CONNECTION TIMER ==========

unsigned long lastWiFiCheck = 0;
unsigned long lastBlynkCheck = 0;

const unsigned long WIFI_INTERVAL = 15000;
const unsigned long BLYNK_INTERVAL = 10000;

// ==========================================
//              RELAY CONTROL
// ==========================================

void setRelay(uint8_t ch, bool nyala, bool syncBlynk = true) {

  if (ch > 3) return;

  relayStatus[ch] = nyala;

  // Active LOW
  digitalWrite(relayPins[ch], nyala ? LOW : HIGH);

  // Sinkronisasi Blynk
  if (syncBlynk && Blynk.connected()) {
    Blynk.virtualWrite(V0 + ch, nyala ? 1 : 0);
  }
}

void setAllRelay(bool nyala) {

  for (uint8_t i = 0; i < 4; i++) {
    setRelay(i, nyala);
  }
}

// ==========================================
//                 BLYNK
// ==========================================

BLYNK_WRITE(V0) {
  setRelay(0, param.asInt() != 0, false);
}

BLYNK_WRITE(V1) {
  setRelay(1, param.asInt() != 0, false);
}

BLYNK_WRITE(V2) {
  setRelay(2, param.asInt() != 0, false);
}

BLYNK_WRITE(V3) {
  setRelay(3, param.asInt() != 0, false);
}

BLYNK_CONNECTED() {

  for (uint8_t i = 0; i < 4; i++) {
    Blynk.virtualWrite(V0 + i, relayStatus[i] ? 1 : 0);
  }

  Serial.println("Blynk Connected");
}

// ==========================================
//              WEB COMPONENTS
// ==========================================

String tombol(
  String nama,
  String icon,
  uint8_t ch
) {

  String html = "<div class='device' id='device" + String(ch) + "'>";

  html += "<div class='device-top'>";
  html += "<div class='device-icon'>" + icon + "</div>";

  html += "<span class='badge ";
  html += relayStatus[ch] ? "active" : "inactive";
  html += "' id='badge" + String(ch) + "'>";

  html += relayStatus[ch] ? "ON" : "OFF";
  html += "</span></div>";

  html += "<div class='device-name'>" + nama + "</div>";

  html += "<div class='device-desc' id='desc" + String(ch) + "'>";
  html += relayStatus[ch] ? "Perangkat aktif" : "Perangkat tidak aktif";
  html += "</div>";

  html += "<button class='control ";
  html += relayStatus[ch] ? "btn-off" : "btn-on";
  html += "' id='btn" + String(ch) + "' data-ch='" + String(ch) + "'>";

  html += relayStatus[ch] ? "MATIKAN" : "NYALAKAN";

  html += "</button></div>";

  return html;
}

// ==========================================
//                WEB PAGE
// ==========================================

String halamanWeb() {

  String html = R"rawliteral(
<!DOCTYPE html>
<html lang="id">
<head>

<meta charset="UTF-8">
<meta name="viewport"
content="width=device-width, initial-scale=1">

<meta name="theme-color" content="#151613">

<title>Novia Home</title>

<style>

:root {
  --bg: #151613;
  --surface: #20211d;
  --surface2: #292a24;
  --text: #f2f0e9;
  --muted: #92938a;
  --gold: #c5a875;
  --green: #9bbd91;
  --red: #d58c83;
  --border: rgba(255,255,255,.07);
}

* {
  box-sizing: border-box;
}

body {
  margin: 0;
  padding: 25px 17px 35px;
  background: var(--bg);
  color: var(--text);
  font-family: Arial, sans-serif;
  font-size: 13px;
  line-height: 1.6;
}

.container {
  max-width: 460px;
  margin: auto;
}

/* HEADER */

.header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 28px;
}

.brand {
  display: flex;
  align-items: center;
  gap: 11px;
}

.brand-icon {
  width: 43px;
  height: 43px;
  display: grid;
  place-items: center;
  border-radius: 13px;
  background: #29271f;
  color: var(--gold);
  font-size: 22px;
}

.brand h1 {
  margin: 0;
  font-size: 17px;
  font-weight: 600;
  letter-spacing: -.4px;
}

.brand p {
  margin: 2px 0 0;
  color: var(--muted);
  font-size: 10px;
  letter-spacing: 1px;
  text-transform: uppercase;
}

/* CONNECTION */

.connection {
  display: flex;
  align-items: center;
  gap: 7px;
  font-size: 9px;
  color: var(--muted);
}

.dot {
  width: 7px;
  height: 7px;
  border-radius: 50%;
  background: var(--red);
}

.dot.online {
  background: var(--green);
  box-shadow: 0 0 8px rgba(155,189,145,.5);
}

/* HERO */

.hero {
  padding: 24px;
  border: 1px solid var(--border);
  border-radius: 18px;
  background: linear-gradient(135deg,#302b20,#22231e);
  margin-bottom: 28px;
}

.hero-label {
  color: var(--gold);
  font-size: 10px;
  letter-spacing: 1.7px;
}

.hero h2 {
  margin: 9px 0 3px;
  font-size: 23px;
  font-weight: 500;
  letter-spacing: -.8px;
}

.hero p {
  color: #aaa99f;
  margin: 0;
  font-size: 11px;
}

/* SECTION */

.section-title {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 14px;
}

.section-title h3 {
  margin: 0;
  font-size: 12px;
  font-weight: 600;
  letter-spacing: .8px;
  text-transform: uppercase;
}

.section-title span {
  color: var(--muted);
  font-size: 10px;
}

/* DEVICE */

.grid {
  display: grid;
  grid-template-columns: repeat(2,minmax(0,1fr));
  gap: 12px;
}

.device {
  min-width: 0;
  padding: 16px;
  border-radius: 15px;
  background: var(--surface);
  border: 1px solid var(--border);
}

.device-top {
  display: flex;
  align-items: center;
  justify-content: space-between;
  margin-bottom: 19px;
}

.device-icon {
  width: 39px;
  height: 39px;
  display: grid;
  place-items: center;
  border-radius: 11px;
  background: var(--surface2);
  color: var(--gold);
  font-size: 19px;
}

.badge {
  padding: 4px 9px;
  border-radius: 20px;
  font-size: 9px;
  letter-spacing: .6px;
}

.active {
  background: rgba(155,189,145,.13);
  color: var(--green);
}

.inactive {
  background: rgba(213,140,131,.12);
  color: var(--red);
}

.device-name {
  font-size: 13px;
  font-weight: 600;
}

.device-desc {
  min-height: 18px;
  margin: 3px 0 15px;
  color: var(--muted);
  font-size: 10px;
}

/* BUTTON */

.control {
  display: block;
  width: 100%;
  padding: 10px 5px;
  border: 0;
  border-radius: 9px;
  font-size: 10px;
  font-weight: 600;
  letter-spacing: .7px;
  cursor: pointer;
  transition: .2s;
}

.btn-on {
  background: var(--gold);
  color: #201d16;
}

.btn-off {
  background: #35302a;
  color: #e0a39a;
}

.control:active,
.master button:active {
  transform: scale(.97);
}

button:disabled {
  opacity: .55;
  cursor: wait;
}

/* MASTER */

.master {
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: 10px;
  margin-top: 23px;
}

.master button {
  padding: 13px 5px;
  border: 0;
  border-radius: 10px;
  font-size: 10px;
  font-weight: 600;
  letter-spacing: .5px;
  cursor: pointer;
}

.all-on {
  background: var(--gold);
  color: #201d16;
}

.all-off {
  background: var(--surface2);
  color: var(--text);
  border: 1px solid var(--border) !important;
}

/* FOOTER */

.footer {
  margin-top: 32px;
  padding-top: 17px;
  border-top: 1px solid var(--border);
  color: #77786f;
  font-size: 10px;
  text-align: center;
  letter-spacing: .6px;
}

@media(max-width:350px) {
  body {
    padding: 18px 12px;
  }

  .device {
    padding: 12px;
  }

  .grid {
    gap: 8px;
  }
}

</style>
</head>

<body>

<div class="container">

  <div class="header">

    <div class="brand">
      <div class="brand-icon">⌂</div>

      <div>
        <h1>Novia Home</h1>
        <p>Smart Living System</p>
      </div>
    </div>

    <div class="connection">
      <span class="dot" id="statusDot"></span>
      <span id="statusText">MEMERIKSA</span>
    </div>

  </div>

  <div class="hero">
    <div class="hero-label">HOME AUTOMATION</div>
    <h2>Kontrol Rumah</h2>
    <p>Kelola perangkat rumah dalam satu tempat.</p>
  </div>

  <div class="section-title">
    <h3>Perangkat Saya</h3>
    <span>04 Perangkat</span>
  </div>

  <div class="grid">

)rawliteral";

  html += tombol("Lampu Luar", "💡", 0);
  html += tombol("Lampu Tengah", "✦", 1);
  html += tombol("Lampu Kamar", "⌂", 2);
  html += tombol("Kipas Angin", "❋", 3);

  html += R"rawliteral(
  </div>

  <div class="master">
    <button class="all-on" data-all="on">
      NYALAKAN SEMUA
    </button>

    <button class="all-off" data-all="off">
      MATIKAN SEMUA
    </button>
  </div>

  <div class="footer">
    NOVIA HOME · ESP32 SMART SYSTEM
  </div>

</div>

<script>

// UPDATE STATUS PERANGKAT

async function updateStatus() {

  try {

    const response = await fetch('/state');
    if (!response.ok) throw new Error('Network error');

    const data = await response.json();

    for (let i = 0; i < 4; i++) {

      const on = data.relay[i] === 1;

      const badge = document.getElementById('badge' + i);
      const desc = document.getElementById('desc' + i);
      const btn = document.getElementById('btn' + i);

      badge.textContent = on ? 'ON' : 'OFF';
      badge.className = 'badge ' + (on ? 'active' : 'inactive');

      desc.textContent = on
        ? 'Perangkat aktif'
        : 'Perangkat tidak aktif';

      btn.textContent = on ? 'MATIKAN' : 'NYALAKAN';
      btn.className = 'control ' + (on ? 'btn-off' : 'btn-on');

    }

    document.getElementById('statusDot')
      .classList.toggle('online', data.blynk);

    document.getElementById('statusText').textContent =
      data.blynk ? 'BLYNK ONLINE' :
      data.wifi ? 'LOKAL / OFFLINE' : 'WIFI OFFLINE';

  } catch (error) {

    document.getElementById('statusText').textContent = 'KONEKSI TERPUTUS';

  }

}

// KONTROL SATU PERANGKAT

document.addEventListener('click', async function(e) {

  const btn = e.target.closest('button');

  if (!btn) return;

  if (btn.dataset.ch !== undefined) {

    btn.disabled = true;

    try {
      await fetch('/toggle?ch=' + btn.dataset.ch);
      await updateStatus();
    } catch (error) {
      console.error(error);
    }

    btn.disabled = false;
  }

  if (btn.dataset.all) {

    document.querySelectorAll('button').forEach(b => b.disabled = true);

    try {
      await fetch('/all-' + btn.dataset.all);
      await updateStatus();
    } catch (error) {
      console.error(error);
    }

    document.querySelectorAll('button').forEach(b => b.disabled = false);
  }

});

// UPDATE OTOMATIS SETIAP 2 DETIK

updateStatus();
setInterval(updateStatus, 2000);

</script>

</body>
</html>
)rawliteral";

  return html;
}

// ==========================================
//              WEB SERVER ROUTES
// ==========================================

void setupRoutes() {

  server.on("/", HTTP_GET, []() {
    server.send(200, "text/html", halamanWeb());
  });

  server.on("/toggle", HTTP_GET, []() {

    if (server.hasArg("ch")) {

      int ch = server.arg("ch").toInt();

      if (ch >= 0 && ch <= 3) {
        setRelay(ch, !relayStatus[ch]);
      }
    }

    server.send(200, "text/plain", "OK");
  });

  server.on("/all-on", HTTP_GET, []() {
    setAllRelay(true);
    server.send(200, "text/plain", "OK");
  });

  server.on("/all-off", HTTP_GET, []() {
    setAllRelay(false);
    server.send(200, "text/plain", "OK");
  });

  // JSON STATUS

  server.on("/state", HTTP_GET, []() {

    String json = "{\"relay\":[";

    for (uint8_t i = 0; i < 4; i++) {
      json += relayStatus[i] ? "1" : "0";

      if (i < 3) json += ",";
    }

    json += "],\"blynk\":";
    json += Blynk.connected() ? "true" : "false";

    json += ",\"wifi\":";
    json += WiFi.status() == WL_CONNECTED ? "true" : "false";

    json += "}";

    server.send(200, "application/json", json);
  });

  server.begin();

  Serial.println("Web Server Started");
}

// ==========================================
//                   SETUP
// ==========================================

void setup() {

  Serial.begin(115200);

  // RELAY INITIALIZATION

  for (uint8_t i = 0; i < 4; i++) {

    pinMode(relayPins[i], OUTPUT);
    digitalWrite(relayPins[i], HIGH);

  }

  // WIFI INITIALIZATION

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, pass);
Serial.print("Nyambung WiFi...");
while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
Serial.println("\nIP: " + WiFi.localIP().toString());

  // BLYNK CONFIGURATION

  Blynk.config(BLYNK_AUTH_TOKEN);

  // WEB SERVER START

  setupRoutes();

  Serial.println("Novia Home V2 Started");

}

// ==========================================
//                    LOOP
// ==========================================

void loop() {

  // WEB SERVER
  server.handleClient();

  // BLYNK
  Blynk.run();

  unsigned long now = millis();

  // WIFI RECONNECT

  if (WiFi.status() != WL_CONNECTED) {

    if (now - lastWiFiCheck >= WIFI_INTERVAL) {

      lastWiFiCheck = now;

      WiFi.reconnect();

      Serial.println("Reconnecting WiFi...");

    }

  }

  // BLYNK RECONNECT

  if (WiFi.status() == WL_CONNECTED &&
      !Blynk.connected()) {

    if (now - lastBlynkCheck >= BLYNK_INTERVAL) {

      lastBlynkCheck = now;

      Serial.println("Connecting Blynk...");

      Blynk.connect(3000);

    }

  }

}
