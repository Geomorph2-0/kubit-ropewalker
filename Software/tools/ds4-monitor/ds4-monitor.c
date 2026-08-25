/*
 * ds4-monitor.c — read a DualShock 4 (or DualSense, or most other pads) and
 * print its live state to the console. Runs on the PC. No microcontroller.
 *
 * Uses SDL2's GameController API, which carries a built-in mapping database.
 * That is the reason for the dependency: raw HID would mean decoding report
 * bytes yourself and handling the v1/v2 pad differences, whereas SDL already
 * knows this controller and hands back named buttons.
 *
 * BUILD
 *   Linux    sudo apt install libsdl2-dev
 *            gcc ds4-monitor.c -o ds4-monitor $(sdl2-config --cflags --libs) -lm
 *
 *   macOS    brew install sdl2
 *            gcc ds4-monitor.c -o ds4-monitor $(sdl2-config --cflags --libs) -lm
 *
 *   Windows  (MSYS2 / MinGW-w64)
 *            pacman -S mingw-w64-x86_64-SDL2
 *            gcc ds4-monitor.c -o ds4-monitor.exe -lmingw32 -lSDL2main -lSDL2
 *
 * RUN
 *   ./ds4-monitor            live panel, redraws in place
 *   ./ds4-monitor --events   plain scrolling log, one line per press/release
 *
 * If <SDL.h> is not found, your install may need <SDL2/SDL.h> instead.
 */

#include <SDL2/SDL.h>

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#endif

/* ------------------------------------------------------------- constants -- */

#define PANEL_LINES   14
#define BAR_WIDTH     21      /* odd, so there is a true centre column */
#define STICK_DEADZONE 0.06f  /* display only; does not filter events */
#define REFRESH_HZ    30

/* Buttons we care about, in display order, with PlayStation labels rather
 * than SDL's Xbox-flavoured names. */
typedef struct {
    SDL_GameControllerButton id;
    const char *label;
} ButtonInfo;

static const ButtonInfo kButtons[] = {
    { SDL_CONTROLLER_BUTTON_A,             "Cross"    },
    { SDL_CONTROLLER_BUTTON_B,             "Circle"   },
    { SDL_CONTROLLER_BUTTON_X,             "Square"   },
    { SDL_CONTROLLER_BUTTON_Y,             "Triangle" },
    { SDL_CONTROLLER_BUTTON_LEFTSHOULDER,  "L1"       },
    { SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, "R1"       },
    { SDL_CONTROLLER_BUTTON_LEFTSTICK,     "L3"       },
    { SDL_CONTROLLER_BUTTON_RIGHTSTICK,    "R3"       },
    { SDL_CONTROLLER_BUTTON_BACK,          "Share"    },
    { SDL_CONTROLLER_BUTTON_START,         "Options"  },
    { SDL_CONTROLLER_BUTTON_GUIDE,         "PS"       },
#if SDL_VERSION_ATLEAST(2, 0, 14)
    { SDL_CONTROLLER_BUTTON_TOUCHPAD,      "Touchpad" },
#endif
};

static const size_t kButtonCount = sizeof(kButtons) / sizeof(kButtons[0]);

/* --------------------------------------------------------------- helpers -- */

