
/*
========================================================
              NOVIA HOME V3
          HYBRID SMART HOME SYSTEM
========================================================

BOARD       : ESP32
CONTROLLER  : BLYNK IOT
WEB SERVER  : ESP32
ACCESS POINT: ALWAYS ON
WIFI        : MANUAL SCAN & CONNECT
STORAGE     : PREFERENCES
TIME        : NTP + INTERNAL ESP32 CLOCK
SCHEDULER   : 3 CHANNEL LIGHTS
TIMEZONE    : WIB UTC+7

========================================================
*/

// ======================================================
// BLYNK CONFIGURATION
// ======================================================

#define BLYNK_TEMPLATE_ID   "TMPL6Yk8Hycr-"
#define BLYNK_TEMPLATE_NAME "Novia Home"
#define BLYNK_AUTH_TOKEN    "uUhUdnlB22cqJfKinHQu8yGMXLRbgzf1"

#define BLYNK_PRINT Serial

// ======================================================
// LIBRARIES
// ======================================================

#include <WiFi.h>
#include <WebServer.h>
#include <BlynkSimpleEsp32.h>
#include <Preferences.h>
#include <time.h>

// ======================================================
// DEVICE CONFIGURATION
// ======================================================

#define RELAY_LUAR   26
#define RELAY_TENGAH 27
#define RELAY_KAMAR  14
#define RELAY_KIPAS  13

const uint8_t relayPins[4] = {
  RELAY_LUAR,
  RELAY_TENGAH,
  RELAY_KAMAR,
  RELAY_KIPAS
};

const char* AP_SSID = "NOVIA HOME SETUP";
const char* AP_PASS = "Noke0710";

const char* NTP_SERVER_1 = "pool.ntp.org";
const char* NTP_SERVER_2 = "time.google.com";
const char* NTP_SERVER_3 = "id.pool.ntp.org";

const long GMT_OFFSET = 7 * 3600;
const int DAYLIGHT_OFFSET = 0;

// ======================================================
// OBJECTS
// ======================================================

WebServer server(80);
Preferences preferences;

// ======================================================
// GLOBAL VARIABLES
// ======================================================

bool relayStatus[4] = {false, false, false, false};

String wifiSSID = "";
String wifiPassword = "";

bool timeValid = false;
bool ntpSynced = false;

unsigned long lastWiFiCheck = 0;
unsigned long lastBlynkCheck = 0;
unsigned long lastTimeCheck = 0;
unsigned long lastScheduleCheck = 0;

const unsigned long WIFI_INTERVAL = 15000;
const unsigned long BLYNK_INTERVAL = 10000;
const unsigned long TIME_INTERVAL = 5000;
const unsigned long SCHEDULE_INTERVAL = 1000;

// ======================================================
// SCHEDULER STRUCTURE
// ======================================================

// Scheduler hanya untuk 3 lampu.
// Channel 0 = Lampu Luar
// Channel 1 = Lampu Tengah
// Channel 2 = Lampu Kamar

struct LampSchedule {
  uint16_t onMinute;
  uint16_t offMinute;

  uint8_t dayMask;

  bool enabled;

  int32_t lastOnKey;
  int32_t lastOffKey;
};

LampSchedule schedules[3];

// ==========================================
// TIMER STRUCTURE
// ==========================================

struct TimerState {
  bool active = false;
  bool targetState = false;
  uint32_t startMillis = 0;
  uint32_t durationMs = 0;
};

TimerState timers[4];

// ======================================================
// STORAGE
// ======================================================

void loadSettings() {

  preferences.begin("novia", false);

  wifiSSID = preferences.getString("ssid", "");
  wifiPassword = preferences.getString("pass", "");

  for (int i = 0; i < 3; i++) {

    String key = "sch" + String(i);

    if (preferences.getBytesLength(key.c_str()) ==
        sizeof(LampSchedule)) {

      preferences.getBytes(
        key.c_str(),
        &schedules[i],
        sizeof(LampSchedule)
      );

    } else {

      schedules[i].onMinute = 18 * 60;
      schedules[i].offMinute = 23 * 60;
      schedules[i].dayMask = 127;
      schedules[i].enabled = false;

      schedules[i].lastOnKey = -1;
      schedules[i].lastOffKey = -1;
    }
  }

  preferences.end();

  Serial.println("Settings Loaded");
}

void saveWiFi() {

  preferences.begin("novia", false);

  preferences.putString("ssid", wifiSSID);
  preferences.putString("pass", wifiPassword);

  preferences.end();
}

void saveSchedule(uint8_t ch) {

  if (ch >= 3) return;

  preferences.begin("novia", false);

  String key = "sch" + String(ch);

  preferences.putBytes(
    key.c_str(),
    &schedules[ch],
    sizeof(LampSchedule)
  );

  preferences.end();
}

// ======================================================
// RELAY CONTROL
// ======================================================

void setRelay(uint8_t ch, bool state, bool syncBlynk) {

  if (ch >= 4) return;

  relayStatus[ch] = state;

  // Active LOW relay
  digitalWrite(
    relayPins[ch],
    state ? LOW : HIGH
  );

  if (syncBlynk && Blynk.connected()) {

    Blynk.virtualWrite(
      V0 + ch,
      state ? 1 : 0
    );
  }

  Serial.printf(
    "Relay %d : %s\n",
    ch,
    state ? "ON" : "OFF"
  );
}

