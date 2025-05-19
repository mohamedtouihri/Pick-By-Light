#include <WiFi.h>
#include <WebServer.h>
#include <TM1637Display.h> // تضمين مكتبة TM1637
#include <vector> // لتسهيل عملية التبديل العشوائي
#include <algorithm> // لـ std::swap

// بيانات شبكة WiFi
#define WIFI_SSID "Wokwi-GUEST" // غيّر هذا إلى اسم شبكتك
#define WIFI_PASSWORD "" // غيّر هذا إلى كلمة مرور شبكتك
// لا حاجة لـ WIFI_CHANNEL غالباً

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
int displayValues[3] = {12, 12, 12}; // يمكن تغيير القيمة الابتدائية هنا

// تتبع حالات النظام
bool ledStates[3] = {false, false, false};      // حالة LED أخضر للمحطة (هل يجب أن يكون أخضر؟)
bool pirLatched[3] = {false, false, false};     // latch لحالة PIR (هل تم اكتشاف حركة منذ آخر إعادة تعيين؟)
bool handledEvent[3] = {false, false, false};   // لمنع معالجة حدث الحركة أكثر من مرة لكل latch
bool warningAlerted[3] = {false, false, false}; // لمنع تكرار تشغيل صفارة المخزون المنخفض لنفس المحطة
bool buzzerState = false;                       // حالة الصفارة (مشغلة فعلياً أم لا)
unsigned long buzzerEndTime = 0;                // وقت انتهاء تشغيل الصفارة الفعلي

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
    buzzerState = false;
    buzzerEndTime = 0;
}

