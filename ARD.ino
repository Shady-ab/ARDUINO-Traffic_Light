/*
  Traffic Lights Simulation - יציב: LDR + טמפ' עם סינון + תרשים זרימה
  - FSM לא חוסם (millis)
  - LDR לעמעום חלק (1s) לרכב
  - מצב חום גבוה עם סף יציב (1s) ורק אם החיישן נראה מחובר
  - כפתור הולכי-רגל מגיב מיידית כשהרכב ירוק קבוע (עם זיכרון בקשה)
  - Serial לדיבוג ותרשים זרימה
*/

#include <Arduino.h>

// ---------------------- הגדרות פינים
#define PIN_CAR_RED     9
#define PIN_CAR_YELLOW 10
#define PIN_CAR_GREEN  11
#define PIN_PED_RED     5
#define PIN_PED_GREEN   3
#define PIN_BUTTON      2

#define PIN_LDR        A0
#define PIN_TEMP       A5

// ---------------------- פרמטרים FSM
#define DEFAULT_GREEN_TIME_MS           5000UL
#define DEFAULT_PED_GREEN_TIME_MS       5000UL
#define YELLOW_TO_GREEN_DELAY_MS        2000UL
#define YELLOW_ONLY_TO_RED_DELAY_MS     2000UL
#define RED_TO_PED_GREEN_DELAY_MS       2000UL
#define PED_RED_TO_CAR_GREEN_DELAY_MS   2000UL
#define GREEN_BLINK_HALF_PERIOD_MS       500UL
#define GREEN_BLINK_CYCLES                  3

// ---------------------- LDR (עמעום)
#define LDR_THRESHOLD_RAW                600
#define DIM_PERCENT                       80
#define BRIGHTNESS_TRANSITION_MS       100UL
#define LDR_DARK_LOW                       1

// ---------------------- טמפרטורה (מצב חם)
#define HOT_MODE_ENABLED                   1
#define TEMP_THRESHOLD_C             30.0
#define HOT_DEBOUNCE_MS                 1000UL

#define TEMP_SENSOR_IS_LM35

#define TEMP_RAW_MIN_CONNECTED 100
#define TEMP_RAW_MAX_CONNECTED 900

// ---------------------- מצבים
enum Mode  { MODE_NORMAL, MODE_HOT };
enum Phase {
  PH_INIT,
  PH_PEDS_GREEN,
  PH_WAIT_PED_RED_BEFORE_CAR_GREEN,
  PH_CAR_RED_YELLOW_TO_GREEN_WAIT,
  PH_CAR_GREEN_STEADY,
  PH_CAR_GREEN_BLINKING,
  PH_CAR_YELLOW_ONLY_WAIT,
  PH_CAR_RED_ONLY,
  PH_WAIT_AFTER_CAR_RED_BEFORE_PED_GREEN
};

Mode          mode_ = MODE_NORMAL;
Phase         phase_ = PH_INIT;

unsigned long tPhaseStart = 0;
unsigned long tLastBlinkToggle = 0;
bool          greenBlinkOn = false;
int           greenBlinkDone = 0;

unsigned long carGreenStart = 0;
unsigned long pedGreenStart = 0;

// ---------------------- לחצן הולכי-רגל
bool pedRequest = false;  // זוכר בקשה של הולכי רגל
bool lastButtonState = HIGH;
unsigned long lastDebounce = 0;
const unsigned long debounceDelay = 50;

void updateButton() {
  bool reading = digitalRead(PIN_BUTTON);
  if (reading != lastButtonState) {
    lastDebounce = millis();
  }
  if ((millis() - lastDebounce) > debounceDelay) {
    if (reading == LOW && phase_ ==PH_CAR_GREEN_STEADY) {
      pedRequest = true; // בקשה נרשמה
      Serial.print(F("BUTTON: request at "));
      Serial.print(millis());
      Serial.println(F(" ms"));
    }
  }
  lastButtonState = reading;
}

// ---------------------- עמעום לפי LDR
float currentBrightness = 1.0f;
float targetBrightness  = 1.0f;
unsigned long tLastBrightnessUpdate = 0;

int lastLightRaw = 0; // לשמירה לצורך הדפסה