void setAllRelay(bool state) {

  for (int i = 0; i < 4; i++) {
    setRelay(i, state, true);
  }
}

// ======================================================
// BLYNK
// ======================================================

BLYNK_WRITE(V0) {
  setRelay(0, param.asInt(), false);
}

BLYNK_WRITE(V1) {
  setRelay(1, param.asInt(), false);
}

BLYNK_WRITE(V2) {
  setRelay(2, param.asInt(), false);
}

BLYNK_WRITE(V3) {
  setRelay(3, param.asInt(), false);
}

BLYNK_CONNECTED() {

  for (int i = 0; i < 4; i++) {

    Blynk.virtualWrite(
      V0 + i,
      relayStatus[i] ? 1 : 0
    );
  }

  Serial.println("Blynk Connected");
}

// ======================================================
// WIFI ACCESS POINT + STATION
// ======================================================

void startAccessPoint() {

  WiFi.mode(WIFI_AP_STA);

  bool result = WiFi.softAP(
    AP_SSID,
    AP_PASS
  );

  if (result) {

    Serial.println("Access Point Started");

    Serial.print("AP IP: ");
    Serial.println(WiFi.softAPIP());
  }
}

void connectSavedWiFi() {

  if (wifiSSID.length() == 0) {

    Serial.println("WiFi not configured");
    return;
  }

  Serial.println("Connecting saved WiFi...");

  WiFi.begin(
    wifiSSID.c_str(),
    wifiPassword.c_str()
  );
}

// ======================================================
// WIFI MANUAL SCAN
// ======================================================

String scanWiFiJSON() {

  int count = WiFi.scanNetworks(false, true);

  String json = "{\"networks\":[";

  for (int i = 0; i < count; i++) {

    if (i > 0) json += ",";

    String ssid = WiFi.SSID(i);

    ssid.replace("\\", "\\\\");
    ssid.replace("\"", "\\\"");
    ssid.replace("\n", " ");

    json += "{\"ssid\":\"";
    json += ssid;
    json += "\",\"rssi\":";
    json += String(WiFi.RSSI(i));
    json += ",\"secure\":";
    json += WiFi.encryptionType(i) == WIFI_AUTH_OPEN
              ? "false"
              : "true";

    json += "}";
  }

  json += "]}";

  WiFi.scanDelete();

  return json;
}

// ======================================================
// TIME MANAGEMENT
// ======================================================

void setupTime() {

  configTime(
    GMT_OFFSET,
    DAYLIGHT_OFFSET,
    NTP_SERVER_1,
    NTP_SERVER_2,
    NTP_SERVER_3
  );

  Serial.println("NTP Configuration Started");
}

void checkTime() {

  time_t now = time(nullptr);

  // Epoch valid mulai tahun 2023
  if (now > 1700000000) {

    timeValid = true;

    if (WiFi.status() == WL_CONNECTED) {
      ntpSynced = true;
    }

  } else {

    timeValid = false;
  }
}

String getTimeString() {

  if (!timeValid) {
    return "WAKTU BELUM TERSINKRON";
  }

  struct tm timeInfo;

  if (!getLocalTime(&timeInfo, 50)) {
    return "WAKTU TIDAK TERSEDIA";
  }

  char buffer[40];

  strftime(
    buffer,
    sizeof(buffer),
    "%d-%m-%Y %H:%M:%S",
    &timeInfo
  );

  return String(buffer);
}

String getTimeSource() {

  if (!timeValid) {
    return "TIME NOT READY";
  }

  if (WiFi.status() == WL_CONNECTED && ntpSynced) {
    return "NTP ONLINE";
  }

  return "INTERNAL CLOCK / OFFLINE";
}

// ======================================================
// SCHEDULER
// ======================================================

int32_t makeScheduleKey(int dayOfYear, int minute) {

  struct tm timeInfo;

  if (!getLocalTime(&timeInfo, 50)) {
    return -1;
  }

  int32_t dayKey =
    (timeInfo.tm_year + 1900) * 1000 +
    dayOfYear;

  return dayKey * 1440 + minute;
}

bool isScheduleDay(uint8_t mask, uint8_t weekday) {

  return (mask & (1 << weekday)) != 0;
}

void processSchedule() {

  if (!timeValid) return;

  struct tm timeInfo;

  if (!getLocalTime(&timeInfo, 50)) return;

  uint16_t currentMinute =
    timeInfo.tm_hour * 60 +
    timeInfo.tm_min;

  int32_t currentKey = makeScheduleKey(
    timeInfo.tm_yday,
    currentMinute
  );

  if (currentKey < 0) return;

  for (uint8_t ch = 0; ch < 3; ch++) {

    LampSchedule &s = schedules[ch];

    if (!s.enabled) continue;

    if (!isScheduleDay(s.dayMask, timeInfo.tm_wday)) {
      continue;
    }

    // Automatic ON
    if (
      currentMinute == s.onMinute &&
      s.lastOnKey != currentKey
    ) {

      setRelay(ch, true, true);

      s.lastOnKey = currentKey;

      saveSchedule(ch);

      Serial.printf(
        "Schedule ON: Channel %d\n",
        ch
      );
    }

    // Automatic OFF
    if (
      currentMinute == s.offMinute &&
      s.lastOffKey != currentKey
    ) {

      setRelay(ch, false, true);

      s.lastOffKey = currentKey;

      saveSchedule(ch);

      Serial.printf(
        "Schedule OFF: Channel %d\n",
        ch
      );
    }
  }
}

