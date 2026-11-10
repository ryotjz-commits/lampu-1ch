
/*
  ==========================================
          NOVIA HOME V3
       PREMIUM SMART HOME SYSTEM
  ==========================================

  Board       : ESP32
  Controller  : Blynk IoT
  Web Server  : Local Network
  WiFi        : Custom Provisioning
  Storage     : Preferences (NVS)
  Relay       : Active LOW

  FEATURES:
  - Custom WiFi Setup Portal
  - WiFi Network Scanner
  - Automatic WiFi Reconnect
  - Automatic AP Fallback
  - Dashboard Local Control
  - Blynk IoT Synchronization
  - 4 Channel Relay
  ==========================================
*/

// ==========================================
// BLYNK CONFIGURATION
// ==========================================

#define BLYNK_TEMPLATE_ID "ISI_TEMPLATE_ID"
#define BLYNK_TEMPLATE_NAME "Novia Home"
#define BLYNK_AUTH_TOKEN "ISI_AUTH_TOKEN"

#define BLYNK_PRINT Serial

// ==========================================
// LIBRARIES
// ==========================================

#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <BlynkSimpleEsp32.h>

// ==========================================
// WIFI PROVISIONING
// ==========================================

const char* AP_SSID = "Novia-Home-Setup";
const char* AP_PASS = "NoviaHome26";

WebServer server(80);
DNSServer dnsServer;
Preferences preferences;

bool apActive = false;
bool wifiPending = false;

String pendingSSID;
String pendingPASS;

unsigned long wifiPendingTime = 0;
unsigned long wifiLostTime = 0;
unsigned long lastWiFiRetry = 0;
unsigned long lastBlynkCheck = 0;

const unsigned long WIFI_RETRY_INTERVAL = 15000;
const unsigned long WIFI_FALLBACK_TIME = 15000;
const unsigned long WIFI_CONNECT_TIMEOUT = 20000;
const unsigned long BLYNK_RETRY_INTERVAL = 10000;

// ==========================================
// RELAY CONFIGURATION
// ==========================================

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

// ==========================================
// DEVICE INFORMATION
// ==========================================

const char* deviceNames[] = {
  "Lampu Luar",
  "Lampu Tengah",
  "Lampu Kamar",
  "Kipas Angin"
};

const char* deviceIcons[] = {
  "💡",
  "✦",
  "⌂",
  "❋"
};

// ==========================================
// RELAY CONTROL
// ==========================================

void setRelay(uint8_t ch, bool nyala,
              bool syncBlynk = true) {

  if (ch >= 4) return;

  relayStatus[ch] = nyala;

  // Active LOW
  digitalWrite(
    relayPins[ch],
    nyala ? LOW : HIGH
  );

  if (syncBlynk && Blynk.connected()) {
    Blynk.virtualWrite(
      V0 + ch,
      nyala ? 1 : 0
    );
  }
}

void setAllRelay(bool nyala) {

  for (uint8_t i = 0; i < 4; i++) {
    setRelay(i, nyala);
  }

}

// ==========================================
// BLYNK CALLBACKS
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

    Blynk.virtualWrite(
      V0 + i,
      relayStatus[i] ? 1 : 0
    );

  }

  Serial.println("Blynk Connected");

}

// ==========================================
// WIFI STORAGE
// ==========================================

String savedSSID() {
  return preferences.getString("ssid", "");
}

String savedPASS() {
  return preferences.getString("pass", "");
}

void saveWiFi(String ssid, String pass) {

  preferences.putString("ssid", ssid);
  preferences.putString("pass", pass);

  Serial.println("WiFi Configuration Saved");

}

// ==========================================
// START CONFIGURATION ACCESS POINT
// ==========================================

void startConfigAP() {

  if (apActive) return;

  WiFi.mode(WIFI_AP_STA);

  bool result = WiFi.softAP(
    AP_SSID,
    AP_PASS
  );

  if (result) {

    dnsServer.start(
      53,
      "*",
      WiFi.softAPIP()
    );

    apActive = true;

    Serial.println("Configuration AP Started");
    Serial.print("SSID: ");
    Serial.println(AP_SSID);

    Serial.print("IP: ");
    Serial.println(WiFi.softAPIP());

  }

}

