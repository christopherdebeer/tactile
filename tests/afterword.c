/* Real sensor-frame signs: clockwise screen roll produces negative gyro Z. */
#include "afterword.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define PI 3.14159265358979323846f
#define DEG (PI/180.0f)
static uint32_t clock_ms;
static float pose_roll, pose_pitch;
static aw_state_t state(void) { aw_state_t s; aw_state(&s); return s; }
static aw_frame_t frame(void) { aw_frame_t f; aw_frame(&f); return f; }

static void sample(float roll, float pitch, const float *gyro, float force, int ms) {
    float a[] = {-sinf(roll)*cosf(pitch)*force,
                  cosf(roll)*cosf(pitch)*force, sinf(pitch)*force};
    for (int t=0; t<ms; t+=20) {
        clock_ms += 20;
        aw_gravity(a);
        aw_tick(clock_ms, gyro, true);
    }
    pose_roll=roll; pose_pitch=pitch;
}
static void hold(int ms) {
    const float g[]={0,0,0};
    sample(pose_roll, pose_pitch, g, 9.81f, ms);
}
static void move(float roll, float pitch, int ms) {
    float from=pose_roll, from_pitch=pose_pitch;
    int steps=ms/20;
    float velocity=(roll-from)/(ms*.001f);
    for(int i=1;i<=steps;i++) {
        float r=from+(roll-from)*i/steps;
        float p=from_pitch+(pitch-from_pitch)*i/steps;
        /* Pure Z roll; pitch motion uses a matching in-plane axis. */
        float pv=(pitch-from_pitch)/(ms*.001f);
        float g[]={-pv*cosf(r),-pv*sinf(r),-velocity};
        sample(r,p,g,9.81f,20);
    }
}
static void init(float roll, float pitch, bool completed) {
    aw_init(440,false,false,completed,"old note",clock_ms);
    pose_roll=roll; pose_pitch=pitch;
    hold(600);
    assert(state().orientation_ready);
    assert(state().rate_multiplier==0);
    assert(!state().tutorial_active);
}
static void close_to(float actual, float expected, float tolerance) {
    assert(fabsf(actual-expected)<tolerance);
}
int main(void) {
    // Old NVS tutorial completion never gates or starts the new interaction.
    init(0.4f,0.3f,false);
    assert(!strcmp(frame().word,"a"));
    close_to(frame().text_rotation_degrees,-0.4f/DEG,.26f);
    assert(state().word_index==0);
    hold(3000);
    assert(state().word_index==0);

    // Angle sets speed; holding does not accelerate and slow turns work.
    move(.4f+20*DEG,.3f,1000);
    hold(300);
    float rate=state().rate_multiplier;
    assert(rate>.5f && rate<.9f);
    hold(5000);
    close_to(state().rate_multiplier,rate,.01f);
    close_to(frame().text_rotation_degrees,-(.4f+20*DEG)/DEG,.26f);
    move(.4f,.3f,200);
    hold(100);
    assert(state().rate_multiplier==0);
    int stopped=state().word_index;
    hold(2000);
    assert(state().word_index==stopped);

    // Broad detent and hysteresis suppress tremor near centre.
    move(.4f+7*DEG,.3f,300); hold(300);
    assert(state().rate_multiplier==0);
    move(.4f+9*DEG,.3f,200); hold(300);
    assert(state().rate_multiplier>0);
    move(.4f+5*DEG,.3f,200); hold(300);
    assert(state().rate_multiplier==0);

    // Symmetric bounded forward/reverse; no focus or pitch navigation.
    move(.4f-35*DEG,.3f,400); hold(300);
    close_to(state().rate_multiplier,-3,.02f);
    move(.4f+35*DEG,.3f,500); hold(300);
    close_to(state().rate_multiplier,3,.02f);
    move(.4f,.3f,300); hold(300);
    stopped=state().word_index;
    move(.4f,.7f,400); hold(700);
    assert(state().word_index==stopped);
    assert(!state().focused && !state().tilt_latched);

    // Stale or flat readings stop immediately; pickup recaptures neutral
    // without moving the current word or re-entering the old tutorial.
    clock_ms+=20; aw_tick(clock_ms,NULL,false);
    assert(!state().orientation_ready && state().rate_multiplier==0);
    sample(0,PI/2,NULL,9.81f,600);
    assert(!state().orientation_ready);
    sample(-.7f,.2f,NULL,9.81f,600);
    assert(state().orientation_ready && state().rate_multiplier==0);
    assert(state().word_index==stopped);
    close_to(frame().text_rotation_degrees,.7f/DEG,.26f);

    // Deliberate centre capture retains the word; inversion changes the dial
    // only, never the direction of display stabilisation.
    aw_tutorial_restart(clock_ms); hold(600);
    assert(state().word_index==stopped);
    aw_set_inversions(true,false); hold(600);
    move(-.7f+20*DEG,.2f,300); hold(300);
    assert(state().rate_multiplier<0);
    close_to(frame().text_rotation_degrees,-(-.7f+20*DEG)/DEG,.26f);

    // Wrap across +/-pi follows the short rotation and stabilises the text.
    init(179*DEG,0,true);
    move(199*DEG,0,400); hold(300);
    close_to(state().roll_radians,20*DEG,.01f);
    close_to(frame().text_rotation_degrees,161,.26f);

    // Gyro prediction follows fast roll without low-pass display lag.
    init(0,0,true);
    move(30*DEG,0,100);
    close_to(state().roll_radians,30*DEG,.01f);
    close_to(frame().text_rotation_degrees,-30,.26f);
    // Translation invalidates rate, rather than becoming a dial command.
    sample(30*DEG,0,NULL,14,100);
    assert(state().rate_multiplier==0);
    // Long task gap also stops; never replay a backlog as a burst of words.
    clock_ms+=500; aw_tick(clock_ms,NULL,true);
    assert(!state().orientation_ready && state().rate_multiplier==0);

    // A little bias cannot integrate indefinitely against trusted gravity.
    init(0,0,true);
    const float bias[]={0,0,.01f};
    sample(0,0,bias,9.81f,30000);
    close_to(state().roll_radians,0,.01f);
    assert(state().rate_multiplier==0);
    assert(aw_set_pace_ms(150) && aw_set_pace_ms(2000));
    assert(!aw_set_pace_ms(149) && !aw_set_pace_ms(2001));
    assert(aw_fixation_index("garden")==2);
    puts("Direct dial, stabilisation, detent, pickup and migration passed");
}
