#include <Arduino.h>
#include <TM1637Display.h>
#include <WiFi.h>
#include <WebServer.h>

// --- Configuration WiFi ---
#define WIFI_SSID "Wokwi-GUEST"
#define WIFI_PASSWORD ""
#define WIFI_CHANNEL 6

// --- Définition des capteurs PIR ---
#define PIR1_PIN 34
#define PIR2_PIN 19
#define PIR3_PIN 0

// --- Bouton et buzzer ---
#define BUTTON_PIN 18
#define BUZZER_PIN 2

// --- Définition des LEDs des 3 stations ---
// Station 1
#define LED1_RED 12
#define LED1_GREEN 13
#define LED1_BLUE 14
// Station 2
#define LED2_RED 23
#define LED2_GREEN 22
#define LED2_BLUE 21
// Station 3
#define LED3_RED 25
#define LED3_GREEN 26
#define LED3_BLUE 27

// --- Définition des broches pour les affichages TM1637 ---
#define CLK1 33
#define DIO1 15
#define CLK2 5
#define DIO2 17
#define CLK3 4
#define DIO3 16

// --- Création des objets d'affichage ---
TM1637Display display1(CLK1, DIO1);
TM1637Display display2(CLK2, DIO2);
TM1637Display display3(CLK3, DIO3);

// --- Variables pour les compteurs affichés ---
int counter1 = 11;
int counter2 = 11;
int counter3 = 11;

// --- Variables pour éviter les déclenchements multiples ---
bool triggered1 = false;
bool triggered2 = false;
bool triggered3 = false;

unsigned long buzzer_disabled_until = 0;

// --- Variables d'état pour l'interface web ---
String station1Status = "OFF"; // état de la LED station 1 ("Red", "Green", "Blue" ou "OFF")
String station2Status = "OFF";
String station3Status = "OFF";
String buzzerState = "OFF"; // "ON" ou "OFF"

// --- Instance du serveur web ---
WebServer server(80);

// --- Variables pour le clignotement ---
unsigned long previousBlinkMillis = 0;
bool blinkState = false;
const unsigned long blinkInterval = 500; // 500 ms

// --- Fonctions d'affichage TM1637 ---
void update_display()
{
  // Pour chaque compteur, si la valeur est 10, on affiche en clignotant,
  // sinon on affiche la valeur normalement.
  if (counter1 == 10)
  {
    if (blinkState)
      display1.showNumberDec(10, false);
    else
      display1.clear();
  }
  else
  {
    display1.showNumberDec(counter1, true);
  }

  if (counter2 == 10)
  {
    if (blinkState)
      display2.showNumberDec(10, false);
    else
      display2.clear();
  }
  else
  {
    display2.showNumberDec(counter2, true);
  }

  if (counter3 == 10)
  {
    if (blinkState)
      display3.showNumberDec(10, false);
    else
      display3.clear();
  }
  else
  {
    display3.showNumberDec(counter3, true);
  }
}

// --- Fonctions pour gérer les LEDs et le buzzer ---
void turn_off_led(int red, int green, int blue)
{
  digitalWrite(red, LOW);
  digitalWrite(green, LOW);
  digitalWrite(blue, LOW);
}

void turn_off_all_leds()
{
  turn_off_led(LED1_RED, LED1_GREEN, LED1_BLUE);
  turn_off_led(LED2_RED, LED2_GREEN, LED2_BLUE);
  turn_off_led(LED3_RED, LED3_GREEN, LED3_BLUE);
}

void buzzer_on()
{
  ledcAttachPin(BUZZER_PIN, 0);
  ledcWriteTone(0, 1000);
  buzzerState = "ON";
}

void buzzer_off()
{
  ledcDetachPin(BUZZER_PIN);
  buzzerState = "OFF";
}

