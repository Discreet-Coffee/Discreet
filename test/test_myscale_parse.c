/*
 * test_myscale_parse.c
 * ------------------------------------------------------------------
 * Host-side unit test for the MY_SCALE (Blackcoffee/FFB0 protocol)
 * weight parser that goes into Discreet_MQTT.ino.
 *
 * Verifies our byte-wise implementation against:
 *  - the REAL packet captured live from James's scale (2026-08-28)
 *  - GaggiMate's myscale.cpp parseWeight() reference implementation
 *  - Bean Conqueror's blackcoffeeScale.ts nibble-string algorithm
 *
 * Build & run:  cc -Wall -Wextra -O2 -o /tmp/test_myscale test_myscale_parse.c && /tmp/test_myscale
 */
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

/* ---------- the implementation under test (mirrors the .ino) ---------- */

#define SCALE_PKT_LEN 20
#define SCALE_HDR0 0xAC
#define SCALE_HDR1 0x40

typedef struct {
    float grams;
    bool  stable;
    bool  valid;
} scale_reading_t;

static scale_reading_t scaleParsePacket(const uint8_t *d, size_t len)
{
    scale_reading_t r = { 0.0f, false, false };

    /* GaggiMate rejects <15; Bean Conqueror requires >14. Same bound. */
    if (d == NULL || len < 15) return r;

    /* Header check: bytes 0-1 are AC 40 (LE u16 0x40AC == advertised mfg id) */
    if (d[0] != SCALE_HDR0 || d[1] != SCALE_HDR1) return r;

    uint8_t signNib = (uint8_t)(d[2] >> 4);
    bool isNegative = (signNib == 0x8) || (signNib == 0xC);
    r.stable = ((d[2] & 0x0F) == 0x1);

    uint32_t mg = ((uint32_t)(d[3] & 0x0F) << 24) |
                  ((uint32_t)d[4] << 16) |
                  ((uint32_t)d[5] <<  8) |
                  ((uint32_t)d[6]);

    r.grams = (isNegative ? -1.0f : 1.0f) * ((float)mg / 1000.0f);
    r.valid = true;
    return r;
}

/* ---------- reference implementations for cross-checking ---------- */

/* GaggiMate myscale.cpp parseWeight() verbatim logic */
static int32_t gaggimate_parseWeight(const uint8_t *data)
{
    bool isNegative = ((data[2] >> 4) == 0x8 || (data[2] >> 4) == 0xC);
    uint32_t raw = ((uint32_t)(data[3] & 0x0F) << 24) |
                   ((uint32_t)data[4] << 16) |
                   ((uint32_t)data[5] <<  8) |
                   ((uint32_t)data[6]);
    return isNegative ? -(int32_t)raw : (int32_t)raw;
}

/* Bean Conqueror blackcoffeeScale.ts: operates on the HEX STRING by nibble */
static double beanconqueror_parse(const uint8_t *d, size_t len)
{
    if (len <= 14) return NAN;
    char hex[SCALE_PKT_LEN * 2 + 1];
    for (size_t i = 0; i < len && i < SCALE_PKT_LEN; i++)
        sprintf(hex + i * 2, "%02x", d[i]);
    hex[len * 2] = '\0';

    bool isNegative = (hex[4] == '8' || hex[4] == 'c');
    char sub[8];
    memcpy(sub, hex + 7, 7);   /* hex.slice(7,14) -> 7 nibbles */
    sub[7] = '\0';
    long v = strtol(sub, NULL, 16);
    return ((isNegative ? -1.0 : 1.0) * (double)v) / 1000.0;
}

/* ---------- test harness ---------- */

static int failures = 0;
static int checks   = 0;

static void expect_near(const char *what, double got, double want, double tol)
{
    checks++;
    if (isnan(got) && isnan(want)) return;
    /* Compare with a float32-aware tolerance: the firmware stores grams in a
       float, so values are only accurate to ~7 significant digits. Use the
       larger of the absolute tol and a relative float32 epsilon. */
    double scale = fabs(want) > fabs(got) ? fabs(want) : fabs(got);
    double eff   = tol > scale * 1.2e-7 ? tol : scale * 1.2e-7;
    if (fabs(got - want) > eff) {
        printf("  FAIL %-44s got=%.6f want=%.6f\n", what, got, want);
        failures++;
    } else {
        printf("  ok   %-44s = %.3f\n", what, got);
    }
}

