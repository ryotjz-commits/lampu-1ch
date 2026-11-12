/*
  ==========================================================
  NOVIA HOME 4CH - ESP32 DEVKIT V1
  Local Web Dashboard + Setup AP + WiFi Scanner + Blynk IoT

  Channel / Blynk mapping:
    V0 = Lampu Depan
    V1 = Lampu Tengah
    V2 = Lampu Kamar
    V3 = Lampu Belakang

  SAFETY:
  This firmware is a logic/UI integration test. It does not
  configure or drive relay GPIO pins. Do not use this demo to
  switch mains-voltage lamps. Mains electrical work must be
  handled and checked by a qualified electrician.
  ==========================================================
*/

#define BLYNK_TEMPLATE_ID   "TMPL6Yk8Hycr-"
#define BLYNK_TEMPLATE_NAME "Novia Home"
#define BLYNK_AUTH_TOKEN    "uUhUdnlB22cqJfKinHQu8yGMXLRbgzf1"

#include <WiFi.h>
#include <WebServer.h>
#include <BlynkSimpleEsp32.h>

// ---------------- USER CONFIG ----------------
const char WIFI_SSID[] = "Tenda";
const char WIFI_PASS[] = "Noke0710";

const char AP_SSID[] = "NOVIA-HOME-SETUP";
const char AP_PASS[] = "Noke0710";  // minimal 8 karakter

// ---------------- OBJECTS ----------------
WebServer server(80);
BlynkTimer blynkTimer;

// ---------------- CHANNELS ----------------
const char* CHANNEL_NAMES[4] = {
  "Lampu Depan",
  "Lampu Tengah",
  "Lampu Kamar",
  "Lampu Belakang"
};

// Logical states only: no GPIO/relay output is driven.
bool channelState[4] = {false, false, false, false};

unsigned long lastWiFiRetry = 0;
unsigned long lastBlynkRetry = 0;
const unsigned long WIFI_RETRY_MS = 15000;
const unsigned long BLYNK_RETRY_MS = 10000;

bool scanRunning = false;
unsigned long scanStartedAt = 0;

// ---------------- HELPERS ----------------
String jsonEscape(const String& input) {
  String out;
  out.reserve(input.length() + 8);
  for (size_t i = 0; i < input.length(); i++) {
    char c = input[i];
    if (c == '\"' || c == '\\') {
      out += '\\';
      out += c;
    } else if (c == '\n') {
      out += "\\n";
    } else if (c == '\r') {
      out += "\\r";
    } else if ((uint8_t)c >= 0x20) {
      out += c;
    }
  }
  return out;
}

String connectionMode() {
  wifi_mode_t mode = WiFi.getMode();
  if (mode == WIFI_AP) return "AP";
  if (mode == WIFI_STA) return "STA";
  if (mode == WIFI_AP_STA) return "AP + STA";
  return "OFF";
}

String currentIp() {
  if (WiFi.status() == WL_CONNECTED) return WiFi.localIP().toString();
  return WiFi.softAPIP().toString();
}

void publishChannelToBlynk(uint8_t index) {
  if (index > 3 || !Blynk.connected()) return;
  Blynk.virtualWrite(V0 + index, channelState[index] ? 1 : 0);
}

void setChannel(uint8_t index, bool state, bool updateBlynk) {
  if (index > 3) return;
  channelState[index] = state;

  Serial.printf("[STATE] %s = %s (logical demo only)\n",
                CHANNEL_NAMES[index], state ? "ON" : "OFF");

  if (updateBlynk) publishChannelToBlynk(index);
}