static void enable_ansi(void)
{
#ifdef _WIN32
    /* Windows consoles ignore escape sequences unless this is switched on. */
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (out != INVALID_HANDLE_VALUE && GetConsoleMode(out, &mode)) {
        SetConsoleMode(out, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    }
#endif
}

/* SDL reports sticks as -32768..32767. Normalise, and clamp the low end so
 * -32768 does not map to slightly beyond -1.0. */
static float normalise_stick(Sint16 raw)
{
    float v = (float)raw / 32767.0f;
    if (v < -1.0f) v = -1.0f;
    if (v >  1.0f) v =  1.0f;
    return v;
}

/* Triggers report 0..32767 on a DS4 through SDL. */
static float normalise_trigger(Sint16 raw)
{
    float v = (float)raw / 32767.0f;
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;
    return v;
}

/* Bipolar bar: marker slides either side of a fixed centre tick. */
static void render_stick_bar(char *out, size_t out_size, float value)
{
    if (out_size < BAR_WIDTH + 1) {
        if (out_size) out[0] = '\0';
        return;
    }

    const int centre = BAR_WIDTH / 2;
    int pos = centre + (int)lrintf(value * (float)centre);
    if (pos < 0) pos = 0;
    if (pos >= BAR_WIDTH) pos = BAR_WIDTH - 1;

    for (int i = 0; i < BAR_WIDTH; i++) {
        out[i] = (i == centre) ? '|' : '-';
    }
    out[pos] = '#';
    out[BAR_WIDTH] = '\0';
}

/* Unipolar bar, filled from the left. */
static void render_trigger_bar(char *out, size_t out_size, float value)
{
    if (out_size < BAR_WIDTH + 1) {
        if (out_size) out[0] = '\0';
        return;
    }

    int filled = (int)lrintf(value * (float)BAR_WIDTH);
    if (filled < 0) filled = 0;
    if (filled > BAR_WIDTH) filled = BAR_WIDTH;

    for (int i = 0; i < BAR_WIDTH; i++) {
        out[i] = (i < filled) ? '#' : '.';
    }
    out[BAR_WIDTH] = '\0';
}

static const char *power_text(SDL_GameController *pad)
{
    SDL_Joystick *js = SDL_GameControllerGetJoystick(pad);
    if (!js) return "unknown";

    switch (SDL_JoystickCurrentPowerLevel(js)) {
        case SDL_JOYSTICK_POWER_EMPTY:  return "empty";
        case SDL_JOYSTICK_POWER_LOW:    return "low";
        case SDL_JOYSTICK_POWER_MEDIUM: return "medium";
        case SDL_JOYSTICK_POWER_FULL:   return "full";
        case SDL_JOYSTICK_POWER_WIRED:  return "wired";
        default:                        return "unknown";
    }
}

/* ----------------------------------------------------------- open / close -- */

static SDL_GameController *open_first_controller(void)
{
    for (int i = 0; i < SDL_NumJoysticks(); i++) {
        if (!SDL_IsGameController(i)) {
            /* Recognised as a joystick but with no mapping entry. Usable via
             * the raw joystick API, but the named buttons below will not work. */
            continue;
        }
        SDL_GameController *pad = SDL_GameControllerOpen(i);
        if (pad) return pad;
        fprintf(stderr, "Could not open controller %d: %s\n", i, SDL_GetError());
    }
    return NULL;
}

/* ------------------------------------------------------------ live panel -- */

static void draw_panel(SDL_GameController *pad, unsigned long frame, bool first)
{
    char lx_bar[BAR_WIDTH + 1], ly_bar[BAR_WIDTH + 1];
    char rx_bar[BAR_WIDTH + 1], ry_bar[BAR_WIDTH + 1];
    char l2_bar[BAR_WIDTH + 1], r2_bar[BAR_WIDTH + 1];

    float lx = normalise_stick(SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTX));
    float ly = normalise_stick(SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTY));
    float rx = normalise_stick(SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_RIGHTX));
    float ry = normalise_stick(SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_RIGHTY));
    float l2 = normalise_trigger(SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_TRIGGERLEFT));
    float r2 = normalise_trigger(SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_TRIGGERRIGHT));

    if (fabsf(lx) < STICK_DEADZONE) lx = 0.0f;
    if (fabsf(ly) < STICK_DEADZONE) ly = 0.0f;
    if (fabsf(rx) < STICK_DEADZONE) rx = 0.0f;
    if (fabsf(ry) < STICK_DEADZONE) ry = 0.0f;

    render_stick_bar(lx_bar, sizeof lx_bar, lx);
    render_stick_bar(ly_bar, sizeof ly_bar, ly);
    render_stick_bar(rx_bar, sizeof rx_bar, rx);
    render_stick_bar(ry_bar, sizeof ry_bar, ry);
    render_trigger_bar(l2_bar, sizeof l2_bar, l2);
    render_trigger_bar(r2_bar, sizeof r2_bar, r2);

    /* Build the pressed-button list. */
    char pressed[256];
    pressed[0] = '\0';
    size_t used = 0;

    for (size_t i = 0; i < kButtonCount; i++) {
        if (!SDL_GameControllerGetButton(pad, kButtons[i].id)) continue;

        size_t need = strlen(kButtons[i].label) + 2;
        if (used + need >= sizeof pressed) break;

        if (used > 0) {
            strcat(pressed, " ");
            used += 1;
        }
        strcat(pressed, kButtons[i].label);
        used += strlen(kButtons[i].label);
    }

    char dpad[32];
    dpad[0] = '\0';
    if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_UP))    strcat(dpad, "U");
    if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_DOWN))  strcat(dpad, "D");
    if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_LEFT))  strcat(dpad, "L");
    if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) strcat(dpad, "R");
    if (dpad[0] == '\0') strcpy(dpad, "-");

    if (!first) {
        printf("\033[%dA", PANEL_LINES);   /* cursor up, redraw in place */
    }

    printf("\033[2K  %s\n", SDL_GameControllerName(pad));
    printf("\033[2K  battery: %-8s   frame: %lu\n", power_text(pad), frame);
    printf("\033[2K\n");
    printf("\033[2K  Left  X  %+.3f  [%s]\n", lx, lx_bar);
    printf("\033[2K  Left  Y  %+.3f  [%s]\n", ly, ly_bar);
    printf("\033[2K  Right X  %+.3f  [%s]\n", rx, rx_bar);
    printf("\033[2K  Right Y  %+.3f  [%s]\n", ry, ry_bar);
    printf("\033[2K\n");
    printf("\033[2K  L2       %.3f   [%s]\n", l2, l2_bar);
    printf("\033[2K  R2       %.3f   [%s]\n", r2, r2_bar);
    printf("\033[2K\n");
    printf("\033[2K  D-pad    %s\n", dpad);
    printf("\033[2K  Pressed  %s\n", pressed[0] ? pressed : "(none)");
    printf("\033[2K\n");

    fflush(stdout);
}

