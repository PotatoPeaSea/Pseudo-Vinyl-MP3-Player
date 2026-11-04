// Pin continuity test — standalone firmware, built only by [env:pintest].
// Drive each wired GPIO high/low one at a time and probe it with a
// multimeter / LED at the far end of the wire (the display/SD/button side).
//
//   pio run -e pintest -t upload && pio device monitor
//
// Serial commands (115200, send with newline):
//   n / p      next / previous pin (previous pin is released to INPUT)
//   h / l      drive current pin HIGH / LOW
//   b          blink current pin at 1 Hz (any command stops it)
//   r          release current pin and read it (INPUT_PULLUP) — press a
//              button / short to GND and read again to test input wiring
//   a          walk: blink every pin for 3 s in turn
//   <number>   jump to that list index (e.g. "7")
//   ?          print pin list
#ifdef PIN_TEST
#include <Arduino.h>
#include "config.h"

struct TestPin { uint8_t gpio; const char* name; bool inputOnly; };

static const TestPin PINS[] = {
    {PIN_TFT_MOSI, "TFT MOSI/SDA", false},
    {PIN_TFT_SCLK, "TFT SCLK/SCL", false},
    {PIN_TFT_CS,   "TFT CS",       false},
    {PIN_TFT_DC,   "TFT DC",       false},
    {PIN_TFT_RST,  "TFT RST",      false},
    {PIN_SD_CLK,   "SD CLK",       false},
    {PIN_SD_CMD,   "SD CMD",       false},
    {PIN_SD_D0,    "SD D0",        false},
    {PIN_ENC_A,    "ENC A",        false},
    {PIN_ENC_B,    "ENC B",        false},
    {PIN_ENC_SW,   "ENC SW",       false},
    {PIN_BTN_PLAY, "BTN PLAY",     false},
    {PIN_BTN_NEXT, "BTN NEXT",     false},
    {PIN_BTN_PREV, "BTN PREV",     false},
    {PIN_BATT_ADC, "BATT ADC",     true},   // GPIO 34 is input-only
};
static const int N = sizeof(PINS) / sizeof(PINS[0]);

static int cur = 0;
static bool blinking = false;

static void release(int i) { pinMode(PINS[i].gpio, INPUT); }

static void show() {
    Serial.printf("[%d] GPIO %d  %s%s\n", cur, PINS[cur].gpio, PINS[cur].name,
                  PINS[cur].inputOnly ? "  (input-only: use 'r')" : "");
}

static void list() {
    for (int i = 0; i < N; i++)
        Serial.printf("%c %2d: GPIO %2d  %s\n", i == cur ? '>' : ' ', i,
                      PINS[i].gpio, PINS[i].name);
}

static void readPin() {
    release(cur);
    if (PINS[cur].inputOnly) {
        Serial.printf("GPIO %d raw ADC = %d\n", PINS[cur].gpio, analogRead(PINS[cur].gpio));
    } else {
        pinMode(PINS[cur].gpio, INPUT_PULLUP);
        delay(2);
        Serial.printf("GPIO %d (pullup) reads %s\n", PINS[cur].gpio,
                      digitalRead(PINS[cur].gpio) ? "HIGH" : "LOW (shorted to GND / pressed)");
    }
}

static bool driveable() {
    if (PINS[cur].inputOnly) { Serial.println("input-only pin, can't drive"); return false; }
    pinMode(PINS[cur].gpio, OUTPUT);
    return true;
}

static void selectPin(int i) {
    blinking = false;
    release(cur);
    cur = (i + N) % N;
    show();
}

static void walk() {
    for (int i = 0; i < N; i++) {
        selectPin(i);
        if (PINS[i].inputOnly) continue;
        pinMode(PINS[i].gpio, OUTPUT);
        for (int k = 0; k < 6; k++) { digitalWrite(PINS[i].gpio, (k & 1) ? LOW : HIGH); delay(500); }
        release(i);
    }
    Serial.println("walk done");
}

void setup() {
    Serial.begin(115200);
    delay(500);
    for (int i = 0; i < N; i++) release(i);
    Serial.println("\n=== Pin continuity test === ('?' for list)");
    list();
    show();
}

void loop() {
    static uint32_t lastBlink = 0;
    static bool level = false;
    if (blinking && millis() - lastBlink >= 500) {
        lastBlink = millis();
        level = !level;
        digitalWrite(PINS[cur].gpio, level);
    }

    if (!Serial.available()) return;
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    if (cmd.isEmpty()) return;
    blinking = false;

    if (isDigit(cmd[0])) {
        int i = cmd.toInt();
        if (i >= 0 && i < N) selectPin(i); else Serial.println("bad index");
        return;
    }
    switch (cmd[0]) {
        case 'n': selectPin(cur + 1); break;
        case 'p': selectPin(cur - 1); break;
        case 'h': if (driveable()) { digitalWrite(PINS[cur].gpio, HIGH); Serial.println("HIGH (3.3V)"); } break;
        case 'l': if (driveable()) { digitalWrite(PINS[cur].gpio, LOW);  Serial.println("LOW (0V)"); } break;
        case 'b': if (driveable()) { blinking = true; Serial.println("blinking 1 Hz"); } break;
        case 'r': readPin(); break;
        case 'a': walk(); break;
        case '?': list(); break;
        default:  Serial.println("cmds: n p h l b r a ? <index>");
    }
}
#endif
