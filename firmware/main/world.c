/* Interior world for Afterword.
 *
 * One primary word at a time. Places and things "tell" themselves word by
 * word (RSVP) and then rest on their name. The hand explores:
 *
 *   hold + twist      turn through what is here (places)
 *   release           go through a way, or take up a thing
 *   tip either way    look closer: a tertiary detail line appears
 *   tap               set down the thing you hold
 *
 * Each thing answers a different sensor channel, so the world doubles as a
 * catalogue of candidate meta-control gestures:
 *
 *   lamp   twist (gyro about the screen axis) winds it; it fades with time
 *   letter twist scrubs the saved note; otherwise it reads itself (RSVP)
 *   bowl   tilt (gravity vs. the pose it was taken up in) pours into soil
 *   seed   stillness grows it, if the soil is wet
 *   bell   a shake or flick rings it
 *   stone  holding the button warms it; it cools when released
 *
 * The cellar stays dark unless the lamp is lit. State evolves while the
 * operator is inside and persists through aw_store("aw_world").
 */
#include "world.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

enum place { HALL, STUDY, GARDEN, CELLAR, PLACES };
enum item {
    OUTWARD, TO_STUDY, TO_GARDEN, TO_STAIR, TO_HALL, UP,
    BELL, LAMP, LETTER, SEED, BOWL, STONE, DARK, ITEMS
};
enum kind { EXIT, THING, VOID };

typedef struct { enum kind kind; const char *name, *telling; int target; } item_t;
static const item_t items[ITEMS] = {
    [OUTWARD]   = {EXIT, "outward", "back to the words", -1},
    [TO_STUDY]  = {EXIT, "study", "a door with warm edges", STUDY},
    [TO_GARDEN] = {EXIT, "garden", "a door left ajar", GARDEN},
    [TO_STAIR]  = {EXIT, "stair", "steps going down", CELLAR},
    [TO_HALL]   = {EXIT, "hall", "back the way you came", HALL},
    [UP]        = {EXIT, "up", "the stair climbs to light", HALL},
    [BELL]      = {THING, "bell", "small brass and patient", -1},
    [LAMP]      = {THING, "lamp", "it winds like a clock", -1},
    [LETTER]    = {THING, "letter", "folded and waiting", -1},
    [SEED]      = {THING, "seed", "it sleeps in the soil", -1},
    [BOWL]      = {THING, "bowl", "rain gathers in it", -1},
    [STONE]     = {THING, "stone", "smooth and keeping cold", -1},
    [DARK]      = {VOID, "darkness", "you cannot see", -1},
};

typedef struct { const char *name, *telling; int items[5]; int n; } place_t;
static const place_t places[PLACES] = {
    [HALL]   = {"hall", "a quiet hall with four doors",
                {BELL, TO_STUDY, TO_GARDEN, TO_STAIR, OUTWARD}, 5},
    [STUDY]  = {"study", "books lean toward the lamp", {LAMP, LETTER, TO_HALL}, 3},
    [GARDEN] = {"garden", "moss and rain and room", {SEED, BOWL, TO_HALL}, 3},
    [CELLAR] = {"cellar", "cool air under the house", {STONE, UP}, 2},
};
static const char *const phases[] = {"dawn", "day", "dusk", "night"};
static const char *const growth_words[] = {"seed", "sprout", "shoot", "bud", "flower"};

#define LOOK 0.25f        /* rad of tip before a detail appears */
#define POUR 0.45f        /* rad of tilt before the bowl pours */
#define TAP_MS 250
#define DETENT 0.20f

static struct { float lamp, water, soil, growth, warmth; int rings; } w = {0, 1, 0, 0, 0, 0};
static int here, focus, holding = -1, tell_i = -1, letter_i;
static bool held, detented, pouring, dirty, has_ref;
static uint32_t last, pressed, repeat_at, tell_at, ring_until, saved_at;
static float angle, tip, ref[3], hold_ref[3], up[3];
static const char *tell_text = "", *note = "";
static int word_ms = 440;
static char spoken[22];

static int wrap(int i, int n) { return (i % n + n) % n; }
static float clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }

static int ring(const int **out) {
    static const int dark[] = {DARK, UP};
    if (here == CELLAR && w.lamp < 0.3f) { *out = dark; return 2; }
    *out = places[here].items;
    return places[here].n;
}

static int at(int offset) {
    const int *r;
    int n = ring(&r);
    return r[wrap(focus + offset, n)];
}

static bool word_of(const char *s, int i, char *out, size_t cap) {
    while (*s) {
        while (*s == ' ') ++s;
        if (!*s) break;
        size_t len = strcspn(s, " ");
        if (i-- == 0) { snprintf(out, cap, "%.*s", (int)len, s); return true; }
        s += len;
    }
    return false;
}

static void tell(const char *text, uint32_t now) {
    tell_text = text;
    tell_i = 0;
    tell_at = now + (uint32_t)word_ms;
}

static void save(uint32_t now) {
    char state[48];
    world_state(state, sizeof(state));
    if (aw_store("aw_world", state)) dirty = false;
    saved_at = now;
}

