#include <Arduino.h>
#include <TM1637Display.h>
#include <WiFi.h>
#include <WebServer.h>

// --- Configuration WiFi ---
#define WIFI_SSID       "Wokwi-GUEST"
#define WIFI_PASSWORD   ""
#define WIFI_CHANNEL    6

// --- Définition des capteurs PIR ---
#define PIR1_PIN 34
#define PIR2_PIN 19
#define PIR3_PIN 0

// --- Bouton et buzzer ---
#define BUTTON_PIN 18
#define BUZZER_PIN 2

// --- Définition des LEDs des 3 stations ---
// Station 1
#define LED1_RED   12
#define LED1_GREEN 13
#define LED1_BLUE  14
// Station 2
#define LED2_RED   23
#define LED2_GREEN 22
#define LED2_BLUE  21
// Station 3
#define LED3_RED   25
#define LED3_GREEN 26
#define LED3_BLUE  27

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
String buzzerState = "OFF";      // "ON" ou "OFF"

// --- Instance du serveur web ---
WebServer server(80);

// --- Variables pour le clignotement ---
unsigned long previousBlinkMillis = 0;
bool blinkState = false;
const unsigned long blinkInterval = 500; // 500 ms

// --- Fonctions d'affichage TM1637 ---
void update_display() {
  // Pour chaque compteur, si la valeur est 10, on affiche en clignotant,
  // sinon on affiche la valeur normalement.
  if (counter1 == 10) {
    if (blinkState)
      display1.showNumberDec(10, false);
    else
      display1.clear();
  } else {
    display1.showNumberDec(counter1, true);
  }

  if (counter2 == 10) {
    if (blinkState)
      display2.showNumberDec(10, false);
    else
      display2.clear();
  } else {
    display2.showNumberDec(counter2, true);
  }

  if (counter3 == 10) {
    if (blinkState)
      display3.showNumberDec(10, false);
    else
      display3.clear();
  } else {
    display3.showNumberDec(counter3, true);
  }
}

// --- Fonctions pour gérer les LEDs et le buzzer ---
void turn_off_led(int red, int green, int blue) {
  digitalWrite(red, LOW);
  digitalWrite(green, LOW);
  digitalWrite(blue, LOW);
}

void turn_off_all_leds() {
  turn_off_led(LED1_RED, LED1_GREEN, LED1_BLUE);
  turn_off_led(LED2_RED, LED2_GREEN, LED2_BLUE);
  turn_off_led(LED3_RED, LED3_GREEN, LED3_BLUE);
}

void buzzer_on() {
  ledcAttachPin(BUZZER_PIN, 0);
  ledcWriteTone(0, 1000);
  buzzerState = "ON";
}

void buzzer_off() {
  ledcDetachPin(BUZZER_PIN);
  buzzerState = "OFF";
}

/*
 * La fonction set_led_status gère la couleur de la LED associée au capteur PIR.
 * Si le capteur est actif et que la LED bleue est allumée, la LED passe au vert et le compteur décrémente.
 * Sinon, la LED passe au rouge et le buzzer s'active.
 */
