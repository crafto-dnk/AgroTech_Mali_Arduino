#include <WiFi.h>
#include <WebServer.h>

// Notre wifi et notre pin
const char* AP_SSID = "AgroTechMali_champ";
const char* AP_PASSWORD = "12345678";

const int RELAY_PIN = 14;
const int BUTTON_PIN = 15;
const int HUMIDITY_PIN = 13;
const int WATER_PIN = 12;
const bool RELAY_ACTIVE_LOW = false;


WebServer server(80);

bool pumpOn = false;
bool lastButtonState = HIGH;
unsigned long lastDebounce = 0;
const unsigned long DEBOUNCE_MS = 50;


void applyRelay(bool on) {
  bool level = RELAY_ACTIVE_LOW ? !on : on;
  digitalWrite(RELAY_PIN, level ? HIGH : LOW);
}

String stateJSON() {
  return "{\"pump\":" + String(pumpOn ? "true" : "false") + "}";
}

// Senseur d'humidite (0–4095 ADC → 0–100%)
int readHumidity() {
  int raw = analogRead(HUMIDITY_PIN);
  int pct = map(raw, 4095, 0, 0, 100);
  return constrain(pct, 0, 100);
}

// fonction pour lire l'eau
bool readWater() {
  return digitalRead(WATER_PIN) == HIGH;
}

// page HTML
const char HTML_PAGE[] PROGMEM = R"rawhtml(
<!DOCTYPE html>
<html lang="fr">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Contrôle Pompe</title>

<style>
:root {
  --bg: #ffffff;
  --panel: #f7f7f7;
  --border: #dcdcdc;
  --green: #2e7d32;
  --orange: #f57c00;
  --gray: #555;
  --dark: #222;
  --on: #2e7d32;
  --off: #c62828;
  --warn: #f9a825;
  --text: #222;
  --dim: #777;
}

* { box-sizing: border-box; margin: 0; padding: 0; }

body {
  background: var(--bg);
  font-family: Arial, sans-serif;
  color: var(--text);
  min-height: 100vh;
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  gap: 20px;
  padding: 20px;
}

#lock {
  display: flex;
  flex-direction: column;
  gap: 16px;
  align-items: center;
}

#app {
  display: none;
}


header { text-align: center; }
header h1 { font-size: 1.8rem; font-weight: bold; }
header p { font-size: 0.8rem; color: var(--dim); }

.ip-banner {
  background: #fff;
  border: 1px solid var(--border);
  border-radius: 8px;
  padding: 10px 20px;
  text-align: center;
  width: 100%;
  max-width: 460px;
}

.card {
  background: var(--panel);
  border: 1px solid var(--border);
  border-radius: 10px;
  padding: 32px;
  width: 100%;
  max-width: 460px;
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 20px;
}

.status-ring {
  width: 130px;
  height: 130px;
  border-radius: 50%;
  border: 3px solid var(--border);
  display: flex;
  align-items: center;
  justify-content: center;
}

.status-ring.active { border-color: var(--on); }
.status-ring.inactive { border-color: var(--off); }

.status-icon { font-size: 3rem; }

.status-label { font-weight: bold; }
.status-label.on { color: var(--on); }
.status-label.off { color: var(--off); }

.btn-toggle {
  width: 100%;
  padding: 14px;
  border-radius: 8px;
  border: none;
  font-weight: bold;
  cursor: pointer;
}

