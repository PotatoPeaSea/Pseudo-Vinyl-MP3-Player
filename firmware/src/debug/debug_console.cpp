#include "../config.h"

#include <Arduino.h>
#include "debug_console.h"
#include "../input/input_manager.h"

// ── Command table ───────────────────────────────────────────
// Each command maps a word (and a single-key shortcut) to an event.
struct Command {
    const char *word;       // Full command, e.g. "play"
    char        key;        // Single-char shortcut, e.g. 'p' (0 = none)
    InputEvent  event;
    const char *help;
};

static const Command COMMANDS[] = {
    { "play",  'p', InputEvent::BTN_PLAY,  "PLAY button (play/pause, select in menus)" },
    { "next",  'n', InputEvent::BTN_NEXT,  "NEXT button (next track, next screen)" },
    { "prev",  'b', InputEvent::BTN_PREV,  "PREV button (prev track, prev screen)" },
    { "cw",    '+', InputEvent::ENC_CW,    "Encoder clockwise (vol up, focus next)" },
    { "ccw",   '-', InputEvent::ENC_CCW,   "Encoder counter-clockwise (vol down, focus prev)" },
    { "press", 'e', InputEvent::ENC_PRESS, "Encoder push (back / to Now Playing)" },
    { "mode",  'm', InputEvent::MODE_CYCLE, "Cycle play mode (normal/shuffle/repeat)" },
};
static const int NUM_COMMANDS = sizeof(COMMANDS) / sizeof(COMMANDS[0]);

// Alternate spellings accepted for convenience
struct Alias { const char *word; const char *canonical; };
static const Alias ALIASES[] = {
    { "pause",  "play" },
    { "up",     "cw"   },
    { "down",   "ccw"  },
    { "right",  "cw"   },
    { "left",   "ccw"  },
    { "enter",  "press" },
    { "enc",    "press" },
    { "back",   "press" },
};
static const int NUM_ALIASES = sizeof(ALIASES) / sizeof(ALIASES[0]);

// ── Line buffer ─────────────────────────────────────────────
static char lineBuf[64];
static uint8_t lineLen = 0;

static void printHelp() {
    Serial.println();
    Serial.println("── Debug Console Commands ─────────────────────────");
    for (int i = 0; i < NUM_COMMANDS; i++) {
        Serial.printf("  %-6s (%c)  %s\n", COMMANDS[i].word, COMMANDS[i].key, COMMANDS[i].help);
    }
    Serial.println("  help   (h)  Show this list");
    Serial.println();
    Serial.println("  Repeat with a count: \"cw 5\" turns the encoder 5 clicks.");
    Serial.println("  Shortcuts need Enter too, except + and - which fire instantly.");
    Serial.println("  Aliases: pause=play, up/right=cw, down/left=ccw,");
    Serial.println("           enter/enc/back=press");
    Serial.println("───────────────────────────────────────────────────");
}

static const Command *findCommand(const char *word) {
    for (int i = 0; i < NUM_ALIASES; i++) {
        if (strcmp(word, ALIASES[i].word) == 0) {
            word = ALIASES[i].canonical;
            break;
        }
    }
    for (int i = 0; i < NUM_COMMANDS; i++) {
        if (strcmp(word, COMMANDS[i].word) == 0) return &COMMANDS[i];
    }
    return nullptr;
}

static void fire(const Command *cmd, int count) {
    if (count < 1) count = 1;
    if (count > 50) count = 50;     // Don't flood the 16-slot event queue absurdly
    for (int i = 0; i < count; i++) {
        Input::inject(cmd->event);
    }
    Serial.printf("[Debug] %s x%d\n", cmd->word, count);
}

static void handleLine(char *line) {
    // Lowercase + split into "word [count]"
    for (char *c = line; *c; c++) *c = tolower(*c);

    char *word = strtok(line, " \t");
    if (!word) return;
    char *countStr = strtok(nullptr, " \t");
    int count = countStr ? atoi(countStr) : 1;

    if (strcmp(word, "help") == 0 || strcmp(word, "h") == 0 || strcmp(word, "?") == 0) {
        printHelp();
        return;
    }

    const Command *cmd = findCommand(word);

    // Single-char shortcut typed as a full line (e.g. "p<Enter>")
    if (!cmd && strlen(word) == 1) {
        for (int i = 0; i < NUM_COMMANDS; i++) {
            if (COMMANDS[i].key == word[0]) { cmd = &COMMANDS[i]; break; }
        }
    }

    if (cmd) {
        fire(cmd, count);
    } else {
        Serial.printf("[Debug] Unknown command: \"%s\" — type \"help\"\n", word);
    }
}

// ── Public API ──────────────────────────────────────────────

void DebugConsole::init() {
    Serial.println("[Debug] Serial console active — drive buttons/encoder over serial");
    printHelp();
}

void DebugConsole::poll() {
    while (Serial.available()) {
        char c = Serial.read();

        if (c == '\r') continue;

        if (c == '\n') {
            lineBuf[lineLen] = '\0';
            if (lineLen > 0) handleLine(lineBuf);
            lineLen = 0;
            continue;
        }

        // +/- fire instantly without Enter; letters must be buffered since
        // they may start a command word (e.g. 'p' begins "press" and "play")
        if (lineLen == 0 && (c == '+' || c == '-')) {
            char tmp[2] = { c, '\0' };
            handleLine(tmp);
            continue;
        }

        if (lineLen < sizeof(lineBuf) - 1) {
            lineBuf[lineLen++] = c;
        }
    }
}