void set_led_status(int sensor_id, int pir_pin, int red, int green, int blue) {
  String *currentStatus;
  bool *triggered;
  if (sensor_id == 1) {
    currentStatus = &station1Status;
    triggered = &triggered1;
  } else if (sensor_id == 2) {
    currentStatus = &station2Status;
    triggered = &triggered2;
  } else {
    currentStatus = &station3Status;
    triggered = &triggered3;
  }

  if (digitalRead(pir_pin) == HIGH) {
    if (digitalRead(blue) == HIGH) { // Si la LED bleue est allumée
      digitalWrite(green, HIGH);
      digitalWrite(red, LOW);
      *currentStatus = "Green";
      
      if (!(*triggered)) {
        if (sensor_id == 1 && counter1 > 0) counter1--;
        else if (sensor_id == 2 && counter2 > 0) counter2--;
        else if (sensor_id == 3 && counter3 > 0) counter3--;
        *triggered = true;
      }
    } else {
      digitalWrite(red, HIGH);
      digitalWrite(green, LOW);
      *currentStatus = "Red";
      
      if (millis() >= buzzer_disabled_until) {
        buzzer_on();
        buzzer_disabled_until = millis() + 5000;
        delay(700);
        buzzer_off();
      }
    }
    update_display();
  } else {
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
    <html>
      <head>
        <title>TM1637 & Web Server - Statut en temps réel</title>
        <meta name="viewport" content="width=device-width, initial-scale=1">
        <style>
          body { font-family: Arial, sans-serif; text-align: center; }
          table { margin: auto; border-collapse: collapse; }
          th, td { padding: 10px; border: 1px solid #ccc; }
        </style>
      </head>
      <body>
        <h1>Statut en temps réel</h1>
        <table>
          <tr><th>Composant</th><th>État</th></tr>
          <tr><td>Station 1 (LED)</td><td id="station1">%STATION1%</td></tr>
          <tr><td>Compteur 1</td><td id="counter1">%COUNTER1%</td></tr>
          <tr><td>Station 2 (LED)</td><td id="station2">%STATION2%</td></tr>
          <tr><td>Compteur 2</td><td id="counter2">%COUNTER2%</td></tr>
          <tr><td>Station 3 (LED)</td><td id="station3">%STATION3%</td></tr>
          <tr><td>Compteur 3</td><td id="counter3">%COUNTER3%</td></tr>
          <tr><td>PIR 1</td><td id="pir1">%PIR1%</td></tr>
          <tr><td>PIR 2</td><td id="pir2">%PIR2%</td></tr>
          <tr><td>PIR 3</td><td id="pir3">%PIR3%</td></tr>
          <tr><td>Buzzer</td><td id="buzzer">%BUZZER%</td></tr>
        </table>
        <script>
          setInterval(function(){
            fetch('/status')
              .then(response => response.json())
              .then(data => {
                document.getElementById('station1').innerHTML = data.station1;
                document.getElementById('counter1').innerHTML = data.counter1;
                document.getElementById('station2').innerHTML = data.station2;
                document.getElementById('counter2').innerHTML = data.counter2;
                document.getElementById('station3').innerHTML = data.station3;
                document.getElementById('counter3').innerHTML = data.counter3;
                document.getElementById('pir1').innerHTML = data.pir1;
                document.getElementById('pir2').innerHTML = data.pir2;
                document.getElementById('pir3').innerHTML = data.pir3;
                document.getElementById('buzzer').innerHTML = data.buzzer;
              });
          }, 1000);
        </script>
      </body>
    </html>
  )";

  // Si le compteur vaut 10, on affiche le message spécifique
  String counter1Str = (counter1 >= 10) ? String(counter1) + " - stocke a bien tot experie" : String(counter1);
  String counter2Str = (counter2 >= 10) ? String(counter2) + " - stocke a bien tot experie" : String(counter2);
  String counter3Str = (counter3 >= 10) ? String(counter3) + " - stocke a bien tot experie" : String(counter3);
  

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

void handleStatus() {
  String json = "{";
  json += "\"station1\":\"" + station1Status + "\",";
  json += "\"station2\":\"" + station2Status + "\",";
  json += "\"station3\":\"" + station3Status + "\",";
  
  String counter1Str = (counter1 <= 10) ? String(counter1) + " - stocke a bien tot experie" : String(counter1);
  String counter2Str = (counter2 <= 10) ? String(counter2) + " - stocke a bien tot experie" : String(counter2);
  String counter3Str = (counter3 <= 10) ? String(counter3) + " - stocke a bien tot experie" : String(counter3);  
  
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

void setup() {
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
  while(WiFi.status() != WL_CONNECTED) {
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

void loop() {
  // Mettre à jour la variable de clignotement
  if (millis() - previousBlinkMillis >= blinkInterval) {
    previousBlinkMillis = millis();
    blinkState = !blinkState;
    update_display();
  }
  
  // Mise à jour de l'état pour chaque station via les capteurs PIR
  set_led_status(1, PIR1_PIN, LED1_RED, LED1_GREEN, LED1_BLUE);
  set_led_status(2, PIR2_PIN, LED2_RED, LED2_GREEN, LED2_BLUE);
  set_led_status(3, PIR3_PIN, LED3_RED, LED3_GREEN, LED3_BLUE);
  
  // Gestion du bouton : allumer aléatoirement 2 LEDs en bleu
  if (digitalRead(BUTTON_PIN) == LOW) {
    turn_off_all_leds();
    // Réinitialiser les états
    station1Status = "OFF";
    station2Status = "OFF";
    station3Status = "OFF";
    
    int choices[] = {LED1_BLUE, LED2_BLUE, LED3_BLUE};
    int chosen1 = random(0, 3);
    int chosen2;
    do {
      chosen2 = random(0, 3);
    } while (chosen2 == chosen1);
    
    digitalWrite(choices[chosen1], HIGH);
    digitalWrite(choices[chosen2], HIGH);
    
    if(chosen1 == 0 || chosen2 == 0) station1Status = "Blue";
    if(chosen1 == 1 || chosen2 == 1) station2Status = "Blue";
    if(chosen1 == 2 || chosen2 == 2) station3Status = "Blue";
    
    while (digitalRead(BUTTON_PIN) == LOW) {
      delay(10);
    }
  }
  
  server.handleClient();
  delay(10);
}