void world_state(char *out, size_t cap) {
    snprintf(out, cap, "%d,%d,%d,%d,%d,%d", (int)(w.lamp * 1000), (int)(w.water * 1000),
             (int)(w.soil * 1000), (int)(w.growth * 1000), (int)(w.warmth * 1000), w.rings);
}

void world_load(const char *state) {
    int v[6];
    if (state && sscanf(state, "%d,%d,%d,%d,%d,%d", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) == 6) {
        w.lamp = clamp01(v[0] / 1000.0f); w.water = clamp01(v[1] / 1000.0f);
        w.soil = clamp01(v[2] / 1000.0f);
        w.growth = v[3] < 0 ? 0 : v[3] > 4000 ? 4 : v[3] / 1000.0f;
        w.warmth = clamp01(v[4] / 1000.0f); w.rings = v[5] < 0 ? 0 : v[5];
    }
}

void world_enter(uint32_t now, const char *saved_note, int ms) {
    note = saved_note ? saved_note : "";
    word_ms = ms > 120 ? ms : 120;
    here = HALL; focus = 0; holding = -1;
    held = has_ref = false;
    last = saved_at = now;
    tell(places[HALL].telling, now);
}

static const char *state_of(int id) {
    switch (id) {
    case LAMP: return w.lamp < 0.05f ? "cold" : w.lamp < 0.3f ? "ember" : w.lamp < 0.7f ? "glowing" : "bright";
    case BOWL: return w.water < 0.05f ? "empty" : w.water < 0.35f ? "low" : w.water < 0.75f ? "half full" : "full";
    case SEED: return growth_words[w.growth >= 4 ? 4 : (int)w.growth];
    case STONE: return w.warmth < 0.2f ? "cold" : w.warmth < 0.5f ? "cool" : w.warmth < 0.8f ? "warm" : "hand warm";
    case BELL: return w.rings ? "it remembers" : "unrung";
    case LETTER: return note[0] ? "written" : "blank";
    case DARK: return "light a lamp";
    default:
        return items[id].target < 0 ? "a way out" : places[items[id].target].name;
    }
}

/* Gravity in, unit vector out; tip = angle from a slowly adapting rest pose. */
static void sense(const float g[3], bool valid, float dt) {
    float m = valid ? sqrtf(g[0] * g[0] + g[1] * g[1] + g[2] * g[2]) : 0;
    if (m < 3.0f) { tip = 0; return; }
    for (int i = 0; i < 3; ++i) up[i] = g[i] / m;
    if (!has_ref) { memcpy(ref, up, sizeof(ref)); has_ref = true; }
    float d = ref[0] * up[0] + ref[1] * up[1] + ref[2] * up[2];
    tip = acosf(d > 1 ? 1 : d < -1 ? -1 : d);
    if (!held) {   /* adapt: a held tilt fades back to rest within a few seconds */
        float k = dt / 3.0f, n = 0;
        for (int i = 0; i < 3; ++i) { ref[i] += (up[i] - ref[i]) * k; n += ref[i] * ref[i]; }
        n = sqrtf(n);
        if (n > 0) for (int i = 0; i < 3; ++i) ref[i] /= n;
    }
}

static float tilt_from(const float r[3]) {
    float d = r[0] * up[0] + r[1] * up[1] + r[2] * up[2];
    return acosf(d > 1 ? 1 : d < -1 ? -1 : d);
}

static void evolve(float dt, uint32_t now) {
    float lamp = w.lamp, soil = w.soil;
    w.lamp = clamp01(w.lamp - dt / 240.0f);
    if (!pouring) w.water = clamp01(w.water + dt / 180.0f);
    if (!(holding == STONE && held)) w.warmth = clamp01(w.warmth - dt / 90.0f);
    w.soil = clamp01(w.soil - dt / 600.0f);
    if ((lamp > 0) != (w.lamp > 0) || (soil > 0) != (w.soil > 0)) dirty = true;
    if (dirty && now - saved_at >= 60000) save(now);
}

static void hold_thing(uint32_t now, bool down, float twist, float speed, bool valid, float dt) {
    pouring = false;
    switch (holding) {
    case LAMP:
        if (down && valid) { w.lamp = clamp01(w.lamp + fabsf(twist) * dt / 8.0f); dirty = true; }
        break;
    case LETTER:
        if (down && valid) {
            angle += twist * dt;
            if (fabsf(angle) >= DETENT && (int32_t)(now - repeat_at) >= 0) {
                letter_i += angle > 0 ? 1 : -1;
                if (letter_i < 0) letter_i = 0;
                angle = 0; detented = true; repeat_at = now + 250;
            }
        } else if (!down && (int32_t)(now - tell_at) >= 0 && tell_i < 0) {
            ++letter_i;
            tell_at = now + (uint32_t)word_ms;
        }
        if (note[0] && !word_of(note, letter_i, spoken, sizeof(spoken))) {
            letter_i = 0; tell_at = now + (uint32_t)word_ms * 3;
        }
        break;
    case BOWL:
        if (valid && tilt_from(hold_ref) > POUR && w.water > 0) {
            float flow = (tilt_from(hold_ref) - POUR) * 1.5f * dt;
            if (flow > w.water) flow = w.water;
            w.water -= flow; w.soil = clamp01(w.soil + flow);
            pouring = dirty = true;
        }
        break;
    case SEED:
        if (valid && speed < 0.15f && w.soil > 0.05f && w.growth < 4) {
            w.growth += dt / 40.0f; w.soil = clamp01(w.soil - dt / 120.0f); dirty = true;
        }
        break;
    case BELL:
        if (valid && speed > 6.0f && (int32_t)(now - ring_until) >= 0) {
            ++w.rings; ring_until = now + 1200; dirty = true;
        }
        break;
    case STONE:
        if (down) { w.warmth = clamp01(w.warmth + dt / 20.0f); dirty = true; }
        break;
    }
}