/*
 * La fonction set_led_status gère la couleur de la LED associée au capteur PIR.
 * Si le capteur est actif et que la LED bleue est allumée, la LED passe au vert et le compteur décrémente.
 * Sinon, la LED passe au rouge et le buzzer s'active.
 */
void set_led_status(int sensor_id, int pir_pin, int red, int green, int blue)
{
  String *currentStatus;
  bool *triggered;
  if (sensor_id == 1)
  {
    currentStatus = &station1Status;
    triggered = &triggered1;
  }
  else if (sensor_id == 2)
  {
    currentStatus = &station2Status;
    triggered = &triggered2;
  }
  else
  {
    currentStatus = &station3Status;
    triggered = &triggered3;
  }

  if (digitalRead(pir_pin) == HIGH)
  {
    if (digitalRead(blue) == HIGH)
    { // Si la LED bleue est allumée
      digitalWrite(green, HIGH);
      digitalWrite(red, LOW);
      *currentStatus = "Green";

      if (!(*triggered))
      {
        if (sensor_id == 1 && counter1 > 0)
          counter1--;
        else if (sensor_id == 2 && counter2 > 0)
          counter2--;
        else if (sensor_id == 3 && counter3 > 0)
          counter3--;
        *triggered = true;
      }
    }
    else
    {
      digitalWrite(red, HIGH);
      digitalWrite(green, LOW);
      *currentStatus = "Red";

      if (millis() >= buzzer_disabled_until)
      {
        buzzer_on();
        buzzer_disabled_until = millis() + 5000;
        delay(700);
        buzzer_off();
      }
    }
    update_display();
  }
  else
  {
    // Ne pas écraser la valeur "Blue" qui aurait été définie par le bouton
    if (*currentStatus != "Blue")
      *currentStatus = "OFF";
    *triggered = false;
  }
}

// --- Fonctions du serveur web ---
//
// La page HTML affiche un tableau avec l'état en temps réel.
// Si le compteur vaut 10, on affiche le message "stocke a bien tot experie".
void handleRoot() {
  String html = R"(
<!DOCTYPE html>
<html lang="fr">
<head>
  <meta charset="UTF-8">
  <title>Statut des Stations - Temps Réel</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    :root {
      --primary: #4CAF50;
      --danger: #f44336;
      --warning: #ff9800;
      --info: #2196F3; /* Bleu pour l'état Blue */
      --bg: #f4f7fa;
      --text: #333;
      --card: #fff;
      --border: #ddd;
      --waiting: #9e9e9e;
      --counter0:rgb(0, 0, 0);
    }

    body {
      font-family: 'Segoe UI', sans-serif;
      background-color: var(--bg);
      margin: 0;
      padding: 20px;
      color: var(--text);
    }

    h1 {
      text-align: center;
      color: var(--primary);
      margin-bottom: 20px;
    }

    #alert-message {
      display: block;
      background: var(--warning);
      padding: 12px;
      margin-bottom: 20px;
      border-radius: 8px;
      font-weight: bold;
      color: white;
      text-align: center;
    }

    .container {
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(250px, 1fr));
      gap: 20px;
      max-width: 1000px;
      margin: auto;
    }

    .card {
      background-color: var(--card);
      border: 1px solid var(--border);
      border-radius: 12px;
      box-shadow: 0 4px 8px rgba(0,0,0,0.05);
      padding: 20px;
      transition: transform 0.2s;
    }

    @keyframes pulse {
      0% { opacity: 0.3; transform: scale(0.95); }
      50% { opacity: 1; transform: scale(1.05); }
      100% { opacity: 0.3; transform: scale(0.95); }
    }

    @keyframes spin {
      0% { transform: rotate(0deg); }
      100% { transform: rotate(360deg); }
    }

    .loading-overlay {
      position: fixed;
      top: 0;
      left: 0;
      width: 100%;
      height: 100%;
      background: rgba(255, 255, 255, 0.9);
      z-index: 1000;
      display: flex;
      flex-direction: column;
      align-items: center;
      justify-content: center;
      font-size: 1.8em;
      font-weight: bold;
      color: var(--primary);
      animation: pulse 2s infinite ease-in-out;
    }

    .loading-overlay .spinner {
      font-size: 3em;
      animation: spin 1.5s linear infinite;
      margin-bottom: 0.5em;
    }

    .card:hover {
      transform: translateY(-4px);
    }

    .label {
      font-weight: bold;
      margin-bottom: 10px;
      font-size: 1.1em;
    }

    .value {
      font-size: 1.5em;
    }

    .status {
      padding: 6px 12px;
      border-radius: 8px;
      color: white;
      font-weight: bold;
      display: inline-block;
    }

    .on { background-color: var(--primary); }
    .off { background-color: var(--danger); }
    .warning { background-color: var(--warning); }
    .info { background-color: var(--info); } /* Utilisé pour l'état Blue */
    .waiting { 
      background-color: var(--waiting);
      animation: pulse 1.5s infinite ease-in-out;
    }
    .counter0 { background-color: var(--counter0); }  
  </style>
