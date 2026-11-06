/* Host regression for the Afterword interior world. */
#include "world.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint32_t clock_ms;
static char stored[48];
static float g[3] = {0, 9.81f, 0};

bool aw_store(const char *key, const char *value) {
    if (!strcmp(key, "aw_world")) snprintf(stored, sizeof(stored), "%s", value);
    return true;
}

static void tick(bool down, float twist, float speed, int ms) {
    for (int t = 0; t < ms; t += 20) {
        clock_ms += 20;
        assert(!world_tick(clock_ms, down, twist, speed, g, true));
    }
}

static aw_frame_t frame(void) { aw_frame_t f; world_frame(&f); return f; }

/* Hold, step `steps` detents, release to choose. Returns world_tick's exit. */
static bool choose(int steps) {
    /* As in Afterword: one twist past the catch, then one detent per 400 ms. */
    tick(true, 0, 0, 60);
    if (steps) {
        tick(true, steps > 0 ? 6 : -6, 6, 40);
        int n = steps > 0 ? steps : -steps;
        for (int i = 1; i < n; i++) tick(true, 0, 0, 400);
    }
    clock_ms += 20;
    return world_tick(clock_ms, false, 0, 0, g, true);
}

static void tap(void) {
    tick(true, 0, 0, 100);
    clock_ms += 20;
    assert(!world_tick(clock_ms, false, 0, 0, g, true));
}

static void settle(void) { tick(false, 0, 0, 3000); }

int main(void) {
    world_enter(0, "under the stair", 300);
    assert(!strcmp(frame().word, "a"));          /* the hall tells itself */
    settle();
    assert(!strcmp(frame().word, "bell"));
    assert(!strcmp(frame().after, "study"));
    assert(!strncmp(frame().section, "HALL", 4));

    /* Cellar is dark until the lamp is lit. */
    assert(!choose(3)); settle();
    assert(!strcmp(frame().word, "darkness"));
    assert(!choose(1)); settle();                 /* up */
    assert(!strcmp(frame().word, "bell"));

    /* Study: wind the lamp by twisting while held. */
    assert(!choose(1)); settle();
    assert(!strcmp(frame().word, "lamp"));
    assert(!choose(0)); settle();
    assert(!strcmp(frame().section, "HOLDING"));
    assert(!strcmp(frame().word, "cold"));
    tick(true, 6, 6, 2000);                       /* ~12 rad of winding */
    tick(false, 0, 0, 20);
    assert(!strcmp(frame().word, "bright"));
    tap();
    assert(strcmp(frame().section, "HOLDING"));

    /* Letter reads the saved note word by word. */
    assert(!choose(1)); settle();
    assert(!strcmp(frame().section, "HOLDING"));
    assert(!strcmp(frame().before, "letter"));
    tap();

    /* Garden: pour the bowl by tilting, then grow the seed by stillness. */
    assert(!choose(1)); settle();                 /* hall */
    assert(!choose(2)); settle();                 /* garden */
    assert(!strcmp(frame().word, "seed"));
    assert(!choose(1)); settle();                 /* take the bowl */
    assert(!strcmp(frame().word, "full"));
    g[0] = 9.81f; g[1] = 0;                       /* tip it a quarter turn */
    tick(false, 0, 0, 200);
    assert(!strcmp(frame().after, "pouring"));
    tick(false, 0, 0, 1500);
    assert(!strcmp(frame().word, "empty"));
    g[0] = 0; g[1] = 9.81f;
    tick(false, 0, 0, 20);
    assert(strcmp(frame().word, "full"));
    tap();
    settle();
    assert(!choose(-1)); settle();                /* take the seed */
    tick(false, 0, 0, 45000);                     /* hold still */
    assert(!strcmp(frame().word, "sprout"));
    tap();

    /* Lamp still lit: the cellar now holds a stone that warms in a held hand. */
    assert(!choose(-1)); settle();                /* hall */
    assert(!choose(3)); settle();                 /* stair */
    assert(!strcmp(frame().word, "stone"));
    assert(!choose(0)); settle();
    tick(true, 0, 0, 15000);
    tick(false, 0, 0, 20);
    assert(!strcmp(frame().word, "warm"));
    tap();

    /* Tipping reveals a tertiary detail instead of the neighbours. */
    g[2] = 9.81f * 0.5f; g[1] = 9.81f * 0.866f;  /* 30 degrees */
    tick(false, 0, 0, 20);
    assert(!strcmp(frame().after, "warm"));
    g[2] = 0; g[1] = 9.81f;
    settle(); settle();

    /* Up, bell rings with a shake, then step outward and persist. */
    assert(!choose(1)); settle();
    assert(!choose(0)); settle();
    assert(!strcmp(frame().before, "bell"));
    tick(false, 0, 8, 40);
    assert(!strcmp(frame().word, "ring"));
    tap();
    assert(choose(-1));                           /* outward */
    int lamp, water, soil, growth, warmth, rings;
    assert(sscanf(stored, "%d,%d,%d,%d,%d,%d", &lamp, &water, &soil, &growth, &warmth, &rings) == 6);
    assert(lamp > 0 && growth >= 1000 && rings == 1);

    world_load("0,1000,0,4000,0,3");
    world_enter(clock_ms, "", 300);
    settle();
    char state[48];
    world_state(state, sizeof(state));
    assert(strstr(state, ",4000,") && !strcmp(strrchr(state, ',') + 1, "3"));
    puts("Interior world exploration, manipulation and persistence passed");
}