// ==========================================
// STOP CONFIGURATION ACCESS POINT
// ==========================================

void stopConfigAP() {

  if (!apActive) return;

  dnsServer.stop();

  WiFi.softAPdisconnect(true);

  WiFi.mode(WIFI_STA);

  apActive = false;

  Serial.println("Configuration AP Stopped");

}

// ==========================================
// WIFI SCAN JSON
// ==========================================

String jsonEscape(String value) {

  value.replace("\\", "\\\\");
  value.replace("\"", "\\\"");
  value.replace("\n", "\\n");
  value.replace("\r", "\\r");

  return value;

}

String scanWiFi() {

  int count = WiFi.scanNetworks(false, true);

  String json = "[";

  for (int i = 0; i < count; i++) {

    if (i > 0) json += ",";

    json += "{";

    json += "\"ssid\":\"";
    json += jsonEscape(WiFi.SSID(i));
    json += "\",";

    json += "\"rssi\":";
    json += String(WiFi.RSSI(i));
    json += ",";

    json += "\"secure\":";
    json += WiFi.encryptionType(i) == WIFI_AUTH_OPEN
            ? "false" : "true";

    json += "}";

  }

  json += "]";

  WiFi.scanDelete();

  return json;

}

// ==========================================
// CUSTOM WIFI PORTAL
// ==========================================

