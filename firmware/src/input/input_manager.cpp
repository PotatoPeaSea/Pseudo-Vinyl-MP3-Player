#include "input_manager.h"
#include "../config.h"

// ── Event Queue ─────────────────────────────────────────────
#define EVENT_QUEUE_SIZE 16
static volatile InputEvent eventQueue[EVENT_QUEUE_SIZE];
static volatile uint8_t qHead = 0;
static volatile uint8_t qTail = 0;

static void pushEvent(InputEvent evt) {
    uint8_t next = (qHead + 1) % EVENT_QUEUE_SIZE;
    if (next != qTail) {    // Don't overflow
        eventQueue[qHead] = evt;
        qHead = next;
    }
}

// ── Button Debounce State ───────────────────────────────────
struct ButtonState {
    uint8_t pin;
    InputEvent event;
    bool lastReading;
    bool stableState;
    unsigned long lastChange;
};

static ButtonState buttons[] = {
    { PIN_BTN_PLAY, InputEvent::BTN_PLAY, true, true, 0 },
    { PIN_BTN_NEXT, InputEvent::BTN_NEXT, true, true, 0 },
    { PIN_BTN_PREV, InputEvent::BTN_PREV, true, true, 0 },
    { PIN_ENC_SW,   InputEvent::ENC_PRESS, true, true, 0 },
};
static const int NUM_BUTTONS = sizeof(buttons) / sizeof(buttons[0]);

// ── Rotary Encoder State ────────────────────────────────────
static volatile int encCounter = 0;
static uint8_t encLastState = 0;

static void IRAM_ATTR encISR() {
    uint8_t a = digitalRead(PIN_ENC_A);
    uint8_t b = digitalRead(PIN_ENC_B);
    uint8_t state = (a << 1) | b;

    // Simple gray-code state machine
    static const int8_t encTable[] = {
        0, -1,  1,  0,
        1,  0,  0, -1,
       -1,  0,  0,  1,
        0,  1, -1,  0
    };

    int8_t dir = encTable[(encLastState << 2) | state];
    encCounter += dir;
    encLastState = state;
}

// ── Public API ──────────────────────────────────────────────

void Input::init() {
    // Buttons (active LOW with internal pull-up)
    for (int i = 0; i < NUM_BUTTONS; i++) {
        pinMode(buttons[i].pin, INPUT_PULLUP);
    }

    // Encoder A/B with interrupt
    pinMode(PIN_ENC_A, INPUT_PULLUP);
    pinMode(PIN_ENC_B, INPUT_PULLUP);
    encLastState = (digitalRead(PIN_ENC_A) << 1) | digitalRead(PIN_ENC_B);
    attachInterrupt(digitalPinToInterrupt(PIN_ENC_A), encISR, CHANGE);
    attachInterrupt(digitalPinToInterrupt(PIN_ENC_B), encISR, CHANGE);

    Serial.println("[Input] Buttons + encoder initialized");
}

void Input::poll() {
    unsigned long now = millis();

    // ── Debounce buttons ────────────────────────────────────
    for (int i = 0; i < NUM_BUTTONS; i++) {
        bool reading = digitalRead(buttons[i].pin);

        if (reading != buttons[i].lastReading) {
            buttons[i].lastChange = now;
        }
        buttons[i].lastReading = reading;

        if ((now - buttons[i].lastChange) > DEBOUNCE_MS) {
            // Trigger on falling edge (HIGH -> LOW = button pressed)
            if (!reading && buttons[i].stableState) {
                pushEvent(buttons[i].event);
            }
            buttons[i].stableState = reading;
        }
    }

    // ── Encoder rotation ────────────────────────────────────
    noInterrupts();
    int count = encCounter;
    encCounter = 0;
    interrupts();

    // Encoder typically generates 2 or 4 counts per detent
    if (count >= 2) {
        pushEvent(InputEvent::ENC_CW);
    } else if (count <= -2) {
        pushEvent(InputEvent::ENC_CCW);
    }
}

InputEvent Input::getEvent() {
    if (qTail == qHead) return InputEvent::NONE;
    InputEvent evt = eventQueue[qTail];
    qTail = (qTail + 1) % EVENT_QUEUE_SIZE;
    return evt;
}

bool Input::hasEvent() {
    return qTail != qHead;
}