/* ------------------------------------------------------------ event mode -- */

static const char *button_label(Uint8 sdl_button)
{
    for (size_t i = 0; i < kButtonCount; i++) {
        if ((Uint8)kButtons[i].id == sdl_button) return kButtons[i].label;
    }
    switch (sdl_button) {
        case SDL_CONTROLLER_BUTTON_DPAD_UP:    return "D-pad Up";
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN:  return "D-pad Down";
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT:  return "D-pad Left";
        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: return "D-pad Right";
        default:                               return "?";
    }
}

/* ------------------------------------------------------------------ main -- */

int main(int argc, char **argv)
{
    bool event_mode = false;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--events") == 0) event_mode = true;
    }

    enable_ansi();

    /* Without this hint, some platforms suppress controller events when the
     * process has no focused window — and this program has no window at all. */
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");

    if (SDL_Init(SDL_INIT_GAMECONTROLLER | SDL_INIT_JOYSTICK) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_GameController *pad = open_first_controller();
    if (!pad) {
        printf("No controller detected. Plug the DS4 in over USB.\n");
        printf("Waiting for one to appear (Ctrl-C to quit)...\n");
    } else {
        printf("Connected: %s\n\n", SDL_GameControllerName(pad));
    }

    bool running = true;
    bool first_draw = true;
    unsigned long frame = 0;
    const Uint32 frame_ms = 1000 / REFRESH_HZ;

    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            switch (ev.type) {
                case SDL_QUIT:
                    running = false;
                    break;

                case SDL_CONTROLLERDEVICEADDED:
                    if (!pad) {
                        pad = SDL_GameControllerOpen(ev.cdevice.which);
                        if (pad) {
                            printf("Connected: %s\n\n", SDL_GameControllerName(pad));
                            first_draw = true;
                        }
                    }
                    break;

                case SDL_CONTROLLERDEVICEREMOVED:
                    if (pad) {
                        SDL_Joystick *js = SDL_GameControllerGetJoystick(pad);
                        if (js && SDL_JoystickInstanceID(js) == ev.cdevice.which) {
                            SDL_GameControllerClose(pad);
                            pad = NULL;
                            printf("\nController disconnected.\n");
                            first_draw = true;
                        }
                    }
                    break;

                case SDL_CONTROLLERBUTTONDOWN:
                    if (event_mode) {
                        printf("DOWN  %s\n", button_label(ev.cbutton.button));
                        fflush(stdout);
                    }
                    /* PS button quits, so you can stop without reaching for
                     * the keyboard. Remove if you would rather it be reported. */
                    if (ev.cbutton.button == SDL_CONTROLLER_BUTTON_GUIDE) {
                        running = false;
                    }
                    break;

                case SDL_CONTROLLERBUTTONUP:
                    if (event_mode) {
                        printf("UP    %s\n", button_label(ev.cbutton.button));
                        fflush(stdout);
                    }
                    break;

                case SDL_CONTROLLERAXISMOTION:
                    if (event_mode) {
                        /* Sticks emit constantly at rest due to noise, so only
                         * log meaningful excursions. */
                        if (abs(ev.caxis.value) > 6000) {
                            printf("AXIS  %-14s %+6d\n",
                                   SDL_GameControllerGetStringForAxis(
                                       (SDL_GameControllerAxis)ev.caxis.axis),
                                   ev.caxis.value);
                            fflush(stdout);
                        }
                    }
                    break;

                default:
                    break;
            }
        }

        if (pad && !event_mode) {
            draw_panel(pad, frame++, first_draw);
            first_draw = false;
        }

        SDL_Delay(frame_ms);
    }

    if (pad) SDL_GameControllerClose(pad);
    SDL_Quit();
    printf("\nDone.\n");
    return 0;
}