void updateBrightnessTargetFromLDR() {
  int lightRaw = analogRead(PIN_LDR);
  lastLightRaw = lightRaw;
#if LDR_DARK_LOW
  if (lightRaw < LDR_THRESHOLD_RAW) targetBrightness = (100 - DIM_PERCENT) / 100.0f;
  else                               targetBrightness = 1.0f;
#else
  if (lightRaw > LDR_THRESHOLD_RAW) targetBrightness = (100 - DIM_PERCENT) / 100.0f;
  else                               targetBrightness = 1.0f;
#endif
}




void updateBrightnessSmooth() {
  // מעבר מיידי – בלי פייד
  currentBrightness = targetBrightness;

  // מצב חם קודם לכל
  if (mode_ == MODE_HOT) {
    setPedsRedOnly();                          // רגליים אדום קבוע
    if (greenBlinkOn) setCarsYellowOnly();     // רכב צהוב מהבהב
    else               allCarOff();            // מכבה את אורות הרכב בלבד
    return;
  }

  // FSM רגיל – מרענן לפי הפאזה
  switch (phase_) {
    case PH_INIT:
      setPedsGreenOnly(); setCarsRedOnly();
      break;

    case PH_PEDS_GREEN:
      setPedsGreenOnly(); setCarsRedOnly();
      break;

    case PH_WAIT_PED_RED_BEFORE_CAR_GREEN:
      setPedsRedOnly(); setCarsRedOnly();
      break;

    case PH_CAR_RED_YELLOW_TO_GREEN_WAIT:
      setPedsRedOnly(); setCarsRedYellow();
      break;

    case PH_CAR_GREEN_STEADY:
      setPedsRedOnly(); setCarsGreenOnly();
      break;

    case PH_CAR_GREEN_BLINKING:
      setPedsRedOnly();
      if (greenBlinkOn) setCarsGreenOnly();
      else              allCarOff();          // מכבה את אורות הרכב בלבד
      break;

    case PH_CAR_YELLOW_ONLY_WAIT:
      setPedsRedOnly(); setCarsYellowOnly();
      break;

    case PH_CAR_RED_ONLY:
      setPedsRedOnly(); setCarsRedOnly();
      break;

    case PH_WAIT_AFTER_CAR_RED_BEFORE_PED_GREEN:
      setPedsRedOnly(); setCarsRedOnly();
      break;
  }
}




// ---------------------- פלטי LED
// inline void pedLedOn(uint8_t p)  { analogWrite(p, 255); }
inline void pedLedOn(uint8_t p) {
  int scaled = (int)(255 * currentBrightness);
  if (scaled < 0) scaled = 0;
  if (scaled > 255) scaled = 255;
  analogWrite(p, scaled);
}


inline void pedLedOff(uint8_t p) { analogWrite(p, 0);   }
void writeCarLedPWM(uint8_t p, uint8_t v) {
  int scaled = (int)(v * currentBrightness);
  if (scaled < 0)   scaled = 0;
  if (scaled > 255) scaled = 255;
  analogWrite(p, scaled);
}
inline void carLedOn(uint8_t p)  { writeCarLedPWM(p, 255); }
inline void carLedOff(uint8_t p) { writeCarLedPWM(p, 0);   }

void allCarOff() { carLedOff(PIN_CAR_RED); carLedOff(PIN_CAR_YELLOW); carLedOff(PIN_CAR_GREEN); }
void allPedOff() { pedLedOff(PIN_PED_RED); pedLedOff(PIN_PED_GREEN); }
void setCarsRedOnly()    { carLedOn(PIN_CAR_RED);    carLedOff(PIN_CAR_YELLOW); carLedOff(PIN_CAR_GREEN); }
void setCarsYellowOnly() { carLedOff(PIN_CAR_RED);   carLedOn(PIN_CAR_YELLOW);  carLedOff(PIN_CAR_GREEN); }
void setCarsRedYellow()  { carLedOn(PIN_CAR_RED);    carLedOn(PIN_CAR_YELLOW);  carLedOff(PIN_CAR_GREEN); }
void setCarsGreenOnly()  { carLedOff(PIN_CAR_RED);   carLedOff(PIN_CAR_YELLOW); carLedOn(PIN_CAR_GREEN);  }
void setPedsRedOnly()    { pedLedOn(PIN_PED_RED);    pedLedOff(PIN_PED_GREEN); }
void setPedsGreenOnly()  { pedLedOff(PIN_PED_RED);   pedLedOn(PIN_PED_GREEN);  }

