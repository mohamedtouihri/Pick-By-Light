#include <WiFi.h>
#include <WebServer.h>
#include <TM1637Display.h> // تضمين مكتبة TM1637
#include <vector> // لتسهيل عملية التبديل العشوائي
#include <algorithm> // لـ std::swap

// بيانات شبكة WiFi
#define WIFI_SSID "Wokwi-GUEST" // غيّر هذا إلى اسم شبكتك
#define WIFI_PASSWORD "" // غيّر هذا إلى كلمة مرور شبكتك

// تعريف أطراف LED RGB (R, G, B)
const int LEDS[3][3] = {
    {12, 13, 14}, // LED1 - GPIOs 12, 13, 14
    {23, 22, 21}, // LED2 - GPIOs 23, 22, 21
    {25, 26, 27}  // LED3 - GPIOs 25, 26, 27
};

// أطراف حساسات PIR
const int PIR_PINS[3] = {34, 19, 0}; // GPIOs 34, 19, 0
// طرف الصفارة
const int BUZZER_PIN = 18; // GPIO 18

// منافذ شاشات TM1637
const int CLK_PINS[3] = {33, 5, 4}; // CLK on GPIOs 33, 5, 4
const int DIO_PINS[3] = {15, 17, 16}; // DIO on GPIOs 15, 17, 16

// إنشاء كائنات العرض
TM1637Display displays[3] = {
    TM1637Display(CLK_PINS[0], DIO_PINS[0]),
    TM1637Display(CLK_PINS[1], DIO_PINS[1]),
    TM1637Display(CLK_PINS[2], DIO_PINS[2])
};

// القيم الابتدائية للعدادات
int displayValues[3] = {3, 3, 3}; // يمكن تغيير القيمة الابتدائية هنا

// تتبع حالات النظام
bool ledStates[3] = {false, false, false};      // حالة LED أخضر للمحطة (هل يجب أن يكون أخضر؟)
bool pirLatched[3] = {false, false, false};     // latch لحالة PIR (هل تم اكتشاف حركة منذ آخر إعادة تعيين؟)
bool handledEvent[3] = {false, false, false};   // لمنع معالجة حدث الحركة أكثر من مرة لكل latch
bool warningAlerted[3] = {false, false, false}; // لمنع تكرار تشغيل صفارة المخزون المنخفض لنفس المحطة

bool systemActive = false;                      // حالة النظام: هل بدأ العمل؟

// NEW: Buzzer state and pattern control
enum BuzzerMode {
    BUZZER_OFF,
    BUZZER_ERROR_CONTINUOUS, // لخطأ الاختيار
    BUZZER_LOW_STOCK_PULSE   // لتنبيه المخزون المنخفض
};

BuzzerMode currentBuzzerMode = BUZZER_OFF;
unsigned long buzzerPatternStartTime = 0; // وقت بدء نمط الصفارة الحالي
unsigned long buzzerDuration = 0;         // المدة الكلية لعملية تنبيه الصفارة

// فترات نغمة المخزون المنخفض (بالمللي ثانية)
const unsigned long LOW_STOCK_BUZZ_ON_TIME = 100; // تشغيل الصفارة لمدة 100ms
const unsigned long LOW_STOCK_BUZZ_OFF_TIME = 100; // إيقاف الصفارة لمدة 100ms
unsigned long nextBuzzerToggleTime = 0; // الوقت التالي لتبديل حالة الصفارة ضمن النمط

WebServer server(80);

// إعادة تهيئة الحالة عند الضغط على زر الاختيار العشوائي أو بدء النظام
void allLEDsOff() {
    Serial.println("Resetting all stations...");
    for (int i = 0; i < 3; i++) {
        // إطفاء جميع ألوان LED
        for (int c = 0; c < 3; c++) digitalWrite(LEDS[i][c], LOW);
        ledStates[i] = false;       // لا يجب أن يكون أخضر الآن
        pirLatched[i] = false;      // مسح حالة اللاتش للحركة
        handledEvent[i] = false;    // مسح حالة معالجة الحدث
        // warningAlerted[i] = false; // لا يتم مسح تنبيه المخزون المنخفض إلا عند إعادة تعبئة المنتج (خارج نطاق هذا الكود)
                                    // إذا أردت مسحه عند كل جولة جديدة، قم بإزالة التعليق عن السطر أعلاه
    }
    digitalWrite(BUZZER_PIN, LOW); // التأكد من إطفاء الصفارة
    currentBuzzerMode = BUZZER_OFF;
    buzzerPatternStartTime = 0;
    buzzerDuration = 0;
    nextBuzzerToggleTime = 0; // Reset toggle time
}