// ---------------- WEB PAGE ----------------
const char INDEX_HTML[] PROGMEM = R"HTML(
<!doctype html>
<html lang="id">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>NOVIA HOME</title>
<style>
:root{color-scheme:dark}
*{box-sizing:border-box}
body{margin:0;padding:18px;background:#101827;color:#edf3ff;font-family:Arial,sans-serif}
main{max-width:720px;margin:auto}
h1{margin:4px 0;font-size:27px;letter-spacing:1px}
.muted{color:#aab8cc;font-size:13px;line-height:1.55}
.panel{background:#1b293b;border:1px solid #34465e;border-radius:15px;padding:16px;margin:14px 0}
.grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:12px}
.channel{background:#23344a;border:1px solid #3a506b;border-radius:13px;padding:15px}
.channel h3{font-size:16px;margin:0 0 8px}
.state{font-size:12px;color:#b7c5d8;margin-bottom:13px}
button{border:0;border-radius:9px;padding:11px 14px;background:#536985;color:white;font-weight:bold;cursor:pointer}
button.active{background:#159765}
button:disabled{opacity:.55}
.row{display:flex;justify-content:space-between;align-items:center;gap:10px;flex-wrap:wrap}
#scanList{margin-top:12px;overflow-wrap:anywhere}
.netrow{padding:7px 0;border-bottom:1px solid #34465e}
@media(max-width:440px){.grid{grid-template-columns:1fr}}
</style>
</head>
<body>
<main>
  <h1>NOVIA HOME</h1>
  <div class="muted">4 CHANNEL CONTROL · LOCAL WEB · BLYNK IoT</div>

  <section class="panel">
    <div class="row"><b>Status perangkat</b><button onclick="refresh()">REFRESH</button></div>
    <div class="muted" id="status">Memuat...</div>
  </section>

  <section class="panel">
    <b>Kontrol ruangan</b>
    <div class="grid" id="channels" style="margin-top:13px"></div>
  </section>

  <section class="panel">
    <div class="row">
      <b>Pemindai WiFi</b>
      <button id="scanBtn" onclick="startScan()">SCAN WIFI</button>
    </div>
    <div class="muted">Menampilkan nama jaringan dan kekuatan sinyal yang terdeteksi ESP32.</div>
    <div id="scanList" class="muted"></div>
  </section>

  <div class="muted">Mode pengujian perangkat lunak: status tombol belum mengendalikan lampu fisik atau GPIO relay.</div>
</main>
<script>
const channelNames=['Lampu Depan','Lampu Tengah','Lampu Kamar','Lampu Belakang'];

async function api(url){
  const response=await fetch(url,{cache:'no-store'});
  if(!response.ok) throw new Error('HTTP '+response.status);
  return response;
}

async function refresh(){
  try{
    const response=await api('/api/status');
    const data=await response.json();
    document.getElementById('status').innerHTML=
      'Mode WiFi: '+data.mode+
      '<br>WiFi rumah: '+data.wifi+
      '<br>Alamat dashboard: '+data.ip+
      '<br>Blynk IoT: '+data.blynk;

    const root=document.getElementById('channels');
    root.replaceChildren();
    data.channels.forEach((channel,index)=>{
      const card=document.createElement('div'); card.className='channel';
      const title=document.createElement('h3'); title.textContent=channel.name;
      const state=document.createElement('div');
      state.className='state';
      state.textContent=channel.on?'STATUS: ON (DEMO)':'STATUS: OFF (DEMO)';
      const button=document.createElement('button');
      button.textContent=channel.on?'MATIKAN':'NYALAKAN';
      if(channel.on) button.className='active';
      button.onclick=()=>toggle(index);
      card.append(title,state,button);
      root.append(card);
    });
  }catch(error){
    document.getElementById('status').textContent='Gagal membaca status: '+error.message;
  }
}

async function toggle(index){
  try{
    await api('/api/channel?ch='+index+'&state=toggle');
    await refresh();
  }catch(error){alert('Kontrol gagal: '+error.message);}
}

async function startScan(){
  const button=document.getElementById('scanBtn');
  const list=document.getElementById('scanList');
  button.disabled=true; list.textContent='Memulai pemindaian...';
  try{
    await api('/api/scan/start');
    pollScan();
  }catch(error){
    list.textContent='Pemindaian gagal: '+error.message;
    button.disabled=false;
  }
}

async function pollScan(){
  const button=document.getElementById('scanBtn');
  const list=document.getElementById('scanList');
  try{
    const response=await api('/api/scan/result');
    const data=await response.json();
    if(data.running){
      list.textContent='Sedang memindai...';
      setTimeout(pollScan,1200);
      return;
    }
    list.replaceChildren();
    if(!data.networks.length){
      list.textContent='Tidak ada jaringan ditemukan.';
    }else{
      data.networks.forEach(network=>{
        const row=document.createElement('div'); row.className='netrow';
        row.textContent=network.ssid+' · '+network.rssi+' dBm · '+(network.secure?'Terkunci':'Terbuka');
        list.append(row);
      });
    }
  }catch(error){list.textContent='Gagal membaca hasil scan: '+error.message;}
  button.disabled=false;
}

refresh();
setInterval(refresh,5000);
</script>
</body>
</html>
)HTML";

// ---------------- API HANDLERS ----------------
void handleRoot() {
  server.send_P(200, "text/html; charset=utf-8", INDEX_HTML);
}

void handleStatus() {
  String json = "{";
  json += "\"mode\":\"" + connectionMode() + "\",";
  json += "\"wifi\":\"" + String(WiFi.status() == WL_CONNECTED ? "Terhubung" : "Belum terhubung") + "\",";
  json += "\"ip\":\"" + currentIp() + "\",";
  json += "\"blynk\":\"" + String(Blynk.connected() ? "Terhubung" : "Terputus") + "\",";
  json += "\"channels\":[";
  for (uint8_t i = 0; i < 4; i++) {
    if (i) json += ",";
    json += "{\"name\":\"" + String(CHANNEL_NAMES[i]) + "\",";
    json += "\"on\":";
    json += channelState[i] ? "true" : "false";
    json += "}";
  }
  json += "]}";
  server.send(200, "application/json", json);
}

void handleChannel() {
  if (!server.hasArg("ch") || !server.hasArg("state")) {
    server.send(400, "text/plain", "Parameter ch dan state wajib ada");
    return;
  }

  int index = server.arg("ch").toInt();
  String requested = server.arg("state");

  if (index < 0 || index > 3 ||
      (requested != "on" && requested != "off" && requested != "toggle")) {
    server.send(400, "text/plain", "Parameter tidak valid");
    return;
  }

  bool nextState = channelState[index];
  if (requested == "on") nextState = true;
  else if (requested == "off") nextState = false;
  else nextState = !nextState;

  setChannel((uint8_t)index, nextState, true);
  server.send(200, "text/plain", "OK");
}

void handleScanStart() {
  if (scanRunning) {
    server.send(202, "text/plain", "Scan sedang berjalan");
    return;
  }

  WiFi.scanDelete();
  int result = WiFi.scanNetworks(true, true); // asynchronous scan
  if (result == WIFI_SCAN_FAILED) {
    server.send(500, "text/plain", "Gagal memulai scan");
    return;
  }

  scanRunning = true;
  scanStartedAt = millis();
  server.send(202, "text/plain", "Scan dimulai");
}

void handleScanResult() {
  int result = WiFi.scanComplete();

  if (result == WIFI_SCAN_RUNNING || (result == WIFI_SCAN_FAILED && scanRunning &&
      millis() - scanStartedAt < 1000)) {
    server.send(200, "application/json", "{\"running\":true,\"networks\":[]}");
    return;
  }

  scanRunning = false;
  if (result < 0) {
    WiFi.scanDelete();
    server.send(200, "application/json", "{\"running\":false,\"networks\":[]}");
    return;
  }

  String json = "{\"running\":false,\"networks\":[";
  for (int i = 0; i < result; i++) {
    if (i) json += ",";
    String ssid = jsonEscape(WiFi.SSID(i));
    bool secure = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
    json += "{\"ssid\":\"" + ssid + "\",";
    json += "\"rssi\":" + String(WiFi.RSSI(i)) + ",";
    json += "\"secure\":";
    json += secure ? "true" : "false";
    json += "}";
  }
  json += "]}";

  WiFi.scanDelete();
  server.send(200, "application/json", json);
}

void startWebServer() {
  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/channel", HTTP_GET, handleChannel);
  server.on("/api/scan/start", HTTP_GET, handleScanStart);
  server.on("/api/scan/result", HTTP_GET, handleScanResult);
  server.onNotFound([]() {
    server.send(404, "text/plain", "NOVIA HOME: halaman tidak ditemukan");
  });
  server.begin();
  Serial.println("[WEB] Server aktif pada port 80");
}

// ---------------- BLYNK HANDLERS ----------------
BLYNK_WRITE(V0) { setChannel(0, param.asInt() == 1, false); }
BLYNK_WRITE(V1) { setChannel(1, param.asInt() == 1, false); }
BLYNK_WRITE(V2) { setChannel(2, param.asInt() == 1, false); }
BLYNK_WRITE(V3) { setChannel(3, param.asInt() == 1, false); }

BLYNK_CONNECTED() {
  Serial.println("[BLYNK] Terhubung");
  Blynk.virtualWrite(V0, channelState[0] ? 1 : 0);
  Blynk.virtualWrite(V1, channelState[1] ? 1 : 0);
  Blynk.virtualWrite(V2, channelState[2] ? 1 : 0);
  Blynk.virtualWrite(V3, channelState[3] ? 1 : 0);
}

// ---------------- CONNECTION MANAGEMENT ----------------
void maintainConnections() {
  const unsigned long now = millis();

  if (WiFi.status() != WL_CONNECTED && now - lastWiFiRetry >= WIFI_RETRY_MS) {
    lastWiFiRetry = now;
    Serial.println("[WIFI] Mencoba menghubungkan ulang...");
    WiFi.disconnect(false, false);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
  }

  if (WiFi.status() == WL_CONNECTED && !Blynk.connected() &&
      now - lastBlynkRetry >= BLYNK_RETRY_MS) {
    lastBlynkRetry = now;
    Serial.println("[BLYNK] Mencoba koneksi...");
    Blynk.connect(1000); // bounded wait, tidak menahan loop lama
  }
}

// ---------------- SETUP & LOOP ----------------
void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println();
  Serial.println("========== NOVIA HOME 4CH ==========");

  WiFi.mode(WIFI_AP_STA);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(false);

  bool apStarted = WiFi.softAP(AP_SSID, AP_PASS);
  Serial.printf("[AP] %s\n", apStarted ? "Aktif" : "Gagal");
  Serial.printf("[AP] SSID: %s\n", AP_SSID);
  Serial.printf("[AP] Buka: http://%s\n", WiFi.softAPIP().toString().c_str());

  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.printf("[WIFI] Menghubungkan ke: %s\n", WIFI_SSID);

  startWebServer();

  Blynk.config(BLYNK_AUTH_TOKEN);
  lastWiFiRetry = millis();
  lastBlynkRetry = millis();
}

void loop() {
  server.handleClient();
  maintainConnections();

  if (Blynk.connected()) {
    Blynk.run();
  }

  delay(2);
}