</head>
<body>
  <h1>Statut des Stations en Temps Réel</h1>
  <div class="loading-overlay" id="loading">
    <div class="spinner">⏳</div>
    <div style="animation: pulse 1.5s infinite ease-in-out;">Chargement des données...</div>
  </div>
  <div id="alert-message">⚠️ Un ou plusieurs stocks sont épuisés. Veuillez réapprovisionner !</div>
  <div class="container">
    <div class="card"><div class="label">Station 1 (LED)</div><div class="value" id="station1"><span class="status info">⏳ En attente...</span></div></div>
    <div class="card"><div class="label">Compteur 1</div><div class="value" id="counter1"><span class="status waiting">⏳ En attente...</span></div></div>
    <div class="card"><div class="label">PIR 1</div><div class="value" id="pir1"><span class="status off">⏳ En attente...</span></div></div>
    <div class="card"><div class="label">Station 2 (LED)</div><div class="value" id="station2"><span class="status waiting">⏳ En attente...</span></div></div>
    <div class="card"><div class="label">Compteur 2</div><div class="value" id="counter2"><span class="status waiting">⏳ En attente...</span></div></div>
    <div class="card"><div class="label">PIR 2</div><div class="value" id="pir2"><span class="status off">⏳ En attente...</span></div></div>
    <div class="card"><div class="label">Station 3 (LED)</div><div class="value" id="station3"><span class="status info">⏳ En attente...</span></div></div>
    <div class="card"><div class="label">Compteur 3</div><div class="value" id="counter3"><span class="status waiting">⏳ En attente...</span></div></div>
    <div class="card"><div class="label">PIR 3</div><div class="value" id="pir3"><span class="status off">⏳ En attente...</span></div></div>
    <div class="card"><div class="label">Buzzer</div><div class="value" id="buzzer"><span class="status off">⏳ En attente...</span></div></div>
  </div>
 <script>
  function updateStatus(id, value) {
    const el = document.getElementById(id);
    let statusClass = 'waiting';
    let symbol = '⏳';
    let displayText = 'En attente...';

    if (value && value !== 'En attente...') {
      if (id.startsWith('station')) {
        if (value === 'Green') {
          statusClass = 'on';
          symbol = '✅';
          displayText = 'Green';
        } else if (value === 'Red') {
          statusClass = 'off';
          symbol = '❌';
          displayText = 'Red';
        } else if (value === 'Blue') {
          statusClass = 'info'; // Utilise la classe info (bleue)
          symbol = '🔵';
          displayText = 'Blue';
        }
      } else if (id.startsWith('pir')) {
        if (value === 'Motion') {
          statusClass = 'on';
          symbol = '👣';
          displayText = 'Motion';
        } else {
          statusClass = 'off';
          symbol = '🚫';
          displayText = 'No Motion';
        }
      } else if (id === 'buzzer') {
        if (value === 'ON') {
          statusClass = 'on';
          symbol = '🔔';
          displayText = 'ON';
        } else {
          statusClass = 'off';
          symbol = '🔕';
          displayText = 'OFF';
        }
      } else if (id.startsWith('counter')) {
        statusClass = value.includes("stocke") ? 'warning' : 'counter0';
        symbol = '';
        displayText = value;
      }
    }

    el.innerHTML = `<span class="status ${statusClass}">${symbol} ${displayText}</span>`;
  }

  function updateAlertMessages(data) {
    const alert = document.getElementById('alert-message');
    if (data && (data.counter1.includes("stocke") || data.counter2.includes("stocke") || data.counter3.includes("stocke"))) {
      alert.style.display = 'block';
    } else {
      alert.style.display = 'none';
    }
  }

  function hideLoading() {
    document.getElementById('loading').style.display = 'none';
  }

  // Initialiser avec les données
  document.addEventListener('DOMContentLoaded', function() {
    updateStatus('station1', 'Blue');
    updateStatus('station3', 'Blue');
    updateStatus('pir1', 'No Motion');
    updateStatus('pir2', 'No Motion');
    updateStatus('pir3', 'No Motion');
    updateStatus('buzzer', 'OFF');
    
    setTimeout(hideLoading, 1000);
  });

  setInterval(() => {
    fetch('/status')
      .then(res => res.json())
      .then(data => {
        updateStatus('station1', data.station1);
        updateStatus('counter1', data.counter1);
        updateStatus('station2', data.station2);
        updateStatus('counter2', data.counter2);
        updateStatus('station3', data.station3);
        updateStatus('counter3', data.counter3);
        updateStatus('pir1', data.pir1);
        updateStatus('pir2', data.pir2);
        updateStatus('pir3', data.pir3);
        updateStatus('buzzer', data.buzzer);
        updateAlertMessages(data);
        hideLoading();
      })
      .catch(error => {
        console.error('Error fetching data:', error);
      });
  }, 1000);