// ======================================================
// HTML DASHBOARD
// ======================================================

String deviceCard(
  String name,
  String icon,
  uint8_t ch
) {

  String html = "<div class='card'>";

  html += "<div class='ico'>" + icon + "</div>";

  html += "<h3>" + name + "</h3>";

  html += "<div id='badge" + String(ch) + "'>";
  html += relayStatus[ch] ? "ON" : "OFF";
  html += "</div>";

  html += "<button onclick='toggle(";
  html += String(ch);
  html += ")'>UBAH STATUS</button>";

  html += "</div>";

  return html;
}

String dashboardHTML() {

  String html = R"rawliteral(
<!DOCTYPE html>
<html lang="id">
<head>

<meta charset="UTF-8">

<meta name="viewport"
content="width=device-width,initial-scale=1">

<meta name="theme-color" content="#151613">

<title>Novia Home V3</title>

<style>

:root {
--bg:#151613;
--card:#22231e;
--gold:#c5a875;
--text:#f2f0e9;
--muted:#92938a;
--green:#9bbd91;
--red:#d58c83;
}

* {
box-sizing:border-box;
}

body {
margin:0;
padding:20px 15px;
background:var(--bg);
color:var(--text);
font-family:Arial,sans-serif;
}

.container {
max-width:500px;
margin:auto;
}

header {
display:flex;
justify-content:space-between;
align-items:center;
margin-bottom:25px;
}

h1 {
font-size:19px;
margin:0;
}

small {
color:var(--muted);
}

.panel {
background:var(--card);
padding:17px;
border-radius:15px;
margin-bottom:15px;
}

.time {
font-size:23px;
color:var(--gold);
margin:10px 0;
}

.grid {
display:grid;
grid-template-columns:1fr 1fr;
gap:12px;
}

.card {
background:var(--card);
border-radius:15px;
padding:16px;
}

.ico {
font-size:26px;
color:var(--gold);
}

.card h3 {
font-size:13px;
}

button {
width:100%;
padding:11px;
border:0;
border-radius:9px;
background:var(--gold);
color:#201d16;
font-weight:bold;
margin-top:10px;
cursor:pointer;
}

button.secondary {
background:#35352f;
color:white;
}

input,select {
width:100%;
padding:12px;
border-radius:8px;
border:1px solid #414139;
background:#181914;
color:white;
margin:6px 0 12px;
}

label {
font-size:12px;
color:var(--muted);
}

.days {
display:grid;
grid-template-columns:repeat(4,1fr);
gap:8px;
}

.days label {
background:#303129;
padding:8px;
border-radius:7px;
text-align:center;
}

.days input {
width:auto;
margin:0 4px 0 0;
}

h2 {
font-size:14px;
margin-top:0;
}

.status {
color:var(--green);
font-size:12px;
}

.footer {
text-align:center;
color:var(--muted);
font-size:10px;
padding:20px;
}

</style>
</head>

<body>

<div class="container">

<header>
<div>
<h1>⌂ NOVIA HOME</h1>
<small>HYBRID SMART HOME V3</small>
</div>
<div id="connection">MEMERIKSA</div>
</header>

<div class="panel">

<small>WAKTU REAL TIME · WIB</small>

<div class="time" id="clock">--:--:--</div>

<div id="date">Memuat tanggal...</div>

<p class="status" id="timeSource">
Memeriksa waktu...
</p>

</div>

<div class="panel">

<h2>STATUS JARINGAN</h2>

<div id="wifiStatus">WiFi: --</div>
<div id="blynkStatus">Blynk: --</div>
<div id="apStatus">Access Point: --</div>
<div id="ipStatus">IP: --</div>

</div>

<h2>PERANGKAT RUMAH</h2>

<div class="grid">

)rawliteral";

  html += deviceCard("Lampu Luar", "💡", 0);
  html += deviceCard("Lampu Tengah", "✦", 1);
  html += deviceCard("Lampu Kamar", "⌂", 2);
  html += deviceCard("Kipas Angin", "❋", 3);

  html += R"rawliteral(
</div>

<div class="panel" style="margin-top:15px">

<h2>KONTROL UTAMA</h2>

<div class="grid">

<button onclick="allRelay('on')">
NYALAKAN SEMUA
</button>

<button class="secondary"
onclick="allRelay('off')">
MATIKAN SEMUA
</button>

</div>

</div>

<div class="panel">

<h2>WIFI MANAGER</h2>

<button onclick="scanWiFi()">
SCAN WIFI SEKARANG
</button>

<label>PILIH JARINGAN WIFI</label>

<select id="ssid">
<option value="">Scan jaringan dahulu</option>
</select>

<label>Password WiFi</label>

<input id="password"
type="password"
placeholder="Masukkan password">

<button onclick="connectWiFi()">
SIMPAN & HUBUNGKAN
</button>

<div id="wifiMessage"></div>

</div>

<div class="panel">

<h2>JADWAL OTOMATIS LAMPU</h2>

<label>Pilih Lampu</label>

<select id="schChannel"
onchange="loadSchedule()">

<option value="0">Lampu Luar</option>
<option value="1">Lampu Tengah</option>
<option value="2">Lampu Kamar</option>

</select>

<label>Jam Menyala</label>

<input type="time" id="onTime"
value="18:00">

