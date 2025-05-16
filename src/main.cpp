#include <WiFi.h>
#include <WebServer.h>
#include <TM1637Display.h> // تضمين مكتبة TM1637

// بيانات شبكة WiFi
#define WIFI_SSID "Wokwi-GUEST"
#define WIFI_PASSWORD ""
#define WIFI_CHANNEL 6

// تعريف أطراف LED RGB (R, G, B)
const int LEDS[3][3] = {
    {12, 13, 14}, // LED1
    {23, 22, 21}, // LED2
    {25, 26, 27}  // LED3
};

// أطراف حساسات PIR
const int PIR_PINS[3] = {34, 19, 0};
// طرف الصفارة
const int BUZZER_PIN = 18;

// منافذ شاشات TM1637
const int CLK_PINS[3] = {33, 5, 4};
const int DIO_PINS[3] = {15, 17, 16};

// إنشاء كائنات العرض
TM1637Display displays[3] = {
    TM1637Display(CLK_PINS[0], DIO_PINS[0]),
    TM1637Display(CLK_PINS[1], DIO_PINS[1]),
    TM1637Display(CLK_PINS[2], DIO_PINS[2])
};

// القيم الابتدائية للعدادات
int displayValues[3] = {15, 15, 15};

// تتبع حالات
bool ledStates[3] = {false, false, false};         // حالة LED أخضر
bool pirLatched[3] = {false, false, false};        // latch لحالة PIR
bool handledEvent[3] = {false, false, false};      // لمنع التكرار لكل latch
bool buzzerState = false;                          // حالة البزر
unsigned long buzzerEndTime = 0;                   // وقت انتهاء تشغيل البزر

WebServer server(80);

// إعادة تهيئة الحالة عند الضغط على زر الاختيار العشوائي
void allLEDsOff() {
    for (int i = 0; i < 3; i++) {
        for (int c = 0; c < 3; c++) digitalWrite(LEDS[i][c], LOW);
        ledStates[i] = false;
        pirLatched[i] = false;
        handledEvent[i] = false;
    }
}

// تفعيل LED أخضر لمحطة محددة وإظهار العداد
void turnGreen(int idx) {
    digitalWrite(LEDS[idx][0], LOW);
    digitalWrite(LEDS[idx][1], HIGH);
    digitalWrite(LEDS[idx][2], LOW);
    ledStates[idx] = true;
    displays[idx].showNumberDec(displayValues[idx]);
}

