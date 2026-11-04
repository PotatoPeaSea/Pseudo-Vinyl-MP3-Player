// Display smoke test — standalone firmware, built only by [env:disptest].
// Talks to the GC9A01 through TFT_eSPI alone (no LVGL, SD, BT or input),
// so it works with only the display wired. Uses the same TFT_eSPI pin
// flags as the main firmware (platformio.ini).
//
//   pio run -e disptest -t upload && pio device monitor
//
// Loops: solid red / green / blue / white fills (1 s each), then a test
// card (colour bars, rings, crosshair, text, frame counter) for 5 s.
#ifdef DISPLAY_TEST
#include <Arduino.h>
#include <TFT_eSPI.h>

static TFT_eSPI tft;

static void fill(uint16_t c, const char* name) {
    Serial.printf("[DispTest] fill %s\n", name);
    tft.fillScreen(c);
    delay(1000);
}

static void testCard(uint32_t loop) {
    Serial.println("[DispTest] test card");
    tft.fillScreen(TFT_BLACK);

    // Colour bars across the middle band
    const uint16_t bars[] = {TFT_WHITE, TFT_YELLOW, TFT_CYAN, TFT_GREEN,
                             TFT_MAGENTA, TFT_RED, TFT_BLUE};
    const int n = sizeof(bars) / sizeof(bars[0]);
    const int w = 240 / n + 1;
    for (int i = 0; i < n; i++) tft.fillRect(i * w, 90, w, 60, bars[i]);

    // Concentric rings — the round panel's edge should just clip the outer one
    tft.drawCircle(120, 120, 119, TFT_WHITE);
    tft.drawCircle(120, 120, 100, TFT_ORANGE);
    tft.drawCircle(120, 120, 20, TFT_WHITE);
    tft.drawFastHLine(0, 120, 240, TFT_DARKGREY);
    tft.drawFastVLine(120, 0, 240, TFT_DARKGREY);

    // "TOP" marker shows orientation
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString("TOP", 120, 30, 4);
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.drawString("Pseudo Vinyl", 120, 62, 2);
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.drawString("loop " + String(loop), 120, 180, 4);
    tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
    tft.drawString("display test", 120, 208, 2);

    delay(5000);
}

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println("\n[DispTest] init GC9A01...");
    tft.init();
    tft.setRotation(0);
    Serial.println("[DispTest] init done");
}

void loop() {
    static uint32_t n = 0;
    fill(TFT_RED, "red");
    fill(TFT_GREEN, "green");
    fill(TFT_BLUE, "blue");
    fill(TFT_WHITE, "white");
    testCard(++n);
}
#endif
