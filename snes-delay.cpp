// SNES SPC700 echo/delay for Korg minilogue xd
// Emulates the S-DSP echo hardware:
//   circular delay buffer + 8-tap FIR filter + feedback
//
// TIME        → delay length (16ms … 480ms)
// DEPTH       → wet/dry mix  (EVOL)
// SHIFT+DEPTH → feedback      (EFB)

#include "userdelfx.h"
#include "float_math.h"

// EarthBound / DOOM SNES FIR coefficients
// Raw bytes: FA 0A 1A 13 13 1A 0A FA (signed int8)
// tap[0] = oldest delayed sample, tap[7] = newest
static const float k_fir[8] = {
    -6.f/128.f, 10.f/128.f, 26.f/128.f, 19.f/128.f,
    19.f/128.f, 26.f/128.f, 10.f/128.f, -6.f/128.f
};

// ~480ms at 48kHz; delfx hat ca. 2.4 MB SDRAM
#define MAX_DELAY 23040U

static float s_buf_l[MAX_DELAY] __sdram;
static float s_buf_r[MAX_DELAY] __sdram;

static uint32_t s_delay;
static uint32_t s_pos;
static float    s_depth;
static float    s_feedback;

// FIR history: [0]=oldest, [7]=newest
static float s_fir_l[8];
static float s_fir_r[8];

static inline float fir_apply(const float h[8]) {
    return h[0]*k_fir[0] + h[1]*k_fir[1] + h[2]*k_fir[2] + h[3]*k_fir[3]
         + h[4]*k_fir[4] + h[5]*k_fir[5] + h[6]*k_fir[6] + h[7]*k_fir[7];
}

static inline void fir_push(float h[8], float v) {
    h[0]=h[1]; h[1]=h[2]; h[2]=h[3]; h[3]=h[4];
    h[4]=h[5]; h[5]=h[6]; h[6]=h[7]; h[7]=v;
}

void DELFX_INIT(uint32_t platform, uint32_t api) {
    s_delay    = 4800U;   // 100ms default
    s_pos      = 0;
    s_depth    = 0.5f;
    s_feedback = 0.3f;
    for (uint32_t i = 0; i < MAX_DELAY; i++)
        s_buf_l[i] = s_buf_r[i] = 0.f;
    for (int i = 0; i < 8; i++)
        s_fir_l[i] = s_fir_r[i] = 0.f;
}

void DELFX_PROCESS(float *xn, uint32_t frames) {
    float * __restrict x = xn;
    const uint32_t delay    = s_delay;
    const float    depth    = s_depth;
    const float    feedback = s_feedback;

    for (uint32_t i = 0; i < frames; i++, x += 2) {
        const float inL = x[0];
        const float inR = x[1];

        // Ältestes Sample aus Ringpuffer lesen
        const float rdL = s_buf_l[s_pos];
        const float rdR = s_buf_r[s_pos];

        // FIR-History aktualisieren und filtern
        fir_push(s_fir_l, rdL);
        fir_push(s_fir_r, rdR);
        const float filtL = fir_apply(s_fir_l);
        const float filtR = fir_apply(s_fir_r);

        // Ringpuffer schreiben: Eingang + Feedback * gefiltertes Echo
        s_buf_l[s_pos] = clip1m1f(inL + feedback * filtL);
        s_buf_r[s_pos] = clip1m1f(inR + feedback * filtR);

        if (++s_pos >= delay) s_pos = 0;

        // Ausgang: Dry + Wet * gefiltertes Echo
        x[0] = clip1m1f(inL + depth * filtL);
        x[1] = clip1m1f(inR + depth * filtR);
    }
}

void DELFX_PARAM(uint8_t index, int32_t value) {
    const float f = q31_to_f32(value);
    switch (index) {
        case k_user_delfx_param_time: {
            // 768 = 16ms (SNES-Minimum), MAX_DELAY = 480ms
            const uint32_t d = (uint32_t)(768.f + f * (MAX_DELAY - 768.f));
            s_delay = (d < 768U) ? 768U : (d > MAX_DELAY ? MAX_DELAY : d);
            if (s_pos >= s_delay) s_pos = 0;
            break;
        }
        case k_user_delfx_param_depth:
            s_depth = f;
            break;
        case k_user_delfx_param_shift_depth:
            s_feedback = f;
            break;
        default:
            break;
    }
}

void DELFX_SUSPEND(void) {}

void DELFX_RESUME(void) {
    // FIR-History leeren, sonst Knackser nach Pause
    for (int i = 0; i < 8; i++)
        s_fir_l[i] = s_fir_r[i] = 0.f;
}
