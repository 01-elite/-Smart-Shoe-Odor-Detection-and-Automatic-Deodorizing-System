#define BLYNK_TEMPLATE_ID "TMPL3P_oT6QJh"
#define BLYNK_TEMPLATE_NAME "Smart Shoe AIoT"
#define BLYNK_AUTH_TOKEN "br_L6dST6GvMiVaZmO0QO8Pl27yzFW1y"

#define BLYNK_PRINT Serial

#include <WiFi.h>
#include <BlynkSimpleEsp32.h>
#include <DHT.h>

// ---------- WIFI ----------
char ssid[] = "Galaxy";
char pass[] = "12345678";

// ---------- PINS ----------
#define DHTPIN   27
#define DHTTYPE  DHT11
#define MQ_PIN   34     // MQ135 AO through the 10k/22k divider
#define BTN_PIN  25     // module PWM pad (presses the mister button)
#define LED_PIN  2      // built-in LED, ON while mister is ON

// ---------- YOUR LIMITS ----------
const float HUM_HIGH = 60.0;    // humidity ABOVE this = high
const int   AIR_HIGH = 100;     // air rise ABOVE this = high

const unsigned long COUNTDOWN_MS = 10000;   // 30 s warning before spray
const unsigned long MIST_TIME    = 4000;    // mister ON for 3 s
const unsigned long REPEAT_GAP   = 20000;  // 3 min before it may spray again
// ---------------------------------

DHT dht(DHTPIN, DHTTYPE);

float baseAir = 0;
bool  mqValid = true;
bool  misterOn = false;

bool counting = false;
unsigned long countStart = 0;
unsigned long lastMist = 0;

void pressButton() {
  digitalWrite(BTN_PIN, HIGH);
  delay(400);
  digitalWrite(BTN_PIN, LOW);
}

void misterSet(bool on) {
  if (on == misterOn) return;
  pressButton();
  misterOn = on;
  digitalWrite(LED_PIN, on ? HIGH : LOW);
  Serial.println(on ? ">>> MISTER ON (button pressed)" : ">>> MISTER OFF (button pressed)");
}

int readAir() {
  long s = 0;
  for (int i = 0; i < 10; i++) {
    s += analogRead(MQ_PIN);
    delay(5);
  }
  return s / 10;
}
BLYNK_WRITE(V6) {
  int buttonState = param.asInt();

  if (buttonState == 1) {
    misterSet(true);
  } else {
    misterSet(false);
  }
}
void setup() {
  Serial.begin(115200);

  pinMode(BTN_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(BTN_PIN, LOW);
  digitalWrite(LED_PIN, LOW);

  analogSetPinAttenuation(MQ_PIN, ADC_11db);
  dht.begin();
  delay(2000);

  Serial.println("Learning clean air for 60 s... keep sensor away from shoes");

  for (int i = 1; i <= 30; i++) {
    baseAir += (readAir() - baseAir) / i;
    delay(2000);
  }

  mqValid = (baseAir > 20 && baseAir < 4000);

  Serial.printf("Clean air value: %.0f  -> MQ135 %s\n",
                baseAir,
                mqValid ? "OK" : "NOT WORKING (check divider)");

  Serial.println("Now place the sensors in/near the shoe.");

  // Connect to Blynk after sensor calibration
  Blynk.begin(BLYNK_AUTH_TOKEN, ssid, pass);
}

void loop() {
  Blynk.run();

  float h = dht.readHumidity();
  float t = dht.readTemperature();
  int air = readAir();

  if (isnan(h) || isnan(t)) {
    Serial.println("DHT read failed, check wiring");
    delay(2000);
    return;
  }

  int rise = mqValid ? (air - (int)baseAir) : 0;

  bool humidHigh = (h > HUM_HIGH);
  bool airHigh   = mqValid && (rise > AIR_HIGH);
  bool high      = humidHigh || airHigh;     // either one is enough

  // ---------- BLYNK DASHBOARD UPDATES ----------
  Blynk.virtualWrite(V0, t);                 // Temperature (Double)
  Blynk.virtualWrite(V1, h);                 // Humidity (change datastream to Double)
  Blynk.virtualWrite(V2, air);               // Raw MQ135 reading
  Blynk.virtualWrite(V3, rise);              // Air rise above baseline
  Blynk.virtualWrite(V4, high ? 1 : 0);      // Risk: 1 = high, 0 = normal
  Blynk.virtualWrite(V5, high ? "High" : "Stable"); // Trend
  Blynk.virtualWrite(V6, misterOn ? 1 : 0); // Atomizer: 1 = ON, 0 = OFF
  // ---------------------------------------------

  Serial.printf("T:%.1fC | H:%.1f%% | Air:%d | Clean:%.0f | Rise:%d | humidHigh:%d airHigh:%d\n",
                t, h, air, baseAir, rise, humidHigh, airHigh);

  bool gapOver = (lastMist == 0) || (millis() - lastMist >= REPEAT_GAP);

  if (!counting) {
    if (high && gapOver) {
      counting = true;
      countStart = millis();
      Serial.println("HIGH detected -> 30 s countdown started");
    } else if (high && !gapOver) {
      Serial.printf("Still high. Next spray possible in %lu s\n",
                    (REPEAT_GAP - (millis() - lastMist)) / 1000);
    }
  } else {
    if (!high) {
      counting = false;
      Serial.println("Readings dropped -> countdown CANCELLED");
    } else {
      unsigned long elapsed = millis() - countStart;

      if (elapsed >= COUNTDOWN_MS) {
        Serial.println("Spraying now!");
        misterSet(true);
        delay(MIST_TIME);
        misterSet(false);
        lastMist = millis();
        counting = false;
      } else {
        Serial.printf("Going to spray in %lu s...\n",
                      (COUNTDOWN_MS - elapsed) / 1000);
      }
    }
  }

  Blynk.run();
  delay(2000);
}