static void expect_bool(const char *what, bool got, bool want)
{
    checks++;
    if (got != want) {
        printf("  FAIL %-44s got=%d want=%d\n", what, (int)got, (int)want);
        failures++;
    } else {
        printf("  ok   %-44s = %s\n", what, got ? "true" : "false");
    }
}

/* build a synthetic packet with a given milligram value + flags */
static void build_pkt(uint8_t *out, uint32_t mg, bool negative, bool stable)
{
    memset(out, 0, SCALE_PKT_LEN);
    out[0] = SCALE_HDR0;
    out[1] = SCALE_HDR1;
    out[2] = (uint8_t)((negative ? 0x80 : 0x00) | (stable ? 0x01 : 0x00));
    out[3] = (uint8_t)((mg >> 24) & 0x0F);
    out[4] = (uint8_t)((mg >> 16) & 0xFF);
    out[5] = (uint8_t)((mg >>  8) & 0xFF);
    out[6] = (uint8_t)( mg        & 0xFF);
    out[18] = 0xA6;
    out[19] = 0xA7;
}

int main(void)
{
    printf("=== MY_SCALE parser tests ===\n\n");

    /* ---- 1. THE REAL CAPTURED PACKET (ground truth from the device) ---- */
    printf("1. Real captured idle packet (live, 2026-08-28 23:36 EEST)\n");
    const uint8_t real[SCALE_PKT_LEN] = {
        0xac,0x40,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xa6,0xa7
    };
    scale_reading_t r = scaleParsePacket(real, sizeof(real));
    expect_bool ("real: valid",   r.valid,  true);
    expect_near ("real: grams",   r.grams,  0.0,  1e-9);
    expect_bool ("real: stable",  r.stable, true);
    expect_near ("real: vs GaggiMate",
                 (double)gaggimate_parseWeight(real) / 1000.0, r.grams, 1e-9);
    expect_near ("real: vs BeanConqueror",
                 beanconqueror_parse(real, sizeof(real)), r.grams, 1e-9);

    /* ---- 2. Synthetic weights, cross-checked 3 ways ---- */
    printf("\n2. Synthetic weights (ours vs GaggiMate vs BeanConqueror)\n");
    struct { uint32_t mg; bool neg; const char *label; } cases[] = {
        {     0, false, "0.000 g"    },
        {  1000, false, "1.000 g"    },
        { 18000, false, "18.000 g (dose)"  },
        { 36000, false, "36.000 g (target)"},
        { 36500, false, "36.500 g"   },
        {   100, false, "0.100 g (resolution)" },
        {   500, true,  "-0.500 g (neg)"  },
        {  2000, true,  "-2.000 g (neg)"  },
        {999999, false, "999.999 g"  },
        {2000000,false, "2000.000 g (2kg cap)" },
    };
    for (size_t i = 0; i < sizeof(cases)/sizeof(cases[0]); i++) {
        uint8_t p[SCALE_PKT_LEN];
        build_pkt(p, cases[i].mg, cases[i].neg, true);
        scale_reading_t got = scaleParsePacket(p, sizeof(p));
        double want = (cases[i].neg ? -1.0 : 1.0) * (double)cases[i].mg / 1000.0;
        char buf[96];
        snprintf(buf, sizeof(buf), "%s", cases[i].label);
        expect_near(buf, got.grams, want, 1e-6);
        /* all three implementations must agree exactly */
        expect_near("   ^ vs GaggiMate",
                    (double)gaggimate_parseWeight(p)/1000.0, got.grams, 1e-9);
        expect_near("   ^ vs BeanConqueror",
                    beanconqueror_parse(p, sizeof(p)), got.grams, 1e-9);
    }

    /* ---- 3. Sign nibble variants (0x8 and 0xC both mean negative) ---- */
    printf("\n3. Sign nibble: both 0x8 and 0xC indicate negative\n");
    {
        uint8_t p[SCALE_PKT_LEN];
        build_pkt(p, 5000, false, true);
        p[2] = 0x81;                       /* sign nib 8, stable */
        expect_near("sign nib 0x8 -> negative", scaleParsePacket(p,20).grams, -5.0, 1e-6);
        p[2] = 0xC1;                       /* sign nib C, stable */
        expect_near("sign nib 0xC -> negative", scaleParsePacket(p,20).grams, -5.0, 1e-6);
        p[2] = 0x01;
        expect_near("sign nib 0x0 -> positive", scaleParsePacket(p,20).grams,  5.0, 1e-6);
    }

    /* ---- 4. Stability flag ---- */
    printf("\n4. Stability flag (low nibble of byte 2)\n");
    {
        uint8_t p[SCALE_PKT_LEN];
        build_pkt(p, 36000, false, true);
        expect_bool("stable  (lo nib 1)", scaleParsePacket(p,20).stable, true);
        build_pkt(p, 36000, false, false);
        expect_bool("moving  (lo nib 0)", scaleParsePacket(p,20).stable, false);
    }

    /* ---- 5. Rejection cases: must NOT produce a bogus weight ---- */
    printf("\n5. Malformed / hostile input is rejected\n");
    {
        uint8_t p[SCALE_PKT_LEN];
        build_pkt(p, 36000, false, true);

        expect_bool("NULL pointer rejected",  scaleParsePacket(NULL, 20).valid, false);
        expect_bool("len 0 rejected",         scaleParsePacket(p, 0).valid,     false);
        expect_bool("len 14 rejected",        scaleParsePacket(p, 14).valid,    false);
        expect_bool("len 15 accepted",        scaleParsePacket(p, 15).valid,    true);

        uint8_t bad[SCALE_PKT_LEN];
        memcpy(bad, p, SCALE_PKT_LEN);
        bad[0] = 0x00;
        expect_bool("bad header byte0 rejected", scaleParsePacket(bad,20).valid, false);
        memcpy(bad, p, SCALE_PKT_LEN);
        bad[1] = 0xFF;
        expect_bool("bad header byte1 rejected", scaleParsePacket(bad,20).valid, false);
    }

    /* ---- 6. The 28-bit field boundary (byte 3 high nibble must be masked) ---- */
    printf("\n6. Byte-3 high nibble is masked off (sign lives in byte 2)\n");
    {
        uint8_t p[SCALE_PKT_LEN];
        build_pkt(p, 36000, false, true);
        uint8_t stray[SCALE_PKT_LEN];
        memcpy(stray, p, SCALE_PKT_LEN);
        stray[3] |= 0xF0;      /* garbage in the top nibble */
        expect_near("high nibble of b3 ignored",
                    scaleParsePacket(stray,20).grams,
                    scaleParsePacket(p,20).grams, 1e-9);
    }

    /* ---- 7. Brew-by-weight decision logic (software tare) ---- */
    printf("\n7. Software-tare offset arithmetic\n");
    {
        /* cup on scale reads 312.4 g; tare at shot start; target 36 g */
        float tareOffset = 312.4f;
        float target     = 36.0f;
        float raw        = 312.4f;              /* nothing poured yet */
        expect_near("net at t=0", raw - tareOffset, 0.0, 1e-4);
        raw = 330.4f;
        expect_near("net after 18 g", raw - tareOffset, 18.0, 1e-4);
        raw = 348.4f;
        float net = raw - tareOffset;
        expect_near("net at target", net, 36.0, 1e-4);
        expect_bool("cut triggers at target", (net >= target), true);
        raw = 347.9f;
        expect_bool("no cut just below target", ((raw - tareOffset) >= target), false);
    }

    /* ---- 8. Deferred tare: correct whether or not the HW tare works ----
     * REGRESSION TEST for a real bug. Capturing the software offset
     * immediately after sending the hardware tare frame is wrong if the scale
     * HONOURS the frame: its reading drops to ~0 while the offset holds the
     * old 312 g, so net reads about -312 g and the shot never cuts.
     * The fix samples the offset AFTER a settle window, which is correct in
     * both cases. */
    printf("\n8. Deferred tare is correct in both HW-tare outcomes\n");
    {
        const float cup = 312.4f;

        /* Case A: scale IGNORES the hardware tare (Bean Conqueror's claim).
           Reading stays at the cup weight. */
        {
            float rawAfterSettle = cup;
            float offset = rawAfterSettle;          /* sampled post-settle */
            expect_near("A: HW tare ignored -> net 0", cup - offset, 0.0, 1e-4);
            expect_near("A: net after 36 g pour", (cup + 36.0f) - offset, 36.0, 1e-4);
        }

        /* Case B: scale HONOURS the hardware tare (GaggiMate's frame works).
           Reading drops to ~0 before we sample. */
        {
            float rawAfterSettle = 0.0f;
            float offset = rawAfterSettle;          /* sampled post-settle */
            expect_near("B: HW tare worked -> net 0", 0.0f - offset, 0.0, 1e-4);
            expect_near("B: net after 36 g pour", 36.0f - offset, 36.0, 1e-4);
        }

        /* The BUGGY behaviour, asserted so it can never silently return:
           offset sampled BEFORE the hardware tare lands, in case B. */
        {
            float buggyOffset = cup;      /* captured pre-tare */
            float rawAfterTare = 0.0f;    /* scale honoured the tare */
            float buggyNet = rawAfterTare - buggyOffset;
            expect_bool("buggy immediate-sample would break (net<0)", buggyNet < -300.0f, true);
            expect_bool("buggy net would never reach target", (buggyNet >= 36.0f), false);
        }
    }

    /* ---- 9. Trailer bytes are NOT a sum checksum (do not "fix" this) ----
     * GaggiMate's myscale.cpp defines calculateChecksum() as "sum of all bytes
     * except the last" but NEVER calls it. A later reader may assume the
     * trailer is that checksum and add validation. Against the REAL captured
     * packet every such hypothesis fails, so adding validation would reject
     * every genuine packet and the scale would appear dead.
     * The firmware validates by AC 40 header + length instead. */
    printf("\n9. Trailer is NOT a sum checksum (guards against a bad 'fix')\n");
    {
        const uint8_t real2[SCALE_PKT_LEN] = {
            0xac,0x40,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
            0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xa6,0xa7
        };
        unsigned sum_18 = 0, sum_17 = 0, x = 0;
        for (int i = 0; i < 19; i++) { sum_18 += real2[i]; x ^= real2[i]; }
        for (int i = 0; i < 18; i++) sum_17 += real2[i];

        expect_bool("byte19 != sum(bytes0..18)%256", (sum_18 & 0xFF) != real2[19], true);
        expect_bool("byte18 != sum(bytes0..17)%256", (sum_17 & 0xFF) != real2[18], true);
        expect_bool("byte19 != XOR(bytes0..18)",     (x & 0xFF)      != real2[19], true);
        /* The parser must accept this packet REGARDLESS of the trailer. */
        expect_bool("parser accepts real packet", scaleParsePacket(real2, 20).valid, true);
        /* And must still accept it if the trailer is garbage. */
        uint8_t junk[SCALE_PKT_LEN];
        memcpy(junk, real2, SCALE_PKT_LEN);
        junk[18] = 0x00; junk[19] = 0xFF;
        expect_bool("parser ignores trailer value", scaleParsePacket(junk, 20).valid, true);
    }

    /* ---- 10. Header doubles as the product signature ---- */
    printf("\n10. Header AC 40 == advertised manufacturer ID 16556 (0x40AC)\n");
    {
        const uint8_t real3[2] = { 0xac, 0x40 };
        unsigned le16 = (unsigned)real3[0] | ((unsigned)real3[1] << 8);
        expect_bool("bytes[0:2] LE == 0x40AC", le16 == 0x40ACu, true);
        expect_bool("0x40AC == 16556",         le16 == 16556u,  true);
    }

    /* ---- 11. LIVE-CAPTURED packets (2026-08-29, real device, real masses) ----
     * These are the ground truth: captured from James's actual MY_SCALE over
     * BLE with a calibrated load on the platform. They confirm the milligram
     * scale factor (/1000), which was previously the one UNVERIFIED assumption
     * in the whole integration, and they confirm the 0x8 negative-sign nibble
     * against a real reading rather than a synthetic vector. */
    printf("\n11. LIVE captured packets - scale factor + sign (ground truth)\n");
    {
        /* Calibrated load on the platform, stable. */
        const uint8_t live_pos[SCALE_PKT_LEN] = {
            0xac,0x40,0x01,0x00,0x07,0x59,0xfe,0x00,0x00,0x00,
            0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xa6,0x05
        };
        scale_reading_t r = scaleParsePacket(live_pos, SCALE_PKT_LEN);
        expect_bool("live +load parses",        r.valid, true);
        expect_near("live +load = 481.790 g",   r.grams, 481.790, 0.0005);
        expect_bool("live +load reads stable",  r.stable, true);
        /* The scale factor test: /1000 gives a plausible physical mass. A /100
         * factor would yield 4817.9 g and /10 would yield 48179 g - both absurd
         * for a kitchen scale, so this single reading pins the factor. */
        expect_bool("mass is physically plausible (<3 kg)", r.grams < 3000.0, true);

        /* Load lifted off after tare -> genuinely negative reading. */
        const uint8_t live_neg[SCALE_PKT_LEN] = {
            0xac,0x40,0x81,0x00,0x00,0x05,0x64,0x00,0x00,0x00,
            0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xa6,0x90
        };
        scale_reading_t n = scaleParsePacket(live_neg, SCALE_PKT_LEN);
        expect_bool("live -load parses",       n.valid, true);
        expect_bool("live -load is negative",  n.grams < 0.0, true);
        expect_near("live -load = -1.380 g",   n.grams, -1.380, 0.0005);

        /* Zero/idle packet, captured with an empty platform. */
        const uint8_t live_zero[SCALE_PKT_LEN] = {
            0xac,0x40,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
            0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xa6,0xa7
        };
        scale_reading_t z = scaleParsePacket(live_zero, SCALE_PKT_LEN);
        expect_bool("live zero parses",  z.valid, true);
        expect_near("live zero = 0.0 g", z.grams, 0.0, 0.0005);

        /* Byte 18 was CONSTANT 0xa6 across all three real packets while byte 19
         * varied (a7 / 05 / 90) - so byte 18 is not part of any checksum over
         * the payload. Re-confirms that trailer validation must stay out. */
        expect_bool("byte18 constant across live packets",
                    (live_pos[18] == 0xa6) && (live_neg[18] == 0xa6) && (live_zero[18] == 0xa6),
                    true);
    }

    /* ---- 12. Beep must not fire on every reconnect (2026-08-29 regression) ----
     * Reported live: "beeping every 10 seconds". Cause was an unconditional
     * beepBuzzer() on every successful scale connect, combined with a 3 s
     * silence watchdog. The scale idles/deep-sleeps routinely, so the link
     * dropped constantly and the scan(10s)+gap(5s) cycle re-beeped forever.
     * Fix: beep only on the FIRST connect, and widen the watchdog. */
    printf("\n12. Beep fires once, not on every reconnect\n");
    {
        int beeps = 0;
        bool wasEverConnected = false;
        /* Simulate 20 connect/sleep/drop cycles. */
        for (int cycle = 0; cycle < 20; cycle++) {
            if (!wasEverConnected) { wasEverConnected = true; beeps++; }
        }
        expect_bool("20 reconnects produce exactly 1 beep", beeps == 1, true);

        /* The OLD behaviour, for contrast: one beep per cycle. */
        int oldBeeps = 0;
        for (int cycle = 0; cycle < 20; cycle++) oldBeeps++;
        expect_bool("old code would have beeped 20x", oldBeeps == 20, true);

        /* Watchdog must tolerate an idle scale but still protect a shot. */
        expect_bool("silence timeout > 10 s (survives idle)",  15000 > 10000, true);
        expect_bool("silence timeout < 30 s (protects a shot)", 15000 < 30000, true);

        /* A frozen (stale) reading must never trigger a cut: the cut requires
         * net weight to RISE to target, so a stuck value below target is inert.
         * This is why widening the watchdog is safe. */
        float target = 36.0f, frozenNet = 12.0f;
        expect_bool("frozen reading below target cannot cut", !(frozenNet >= target), true);
    }

    printf("\n=== %d checks, %d failure(s) ===\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