<label>Jam Mati</label>

<input type="time" id="offTime"
value="23:00">

<label>Hari Aktif</label>

<div class="days">

<label><input type="checkbox" value="1">Sen</label>
<label><input type="checkbox" value="2">Sel</label>
<label><input type="checkbox" value="3">Rab</label>
<label><input type="checkbox" value="4">Kam</label>
<label><input type="checkbox" value="5">Jum</label>
<label><input type="checkbox" value="6">Sab</label>
<label><input type="checkbox" value="0">Min</label>

</div>

<br>

<label>
<input type="checkbox" id="schEnabled">
Aktifkan jadwal lampu
</label>

<button onclick="saveSchedule()">
SIMPAN JADWAL
</button>

<div id="schMessage"></div>

</div>

<div class="panel">

<h2>INFORMASI SISTEM</h2>

<div>Access Point: NOVIA HOME SETUP</div>
<div>Password: NoviaHome123</div>
<div>AP Address: 192.168.4.1</div>

</div>

<div class="footer">
NOVIA HOME V3 · ESP32 HYBRID SYSTEM
</div>

</div>

<script>

// ========================================
// DEVICE CONTROL
// ========================================

async function toggle(ch) {

await fetch('/toggle?ch='+ch);

updateStatus();

}

async function allRelay(state) {

await fetch('/all-'+state);

updateStatus();

}

// ========================================
// STATUS UPDATE
// ========================================

async function updateStatus() {

try {

const r = await fetch('/state');
const d = await r.json();

for(let i=0;i<4;i++) {

document.getElementById('badge'+i)
.textContent = d.relay[i] ? 'ON' : 'OFF';

}

document.getElementById('connection')
.textContent = d.blynk ? 'BLYNK ONLINE' :
d.wifi ? 'WIFI LOKAL' : 'OFFLINE';

document.getElementById('wifiStatus')
.textContent = 'WiFi: '+(d.wifi ? 'CONNECTED' : 'DISCONNECTED');

document.getElementById('blynkStatus')
.textContent = 'Blynk: '+(d.blynk ? 'CONNECTED' : 'OFFLINE');

document.getElementById('apStatus')
.textContent = 'AP: '+(d.ap ? 'ACTIVE' : 'INACTIVE');

document.getElementById('ipStatus')
.textContent = 'IP: '+d.ip;

document.getElementById('clock')
.textContent = d.time;

document.getElementById('date')
.textContent = d.date;

document.getElementById('timeSource')
.textContent = d.timeSource;

} catch(e) {

document.getElementById('connection')
.textContent = 'SERVER TERPUTUS';

}

}

// ========================================
// WIFI SCAN
// ========================================

async function scanWiFi() {

const select = document.getElementById('ssid');

select.innerHTML =
'<option>Memindai jaringan...</option>';

try {

const r = await fetch('/scan');
const d = await r.json();

select.innerHTML = '';

d.networks.forEach(n => {

let opt = document.createElement('option');

opt.value = n.ssid;

opt.textContent =
n.ssid+' ('+n.rssi+' dBm)'+
(n.secure ? ' 🔒' : ' · Terbuka');

select.appendChild(opt);

});

if(d.networks.length === 0) {

select.innerHTML =
'<option>Tidak ada jaringan ditemukan</option>';

}

} catch(e) {

select.innerHTML =
'<option>Scan gagal</option>';

}

}

// ========================================
// WIFI CONNECT
// ========================================

async function connectWiFi() {

const ssid = document.getElementById('ssid').value;
const password = document.getElementById('password').value;

if(!ssid) {

alert('Pilih jaringan WiFi dahulu');
return;

}

const body = new URLSearchParams();

body.append('ssid',ssid);
body.append('pass',password);

const r = await fetch('/wifi-save',{
method:'POST',
headers:{
'Content-Type':'application/x-www-form-urlencoded'
},
body:body
});

document.getElementById('wifiMessage')
.textContent = await r.text();

}

// ========================================
// SCHEDULER
// ========================================

async function loadSchedule() {

const ch = document.getElementById('schChannel').value;

const r = await fetch('/schedule?ch='+ch);
const d = await r.json();

document.getElementById('onTime').value = d.on;
document.getElementById('offTime').value = d.off;
document.getElementById('schEnabled').checked = d.enabled;

document.querySelectorAll('.days input')
.forEach(el => {

el.checked = (d.days & (1 << Number(el.value))) !== 0;

});

}

async function saveSchedule() {

let days = 0;

document.querySelectorAll('.days input:checked')
.forEach(el => {

days |= 1 << Number(el.value);

});

const body = new URLSearchParams();

body.append('ch',document.getElementById('schChannel').value);
body.append('on',document.getElementById('onTime').value);
body.append('off',document.getElementById('offTime').value);
body.append('days',days);
body.append('enabled',document.getElementById('schEnabled').checked ? 1 : 0);

const r = await fetch('/schedule-save',{
method:'POST',
headers:{
'Content-Type':'application/x-www-form-urlencoded'
},
body:body
});

document.getElementById('schMessage')
.textContent = await r.text();

}

updateStatus();

loadSchedule();

setInterval(updateStatus,2000);

</script>

</body>
</html>
)rawliteral";

  return html;
}

// ======================================================
// WEB ROUTES
// ======================================================