// تفعيل LED أخضر لمحطة محددة وإظهار العداد
void turnGreen(int idx) {
    if (idx < 0 || idx >= 3) return; // تحقق من أن الرقم صالح
    Serial.print("Turning Green: Station ");
    Serial.println(idx+1);
    digitalWrite(LEDS[idx][0], LOW); // إطفاء الأحمر
    digitalWrite(LEDS[idx][1], HIGH); // تشغيل الأخضر
    digitalWrite(LEDS[idx][2], LOW); // إطفاء الأزرق (غير مستخدم)
    ledStates[idx] = true;
    // يتم تحديث العرض في loop أو عند الحاجة، لكن يمكن عرضه هنا أيضاً عند التفعيل
    // displays[idx].showNumberDec(displayValues[idx]);
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
            const cntText = data['DISPLAY' + i]; // This string includes " - Le produit est presque achevé" if low
            cntEl.textContent = cntText; // Display the text as is

            // Apply warning style if the text indicates low stock
            if (cntText.includes(" !!!")) {
                cntEl.classList.remove('counter-style');
                cntEl.classList.add('counter-warning');
            } else {
                cntEl.classList.remove('counter-warning');
                cntEl.classList.add('counter-style');
            }
        }

        // NEW: Function to update the global warning banner
        function updateWarning(data) {
            const warnEl = document.getElementById('warning');
            const depletedStations = data.DEPLETED_STATIONS || []; // Get the array of depleted station names

            if (depletedStations.length === 0) {
                warnEl.classList.add('hidden'); // Hide banner if no stations are depleted
            } else {
                let msg;
            if (depletedStations.length === 3) {
                msg = '⚠️ Attention : Toutes les stations sont presque à court de stock ! Veuillez les réapprovisionner immédiatement.';
            } else if (depletedStations.length === 1) {
                msg = '⚠️ Attention : Le stock est presque vide dans ' + depletedStations[0] + ' ! Veuillez les réapprovisionner immédiatement.';
            } else { // length is 2
                msg = '⚠️ Attention : Le stock est presque vide dans ' + depletedStations.join(' et ') + ' ! Veuillez les réapprovisionner immédiatement.';
            }
                warnEl.textContent = msg; // Set the dynamic message
                warnEl.classList.remove('hidden'); // Show the banner
            }
        }

        // NEW: Function to update the buzzer UI status based on warnings or actual state
        function updateBuzzerUI(data) {
            const buzzEl = document.getElementById('buzzerState');
            // Check if any counter is currently showing the warning style (indicating low stock)
            const anyCounterWarning = document.querySelectorAll('.counter.counter-warning').length > 0;

            if (anyCounterWarning) {
                // If any counter shows warning, set buzzer status to Attention
                buzzEl.className = 'value buzzer-attention';
                buzzEl.textContent = '🔔 Attention !'; // Or any specific warning text
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
    std::vector<String> depletedStationsList;

    // Build station data (LED, PIR, DISPLAY) and find depleted stations
    for (int i = 0; i < 3; i++) {
        bool greenOn = digitalRead(LEDS[i][1]) == HIGH;
        bool redOn   = digitalRead(LEDS[i][0]) == HIGH;
        String led = greenOn ? "Green" : (redOn ? "Red" : "En attente");
        // حالة PIR تعتمد على pirLatched للدلالة على اكتشاف حركة منذ آخر إعادة تعيين
        String pir = pirLatched[i] ? "DETECTED" : "NONE";
        String cnt = String(displayValues[i]);

        // Keep adding the text for JS to detect low stock
        if (displayValues[i] <= 10) {
            cnt += " !!!"; // Add warning text
            depletedStationsList.push_back("\"Station " + String(i+1) + "\""); // Add to list for the banner
        }

        json += "\"LED" + String(i+1) + "\":\"" + led + "\",";
        json += "\"PIR" + String(i+1) + "\":\"" + pir + "\",";
        json += "\"DISPLAY" + String(i+1) + "\":\"" + cnt + "\""; // Enclose string in quotes
        if (i < 2) json += ",";
    }

    // Add DEPLETED_STATIONS array to JSON
    json += ",\"DEPLETED_STATIONS\":[";
    for(size_t i = 0; i < depletedStationsList.size(); ++i) {
        json += depletedStationsList[i];
        if (i < depletedStationsList.size() - 1) json += ",";
    }
    json += "]";

    // Add BUZZER state to JSON
    json += ",\"BUZZER\":\"" + String(buzzerState ? "ON" : "OFF") + "\""; // Add buzzer state

    json += "}"; // Close JSON object
    server.send(200, "application/json", json);
}

// اختيار محطتين عشوائيًا وبدء الجولة
void handleRandomize() {
    allLEDsOff(); // إعادة تعيين كل شيء أولاً (عدا حالة warningAlerted إذا لم تغيرها allLEDsOff)

    std::vector<int> idxs = {0, 1, 2};
    // خلط المتجهات عشوائياً (Fisher-Yates Shuffle)
    for (int i = idxs.size() - 1; i > 0; i--) {
        int j = random(0, i + 1);
        std::swap(idxs[i], idxs[j]); // استخدم std::swap
    }

    // اختيار أول عنصرين لتشغيل الضوء الأخضر فيهما (يجب أن تكون محطات مختلفة الآن)
    turnGreen(idxs[0]);
    turnGreen(idxs[1]);
    // المحطة الثالثة (idxs[2]) ستبقى بحالة "En attente"

    Serial.print("LEDs ON: Station ");
    Serial.print(idxs[0]+1);
    Serial.print(" & Station ");
    Serial.println(idxs[1]+1);

    server.send(200, "text/plain", "Randomization complete. Stations " + String(idxs[0]+1) + " and " + String(idxs[1]+1) + " are green.");
}


void setup() {
    Serial.begin(115200);
    // تهيئة البذور العشوائية بشكل جيد
    randomSeed(analogRead(0));
    // يمكن أيضاً استخدام الوقت إذا كان متوفراً من NTP

    // تهيئة الأطراف والحالات الابتدائية
    for (int i = 0; i < 3; i++) {
        for (int c = 0; c < 3; c++) pinMode(LEDS[i][c], OUTPUT);
        pinMode(PIR_PINS[i], INPUT); // PIR pins as input
        // PIR GPIOs 34, 35, 36, 39 are Input Only on ESP32, no need for pinMode(..., INPUT) if using digitalRead correctly.
        // However, explicitly setting INPUT mode is good practice for clarity and compatibility.
        displays[i].setBrightness(0x0f); // تعيين أقصى سطوع
        displays[i].showNumberDec(displayValues[i]);
        pirLatched[i] = false;
        handledEvent[i] = false;
        warningAlerted[i] = false; // Initialize the new flag
    }
    pinMode(BUZZER_PIN, OUTPUT); // Buzzer pin as output
    digitalWrite(BUZZER_PIN, LOW); // Ensure buzzer is off initially

    allLEDsOff(); // التأكد من أن جميع الـ LEDs والصفارة مطفأة في البداية

    Serial.print("Connecting to ");
    Serial.println(WIFI_SSID);
    //WiFi.begin(WIFI_SSID, WIFI_PASSWORD, WIFI_CHANNEL); // Channel is often not needed
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
    server.on("/randomize", handleRandomize); // ربط مسار /randomize بالدالة المخصصة

    server.begin(); // بدء تشغيل خادم الويب
    Serial.println("HTTP server started");
}

void loop() {
    unsigned long now = millis(); // الحصول على الوقت الحالي
    server.handleClient(); // معالجة طلبات خادم الويب باستمرار

    // حلقة لمعالجة كل محطة على حدة
    for (int i = 0; i < 3; i++) {
        // اقرأ حالة حساس PIR للمحطة الحالية
        bool pir = digitalRead(PIR_PINS[i]) == HIGH;

        // منطق PIR latch: إذا تم اكتشاف حركة (pir == true) ولم يتم تسجيلها بعد (!pirLatched[i])، قم بتثبيت الحالة
        // هذا يضمن أن حدث الحركة القصير يتم التقاطه ومعالجته مرة واحدة.
        if (pir && !pirLatched[i]) {
            pirLatched[i] = true;
            Serial.print("Motion detected on Station ");
            Serial.println(i+1);
        }

        // معالجة الحدث فقط إذا تم تثبيت PIR (pirLatched[i] == true) ولم يتم التعامل معه بعد (!handledEvent[i])
        if (pirLatched[i] && !handledEvent[i]) {
            // تحقق مما إذا كان الضوء أخضر (اختيار صحيح متوقع لهذه المحطة)
            if (ledStates[i]) { // الشرط ledStates[i] == true هو نفسه ledStates[i]
                // **** إجراءات الالتقاط الصحيح ****
                displayValues[i]--; // إنقاص العداد
                displays[i].showNumberDec(displayValues[i]); // عرض القيمة الجديدة على شاشة TM1637
                Serial.print("Correct pick on Station ");
                Serial.print(i+1);
                Serial.print(". Counter is now: ");
                Serial.println(displayValues[i]);

                // ** NEW: تشغيل الصفارة إذا أصبح المخزون منخفضًا (<= 10) ولم يتم التنبيه له من قبل **
                // يتم تشغيل هذا التنبيه فقط مرة واحدة لكل محطة بعد انخفاض العداد إلى 10 أو أقل
                if (displayValues[i] <= 10 && !warningAlerted[i]) {
                     Serial.print("Low stock condition met for Station ");
                     Serial.println(i+1);
                    if (!buzzerState) { // تشغيل الصفارة فقط إذا لم تكن مشغلة بالفعل من تنبيه آخر
                        Serial.println("Triggering low stock buzzer (3s)");
                        digitalWrite(BUZZER_PIN, HIGH);
                        buzzerState = true;
                        buzzerEndTime = now + 3000; // تشغيل الصفارة لمدة 3 ثوانٍ كما هو مطلوب للتنبيه
                    } else {
                        Serial.println("Buzzer already ON, extending timer if needed.");
                        // يمكن اختيار تمديد الوقت إذا كانت الصفارة مشغلة بالفعل
                        // buzzerEndTime = max(buzzerEndTime, now + 3000);
                    }
                    warningAlerted[i] = true; // تم وضع علامة على المحطة بأنها أطلقت تنبيه المخزون المنخفض
                }

                handledEvent[i] = true; // تم التعامل مع حدث الحركة هذا بنجاح (اختيار صحيح)
            }
            // إذا لم يكن الضوء أخضر (اختيار خاطئ)
            else {
                // **** إجراءات الالتقاط الخاطئ ****
                Serial.print("Incorrect pick on Station ");
                Serial.println(i+1);
                // تشغيل المصباح الأحمر (إذا لم يكن مشتعل بالفعل)
                // تأكد من إطفاء الأخضر والأزرق
                digitalWrite(LEDS[i][0], HIGH); // أحمر HIGH
                digitalWrite(LEDS[i][1], LOW);  // أخضر LOW
                digitalWrite(LEDS[i][2], LOW);  // أزرق LOW

                // تشغيل الصفارة (إذا لم تكن مشغلة بالفعل)
                if (!buzzerState) {
                     Serial.println("Triggering incorrect pick buzzer (3s)");
                     digitalWrite(BUZZER_PIN, HIGH);
                     buzzerState = true;
                     buzzerEndTime = now + 3000; // تشغيل الصفارة لمدة 3 ثوانٍ لتنبيه الخطأ
                } else {
                    Serial.println("Buzzer already ON, extending timer if needed.");
                    // يمكن اختيار تمديد الوقت إذا كانت الصفارة مشغلة بالفعل
                    // buzzerEndTime = max(buzzerEndTime, now + 3000);
                }

                handledEvent[i] = true; // تم التعامل مع حدث الحركة هذا بنجاح (اختيار خاطئ)
            }
        }
        // ملاحظة: إعادة تعيين pirLatched و handledEvent يتم عند بدء جولة عشوائية جديدة (دالة allLEDsOff)
        // إعادة تعيين warningAlerted يتم أيضاً في allLEDsOff إذا قمت بإزالة التعليق عن السطر
    }

    // إيقاف الصفارة بعد انتهاء مدتها المحددة
    if (buzzerState && now >= buzzerEndTime) {
        Serial.println("Buzzer timer ended, turning OFF.");
        digitalWrite(BUZZER_PIN, LOW);
        buzzerState = false;
        buzzerEndTime = 0; // إعادة تعيين وقت الانتهاء
    }

    // تأخير بسيط لمنع انهيار النظام (اختياري لكن موصى به)
    delay(10);
}