// تفعيل LED أخضر لمحطة محددة وإظهار العداد
void turnGreen(int idx) {
    if (idx < 0 || idx >= 3) return; // تحقق من أن الرقم صالح
    Serial.print("Turning Green: Station ");
    Serial.println(idx + 1);
    digitalWrite(LEDS[idx][0], LOW);    // إطفاء الأحمر
    digitalWrite(LEDS[idx][1], HIGH); // تشغيل الأخضر
    digitalWrite(LEDS[idx][2], LOW);    // إطفاء الأزرق (غير مستخدم)
    ledStates[idx] = true;
}

// صفحة HTML رئيسية مع التصميم والتفاعل (سيتم تحديث قسم JavaScript فقط)
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
        body { font-size: 1.1rem; }
        .label { font-weight: bold; color: #1f2937; }
        .value { padding: 0.5rem 1rem; border-radius: 0.5rem; font-weight: bold; display: inline-block; min-width: 100px; text-align: center; }
        .led-on { background-color: #bbf7d0; color: #166534; } /* Green */
        .led-off { background-color: #e5e7eb; color: #4b5563; } /* Gray/Waiting */
        .led-error { background-color: #fecaca; color: #991b1b; } /* Red Error */
        .pir-active { background-color: #bfdbfe; color: #1d4ed8; } /* Blue Active */
        .pir-inactive { background-color: #d1d5db; color: #374151; } /* Dark Gray Inactive */
        .counter-style { background-color: #fef3c7; color: #92400e; } /* Yellow/Orange Normal */
        /* NEW: Warning Counter Style */
        .counter-warning {
            background-color: #f87171; /* Red */
            color: #ffffff; /* White text */
            font-weight: bold;
            animation: blink 1s infinite; /* Blink effect */
        }
        /* NEW: Blink Animation */
        @keyframes blink {
            0%, 100% { opacity: 1; }
            50% { opacity: 0.5; } /* Slight transparency change */
        }
        .buzzer-on { background-color: #fcd34d; color: #78350f; } /* Orange */
        .buzzer-off { background-color: #e5e7eb; color: #4b5563; } /* Gray */
        .buzzer-attention { background-color: #facc15; color: #854d0e; animation: pulse 1.5s infinite; } /* Yellow/Amber Pulse */

          /* Pulsing animation for Attention buzzer status */
        @keyframes pulse {
            0% {
                box-shadow: 0 0 0 0 rgba(250, 204, 21, 0.7);
            }
            70% {
                box-shadow: 0 0 0 10px rgba(250, 204, 21, 0);
            }
            100% {
                box-shadow: 0 0 0 0 rgba(250, 204, 21, 0);
            }
        }

    </style>
</head>
<body class="bg-gray-100 font-sans min-h-screen flex flex-col">
    <header class="bg-white shadow p-6 flex flex-col sm:flex-row justify-between items-center text-xl">
        <div class="flex flex-col sm:flex-row items-center space-y-4 sm:space-y-0 sm:space-x-10">
            <div class="flex items-center space-x-2">
                <img src="https://upload.wikimedia.org/wikipedia/commons/thumb/f/f4/Sumitomo_Electric_Industries_logo.svg/512px-Sumitomo_Electric_Industries_logo.svg.png" alt="SEBN TN Logo" class="h-12 sm:h-14">
            </div>
            <div class="flex flex-col sm:flex-row items-center space-y-4 sm:space-y-0 sm:space-x-6">
                <div class="flex items-center space-x-3">
                    <img src="https://upload.wikimedia.org/wikipedia/commons/thumb/9/90/Mercedes-Logo.svg/512px-Mercedes-Logo.svg.png" alt="Mercedes-Benz Logo" class="h-12 sm:h-14" />
                    <span class="font-bold text-gray-700 text-xl sm:text-2xl">Mercedes-Benz</span>
                </div>
                <span class="text-blue-600 font-semibold text-lg sm:text-xl whitespace-nowrap text-center">Pick by Light - SEBN TN</span>
            </div>
        </div>
        <button id="startButton" class="mt-4 sm:mt-0 bg-blue-600 text-white rounded-xl hover:bg-blue-700 transition px-6 py-3 text-lg"><i class="fas fa-play-circle mr-2"></i> Démarrer le système</button>
    </header>

    <div id="warning" class="hidden bg-red-600 text-white p-6 text-center font-bold mx-8 my-6 rounded-lg text-2xl">
        </div>

    <main class="flex-grow px-8 flex flex-col justify-center items-center">
        <div class="grid grid-cols-1 md:grid-cols-3 gap-12 max-w-screen-lg mx-auto">
            <div class="station bg-white p-9 space-y-8 rounded-lg shadow-md text-align" id="station1">
                <h2 class="font-bold text-xl text-center">Station 1</h2>
                <p><span class="label"><i class="icon fas fa-lightbulb"></i> LED :</span> <span class="led-state value led-off">⏳ En attente</span></p>
                <p><span class="label"><i class="icon fas fa-eye"></i> PIR :</span> <span class="pir-state value pir-inactive">🚫 Inactif</span></p>
                <p><span class="label"><i class="icon fas fa-boxes"></i> Compteur :</span> <span class="counter value counter-style">0</span></p>
            </div>
            <div class="station bg-white p-9 space-y-8 rounded-lg shadow-md text-align" id="station2">
                <h2 class="font-bold text-xl text-center">Station 2</h2>
                <p><span class="label"><i class="icon fas fa-lightbulb"></i> LED :</span> <span class="led-state value led-off">⏳ En attente</span></p>
                <p><span class="label"><i class="icon fas fa-eye"></i> PIR :</span> <span class="pir-state value pir-inactive">🚫 Inactif</span></p>
                <p><span class="label"><i class="icon fas fa-boxes"></i> Compteur :</span> <span class="counter value counter-style">0</span></p>
            </div>
            <div class="station bg-white p-9 space-y-8 rounded-lg shadow-md text-align" id="station3">
                <h2 class="font-bold text-xl text-center">Station 3</h2>
                <p><span class="label"><i class="icon fas fa-lightbulb"></i> LED :</span> <span class="led-state value led-off">⏳ En attente</span></p>
                <p><span class="label"><i class="icon fas fa-eye"></i> PIR :</span> <span class="pir-state value pir-inactive">🚫 Inactif</span></p>
                <p><span class="label"><i class="icon fas fa-boxes"></i> Compteur :</span> <span class="counter value counter-style">0</span></p>
            </div>
        </div>
    </main>

    <footer class="bg-white shadow p-6 text-center">
        <span class="label"><i class="fas fa-bell"></i> État du Buzzer :</span>
        <span id="buzzerState" class="value buzzer-off">🔕 Éteint</span>
    </footer>

    <script>
        function randomizeLEDs() {
            fetch('/randomize')
                .then(res => { if (!res.ok) throw new Error('Network response was not ok.'); return res.text(); })
                .then(msg => console.log('Randomize initiated:', msg))
                .catch(err => console.error('Error initiating randomize:', err));
        }

        function updateStation(i, data) {
            // Update LED state
            const ledVal = data['LED' + i];
            const ledEl = document.querySelector('#station' + i + ' .led-state');
            ledEl.className = 'led-state value ' +
                (ledVal === 'Green' ? 'led-on' :
                 ledVal === 'Red' ? 'led-error' : 'led-off');
            ledEl.textContent = ledVal === 'Green' ? '✅ Vert' : ledVal === 'Red' ? '❌ Rouge' : '⏳ En attente';

            // Update PIR state
            const pirVal = data['PIR' + i];
            const pirEl = document.querySelector('#station' + i + ' .pir-state');
            pirEl.className = 'pir-state value ' + (pirVal === 'DETECTED' ? 'pir-active' : 'pir-inactive');
            pirEl.textContent = pirVal === 'DETECTED' ? '👣 Mouvement' : '🚫 Inactif';


            // Update Counter state and style
            const cntEl = document.querySelector('#station' + i + ' .counter');
            const cntText = data['DISPLAY' + i]; // This string includes " !!!" if low or " ---" if depleted
            cntEl.textContent = cntText; // Display the text as is

            // Apply warning/depleted style
            if (cntText.includes(" ⚠️")) { // Low stock
                cntEl.classList.remove('counter-style', 'counter-depleted');
                cntEl.classList.add('counter-warning');
            } else if (cntText.includes("0")) { // Depleted
                cntEl.classList.remove('counter-style', 'counter-warning');
                cntEl.classList.add('counter-depleted'); // NEW class for depleted
            } else { // Normal
                cntEl.classList.remove('counter-warning', 'counter-depleted');
                cntEl.classList.add('counter-style');
            }
        }

        // Function to update the global warning banner
        function updateWarning(data) {
            const warnEl = document.getElementById('warning');
            const warningMessage = data.WARNING_MESSAGE; // Get the combined message

            if (warningMessage && warningMessage !== "") {
                warnEl.textContent = '⚠️ ' + warningMessage; // Set the dynamic message
                warnEl.classList.remove('hidden'); // Show the banner
            } else {
                warnEl.classList.add('hidden'); // Hide banner if no warning message
            }
        }

        // Function to update the buzzer UI status based on warnings or actual state
        function updateBuzzerUI(data) {
            const buzzEl = document.getElementById('buzzerState');
            // Check if any counter is currently showing the warning style (indicating low stock)
            // or if any counter indicates depleted stock
            const anyCounterWarning = document.querySelectorAll('.counter.counter-warning').length > 0;
            const anyCounterDepleted = document.querySelectorAll('.counter.counter-depleted').length > 0;

            if (anyCounterWarning || anyCounterDepleted) {
                // If any counter shows warning or depleted, set buzzer status to Attention
                buzzEl.className = 'value buzzer-attention';
                buzzEl.textContent = '🔔 Attention !';
            } else {
                // Otherwise, show the actual buzzer state from the backend
                buzzEl.className = 'value ' + (data.BUZZER === 'ON' ? 'buzzer-on' : 'buzzer-off');
                buzzEl.textContent = data.BUZZER === 'ON' ? '🔔 Activé' : '🔕 Éteint';
            }
        }

        function fetchStatus() {
            fetch('/status')
                .then(res => {
                    if (!res.ok) throw new Error('Network response was not ok.');
                    return res.json();
                })
                .then(data => {
                    // Update each station's display
                    [1, 2, 3].forEach(i => updateStation(i, data));
                    // Update the global warning banner
                    updateWarning(data);
                    // Update the buzzer UI state based on status and warnings
                    updateBuzzerUI(data); // Call the new function
                })
                .catch(err => console.error('Error fetching status:', err));
        }

        // Event listener for the start button
        document.getElementById('startButton').addEventListener('click', randomizeLEDs);

        // Fetch status periodically
        setInterval(fetchStatus, 1000); // Update every 1 second

        // Fetch status immediately on page load
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
    std::vector<String> depletedStationsList; // لقائمة المخزون المستنفد (0)
    std::vector<String> lowStockStationsList; // لقائمة المخزون شبه المستنفد (<=2 و >0)

    // بناء بيانات المحطات (LED, PIR, DISPLAY) وإيجاد المحطات المستنفدة / ذات المخزون المنخفض
    for (int i = 0; i < 3; i++) {
        String led;
        String pir;

        if (systemActive) {
            bool greenOn = digitalRead(LEDS[i][1]) == HIGH;
            bool redOn   = digitalRead(LEDS[i][0]) == HIGH;
            led = greenOn ? "Green" : (redOn ? "Red" : "Waiting");
            pir = pirLatched[i] ? "DETECTED" : "NONE";
        } else {
            led = "Waiting";
            pir = "NONE";
        }

        String cnt = String(displayValues[i]);

        if (displayValues[i] == 0) {
            cnt = "0";
            depletedStationsList.push_back(String(i + 1));
        } else if (displayValues[i] <= 2) {
            cnt += " ⚠️";
            lowStockStationsList.push_back(String(i + 1));
        }

        json += "\"LED" + String(i + 1) + "\":\"" + led + "\",";
        json += "\"PIR" + String(i + 1) + "\":\"" + pir + "\",";
        json += "\"DISPLAY" + String(i + 1) + "\":\"" + cnt + "\"";
        if (i < 2) json += ",";
    }

    String warningMessage = "";

    if (!depletedStationsList.empty()) {
        warningMessage += "Station" + String(depletedStationsList.size() > 1 ? "s " : " ");
        for (size_t i = 0; i < depletedStationsList.size(); ++i) {
            warningMessage += depletedStationsList[i];
            if (depletedStationsList.size() > 1) {
                if (i < depletedStationsList.size() - 2) {
                    warningMessage += ", ";
                } else if (i == depletedStationsList.size() - 2) {
                    warningMessage += " et ";
                }
            }
        }
        warningMessage += " : Stock épuisé."; // تمت إزالة المسافة الزائدة هنا
    }

    if (!lowStockStationsList.empty()) {
        if (!warningMessage.isEmpty()) {
            // *** هذا هو التغيير الرئيسي لإضافة فاصل جذاب ***
            warningMessage += " --- 🚨 --- "; // فاصل باستخدام رمز تعبيري
        }
        warningMessage += "Station" + String(lowStockStationsList.size() > 1 ? "s " : " ");
        for (size_t i = 0; i < lowStockStationsList.size(); ++i) {
            warningMessage += lowStockStationsList[i];
            if (lowStockStationsList.size() > 1) {
                if (i < lowStockStationsList.size() - 2) {
                    warningMessage += ", ";
                } else if (i == lowStockStationsList.size() - 2) {
                    warningMessage += " et ";
                }
            }
        }
        warningMessage += " : Stock presque épuisé.";
    }

    if (!warningMessage.isEmpty()) {
        warningMessage += " Merci de recharger.";
    }

    json += ",\"WARNING_MESSAGE\":\"" + warningMessage + "\"";
    json += ",\"BUZZER\":\"" + String((systemActive && currentBuzzerMode != BUZZER_OFF) ? "ON" : "OFF") + "\"";
    json += "}";
    server.send(200, "application/json", json);
}
// اختيار محطتين عشوائيًا وبدء الجولة
void handleRandomize() {
    allLEDsOff(); // إعادة تعيين كل شيء أولاً
    systemActive = true; // تفعيل النظام عند بدء الجولة

    std::vector<int> availableStations;
    for (int i = 0; i < 3; ++i) {
        if (displayValues[i] > 0) { // Only consider stations with stock > 0
            availableStations.push_back(i);
        }
    }

    if (availableStations.empty()) {
        Serial.println("No stations available with stock > 0 to randomize.");
        server.send(200, "text/plain", "Randomization complete. No stations have stock to pick from.");
        return;
    }

    // خلط المتجهات عشوائياً (Fisher-Yates Shuffle)
    for (int i = availableStations.size() - 1; i > 0; i--) {
        int j = random(0, i + 1);
        std::swap(availableStations[i], availableStations[j]); // استخدم std::swap
    }

    // اختيار عدد المحطات التي ستضيء بالأخضر عشوائياً
    // Ensure we don't try to pick more stations than available
    int numStationsToTurnGreen = random(1, std::min((int)availableStations.size() + 1, 3)); // will return 1 or 2, but not more than available stations

    Serial.print("Randomizing: Turning GREEN for ");
    Serial.print(numStationsToTurnGreen);
    Serial.println(" station(s).");

    String activatedStations = "";
    for (int i = 0; i < numStationsToTurnGreen; ++i) {
        turnGreen(availableStations[i]);
        activatedStations += (String)(availableStations[i] + 1);
        if (i < numStationsToTurnGreen - 1) {
            activatedStations += " & ";
        }
    }

    if (numStationsToTurnGreen == 0) {
        Serial.println("No stations turned green.");
        server.send(200, "text/plain", "Randomization complete. No stations turned green.");
    } else {
        Serial.print("LEDs ON: Station ");
        Serial.println(activatedStations);
        server.send(200, "text/plain", "Randomization complete. Station(s) " + activatedStations + " are green.");
    }
}

// NEW: دالة للتحكم في نمط الصفارة
void handleBuzzerPattern(unsigned long now) {
    if (currentBuzzerMode == BUZZER_OFF) return; // لا تفعل شيئًا إذا كانت الصفارة مطفأة

    // إيقاف الصفارة تمامًا إذا انتهت مدة النمط الكلية
    if (now >= buzzerPatternStartTime + buzzerDuration) {
        digitalWrite(BUZZER_PIN, LOW);
        currentBuzzerMode = BUZZER_OFF;
        Serial.println("Buzzer pattern ended.");
        return;
    }

    // منطق تشغيل/إيقاف الصفارة بناءً على النمط المحدد
    if (currentBuzzerMode == BUZZER_ERROR_CONTINUOUS) {
        digitalWrite(BUZZER_PIN, HIGH); // تشغيل مستمر
    } else if (currentBuzzerMode == BUZZER_LOW_STOCK_PULSE) {
        if (now >= nextBuzzerToggleTime) {
            // تبديل حالة الصفارة (تشغيل -> إيقاف، إيقاف -> تشغيل)
            if (digitalRead(BUZZER_PIN) == LOW) {
                digitalWrite(BUZZER_PIN, HIGH);
                nextBuzzerToggleTime = now + LOW_STOCK_BUZZ_ON_TIME;
            } else {
                digitalWrite(BUZZER_PIN, LOW);
                nextBuzzerToggleTime = now + LOW_STOCK_BUZZ_OFF_TIME;
            }
        }
    }
}

void setup() {
    Serial.begin(115200);
    // تهيئة البذور العشوائية بشكل جيد
    randomSeed(analogRead(0));

    // تهيئة الأطراف والحالات الابتدائية
    for (int i = 0; i < 3; i++) {
        for (int c = 0; c < 3; c++) pinMode(LEDS[i][c], OUTPUT);
        pinMode(PIR_PINS[i], INPUT);
        displays[i].setBrightness(0x0f); // تعيين أقصى سطوع
        displays[i].showNumberDec(displayValues[i]); // Show initial counter values
        pirLatched[i] = false;
        handledEvent[i] = false;
        warningAlerted[i] = false;
    }
    pinMode(BUZZER_PIN, OUTPUT); // Buzzer pin as output
    digitalWrite(BUZZER_PIN, LOW); // Ensure buzzer is off initially

    allLEDsOff(); // التأكد من أن جميع الـ LEDs والصفارة مطفأة في البداية
    systemActive = false; // النظام يبدأ غير نشط

    Serial.print("Connecting to ");
    Serial.println(WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nConnected!");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());

    // إعداد معالجات طلبات الويب
    server.on("/", handleRoot);
    server.on("/status", handleStatus);
    server.on("/randomize", handleRandomize);

    server.begin(); // بدء تشغيل خادم الويب
    Serial.println("HTTP server started");
}

void loop() {
    unsigned long now = millis(); // الحصول على الوقت الحالي
    server.handleClient(); // معالجة طلبات خادم الويب باستمرار

    // تحديث شاشات العدادات بغض النظر عن حالة النظام
    for (int i = 0; i < 3; i++) {
        displays[i].showNumberDec(displayValues[i]);
    }

    // كل منطق تفاعل الحساسات والأضواء والصفارة يعمل فقط إذا كان النظام نشطاً
    if (systemActive) {
        // حلقة لمعالجة كل محطة على حدة
        for (int i = 0; i < 3; i++) {
            // **NEW: Check if counter is 0 for this station**
            if (displayValues[i] == 0) {
                // If counter is 0, turn off LEDs and reset PIR states for this station
                for (int c = 0; c < 3; c++) digitalWrite(LEDS[i][c], LOW); // Turn off all LEDs
                ledStates[i] = false; // Ensure LED state is false
                pirLatched[i] = false; // Reset PIR latch
                handledEvent[i] = false; // Reset handled event
                // Do NOT return here, continue to check other stations.
                continue; // Skip the rest of the loop for this station if depleted
            }

            // اقرأ حالة حساس PIR للمحطة الحالية
            bool pir = digitalRead(PIR_PINS[i]) == HIGH;

            // منطق PIR latch: إذا تم اكتشاف حركة (pir == true) ولم يتم تسجيلها بعد (!pirLatched[i])، قم بتثبيت الحالة
            if (pir && !pirLatched[i]) {
                pirLatched[i] = true;
                Serial.print("Motion detected on Station ");
                Serial.println(i + 1);
            }

            // معالجة الحدث فقط إذا تم تثبيت PIR (pirLatched[i] == true) ولم يتم التعامل معه بعد (!handledEvent[i])
            if (pirLatched[i] && !handledEvent[i]) {
                // تحقق مما إذا كان الضوء أخضر (اختيار صحيح متوقع لهذه المحطة)
                if (ledStates[i]) {
                    // **** إجراءات الالتقاط الصحيح ****
                    displayValues[i]--; // إنقاص العداد
                    Serial.print("Correct pick on Station ");
                    Serial.print(i + 1);
                    Serial.print(". Counter is now: ");
                    Serial.println(displayValues[i]);

                    // تشغيل الصفارة إذا أصبح المخزون منخفضًا (<= 10) ولم يتم التنبيه له من قبل
                    // أو إذا أصبح 0 ولم يتم التنبيه له
                    if (displayValues[i] <= 10 && !warningAlerted[i]) {
                        Serial.print("Low stock/Depleted condition met for Station ");
                        Serial.println(i + 1);
                        // تفعيل نمط المخزون المنخفض/النفاد
                        currentBuzzerMode = BUZZER_LOW_STOCK_PULSE;
                        buzzerPatternStartTime = now;
                        buzzerDuration = 3000; // تنبيه لمدة 3 ثوانٍ
                        nextBuzzerToggleTime = now + LOW_STOCK_BUZZ_ON_TIME; // أول تبديل
                        Serial.println("Triggering low stock/depleted pulsed buzzer (3s)");
                        warningAlerted[i] = true; // تم وضع علامة على المحطة بأنها أطلقت تنبيه
                    } else if (displayValues[i] == 0 && !warningAlerted[i]) { // Specific check for 0 if not already caught by <=10
                        Serial.print("Stock depleted on Station ");
                        Serial.println(i + 1);
                        currentBuzzerMode = BUZZER_LOW_STOCK_PULSE; // Use same pulse for depleted
                        buzzerPatternStartTime = now;
                        buzzerDuration = 3000;
                        nextBuzzerToggleTime = now + LOW_STOCK_BUZZ_ON_TIME;
                        Serial.println("Triggering depleted stock pulsed buzzer (3s)");
                        warningAlerted[i] = true;
                    }


                    handledEvent[i] = true; // تم التعامل مع حدث الحركة هذا بنجاح (اختيار صحيح)
                }
                // إذا لم يكن الضوء أخضر (اختيار خاطئ)
                else {
                    // **** إجراءات الالتقاط الخاطئ ****
                    Serial.print("Incorrect pick on Station ");
                    Serial.println(i + 1);
                    // تشغيل المصباح الأحمر
                    digitalWrite(LEDS[i][0], HIGH); // أحمر HIGH
                    digitalWrite(LEDS[i][1], LOW);  // أخضر LOW
                    digitalWrite(LEDS[i][2], LOW);  // أزرق LOW

                    // تفعيل نمط الخطأ المستمر
                    currentBuzzerMode = BUZZER_ERROR_CONTINUOUS;
                    buzzerPatternStartTime = now;
                    buzzerDuration = 3000; // تنبيه لمدة 3 ثوانٍ
                    Serial.println("Triggering incorrect pick continuous buzzer (3s)");

                    handledEvent[i] = true; // تم التعامل مع حدث الحركة هذا بنجاح (اختيار خاطئ)
                }
            }
        }
        // معالجة نمط الصفارة الحالي
        handleBuzzerPattern(now);

    } else { // System is NOT active
        // التأكد من أن جميع الـ LEDs مطفأة والصفارة مطفأة عندما يكون النظام غير نشط
        for (int i = 0; i < 3; i++) {
            for (int c = 0; c < 3; c++) digitalWrite(LEDS[i][c], LOW);
            pirLatched[i] = false;
            handledEvent[i] = false;
        }
        digitalWrite(BUZZER_PIN, LOW);
        currentBuzzerMode = BUZZER_OFF; // التأكد من إطفاء الصفارة
        buzzerPatternStartTime = 0;
        buzzerDuration = 0;
        nextBuzzerToggleTime = 0;
    }

    // تأخير بسيط لمنع انهيار النظام
    delay(10);
}