// صفحة HTML رئيسية مع التصميم والتفاعل
void handleRoot() {
    String html = R"rawliteral(
<!DOCTYPE html>
<html lang="fr">

<head>
  <meta charset="UTF-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1.0" />
  <title>Pick by Light - SEBN TN & Mercedes-Benz</title>
  <link href="https://cdn.jsdelivr.net/npm/tailwindcss@2.2.19/dist/tailwind.min.css" rel="stylesheet">
  <link href="https://cdnjs.cloudflare.com/ajax/libs/font-awesome/5.15.4/css/all.min.css" rel="stylesheet">
  <style>
    body {
      font-size: 1.1rem;
    }

    .label {
      font-weight: bold;
      color: #1f2937;
    }

    .value {
      padding: 0.5rem 1rem;
      border-radius: 0.5rem;
      font-weight: bold;
      display: inline-block;
      min-width: 100px;
      text-align: center;
    }

    /* Colors & States */
    .led-on {
      background-color: #bbf7d0;
      color: #166534;
    }

    .led-off {
      background-color: #e5e7eb;
      color: #4b5563;
    }

    .led-error {
      background-color: #fecaca;
      color: #991b1b;
    }

    .pir-active {
      background-color: #bfdbfe;
      color: #1d4ed8;
    }

    .pir-inactive {
      background-color: #d1d5db;
      color: #374151;
    }

    .counter-style {
      background-color: #fef3c7;
      color: #92400e;
    }

    .buzzer-on {
      background-color: #fcd34d;
      color: #78350f;
    }

    .buzzer-off {
      background-color: #e5e7eb;
      color: #4b5563;
    }
  </style>
</head>

<body class="bg-gray-100 font-sans min-h-screen flex flex-col">

  <!-- Header -->
  <header class="bg-white shadow p-6 flex flex-col sm:flex-row justify-between items-center text-xl">
    <div class="flex flex-col sm:flex-row items-center space-y-4 sm:space-y-0 sm:space-x-10">
      <div class="flex items-center space-x-2">
        <img
          src="https://upload.wikimedia.org/wikipedia/commons/thumb/f/f4/Sumitomo_Electric_Industries_logo.svg/512px-Sumitomo_Electric_Industries_logo.svg.png"
          alt="SEBN TN Logo" class="h-12 sm:h-14">
      </div>
      <div class="flex flex-col sm:flex-row items-center sm:items-center space-y-4 sm:space-y-0 sm:space-x-6">

        <div class="flex items-center space-x-3">
          <img
            src="https://upload.wikimedia.org/wikipedia/commons/thumb/9/90/Mercedes-Logo.svg/512px-Mercedes-Logo.svg.png"
            alt="Mercedes-Benz Logo"
            class="h-12 sm:h-14"
          />
          <span class="font-bold text-gray-700 text-xl sm:text-2xl">
            Mercedes-Benz
          </span>
        </div>
      
        <span class="hidden sm:inline text-blue-600 font-semibold text-lg sm:text-xl whitespace-nowrap">
          Pick by Light - SEBN TN
        </span>
      
        <span class="block sm:hidden text-blue-600 font-semibold text-lg text-center mt-2">
          Pick by Light - SEBN TN
        </span>
      
      </div>
      
      </div>
      <button id="startButton" class="mt-4 sm:mt-0 bg-blue-600 text-white rounded-xl hover:bg-blue-700 transition
                   px-6 py-3 text-lg">
        Démarrer le système
      </button>
  </header>

  <!-- Warning Banner -->
  <div id="warning" class="hidden bg-red-600 text-white p-8 text-center font-bold mx-8 mb-12 rounded-lg text-2xl">
    ⚠️ Attention : produit épuisé ! Veuillez recharger avant de continuer.
  </div>

  <!-- Main Stations -->
  <main class="flex-grow px-8 pt-8 pb-6 flex flex-col justify-start items-center">
  <div class="max-w-10xl mx-auto grid grid-cols-1 md:grid-cols-3 gap-6 h-full mt-8">

    <!-- Station 1 -->
    <div class="station bg-white p-14 space-y-6 rounded-2xl shadow-2xl text-lg min-h-[320px]" id="station1">
      <h2 class="font-bold text-3xl">Station 1</h2>
      <p><span class="label"><i class="icon fas fa-lightbulb"></i> LED :</span>
        <span class="led-state value led-off">⏳ En attente</span>
      </p>
      <p><span class="label"><i class="icon fas fa-eye"></i> PIR :</span>
        <span class="pir-state value pir-inactive">🚫 Inactif</span>
      </p>
      <p><span class="label"><i class="icon fas fa-cogs"></i> Compteur :</span>
        <span class="counter value counter-style">0</span>
      </p>
    </div>

    <!-- Station 2 -->
    <div class="station bg-white p-14 space-y-6 rounded-2xl shadow-2xl text-lg min-h-[320px]" id="station2">
      <h2 class="font-bold text-3xl">Station 2</h2>
      <p><span class="label"><i class="icon fas fa-lightbulb"></i> LED :</span>
        <span class="led-state value led-off">⏳ En attente</span>
      </p>
      <p><span class="label"><i class="icon fas fa-eye"></i> PIR :</span>
        <span class="pir-state value pir-inactive">🚫 Inactif</span>
      </p>
      <p><span class="label"><i class="icon fas fa-cogs"></i> Compteur :</span>
        <span class="counter value counter-style">0</span>
      </p>
    </div>

    <!-- Station 3 -->
    <div class="station bg-white p-14 space-y-6 rounded-2xl shadow-2xl text-lg min-h-[320px]" id="station3">
      <h2 class="font-bold text-3xl">Station 3</h2>
      <p><span class="label"><i class="icon fas fa-lightbulb"></i> LED :</span>
        <span class="led-state value led-off">⏳ En attente</span>
      </p>
      <p><span class="label"><i class="icon fas fa-eye"></i> PIR :</span>
        <span class="pir-state value pir-inactive">🚫 Inactif</span>
      </p>
      <p><span class="label"><i class="icon fas fa-cogs"></i> Compteur :</span>
        <span class="counter value counter-style">0</span>
      </p>
    </div>

  </div>
</main>

  <!-- Buzzer Footer -->
  <footer class="bg-white shadow mt-auto py-6">
    <div class="max-w-7xl mx-auto px-8 text-center text-xl">
      <p><span class="label"><i class="icon fas fa-bell"></i> État du Buzzer :</span>
        <span id="buzzerState" class="value buzzer-off">🔕 Éteint</span>
      </p>
    </div>
  </footer>

  <!-- Script d'interaction -->
  <script>
    function randomizeLEDs() {
      fetch('/randomize')
        .then(res => { if (!res.ok) throw new Error('Erreur réseau'); return res.text(); })
        .then(msg => console.log('Randomize:', msg))
        .catch(err => console.error(err));
    }

    function updateStation(i, data) {
      const ledVal = data['LED' + i];
      const ledEl = document.querySelector('#station' + i + ' .led-state');
      ledEl.className = 'led-state value ' +
        (ledVal === 'Green' ? 'led-on' :
          ledVal === 'Red' ? 'led-error' : 'led-off');
      ledEl.textContent = ledVal === 'Green' ? '✅ Vert' : ledVal === 'Red' ? '❌ Rouge' : '⏳ En attente';

      const pirVal = data['PIR' + i];
      const pirEl = document.querySelector('#station' + i + ' .pir-state');
      pirEl.className = 'pir-state value ' + (pirVal === 'DETECTED' ? 'pir-active' : 'pir-inactive');
      pirEl.textContent = pirVal === 'DETECTED' ? '👣 Mouvement' : '🚫 Inactif';

      const cntVal = data['DISPLAY' + i];
      const cntEl = document.querySelector('#station' + i + ' .counter');
      cntEl.textContent = cntVal;
    }

    function updateBuzzer(data) {
      const buzzEl = document.getElementById('buzzerState');
      buzzEl.className = 'value ' + (data.BUZZER === 'ON' ? 'buzzer-on' : 'buzzer-off');
      buzzEl.textContent = data.BUZZER === 'ON' ? '🔔 Activé' : '🔕 Éteint';
    }

    function toggleWarning(show) {
      const warnEl = document.getElementById('warning');
      if (show) warnEl.classList.remove('hidden'); else warnEl.classList.add('hidden');
    }

    function fetchStatus() {
      fetch('/status')
        .then(res => res.json())
        .then(data => {
          [1, 2, 3].forEach(i => updateStation(i, data));
          updateBuzzer(data);
          toggleWarning(data.PRODUCT_DEPLETED === true);
        })
        .catch(err => console.error('Status erreur:', err));
    }

    document.getElementById('startButton').addEventListener('click', randomizeLEDs);
    setInterval(fetchStatus, 1000);
    fetchStatus();
  </script>
</body>

</html>
)rawliteral";
    server.send(200, "text/html", html);
}