.btn-toggle.turn-on { background: var(--green); color: #fff; }
.btn-toggle.turn-off { background: var(--orange); color: #fff; }

.data-grid {
  width: 100%;
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: 10px;
}

.data-cell {
  background: #fff;
  border: 1px solid var(--border);
  padding: 10px;
}

.sensor-section { width: 100%; max-width: 460px; }

.sensor-grid {
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: 12px;
}

.sensor-card {
  background: #fff;
  border: 1px solid var(--border);
  padding: 16px;
  text-align: center;
}

.s-bar-bg { height: 6px; background: #eee; }
.s-bar-fill { height: 6px; }

.log {
  width: 100%;
  max-width: 460px;
  background: #fafafa;
  border: 1px solid var(--border);
  padding: 10px;
  font-size: 0.75rem;
  max-height: 160px;
  overflow-y: auto;
}

.log .ts { color: var(--orange); }
</style>
</head>

<body>

<div id="lock">
  <h2>Code PIN requis</h2>

  <input
    type="password"
    id="pin"
    placeholder="Entrer le code"
    onkeydown="if(event.key==='Enter') unlock();"
  >

  <button onclick="unlock()">Accéder</button>

  <div id="pin-error" style="color:#c62828;font-size:.9rem;"></div>
</div>

<div id="app">
<header>
  <h1>
    <span style="color:#2e7d32;">AGRO</span>
    <span style="color:#f57c00;">TECH</span>
    <span style="color:#555;">POMPE</span>
  </h1>
  <p>Contrôleur de terrain</p>
</header>

<div class="ip-banner">
  <div id="ip-display">chargement…</div>
</div>

<div class="card">
  <div id="ring" class="status-ring inactive">
    <span class="status-icon" id="icon">💧</span>
  </div>

  <div id="state-label" class="status-label off">POMPE ÉTEINTE</div>

  <button id="toggle-btn" class="btn-toggle turn-on" onclick="togglePump()">
    ACTIVER LA POMPE
  </button>

  <div class="data-grid">
    <div class="data-cell"><span id="uptime">—</span></div>
    <div class="data-cell"><span id="clients">—</span></div>
    <div class="data-cell"><span id="last-action">—</span></div>
    <div class="data-cell"><span id="source">—</span></div>
  </div>
</div>

<div class="sensor-section">
  <div class="sensor-grid">

    <div class="sensor-card">
      <span id="hum-val">—</span>
      <div class="s-bar-bg">
        <div class="s-bar-fill" id="hum-bar"></div>
      </div>
    </div>

    <div class="sensor-card" id="water-card">
      <span id="water-val">—</span>
    </div>

  </div>
</div>

<div class="log" id="log"></div>
</div>

<script>
let pumpState = false;

document.addEventListener('DOMContentLoaded', () => {
  document.getElementById('app').style.display = "none";
  document.getElementById('lock').style.display = "flex";
});

function unlock() {
  const val = document.getElementById('pin').value;

  if (val === "2026") {
    document.getElementById('lock').style.display = "none";
    document.getElementById('app').style.display = "block";
    document.getElementById('pin-error').textContent = "";
  } else {
    document.getElementById('pin').value = "";
    document.getElementById('pin-error').textContent = "Code PIN incorrect";
  }
}

function ts() {
  return new Date().toTimeString().slice(0,8);
}

function addLog(msg, cls='') {
  const log = document.getElementById('log');
  const el = document.createElement('div');
  el.innerHTML = '<span class="ts">[' + ts() + ']</span> ' + msg;
  log.prepend(el);
  while (log.children.length > 40) log.removeChild(log.lastChild);
}

function updatePumpUI(on, source) {
  pumpState = on;

  document.getElementById('ring').className = 'status-ring ' + (on ? 'active' : 'inactive');

  document.getElementById('state-label').className = 'status-label ' + (on ? 'on' : 'off');
  document.getElementById('state-label').textContent = on ? 'POMPE EN MARCHE' : 'POMPE ÉTEINTE';

  const icon = document.getElementById('icon');
  if (icon) icon.textContent = on ? '🌊' : '💧';

  document.getElementById('toggle-btn').className = 'btn-toggle ' + (on ? 'turn-off' : 'turn-on');
  document.getElementById('toggle-btn').textContent = on ? 'ARRÊTER LA POMPE' : 'ACTIVER LA POMPE';

  const last = document.getElementById('last-action');
  if (last) last.textContent = ts();

  const sourceEl = document.getElementById('source');
  if (sourceEl && source) sourceEl.textContent = source;
}

function updateSensors(hum, water) {
  const bar = document.getElementById('hum-bar');
  const pct = Math.max(0, Math.min(100, hum));
  bar.style.width = pct + '%';
  bar.style.background = pct < 30 ? '#c62828' : pct < 60 ? '#f9a825' : '#2e7d32';

  document.getElementById('hum-val').textContent = pct + '%';

  const val = document.getElementById('water-val');
  if (water) {
    val.textContent = 'PRÉSENT';
  } else {
    val.textContent = 'ABSENT';
  }
}

async function togglePump() {
  try {
    const res = await fetch('/toggle', { method: 'POST' });
    const data = await res.json();
    updatePumpUI(data.pump, 'WEB');
    addLog(data.pump ? 'Pompe ACTIVÉE via web' : 'Pompe ARRÊTÉE via web');
  } catch(e) {
    addLog('Erreur');
  }
}

function fmtUptime(s) {
  if (!s && s !== 0) return '—';
  const h = Math.floor(s/3600), m = Math.floor((s%3600)/60), sec = s%60;
  return (h ? h+'h ' : '') + (m ? m+'m ' : '') + sec+'s';
}

async function poll() {
  try {
    const res = await fetch('/status');
    const data = await res.json();

    if (data.pump !== pumpState) {
      updatePumpUI(data.pump, data.source || 'BTN');
      addLog(data.pump ? 'Pompe EN MARCHE' : 'Pompe ARRÊTÉE');
    }

    document.getElementById('uptime').textContent = fmtUptime(data.uptime);
    document.getElementById('clients').textContent = data.clients + ' connecté(s)';
    document.getElementById('ip-display').textContent = data.ip;

    updateSensors(data.humidity, data.water);
  } catch(e) {}
}

poll();
setInterval(poll, 1500);
</script>

</body>
</html>
)rawhtml";


// route http
void handleRoot() {
  server.send_P(200, "text/html", HTML_PAGE);
}

void handleStatus() {
  int hum = readHumidity();
  bool water = readWater();

  String json = "{";
  json += "\"pump\":" + String(pumpOn ? "true" : "false") + ",";
  json += "\"uptime\":" + String(millis() / 1000) + ",";
  json += "\"clients\":" + String(WiFi.softAPgetStationNum()) + ",";
  json += "\"humidity\":" + String(hum) + ",";
  json += "\"water\":" + String(water ? "true" : "false") + ",";
  json += "\"ip\":\"" + WiFi.softAPIP().toString() + "\"";
  json += "}";
  server.send(200, "application/json", json);
}

void handleToggle() {
  pumpOn = !pumpOn;
  applyRelay(pumpOn);
  server.send(200, "application/json", stateJSON());
  Serial.printf("[WEB] Pompe %s\n", pumpOn ? "ON" : "OFF");
}

void handleNotFound() {
  server.send(404, "text/plain", "Non trouvé");
}

// Au commencement
void setup() {
  Serial.begin(115200);
  delay(300);

  Serial.println("\n=============================");
  Serial.println("   AgroTechMali Ctrl Pompe");
  Serial.println("=============================");

  // GPIO
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(WATER_PIN, INPUT_PULLUP);  // digital water sensor
  // HUMIDITY_PIN (13) is ADC — no pinMode needed on ESP32
  applyRelay(false);
  Serial.println("[GPIO] Relais OFF, bouton prêt");
  Serial.println("[GPIO] Capteur humidité sur pin 13 (ADC)");
  Serial.println("[GPIO] Capteur eau sur pin 12 (digital)");

  // Start Access Point
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASSWORD);
  delay(100);

  IPAddress ip = WiFi.softAPIP();
  Serial.println("[AP]   Démarré avec succès");
  Serial.printf("[AP]   SSID     : %s\n", AP_SSID);
  Serial.printf("[AP]   Mot de passe : %s\n", AP_PASSWORD);
  Serial.printf("[AP]   Adresse IP  : %s\n", ip.toString().c_str());
  Serial.println("-----------------------------");
  Serial.printf("[WEB]  Ouvrir navigateur → http://%s\n", ip.toString().c_str());
  Serial.println("=============================\n");

  // HTTP routes
  server.on("/", HTTP_GET, handleRoot);
  server.on("/status", HTTP_GET, handleStatus);
  server.on("/toggle", HTTP_POST, handleToggle);
  server.onNotFound(handleNotFound);
  server.begin();
  Serial.println("[WEB]  Serveur démarré sur port 80");
}

// boucle de fonctionnement
void loop() {
  server.handleClient();
  Serial.println("Water");
  Serial.println(digitalRead(WATER_PIN));
  Serial.println("Humidity");
  Serial.println(analogRead(HUMIDITY_PIN));
  delay(200);
  // gestion d'un bouton physique (pas present sur le proto)
  bool reading = digitalRead(BUTTON_PIN);
  if (reading != lastButtonState) lastDebounce = millis();

  if ((millis() - lastDebounce) > DEBOUNCE_MS) {
    static bool confirmedState = HIGH;
    if (reading != confirmedState) {
      confirmedState = reading;
      if (confirmedState == LOW) {
        pumpOn = !pumpOn;
        applyRelay(pumpOn);
        Serial.printf("[BTN]  Pompe %s\n", pumpOn ? "ON" : "OFF");
      }
    }
  }
  lastButtonState = reading;
}