float readTempC_rawAware(int* outRaw) {
  int raw = analogRead(PIN_TEMP);             // קריאה אנלוגית (0–1023)
  if (outRaw) *outRaw = raw;
  float millivolts = (raw * 5000.0) / 1023.0; // ממירים ל-mV (5V רפרנס)
  return millivolts / 10.0;                   // LM35: כל 10mV = 1°C
}



// ---------------------- HOT MODE
bool hotAbove = false;
unsigned long hotSince = 0;
bool coolAbove = false;
unsigned long coolSince = 0;

// === PRINT TIMERS ===
unsigned long lastTempPrint = 0;
const unsigned long TEMP_PRINT_INTERVAL = 2000; // 2 seconds
unsigned long lastLdrPrint = 0;
const unsigned long LDR_PRINT_INTERVAL = 2000; // 2 seconds

void enterHotMode() {
  mode_ = MODE_HOT;
  setPedsRedOnly();
  setCarsYellowOnly();
  tLastBlinkToggle = millis();
  greenBlinkOn = true;
  Serial.println(F("FLOW: Enter HOT mode -> Yellow blink + Ped Red"));
}

void exitHotModeToInitial() {
  mode_ = MODE_NORMAL;
  phase_ = PH_INIT;
  tPhaseStart = millis();
  setPedsGreenOnly();
  setCarsRedOnly();
  pedGreenStart = tPhaseStart;
  Serial.println(F("FLOW: Exit HOT mode -> Back to Init (Ped Green, Car Red)"));
}

// ---------------------- SETUP
void setup() {
  pinMode(PIN_CAR_RED, OUTPUT);
  pinMode(PIN_CAR_YELLOW, OUTPUT);
  pinMode(PIN_CAR_GREEN, OUTPUT);
  pinMode(PIN_PED_RED, OUTPUT);
  pinMode(PIN_PED_GREEN, OUTPUT);
  pinMode(PIN_BUTTON, INPUT_PULLUP);

  allCarOff(); allPedOff();
  setPedsGreenOnly(); setCarsRedOnly();
  mode_ = MODE_NORMAL; phase_ = PH_PEDS_GREEN;
  tPhaseStart = millis(); pedGreenStart = tPhaseStart;
  tLastBrightnessUpdate = millis();

  Serial.begin(9600);
  Serial.println(F("=== Traffic Lights Stable (LDR+Temp+Flow Debug) ==="));
}