void setupRoutes() {

  // DASHBOARD
  server.on("/", HTTP_GET, []() {

    server.send(
      200,
      "text/html; charset=utf-8",
      dashboardHTML()
    );

  });

  // RELAY TOGGLE
  server.on("/toggle", HTTP_GET, []() {

    if (server.hasArg("ch")) {

      int ch = server.arg("ch").toInt();

      if (ch >= 0 && ch <= 3) {

        setRelay(
          ch,
          !relayStatus[ch],
          true
        );
      }
    }

    server.send(200, "text/plain", "OK");

  });

  // ALL ON
  server.on("/all-on", HTTP_GET, []() {

    setAllRelay(true);

    server.send(200, "text/plain", "OK");

  });

  // ALL OFF
  server.on("/all-off", HTTP_GET, []() {

    setAllRelay(false);

    server.send(200, "text/plain", "OK");

  });

  // WIFI SCAN
  server.on("/scan", HTTP_GET, []() {

    server.send(
      200,
      "application/json",
      scanWiFiJSON()
    );

  });

  // WIFI SAVE
    // WIFI SAVE
  server.on("/wifi-save", HTTP_POST, []() {

    String newSSID = server.arg("ssid");
    String newPassword = server.arg("password");

    newSSID.trim();

    if (newSSID.length() == 0) {
      server.send(400, "text/plain", "SSID tidak boleh kosong");
      return;
    }

    preferences.begin("wifi", false);
    preferences.putString("ssid", newSSID);
    preferences.putString("password", newPassword);
    preferences.end();

    server.send(
      200,
      "text/plain",
      "Konfigurasi WiFi disimpan. ESP32 akan restart..."
    );

    delay(1000);
    ESP.restart();
  });


  // WIFI SCAN
  server.on("/scan", HTTP_GET, []() {

    int networks = WiFi.scanNetworks();

    String json = "[";

    for (int i = 0; i < networks; i++) {

      if (i > 0) json += ",";

      String ssid = WiFi.SSID(i);
      ssid.replace("\\", "\\\\");
      ssid.replace("\"", "\\\"");
      ssid.replace("\n", "");

      json += "{";
      json += "\"ssid\":\"" + ssid + "\",";
      json += "\"rssi\":" + String(WiFi.RSSI(i)) + ",";
      json += "\"secure\":" + String(
        WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "false" : "true"
      );
      json += "}";
    }

    json += "]";

    WiFi.scanDelete();

    server.send(200, "application/json", json);
  });


  // RELAY TOGGLE
  server.on("/toggle", HTTP_GET, []() {

    if (!server.hasArg("ch")) {
      server.send(400, "text/plain", "Channel tidak ditemukan");
      return;
    }

    int ch = server.arg("ch").toInt();

    if (ch < 0 || ch >= 4) {
      server.send(400, "text/plain", "Channel tidak valid");
      return;
    }

    bool newState = !relayStatus[ch];

    // Manual control: membatalkan timer perangkat
    timers[ch].active = false;

    setRelay(ch, newState, true);

    server.send(200, "text/plain", "OK");
  });


  // ALL ON
  server.on("/all-on", HTTP_GET, []() {

    for (int i = 0; i < 4; i++) {

      timers[i].active = false;

      setRelay(i, true, true);
    }

    server.send(200, "text/plain", "Semua perangkat ON");
  });


  // ALL OFF
  server.on("/all-off", HTTP_GET, []() {

    for (int i = 0; i < 4; i++) {

      timers[i].active = false;

      setRelay(i, false, true);
    }

    server.send(200, "text/plain", "Semua perangkat OFF");
  });


  // TIMER START
  server.on("/timer-start", HTTP_POST, []() {

    if (
      !server.hasArg("ch") ||
      !server.hasArg("action") ||
      !server.hasArg("hours") ||
      !server.hasArg("minutes")
    ) {
      server.send(400, "text/plain", "Data timer tidak lengkap");
      return;
    }

    int ch = server.arg("ch").toInt();
    int hours = server.arg("hours").toInt();
    int minutes = server.arg("minutes").toInt();

    String action = server.arg("action");

    if (ch < 0 || ch >= 4) {
      server.send(400, "text/plain", "Channel tidak valid");
      return;
    }

    if (
      hours < 0 ||
      hours > 24 ||
      minutes < 0 ||
      minutes > 59 ||
      (hours == 0 && minutes == 0)
    ) {
      server.send(400, "text/plain", "Durasi timer tidak valid");
      return;
    }

    if (action != "on" && action != "off") {
      server.send(400, "text/plain", "Aksi timer tidak valid");
      return;
    }

    uint32_t duration =
      ((uint32_t)hours * 3600UL + (uint32_t)minutes * 60UL) * 1000UL;

    timers[ch].active = true;
    timers[ch].targetState = (action == "on");
    timers[ch].startMillis = millis();
    timers[ch].durationMs = duration;

    server.send(200, "text/plain", "Timer berhasil dimulai");
  });


  // TIMER CANCEL
  server.on("/timer-cancel", HTTP_POST, []() {

    if (!server.hasArg("ch")) {
      server.send(400, "text/plain", "Channel tidak ditemukan");
      return;
    }

    int ch = server.arg("ch").toInt();

    if (ch < 0 || ch >= 4) {
      server.send(400, "text/plain", "Channel tidak valid");
      return;
    }

    timers[ch].active = false;

    server.send(200, "text/plain", "Timer dibatalkan");
  });


  // DEVICE STATE
  server.on("/state", HTTP_GET, []() {

    String json = "{";

    json += "\"wifiConnected\":";
    json += (WiFi.status() == WL_CONNECTED ? "true" : "false");

    json += ",\"blynkConnected\":";
    json += (Blynk.connected() ? "true" : "false");

    json += ",\"apActive\":";
    json += (WiFi.getMode() == WIFI_AP ||
             WiFi.getMode() == WIFI_AP_STA ? "true" : "false");

    json += ",\"ip\":\"";
    json += (WiFi.status() == WL_CONNECTED
             ? WiFi.localIP().toString()
             : WiFi.softAPIP().toString());
    json += "\"";

    json += ",\"relays\":[";

    for (int i = 0; i < 4; i++) {

      if (i > 0) json += ",";

      json += (relayStatus[i] ? "true" : "false");
    }

    json += "]";

    json += ",\"timers\":[";

    for (int i = 0; i < 4; i++) {

      if (i > 0) json += ",";

      uint32_t remaining = 0;

      if (timers[i].active) {

        uint32_t elapsed = millis() - timers[i].startMillis;

        if (elapsed < timers[i].durationMs) {
          remaining = timers[i].durationMs - elapsed;
        }
      }

      json += "{";
      json += "\"active\":";
      json += (timers[i].active ? "true" : "false");

      json += ",\"target\":";
      json += (timers[i].targetState ? "true" : "false");

      json += ",\"remaining\":";
      json += String(remaining / 1000UL);

      json += "}";
    }

    json += "]";

    time_t now = time(nullptr);

    json += ",\"ntpValid\":";
    json += (now > 1700000000 ? "true" : "false");

    json += ",\"time\":\"";

    if (now > 1700000000) {

      struct tm timeinfo;

      if (getLocalTime(&timeinfo, 10)) {

        char timeBuffer[30];

        strftime(
          timeBuffer,
          sizeof(timeBuffer),
          "%Y-%m-%d %H:%M:%S",
          &timeinfo
        );

        json += timeBuffer;
      }
    }

    json += "\"";
    json += "}";

    server.send(200, "application/json", json);
  });


  // DASHBOARD ROOT
  server.on("/", HTTP_GET, []() {

    server.send(
      200,
      "text/html",
      R"rawliteral(
<!DOCTYPE html>
<html lang="id">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<meta name="theme-color" content="#101827">
<title>NOVIA HOME V3</title>

<style>
*{box-sizing:border-box}
body{
  margin:0;
  padding:18px;
  background:#101827;
  color:#f5f7fb;
  font-family:Arial,sans-serif
}
header{
  margin-bottom:20px
}
h1{
  margin:0;
  color:#7ecbff;
  font-size:25px
}
.sub{
  color:#9ba9bb;
  font-size:13px;
  margin-top:6px
}
.info,.card{
  background:#1b293b;
  border:1px solid #304258;
  border-radius:15px;
  padding:16px;
  margin-bottom:14px
}
.grid{
  display:grid;
  grid-template-columns:1fr 1fr;
  gap:12px
}
.device{
  background:#24364c;
  border-radius:12px;
  padding:15px;
  text-align:center
}
.device h3{
  font-size:14px;
  margin:0 0 12px
}
.status{
  font-size:12px;
  color:#aebdd0;
  margin-bottom:12px
}
button{
  border:0;
  border-radius:9px;
  padding:11px 14px;
  font-weight:bold;
  cursor:pointer;
  color:#fff;
  background:#247bb6
}
.off{background:#596779}
.danger{background:#c34c4c}
.success{background:#218c68}
input,select{
  width:100%;
  padding:11px;
  margin:6px 0 12px;
  border:1px solid #40546c;
  border-radius:8px;
  background:#101827;
  color:white
}
label{
  font-size:12px;
  color:#b8c7d8
}
.row{
  display:flex;
  gap:8px
}
.row>*{flex:1}
.timer-status{
  color:#7ecbff;
  font-size:12px;
  margin-top:9px
}
small{color:#9ba9bb}
@media(max-width:360px){
  .grid{grid-template-columns:1fr}
}
</style>
</head>

<body>

<header>
  <h1>NOVIA HOME</h1>
  <div class="sub">SMART HOME CONTROL SYSTEM V3</div>
</header>

<div class="info">
  <div>WiFi: <b id="wifi">Memeriksa...</b></div>
  <div>IP: <b id="ip">-</b></div>
  <div>Blynk: <b id="blynk">-</b></div>
  <div>Waktu NTP: <b id="clock">Belum tersinkron</b></div>
</div>

<div class="grid">

  <div class="device">
    <h3>Lampu Luar</h3>
    <div class="status" id="status0">-</div>
    <button id="btn0" onclick="toggleRelay(0)">...</button>
  </div>

  <div class="device">
    <h3>Lampu Tengah</h3>
    <div class="status" id="status1">-</div>
    <button id="btn1" onclick="toggleRelay(1)">...</button>
  </div>

  <div class="device">
    <h3>Lampu Kamar</h3>
    <div class="status" id="status2">-</div>
    <button id="btn2" onclick="toggleRelay(2)">...</button>
  </div>

  <div class="device">
    <h3>Kipas Angin</h3>
    <div class="status" id="status3">-</div>
    <button id="btn3" onclick="toggleRelay(3)">...</button>
  </div>

</div>

<div class="card">
  <h3>Kontrol Semua Perangkat</h3>
  <div class="row">
    <button class="success" onclick="allOn()">SEMUA ON</button>
    <button class="danger" onclick="allOff()">SEMUA OFF</button>
  </div>
</div>

<div class="card">
  <h3>Timer Otomatis</h3>

  <label>Perangkat</label>
  <select id="timerDevice">
    <option value="0">Lampu Luar</option>
    <option value="1">Lampu Tengah</option>
    <option value="2">Lampu Kamar</option>
    <option value="3">Kipas Angin</option>
  </select>

  <label>Aksi setelah waktu habis</label>
  <select id="timerAction">
    <option value="off">MATIKAN</option>
    <option value="on">NYALAKAN</option>
  </select>

  <div class="row">
    <div>
      <label>Jam</label>
      <input id="hours" type="number" min="0" max="24" value="1">
    </div>
    <div>
      <label>Menit</label>
      <input id="minutes" type="number" min="0" max="59" value="30">
    </div>
  </div>

  <button class="success" style="width:100%" onclick="startTimer()">
    MULAI TIMER
  </button>

  <p class="timer-status" id="timerInfo">Belum ada timer</p>

  <button class="off" style="width:100%" onclick="cancelTimer()">
    BATALKAN TIMER
  </button>
</div>

<div class="card">
  <h3>Pengaturan WiFi</h3>

  <button onclick="scanWifi()">SCAN WIFI</button>

  <label>Jaringan WiFi</label>
  <select id="ssid"></select>

  <label>Password WiFi</label>
  <input id="password" type="password" placeholder="Password WiFi">

  <button class="success" style="width:100%" onclick="saveWifi()">
    SIMPAN WIFI
  </button>

  <small>ESP32 akan restart setelah pengaturan WiFi disimpan.</small>
</div>

<script>
let latestState = null;

async function request(url, options={}) {
  const response = await fetch(url, options);

  if (!response.ok) {
    throw new Error(await response.text());
  }

  return response;
}

async function toggleRelay(ch) {
  try {
    await request('/toggle?ch='+ch);
    updateState();
  } catch(e) {
    alert(e.message);
  }
}

async function allOn() {
  await request('/all-on');
  updateState();
}

async function allOff() {
  await request('/all-off');
  updateState();
}

async function startTimer() {
  const data = new URLSearchParams();

  data.append('ch', document.getElementById('timerDevice').value);
  data.append('action', document.getElementById('timerAction').value);
  data.append('hours', document.getElementById('hours').value);
  data.append('minutes', document.getElementById('minutes').value);

  try {
    await request('/timer-start', {
      method:'POST',
      headers:{'Content-Type':'application/x-www-form-urlencoded'},
      body:data
    });

    alert('Timer berhasil dimulai');
    updateState();
  } catch(e) {
    alert(e.message);
  }
}

async function cancelTimer() {
  const data = new URLSearchParams();

  data.append('ch', document.getElementById('timerDevice').value);

  try {
    await request('/timer-cancel', {
      method:'POST',
      headers:{'Content-Type':'application/x-www-form-urlencoded'},
      body:data
    });

    updateState();
  } catch(e) {
    alert(e.message);
  }
}

async function scanWifi() {
  const select = document.getElementById('ssid');

  select.innerHTML = '<option>Memindai jaringan...</option>';

  try {
    const response = await request('/scan');
    const networks = await response.json();

    select.innerHTML = '';

    networks.forEach(network => {
      const option = document.createElement('option');

      option.value = network.ssid;
      option.textContent =
        network.ssid + ' (' + network.rssi + ' dBm)' +
        (network.secure ? ' 🔒' : '');

      select.appendChild(option);
    });

    if (!networks.length) {
      select.innerHTML = '<option value="">Tidak ada jaringan</option>';
    }

  } catch(e) {
    select.innerHTML = '<option>Gagal scan WiFi</option>';
  }
}

async function saveWifi() {
  const data = new URLSearchParams();

  data.append('ssid', document.getElementById('ssid').value);
  data.append('password', document.getElementById('password').value);

  try {
    await request('/wifi-save', {
      method:'POST',
      headers:{'Content-Type':'application/x-www-form-urlencoded'},
      body:data
    });

    alert('Pengaturan WiFi disimpan. ESP32 restart.');
  } catch(e) {
    alert(e.message);
  }
}

async function updateState() {
  try {
    const response = await request('/state');
    const state = await response.json();

    latestState = state;

    document.getElementById('wifi').textContent =
      state.wifiConnected ? 'Terhubung' : 'Tidak terhubung';

    document.getElementById('ip').textContent = state.ip;

    document.getElementById('blynk').textContent =
      state.blynkConnected ? 'Terhubung' : 'Terputus';

    document.getElementById('clock').textContent =
      state.ntpValid ? state.time + ' WIB' : 'Belum tersinkron';

    state.relays.forEach((on, i) => {
      document.getElementById('status'+i).textContent =
        on ? 'MENYALA' : 'MATI';

      const button = document.getElementById('btn'+i);

      button.textContent = on ? 'MATIKAN' : 'NYALAKAN';
      button.className = on ? 'danger' : 'success';
    });

    const activeTimers = state.timers
      .map((timer, i) => {
        if (!timer.active) return null;

        const h = Math.floor(timer.remaining / 3600);
        const m = Math.floor((timer.remaining % 3600) / 60);
        const s = timer.remaining % 60;

        const names = [
          'Lampu Luar',
          'Lampu Tengah',
          'Lampu Kamar',
          'Kipas Angin'
        ];

        return names[i] + ': ' +
          h + 'j ' + m + 'm ' + s + 'd → ' +
          (timer.target ? 'ON' : 'OFF');
      })
      .filter(Boolean);

    document.getElementById('timerInfo').textContent =
      activeTimers.length ? activeTimers.join(' | ') : 'Belum ada timer aktif';

  } catch(e) {
    console.log(e);
  }
}

scanWifi();
updateState();

setInterval(updateState, 1000);
</script>

</body>
</html>
)rawliteral"
    );
  });


  // NOT FOUND
  server.onNotFound([]() {
    server.send(404, "text/plain", "NOVIA HOME: Halaman tidak ditemukan");
  });


  // START SERVER
  server.begin();

  Serial.println("Web server started");
  Serial.println("NOVIA HOME V3 READY");
}


// ==========================================
// TIMER PROCESS
// ==========================================

void handleTimers() {

  uint32_t now = millis();

  for (int i = 0; i < 4; i++) {

    if (!timers[i].active) {
      continue;
    }

    uint32_t elapsed = now - timers[i].startMillis;

    if (elapsed >= timers[i].durationMs) {

      bool target = timers[i].targetState;

      timers[i].active = false;

      setRelay(i, target, true);

      Serial.print("Timer selesai pada channel ");
      Serial.println(i);
    }
  }
}


// ==========================================
// WIFI CONNECTION MONITOR
// ==========================================

void handleWiFi() {

  static unsigned long lastCheck = 0;

  if (millis() - lastCheck < 10000) {
    return;
  }

  lastCheck = millis();

  if (WiFi.status() != WL_CONNECTED) {

    Serial.println("WiFi disconnected");

    if (WiFi.SSID().length() > 0) {
  WiFi.reconnect();
    }
  }
}


// ==========================================
// BLYNK CONNECTION MONITOR
// ==========================================

void handleBlynk() {

  static unsigned long lastAttempt = 0;

  if (WiFi.status() != WL_CONNECTED) {
    return;
  }

  if (Blynk.connected()) {
    return;
  }

  if (millis() - lastAttempt < 10000) {
    return;
  }

  lastAttempt = millis();

  Serial.println("Connecting to Blynk...");

  Blynk.connect(1000);
}


// ==========================================
// NTP CLOCK
// ==========================================

void handleNTP() {

  static bool ntpConfigured = false;

  if (WiFi.status() != WL_CONNECTED) {
    return;
  }

  if (!ntpConfigured) {

    configTzTime(
      "WIB-7",
      "pool.ntp.org",
      "time.google.com",
      "id.pool.ntp.org"
    );

    ntpConfigured = true;

    Serial.println("NTP synchronization started");
  }
}



// ==========================================
// MAIN SETUP
// ==========================================

void setup() {

  Serial.begin(115200);
  delay(500);

  Serial.println();
  Serial.println("================================");
  Serial.println("       NOVIA HOME V3");
  Serial.println("       SYSTEM STARTING");
  Serial.println("================================");

  // ------------------------------------------
  // INITIALIZE RELAYS
  // ------------------------------------------

  for (int i = 0; i < 4; i++) {

    pinMode(relayPins[i], OUTPUT);

    // Active LOW: HIGH = OFF
    digitalWrite(relayPins[i], HIGH);

    relayStatus[i] = false;
  }

  Serial.println("Relay initialized");

  // ------------------------------------------
  // LOAD SAVED SETTINGS
  // ------------------------------------------

  loadSettings();

  // ------------------------------------------
  // WIFI CONNECTION
  // ------------------------------------------

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);

  if (wifiSSID.length() > 0) {

    connectSavedWiFi();

    Serial.println("Connecting saved WiFi...");

    unsigned long startAttempt = millis();

    while (
      WiFi.status() != WL_CONNECTED &&
      millis() - startAttempt < 20000
    ) {
      delay(250);
      yield();
    }
  }

  // ------------------------------------------
  // WIFI RESULT
  // ------------------------------------------

  if (WiFi.status() == WL_CONNECTED) {

    Serial.println("WiFi Connected");

    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());

    // AP dimatikan setelah STA berhasil
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);

    // NTP
    setupTime();

  } else {

    Serial.println("WiFi unavailable");
    Serial.println("Starting configuration AP");

    // AP hanya untuk konfigurasi / pemulihan
    startAccessPoint();
  }

  // ------------------------------------------
  // BLYNK CONFIGURATION
  // ------------------------------------------

  Blynk.config(BLYNK_AUTH_TOKEN);

  if (WiFi.status() == WL_CONNECTED) {
    Blynk.connect(1000);
  }

  // ------------------------------------------
  // WEB SERVER
  // ------------------------------------------

  setupRoutes();

  Serial.println("Web server initialized");

  // ------------------------------------------
  // SYSTEM READY
  // ------------------------------------------

  Serial.println("================================");
  Serial.println("       NOVIA HOME READY");
  Serial.println("================================");
}

// ==========================================
// MAIN LOOP
// ==========================================

void loop() {

  server.handleClient();

  handleTimers();

  handleWiFi();

  handleNTP();

  if (WiFi.status() == WL_CONNECTED) {
    Blynk.run();
    handleBlynk();
  }

  yield();
}