// إرسال الحالة كـ JSON
void handleStatus() {
    String json = "{";
    for (int i = 0; i < 3; i++) {
        bool greenOn = digitalRead(LEDS[i][1]) == HIGH;
        bool redOn   = digitalRead(LEDS[i][0]) == HIGH;
        String led = greenOn ? "Green" : (redOn ? "Red" : "En attente");
        String pir = pirLatched[i] ? "DETECTED" : "NONE";
        String cnt = String(displayValues[i]);
        if (displayValues[i] <= 10) cnt += " - Le produit est presque achevé";
        json += "\"LED" + String(i+1) + "\":\"" + led + "\",";
        json += "\"PIR" + String(i+1) + "\":\"" + pir + "\",";
        json += "\"DISPLAY" + String(i+1) + "\":\"" + cnt + "\",";
    }
    json += "\"BUZZER\":\"" + String(buzzerState ? "ON" : "OFF") + "\"}";
    server.send(200, "application/json", json);
}

// اختيار محطتين عشوائيًا
void handleRandomize() {
    allLEDsOff();
    int idxs[3] = {0,1,2};
    for (int i = 2; i > 0; i--) { int j = random(0, i+1); std::swap(idxs[i], idxs[j]); }
    turnGreen(idxs[0]);
    turnGreen(idxs[1]);
    server.send(200, "text/plain", "LEDs ON: LED" + String(idxs[0]+1) + " & LED" + String(idxs[1]+1));
}

void setup() {
    Serial.begin(115200);
    for (int i = 0; i < 3; i++) {
        for (int c = 0; c < 3; c++) pinMode(LEDS[i][c], OUTPUT);
        pinMode(PIR_PINS[i], INPUT);
        displays[i].setBrightness(7);
        displays[i].showNumberDec(displayValues[i]);
        pirLatched[i] = false;
        handledEvent[i] = false;
    }
    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, LOW);
    allLEDsOff();

    WiFi.begin(WIFI_SSID, WIFI_PASSWORD, WIFI_CHANNEL);
    Serial.print("Connexion à ");
    Serial.print(WIFI_SSID);
    while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      Serial.print(".");
    }
    Serial.println("\nConnected!");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    server.on("/", handleRoot);
    server.on("/status", handleStatus);
    server.on("/randomize", handleRandomize);
    server.begin();
}

void loop() {
    unsigned long now = millis();
    for (int i = 0; i < 3; i++) {
        bool pir = digitalRead(PIR_PINS[i]) == HIGH;
        // PIR latch
        if (pir && !pirLatched[i]) pirLatched[i] = true;

        // decrement once if green & latched
        if (ledStates[i] && pirLatched[i] && !handledEvent[i]) {
            displayValues[i]--;
            displays[i].showNumberDec(displayValues[i]);
            handledEvent[i] = true;
        }
        // red + buzzer once if red condition & latched
        else if (!ledStates[i] && pirLatched[i] && !handledEvent[i]) {
            digitalWrite(LEDS[i][0], HIGH);
            digitalWrite(LEDS[i][1], LOW);
            digitalWrite(LEDS[i][2], LOW);
            digitalWrite(BUZZER_PIN, HIGH);
            buzzerState = true;
            buzzerEndTime = now + 3000;
            handledEvent[i] = true;
        }
    }
    // stop buzzer after 3s
    if (buzzerState && now >= buzzerEndTime) {
        digitalWrite(BUZZER_PIN, LOW);
        buzzerState = false;
    }
    server.handleClient();
}