String halamanWiFi() {

  String html = R"rawliteral(
<!DOCTYPE html>
<html lang="id">
<head>

<meta charset="UTF-8">

<meta name="viewport"
content="width=device-width,initial-scale=1">

<meta name="theme-color" content="#151613">

<title>WiFi Setup - Novia Home</title>

<style>

:root {
  --bg:#151613;
  --surface:#20211d;
  --surface2:#292a24;
  --text:#f2f0e9;
  --muted:#92938a;
  --gold:#c5a875;
  --green:#9bbd91;
  --red:#d58c83;
  --border:rgba(255,255,255,.07);
}

* {
  box-sizing:border-box;
}

body {
  margin:0;
  padding:28px 17px;
  background:var(--bg);
  color:var(--text);
  font-family:Arial,sans-serif;
  font-size:13px;
  line-height:1.6;
}

.container {
  max-width:420px;
  margin:auto;
}

.header {
  display:flex;
  align-items:center;
  gap:12px;
  margin-bottom:32px;
}

.logo {
  width:43px;
  height:43px;
  display:grid;
  place-items:center;
  border-radius:13px;
  background:#29271f;
  color:var(--gold);
  font-size:23px;
}

h1 {
  margin:0;
  font-size:17px;
}

.subtitle {
  color:var(--muted);
  font-size:10px;
  letter-spacing:1px;
}

.card {
  background:var(--surface);
  border:1px solid var(--border);
  border-radius:17px;
  padding:21px;
}

.label {
  color:var(--gold);
  font-size:10px;
  letter-spacing:1.5px;
}

h2 {
  margin:8px 0 5px;
  font-size:22px;
  font-weight:500;
}

.desc {
  margin:0 0 23px;
  color:var(--muted);
  font-size:11px;
}

.field {
  margin-bottom:17px;
}

.field label {
  display:block;
  margin-bottom:8px;
  font-size:11px;
  color:#c9c8bd;
}

select,input {
  width:100%;
  padding:13px;
  border:1px solid var(--border);
  border-radius:9px;
  background:var(--surface2);
  color:var(--text);
  font-size:12px;
  outline:none;
}

select:focus,input:focus {
  border-color:var(--gold);
}

.row {
  display:flex;
  align-items:center;
  justify-content:space-between;
  margin-bottom:8px;
}

.scan {
  border:0;
  background:transparent;
  color:var(--gold);
  font-size:10px;
  cursor:pointer;
}

.save {
  width:100%;
  padding:13px;
  border:0;
  border-radius:9px;
  background:var(--gold);
  color:#201d16;
  font-size:11px;
  font-weight:bold;
  letter-spacing:.7px;
  cursor:pointer;
}

.save:disabled {
  opacity:.5;
}

.message {
  display:none;
  margin-top:15px;
  padding:12px;
  border-radius:9px;
  background:var(--surface2);
  color:var(--green);
  font-size:11px;
}

.info {
  margin-top:18px;
  padding:13px;
  border-radius:10px;
  background:var(--surface2);
  color:var(--muted);
  font-size:10px;
}

.footer {
  margin-top:27px;
  text-align:center;
  color:#77786f;
  font-size:10px;
  letter-spacing:.7px;
}

</style>
</head>

<body>

<div class="container">

  <div class="header">

    <div class="logo">⌂</div>

    <div>
      <h1>Novia Home</h1>
      <div class="subtitle">SMART WIFI CONFIGURATION</div>
    </div>

  </div>

  <div class="card">

    <div class="label">NETWORK SETTINGS</div>

    <h2>Hubungkan WiFi</h2>

    <p class="desc">
      Pilih jaringan WiFi rumah dan masukkan password
      untuk menghubungkan perangkat.
    </p>

    <div class="field">

      <div class="row">

        <label>Jaringan WiFi</label>

        <button class="scan" onclick="scanWiFi()">
          ↻ SCAN ULANG
        </button>

      </div>

      <select id="ssid">
        <option value="">Memindai jaringan...</option>
      </select>

    </div>

    <div class="field">

      <label>Password WiFi</label>

      <input
        type="password"
        id="password"
        placeholder="Masukkan password WiFi"
        autocomplete="off"
      >

    </div>

    <button class="save" id="saveBtn"
      onclick="saveWiFi()">

      SIMPAN & HUBUNGKAN

    </button>

    <div class="message" id="message"></div>

    <div class="info">

      <b>Informasi</b><br>

      Pengaturan WiFi akan disimpan secara
      otomatis setelah koneksi berhasil.

    </div>

  </div>

  <div class="footer">
    NOVIA HOME · ESP32 SMART SYSTEM
  </div>

</div>

<script>

async function scanWiFi() {

  const select = document.getElementById('ssid');

  select.innerHTML =
    '<option value="">Memindai jaringan...</option>';

  try {

    const response = await fetch('/scan');
    const networks = await response.json();

    select.innerHTML =
      '<option value="">Pilih jaringan WiFi</option>';

    networks.sort((a,b) => b.rssi - a.rssi);

    networks.forEach(network => {

      if (!network.ssid) return;

      const option = document.createElement('option');

      option.value = network.ssid;

      option.textContent =
        network.ssid +
        ' · ' + network.rssi + ' dBm' +
        (network.secure ? ' 🔒' : ' · Open');

      select.appendChild(option);

    });

    if (networks.length === 0) {
      select.innerHTML =
        '<option value="">Tidak ada jaringan ditemukan</option>';
    }

  } catch(error) {

    select.innerHTML =
      '<option value="">Gagal memindai jaringan</option>';

  }

}

async function saveWiFi() {

  const ssid = document.getElementById('ssid').value;
  const password = document.getElementById('password').value;

  const button = document.getElementById('saveBtn');
  const message = document.getElementById('message');

  if (!ssid) {
    alert('Pilih jaringan WiFi terlebih dahulu');
    return;
  }

  button.disabled = true;
  button.textContent = 'MENGHUBUNGKAN...';

  message.style.display = 'block';
  message.textContent = 'Sedang menghubungkan ke WiFi...';

  try {

    const response = await fetch('/save-wifi', {

      method:'POST',

      headers:{
        'Content-Type':'application/x-www-form-urlencoded'
      },

      body:
        'ssid=' + encodeURIComponent(ssid) +
        '&pass=' + encodeURIComponent(password)

    });

    const result = await response.json();

    if (!result.ok) {
      throw new Error(result.message);
    }

    checkConnection();

  } catch(error) {

    message.textContent = 'Gagal mengirim konfigurasi.';

    button.disabled = false;
    button.textContent = 'COBA LAGI';

  }

}

async function checkConnection() {

  const button = document.getElementById('saveBtn');
  const message = document.getElementById('message');

  const timer = setInterval(async () => {

    try {

      const response = await fetch('/wifi-status');
      const data = await response.json();

      if (data.connected) {

        clearInterval(timer);

        message.textContent =
          'WiFi berhasil terhubung! IP: ' + data.ip;

        button.textContent = 'BERHASIL';

        setTimeout(() => {
          window.location.href = '/';
        }, 2500);

      } else if (!data.pending) {

        clearInterval(timer);

        message.textContent =
          'Gagal terhubung. Periksa password dan coba kembali.';

        button.disabled = false;
        button.textContent = 'COBA LAGI';

      }

    } catch(error) {}

  }, 1500);

}

scanWiFi();

</script>

</body>
</html>
)rawliteral";

  return html;

}