</script>
</body>
</html>
)";

  // Si le compteur vaut 10, on affiche le message spécifique
  String counter1Str = (counter1 >= 10) ? String(counter1) + " - Le produit est presque achevé" : String(counter1);
  String counter2Str = (counter2 >= 10) ? String(counter2) + " - Le produit est presque achevé" : String(counter2);
  String counter3Str = (counter3 >= 10) ? String(counter3) + " - Le produit est presque achevé" : String(counter3);

  html.replace("%STATION1%", station1Status);
  html.replace("%STATION2%", station2Status);
  html.replace("%STATION3%", station3Status);
  html.replace("%COUNTER1%", counter1Str);
  html.replace("%COUNTER2%", counter2Str);
  html.replace("%COUNTER3%", counter3Str);
  html.replace("%PIR1%", digitalRead(PIR1_PIN) == HIGH ? "Motion" : "No Motion");
  html.replace("%PIR2%", digitalRead(PIR2_PIN) == HIGH ? "Motion" : "No Motion");
  html.replace("%PIR3%", digitalRead(PIR3_PIN) == HIGH ? "Motion" : "No Motion");
  html.replace("%BUZZER%", buzzerState);
  server.send(200, "text/html", html);
}

void handleStatus()
{
  String json = "{";
  json += "\"station1\":\"" + station1Status + "\",";
  json += "\"station2\":\"" + station2Status + "\",";
  json += "\"station3\":\"" + station3Status + "\",";

  String counter1Str = (counter1 <= 10) ? String(counter1) + " - Le produit est presque achevé" : String(counter1);
  String counter2Str = (counter2 <= 10) ? String(counter2) + " - Le produit est presque achevé" : String(counter2);
  String counter3Str = (counter3 <= 10) ? String(counter3) + " - Le produit est presque achevé" : String(counter3);

  json += "\"counter1\":\"" + counter1Str + "\",";
  json += "\"counter2\":\"" + counter2Str + "\",";
  json += "\"counter3\":\"" + counter3Str + "\",";
  json += "\"pir1\":\"" + String(digitalRead(PIR1_PIN) == HIGH ? "Motion" : "No Motion") + "\",";
  json += "\"pir2\":\"" + String(digitalRead(PIR2_PIN) == HIGH ? "Motion" : "No Motion") + "\",";
  json += "\"pir3\":\"" + String(digitalRead(PIR3_PIN) == HIGH ? "Motion" : "No Motion") + "\",";
  json += "\"buzzer\":\"" + buzzerState + "\"";
  json += "}";
  server.send(200, "application/json", json);
}