// ---------------------- LOOP
void loop() {
  unsigned long now = millis();

  // עדכונים
  updateButton();
  updateBrightnessTargetFromLDR();
  updateBrightnessSmooth();

  float tempC = 0.0;
  int tempRaw = 0;
#if HOT_MODE_ENABLED
  int tempx;
  tempC = readTempC_rawAware(&tempRaw);

  // === TEMP PRINT (every 2s) ===
  if (now - lastTempPrint >= TEMP_PRINT_INTERVAL) {
    lastTempPrint = now;
    Serial.print(F("TEMP: "));
    Serial.print(tempC);
    tempx=tempC;
    Serial.println(F(" C"));
  }

  // === LDR PRINT (every 2s) ===
  if (now - lastLdrPrint >= LDR_PRINT_INTERVAL) {
    lastLdrPrint = now;
    Serial.print(F("LDR raw: "));
    Serial.print(lastLightRaw);
#if LDR_DARK_LOW
    if (lastLightRaw < LDR_THRESHOLD_RAW) Serial.println(F(" -> DIM"));
    else                                  Serial.println(F(" -> BRIGHT"));
#else
    if (lastLightRaw > LDR_THRESHOLD_RAW) Serial.println(F(" -> DIM"));
    else                                  Serial.println(F(" -> BRIGHT"));
#endif
  }

  if (mode_ == MODE_HOT) {
    setPedsRedOnly();
    if (now - tLastBlinkToggle >= 1000UL) {
      tLastBlinkToggle = now; greenBlinkOn = !greenBlinkOn;
    }
    if (greenBlinkOn) setCarsYellowOnly(); else allCarOff();

    if (tempC < TEMP_THRESHOLD_C) {
      if (!coolAbove) { coolAbove = true; coolSince = now; }
      if (coolAbove && (now - coolSince >= HOT_DEBOUNCE_MS)) {
        exitHotModeToInitial();
        coolAbove = false; hotAbove = false;
      }
    } else {
      coolAbove = false;
    }
    return;
  } 
  else {
    if (tempx >= TEMP_THRESHOLD_C) {
      
      if (!hotAbove) { hotAbove = true; hotSince = now; 
        enterHotMode();
        coolAbove = false;
        return;
      }
    } else {
      hotAbove = false;
      
    }
  }
#endif

  // ---------- FSM רגיל ----------
  switch (phase_) {
    case PH_INIT:
      setPedsGreenOnly(); setCarsRedOnly();
      phase_ = PH_PEDS_GREEN; tPhaseStart = now; pedGreenStart = now;
      Serial.println(F("FLOW: Init -> Ped Green, Car Red"));
      break;

    case PH_PEDS_GREEN:
      if (now - pedGreenStart >= DEFAULT_PED_GREEN_TIME_MS) {
        setPedsRedOnly();
        phase_ = PH_WAIT_PED_RED_BEFORE_CAR_GREEN; tPhaseStart = now;
        Serial.println(F("FLOW: Ped Green -> Ped Red (delay before Car Green)"));
      }
      break;

    case PH_WAIT_PED_RED_BEFORE_CAR_GREEN:
      if (now - tPhaseStart >= PED_RED_TO_CAR_GREEN_DELAY_MS) {
        setCarsRedYellow();
        phase_ = PH_CAR_RED_YELLOW_TO_GREEN_WAIT; tPhaseStart = now;
        Serial.println(F("FLOW: Ped Red -> Car Red+Yellow"));
      }
      break;

    case PH_CAR_RED_YELLOW_TO_GREEN_WAIT:
      if (now - tPhaseStart >= YELLOW_TO_GREEN_DELAY_MS) {
        setCarsGreenOnly();
        phase_ = PH_CAR_GREEN_STEADY; carGreenStart = now;
        Serial.println(F("FLOW: Car Red+Yellow -> Car Green steady"));
      }
      break;

    case PH_CAR_GREEN_STEADY: {
      bool timeUp  = (now - carGreenStart >= DEFAULT_GREEN_TIME_MS);
      if (timeUp || pedRequest) {
        pedRequest = false; // מאפס בקשה אחרי שימוש
        phase_ = PH_CAR_GREEN_BLINKING;
        tPhaseStart = now; tLastBlinkToggle = now;
        greenBlinkOn = false; greenBlinkDone = 0;
        Serial.println(F("FLOW: Car Green steady -> Blinking x3"));
      }
      break;
    }

    case PH_CAR_GREEN_BLINKING:
      if (now - tLastBlinkToggle >= GREEN_BLINK_HALF_PERIOD_MS) {
        tLastBlinkToggle = now; greenBlinkOn = !greenBlinkOn;
        if (greenBlinkOn) greenBlinkDone++;
      }
      if (greenBlinkOn) setCarsGreenOnly();
      else allCarOff();
      if (greenBlinkDone >= GREEN_BLINK_CYCLES) {
        setCarsYellowOnly(); phase_ = PH_CAR_YELLOW_ONLY_WAIT; tPhaseStart = now;
        Serial.println(F("FLOW: Car Green blink -> Car Yellow"));
      }
      break;

    case PH_CAR_YELLOW_ONLY_WAIT:
      if (now - tPhaseStart >= YELLOW_ONLY_TO_RED_DELAY_MS) {
        setCarsRedOnly(); phase_ = PH_CAR_RED_ONLY; tPhaseStart = now;
        Serial.println(F("FLOW: Car Yellow -> Car Red only"));
      }
      break;

    case PH_CAR_RED_ONLY:
      phase_ = PH_WAIT_AFTER_CAR_RED_BEFORE_PED_GREEN; tPhaseStart = now;
      Serial.println(F("FLOW: Car Red only -> wait before Ped Green"));
      break;

    case PH_WAIT_AFTER_CAR_RED_BEFORE_PED_GREEN:
      if (now - tPhaseStart >= RED_TO_PED_GREEN_DELAY_MS) {
        setPedsGreenOnly(); phase_ = PH_PEDS_GREEN; pedGreenStart = now;
        Serial.println(F("FLOW: Car Red -> Ped Green"));
      }
      break;
  }
}