// ==========================================
// DASHBOARD DEVICE COMPONENT
// ==========================================

String tombol(String nama, String icon, uint8_t ch) {

  String html =
    "<div class='device' id='device" + String(ch) + "'>";

  html += "<div class='device-top'>";
  html += "<div class='device-icon'>" + icon + "</div>";

  html += "<span class='badge ";
  html += relayStatus[ch] ? "active" : "inactive";
  html += "' id='badge" + String(ch) + "'>";

  html += relayStatus[ch] ? "ON" : "OFF";

  html += "</span></div>";

  html += "<div class='device-name'>" + nama + "</div>";

  html += "<div class='device-desc' id='desc";
  html += String(ch) + "'>";

  html += relayStatus[ch]
          ? "Perangkat aktif"
          : "Perangkat tidak aktif";

  html += "</div>";

  html += "<button class='control ";
  html += relayStatus[ch] ? "btn-off" : "btn-on";
  html += "' id='btn" + String(ch) + "' data-ch='";
  html += String(ch) + "'>";

  html += relayStatus[ch] ? "MATIKAN" : "NYALAKAN";

  html += "</button></div>";

  return html;

}

// ==========================================
// MAIN DASHBOARD
// ==========================================

String halamanWeb() {

  String html = R"rawliteral(
<!DOCTYPE html>
<html lang="id">
<head>

<meta charset="UTF-8">

<meta name="viewport"
content="width=device-width,initial-scale=1">

<meta name="theme-color" content="#151613">

<title>Novia Home</title>

<style>

:root {
  --bg:#151613;
  --surface:#20211d;
  --surface2:#292a24;
  --text:#f2f0e9;
  --muted:#92938a;
  --gold:#c5a875;
  --green:#9bbd91;
  --red:#d58c83;
  --border:rgba(255,255,255,.07);
}

* {box-sizing:border-box}

body {
  margin:0;
  padding:25px 17px 35px;
  background:var(--bg);
  color:var(--text);
  font-family:Arial,sans-serif;
  font-size:13px;
  line-height:1.6;
}

.container {
  max-width:460px;
  margin:auto;
}

.header {
  display:flex;
  justify-content:space-between;
  align-items:center;
  margin-bottom:28px;
}

.brand {
  display:flex;
  align-items:center;
  gap:11px;
}

.brand-icon {
  width:43px;
  height:43px;
  display:grid;
  place-items:center;
  border-radius:13px;
  background:#29271f;
  color:var(--gold);
  font-size:22px;
}

.brand h1 {
  margin:0;
  font-size:17px;
}

.brand p {
  margin:2px 0 0;
  color:var(--muted);
  font-size:10px;
  letter-spacing:1px;
}

.connection {
  display:flex;
  align-items:center;
  gap:7px;
  font-size:9px;
  color:var(--muted);
}

.dot {
  width:7px;
  height:7px;
  border-radius:50%;
  background:var(--red);
}

.dot.online {
  background:var(--green);
  box-shadow:0 0 8px rgba(155,189,145,.5);
}

.hero {
  padding:24px;
  border:1px solid var(--border);
  border-radius:18px;
  background:linear-gradient(135deg,#302b20,#22231e);
  margin-bottom:28px;
}

.hero-label {
  color:var(--gold);
  font-size:10px;
  letter-spacing:1.7px;
}

.hero h2 {
  margin:9px 0 3px;
  font-size:23px;
  font-weight:500;
}

.hero p {
  color:#aaa99f;
  margin:0;
  font-size:11px;
}

.section-title {
  display:flex;
  justify-content:space-between;
  align-items:center;
  margin-bottom:14px;
}

.section-title h3 {
  margin:0;
  font-size:12px;
  letter-spacing:.8px;
}

.section-title span {
  color:var(--muted);
  font-size:10px;
}

.grid {
  display:grid;
  grid-template-columns:repeat(2,minmax(0,1fr));
  gap:12px;
}

.device {
  min-width:0;
  padding:16px;
  border-radius:15px;
  background:var(--surface);
  border:1px solid var(--border);
}

.device-top {
  display:flex;
  align-items:center;
  justify-content:space-between;
  margin-bottom:19px;
}

.device-icon {
  width:39px;
  height:39px;
  display:grid;
  place-items:center;
  border-radius:11px;
  background:var(--surface2);
  color:var(--gold);
  font-size:19px;
}

.badge {
  padding:4px 9px;
  border-radius:20px;
  font-size:9px;
}

.active {
  background:rgba(155,189,145,.13);
  color:var(--green);
}

.inactive {
  background:rgba(213,140,131,.12);
  color:var(--red);
}

.device-name {
  font-size:13px;
  font-weight:600;
}

.device-desc {
  min-height:18px;
  margin:3px 0 15px;
  color:var(--muted);
  font-size:10px;
}

.control {
  width:100%;
  padding:10px 5px;
  border:0;
  border-radius:9px;
  font-size:10px;
  font-weight:600;
  cursor:pointer;
}

.btn-on {
  background:var(--gold);
  color:#201d16;
}

.btn-off {
  background:#35302a;
  color:#e0a39a;
}

.master {
  display:grid;
  grid-template-columns:1fr 1fr;
  gap:10px;
  margin-top:23px;
}

.master button {
  padding:13px 5px;
  border:0;
  border-radius:10px;
  font-size:10px;
  font-weight:600;
  cursor:pointer;
}

.all-on {
  background:var(--gold);
  color:#201d16;
}

.all-off {
  background:var(--surface2);
  color:var(--text);
}

.network {
  display:flex;
  justify-content:space-between;
  align-items:center;
  margin-top:23px;
  padding:15px;
  background:var(--surface);
  border:1px solid var(--border);
  border-radius:12px;
}

.network-info {
  min-width:0;
}

.network-title {
  font-size:11px;
  font-weight:600;
}

.network-ssid {
  margin-top:3px;
  color:var(--muted);
  font-size:10px;
  overflow-wrap:anywhere;
}

.network a {
  color:var(--gold);
  text-decoration:none;
  font-size:10px;
  font-weight:600;
  white-space:nowrap;
}

.footer {
  margin-top:32px;
  padding-top:17px;
  border-top:1px solid var(--border);
  color:#77786f;
  font-size:10px;
  text-align:center;
}

button:disabled {
  opacity:.55;
}

@media(max-width:350px) {
  body {padding:18px 12px}
  .device {padding:12px}
  .grid {gap:8px}
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

  for (uint8_t i = 0; i < 4; i++) {
    html += tombol(
      deviceNames[i],
      deviceIcons[i],
      i
    );
  }

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

  <div class="network">

    <div class="network-info">
      <div class="network-title">Jaringan WiFi</div>
      <div class="network-ssid" id="wifiSSID">
        Memeriksa jaringan...
      </div>
    </div>

    <a href="/wifi">PENGATURAN →</a>

  </div>

  <div class="footer">
    NOVIA HOME · ESP32 SMART SYSTEM
  </div>

</div>

<script>

async function updateStatus() {

  try {

    const response = await fetch('/state');

    if (!response.ok) throw new Error();

    const data = await response.json();

    for (let i = 0; i < 4; i++) {

      const on = data.relay[i] === 1;

      const badge = document.getElementById('badge' + i);
      const desc = document.getElementById('desc' + i);
      const btn = document.getElementById('btn' + i);

      badge.textContent = on ? 'ON' : 'OFF';

      badge.classN