void setup()
{
  Serial.begin(115200);
  randomSeed(analogRead(0));

  // Initialisation des capteurs et composants
  pinMode(PIR1_PIN, INPUT);
  pinMode(PIR2_PIN, INPUT);
  pinMode(PIR3_PIN, INPUT);

  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(BUZZER_PIN, OUTPUT);

  pinMode(LED1_RED, OUTPUT);
  pinMode(LED1_GREEN, OUTPUT);
  pinMode(LED1_BLUE, OUTPUT);

  pinMode(LED2_RED, OUTPUT);
  pinMode(LED2_GREEN, OUTPUT);
  pinMode(LED2_BLUE, OUTPUT);

  pinMode(LED3_RED, OUTPUT);
  pinMode(LED3_GREEN, OUTPUT);
  pinMode(LED3_BLUE, OUTPUT);

  display1.setBrightness(0x0F);
  display2.setBrightness(0x0F);
  display3.setBrightness(0x0F);

  turn_off_all_leds();
  update_display();

  // --- Connexion WiFi ---
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD, WIFI_CHANNEL);
  Serial.print("Connexion à ");
  Serial.print(WIFI_SSID);
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(100);
    Serial.print(".");
  }
  Serial.println(" Connecté !");
  Serial.print("Adresse IP : ");
  Serial.println(WiFi.localIP());

  // --- Configuration du serveur web ---
  server.on("/", handleRoot);
  server.on("/status", handleStatus);
  server.begin();
  Serial.println("Serveur HTTP démarré");
}

void loop()
{
  // Mettre à jour la variable de clignotement
  if (millis() - previousBlinkMillis >= blinkInterval)
  {
    previousBlinkMillis = millis();
    blinkState = !blinkState;
    update_display();
  }

  // Mise à jour de l'état pour chaque station via les capteurs PIR
  set_led_status(1, PIR1_PIN, LED1_RED, LED1_GREEN, LED1_BLUE);
  set_led_status(2, PIR2_PIN, LED2_RED, LED2_GREEN, LED2_BLUE);
  set_led_status(3, PIR3_PIN, LED3_RED, LED3_GREEN, LED3_BLUE);

  // Gestion du bouton : allumer aléatoirement 2 LEDs en bleu
  if (digitalRead(BUTTON_PIN) == LOW)
  {
    turn_off_all_leds();
    // Réinitialiser les états
    station1Status = "OFF";
    station2Status = "OFF";
    station3Status = "OFF";

    int choices[] = {LED1_BLUE, LED2_BLUE, LED3_BLUE};
    int chosen1 = random(0, 3);
    int chosen2;
    do
    {
      chosen2 = random(0, 3);
    } while (chosen2 == chosen1);

    digitalWrite(choices[chosen1], HIGH);
    digitalWrite(choices[chosen2], HIGH);

    if (chosen1 == 0 || chosen2 == 0)
      station1Status = "Blue";
    if (chosen1 == 1 || chosen2 == 1)
      station2Status = "Blue";
    if (chosen1 == 2 || chosen2 == 2)
      station3Status = "Blue";

    while (digitalRead(BUTTON_PIN) == LOW)
    {
      delay(10);
    }
  }

  server.handleClient();
  delay(10);
}