static bool choose(uint32_t now) {
    int id = at(0);
    if (items[id].kind == EXIT) {
        if (items[id].target < 0) { save(now); return true; }
        here = items[id].target;
        focus = 0;
        tell(places[here].telling, now);
    } else if (items[id].kind == THING) {
        holding = id;
        memcpy(hold_ref, up, sizeof(hold_ref));
        letter_i = 0;
        tell(items[id].telling, now);
    } else {
        tell(items[id].telling, now);
    }
    return false;
}

bool world_tick(uint32_t now, bool down, float twist, float speed, const float g[3], bool valid) {
    float dt = (now - last) * 0.001f;
    if (dt > 0.1f) dt = 0.1f;
    last = now;
    sense(g, valid, dt);
    if (down && !held) { pressed = repeat_at = now; angle = 0; detented = false; }

    if (tell_i >= 0 && (int32_t)(now - tell_at) >= 0) {
        char scratch[22];
        ++tell_i;
        tell_at = now + (uint32_t)word_ms;
        if (!word_of(tell_text, tell_i, scratch, sizeof(scratch))) tell_i = -1;
    }

    bool out = false;
    if (holding >= 0) {
        hold_thing(now, down, twist, speed, valid, dt);
        if (!down && held && now - pressed < TAP_MS && !detented) {
            holding = -1;
            pouring = false;
            save(now);
            tell_i = -1;
        }
    } else {
        if (down && held && valid) {
            angle += twist * dt;
            if (fabsf(angle) >= DETENT && (int32_t)(now - repeat_at) >= 0) {
                const int *r;
                focus = wrap(focus + (angle > 0 ? 1 : -1), ring(&r));
                tell_i = -1;
                detented = true;
                repeat_at = now + 400;
            }
        }
        if (!down && held && now - pressed >= 40) out = choose(now);
    }
    evolve(dt, now);
    held = down;
    return out;
}

#define PUT(field, ...) snprintf(f->field, sizeof(f->field), __VA_ARGS__)
void world_frame(aw_frame_t *f) {
    memset(f, 0, sizeof(*f));
    char current[22] = "";
    bool telling = tell_i >= 0 && word_of(tell_text, tell_i, current, sizeof(current));
    if (holding >= 0) {
        PUT(section, "HOLDING");
        PUT(before, "%s", items[holding].name);
        if (telling) PUT(word, "%s", current);
        else if (holding == LETTER) PUT(word, "%s", note[0] ? spoken : "blank page");
        else if (holding == BELL && (int32_t)(last - ring_until) < 0) PUT(word, "ring");
        else PUT(word, "%s", holding == BELL ? "bell" : state_of(holding));
        switch (holding) {
        case LAMP: PUT(after, "%s", held ? "winding" : "turn to wind"); break;
        case LETTER: PUT(after, "turn to scrub"); break;
        case BOWL: PUT(after, "%s", pouring ? "pouring" : w.water < 0.05f ? "waiting for rain" : "tip to pour"); break;
        case SEED: PUT(after, "%s", w.growth >= 4 ? "in bloom" : w.soil < 0.05f ? "it is thirsty" : "hold still"); break;
        case BELL: PUT(after, "rung %d times", w.rings); break;
        case STONE: PUT(after, "%s", held ? "warming" : "hold it close"); break;
        }
        PUT(hint, "tap: set it down");
        return;
    }
    char name[12];
    size_t i = 0;
    for (const char *s = places[here].name; *s && i + 1 < sizeof(name); ++s) name[i++] = (char)(*s - 32);
    name[i] = 0;
    PUT(section, "%s  %s", name, phases[(last / 90000u) % 4]);
    int id = at(0);
    PUT(word, "%s", telling ? current : items[id].name);
    PUT(before, "%s", items[at(-1)].name);
    PUT(after, "%s", items[at(1)].name);
    if (tip > LOOK && !held) {      /* look closer: replace context with detail */
        PUT(before, "%s", items[id].name);
        PUT(after, "%s", state_of(id));
    }
    PUT(hint, "%s", held ? "release to choose" : "hold+twist / tip");
}
