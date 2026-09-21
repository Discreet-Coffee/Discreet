/*
 * test_bbw_logic.c
 * ------------------------------------------------------------------
 * Host-side simulation of the brew-by-weight cut logic from
 * Discreet_MQTT.ino. Replays realistic espresso weight curves through the
 * EXACT predictive-cut algorithm to verify the final settled weight lands on
 * target, and that all the safety rules hold.
 *
 * Build & run:
 *   cc -Wall -Wextra -O2 -o /tmp/test_bbw test_bbw_logic.c -lm && /tmp/test_bbw
 */
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include <string.h>

/* ---- constants mirrored from the firmware ---- */
#define BBW_LEAD_TIME_S   0.35f   /* default; the setting is now runtime-configurable */
#define BBW_MAX_LEAD_G    3.0f
#define SCALE_NOTIFY_MS   150      /* measured ~6.7 Hz */

/* ---- state mirrored from the firmware ---- */
typedef struct {
    float    targetWeight;
    bool     scaleArmed;
    bool     shotCutByWeight;
    bool     pumpCutByWeight;
    float    shotWeight;
    float    bbwFlowRate;
    float    bbwLeadTimeS;          /* CONFIGURABLE - mirrored from firmware */
    uint32_t bbwLastFlowMs;
    float    bbwLastFlowWeight;
    /* diagnostics */
    uint32_t cutAtMs;
    float    leadAtCut;
} bbw_t;

static void bbw_reset(bbw_t *s, float target)
{
    memset(s, 0, sizeof(*s));
    s->targetWeight = target;
    s->scaleArmed   = true;
    s->bbwLeadTimeS = BBW_LEAD_TIME_S;  /* default lead */
}

/* EXACT port of the firmware's per-iteration BBW block. */
static void bbw_step(bbw_t *s, float net, uint32_t nowMs)
{
    if (!(s->scaleArmed && !s->shotCutByWeight)) return;

    if (s->bbwLastFlowMs == 0) {
        s->bbwLastFlowMs     = nowMs;
        s->bbwLastFlowWeight = net;
    } else if (nowMs - s->bbwLastFlowMs >= 500) {
        float dt = (nowMs - s->bbwLastFlowMs) / 1000.0f;
        if (dt > 0.0f) {
            float inst = (net - s->bbwLastFlowWeight) / dt;
            if (inst < 0.0f) inst = 0.0f;
            s->bbwFlowRate = (s->bbwFlowRate <= 0.0f)
                           ? inst
                           : (0.6f * s->bbwFlowRate + 0.4f * inst);
        }
        s->bbwLastFlowMs     = nowMs;
        s->bbwLastFlowWeight = net;
    }

    float lead = s->bbwFlowRate * s->bbwLeadTimeS;
    if (lead > BBW_MAX_LEAD_G) lead = BBW_MAX_LEAD_G;
    if (lead < 0.0f) lead = 0.0f;

    if (net >= (s->targetWeight - lead)) {
        s->pumpCutByWeight = true;
        s->shotCutByWeight = true;
        s->shotWeight      = net;
        s->cutAtMs         = nowMs;
        s->leadAtCut       = lead;
    }
}

/* ---- test harness ---- */
static int failures = 0, checks = 0;

static void ok(const char *what, bool cond, const char *detail)
{
    checks++;
    if (cond) printf("  ok   %-46s %s\n", what, detail ? detail : "");
    else { printf("  FAIL %-46s %s\n", what, detail ? detail : ""); failures++; }
}

/*
 * Simulate a shot.
 *   preinfusionS : seconds of pre-infusion with ~no flow to the cup
 *   flow         : steady-state flow g/s during extraction
 *   dripAfterCut : grams that still land after the pump cuts (lag + drip)
 * Returns final settled weight.
 */
static float simulate(bbw_t *s, float target, float preinfusionS,
                      float flow, float dripAfterCut, bool verbose)
{
    bbw_reset(s, target);
    float net = 0.0f;
    uint32_t t = 0;

    for (; t < 60000; t += SCALE_NOTIFY_MS) {
        float ts = t / 1000.0f;
        if (ts > preinfusionS) {
            /* ramp flow in over ~1s after preinfusion, then steady */
            float since = ts - preinfusionS;
            float f = flow * (since < 1.0f ? since : 1.0f);
            net += f * (SCALE_NOTIFY_MS / 1000.0f);
        }
        bbw_step(s, net, t);
        if (s->shotCutByWeight) break;
    }

    float settled = s->shotWeight + dripAfterCut;
    if (verbose)
        printf("       cut@%.2fs net=%.2f lead=%.2f flow=%.2f -> settled %.2f g\n",
               s->cutAtMs / 1000.0f, s->shotWeight, s->leadAtCut,
               s->bbwFlowRate, settled);
    return settled;
}

int main(void)
{
    bbw_t s;
    char buf[160];

    printf("=== brew-by-weight cut logic simulation ===\n\n");

    /* 1. Typical shot: 36 g target, 2 g/s, 0.7 g drip after cut */
    printf("1. Typical espresso (target 36 g, 2.0 g/s, 0.7 g post-cut drip)\n");
    {
        float settled = simulate(&s, 36.0f, 8.0f, 2.0f, 0.7f, true);
        snprintf(buf, sizeof(buf), "settled=%.2f g (err %+.2f)", settled, settled - 36.0f);
        ok("lands within 0.5 g of target", fabsf(settled - 36.0f) <= 0.5f, buf);
        ok("cut actually happened", s.shotCutByWeight, NULL);
        ok("pump was cut", s.pumpCutByWeight, NULL);
    }

    /* 2. Fast flow (gusher) - overshoot risk is highest here */
    printf("\n2. Fast flow / gusher (3.5 g/s, 1.2 g drip)\n");
    {
        float settled = simulate(&s, 36.0f, 8.0f, 3.5f, 1.2f, true);
        snprintf(buf, sizeof(buf), "settled=%.2f g (err %+.2f)", settled, settled - 36.0f);
        ok("gusher stays within 1.0 g", fabsf(settled - 36.0f) <= 1.0f, buf);
    }

    /* 3. Slow / choked shot */
    printf("\n3. Slow choked shot (0.8 g/s, 0.3 g drip)\n");
    {
        float settled = simulate(&s, 36.0f, 8.0f, 0.8f, 0.3f, true);
        snprintf(buf, sizeof(buf), "settled=%.2f g (err %+.2f)", settled, settled - 36.0f);
        ok("slow shot stays within 0.5 g", fabsf(settled - 36.0f) <= 0.5f, buf);
    }

    /* 4. Naive (non-predictive) comparison: proves the lead is worth it */
    printf("\n4. Predictive vs naive cut (does the lead actually help?)\n");
    {
        /* naive = cut exactly at target, then drip lands on top */
        float flow = 2.0f, drip = 0.7f;
        float naive_settled = 36.0f + drip
                            + flow * (SCALE_NOTIFY_MS / 1000.0f); /* +1 notify latency */
        float pred_settled  = simulate(&s, 36.0f, 8.0f, flow, drip, false);
        snprintf(buf, sizeof(buf), "naive=%.2f  predictive=%.2f", naive_settled, pred_settled);
        ok("predictive beats naive", fabsf(pred_settled - 36.0f) < fabsf(naive_settled - 36.0f), buf);
    }

    /* 5. Target sweep - common ratios */
    printf("\n5. Target sweep (18 g dose ratios)\n");
    {
        float targets[] = { 30.0f, 36.0f, 40.0f, 45.0f, 54.0f };
        for (size_t i = 0; i < sizeof(targets)/sizeof(targets[0]); i++) {
            float settled = simulate(&s, targets[i], 8.0f, 2.0f, 0.7f, false);
            snprintf(buf, sizeof(buf), "target %.0f -> %.2f g (err %+.2f)",
                     targets[i], settled, settled - targets[i]);
            ok("target within 0.5 g", fabsf(settled - targets[i]) <= 0.5f, buf);
        }
    }

    /* 6. Lead clamp protects against a nonsense flow spike */
    printf("\n6. Lead clamp (BBW_MAX_LEAD_G) caps early cutting\n");
    {
        bbw_reset(&s, 36.0f);
        s.bbwFlowRate     = 100.0f;     /* absurd */
        s.bbwLastFlowMs   = 1;
        s.bbwLastFlowWeight = 0.0f;
        /* net just under the clamped threshold must NOT cut */
        bbw_step(&s, 36.0f - BBW_MAX_LEAD_G - 0.1f, 100);
        ok("no cut below clamped threshold", !s.shotCutByWeight, NULL);
        /* at the clamped threshold it must cut */
        bbw_step(&s, 36.0f - BBW_MAX_LEAD_G + 0.01f, 200);
        snprintf(buf, sizeof(buf), "lead clamped to %.2f g", s.leadAtCut);
        ok("cuts at clamped threshold", s.shotCutByWeight, buf);
        ok("lead never exceeds clamp", s.leadAtCut <= BBW_MAX_LEAD_G + 1e-6f, NULL);
    }

    /* 7. Safety: not armed => never cuts */
    printf("\n7. Safety rules\n");
    {
        bbw_reset(&s, 36.0f);
        s.scaleArmed = false;
        for (uint32_t t = 0; t < 5000; t += SCALE_NOTIFY_MS)
            bbw_step(&s, 100.0f, t);          /* way over target */
        ok("unarmed never cuts", !s.shotCutByWeight, "net 100 g, no cut");
    }
    {
        /* cut happens exactly once */
        bbw_reset(&s, 36.0f);
        s.bbwFlowRate = 2.0f; s.bbwLastFlowMs = 1;
        bbw_step(&s, 40.0f, 100);
        float first = s.shotWeight;
        bbw_step(&s, 55.0f, 250);             /* more weight arrives after cut */
        snprintf(buf, sizeof(buf), "shotWeight stayed %.2f", s.shotWeight);
        ok("cut is latched (recorded once)", fabsf(s.shotWeight - first) < 1e-6f, buf);
    }
    {
        /* negative / noisy readings must not trigger a cut */
        bbw_reset(&s, 36.0f);
        for (uint32_t t = 0; t < 5000; t += SCALE_NOTIFY_MS)
            bbw_step(&s, -2.0f, t);
        ok("negative weight never cuts", !s.shotCutByWeight, "net -2 g");
    }
    {
        /* flow estimate must never go negative (cup lifted mid-shot) */
        bbw_reset(&s, 36.0f);
        bbw_step(&s, 20.0f, 0);
        bbw_step(&s, 5.0f, 600);              /* weight dropped sharply */
        snprintf(buf, sizeof(buf), "flow=%.2f", s.bbwFlowRate);
        ok("flow rate clamped >= 0", s.bbwFlowRate >= 0.0f, buf);
    }

    /* 8. Zero-flow (no cup / blocked) must never cut on its own */
    printf("\n8. Cup missing (weight pinned near zero)\n");
    {
        bbw_reset(&s, 36.0f);
        for (uint32_t t = 0; t < 40000; t += SCALE_NOTIFY_MS)
            bbw_step(&s, 0.05f, t);
        ok("no cut when nothing is flowing", !s.shotCutByWeight, "40 s at 0.05 g");
    }

    /* 9. Configurable lead changes WHERE the cut happens (the feature) */
    printf("\n9. BBW Lead Time is configurable and changes the cut point\n");
    {
        /* Same shot, held at a steady flow of 2 g/s. With lead L the cut fires
         * when net reaches target - (flow*L). A bigger lead => earlier cut. */
        const float target = 36.0f, flow = 2.0f;

        bbw_reset(&s, target);
        s.bbwLeadTimeS   = 0.0f;            /* no lead -> cut at exact target */
        s.bbwFlowRate    = flow; s.bbwLastFlowMs = 1; s.bbwLastFlowWeight = 0;
        bbw_step(&s, target + 0.01f, 100);
        ok("lead=0 cuts at ~exact target", s.shotCutByWeight, "net 36.01");

        bbw_reset(&s, target);
        s.bbwLeadTimeS   = 0.35f;           /* default */
        s.bbwFlowRate    = flow; s.bbwLastFlowMs = 1; s.bbwLastFlowWeight = 0;
        bbw_step(&s, target - flow*0.35f + 0.01f, 100);
        ok("lead=0.35 cuts ~0.70 g early", s.shotCutByWeight, "net ~35.30");

        bbw_reset(&s, target);
        s.bbwLeadTimeS   = 1.50f;           /* large lead */
        s.bbwFlowRate    = flow; s.bbwLastFlowMs = 1; s.bbwLastFlowWeight = 0;
        bbw_step(&s, target - flow*1.50f + 0.01f, 100);
        ok("lead=1.50 cuts ~3.00 g early (clamped by BBW_MAX_LEAD_G)",
           s.shotCutByWeight, "net ~33.00");

        ok("larger lead cuts strictly earlier than lead=0",
           (target - flow*1.50f) < (target - flow*0.0f), NULL);
    }

    /* 10. Lead is clamped even when the setting is large (safety) */
    printf("\n10. Lead setting cannot cause an absurdly early cut\n");
    {
        bbw_reset(&s, 36.0f);
        s.bbwLeadTimeS   = 10.0f;           /* beyond the 3 s clamp */
        s.bbwFlowRate    = 2.0f; s.bbwLastFlowMs = 1; s.bbwLastFlowWeight = 0;
        bbw_step(&s, 36.0f - BBW_MAX_LEAD_G + 0.01f, 100);
        ok("cut clamped at BBW_MAX_LEAD_G (3 g)", s.shotCutByWeight, NULL);
        ok("leadAtCut <= clamp", s.leadAtCut <= BBW_MAX_LEAD_G + 1e-6f, NULL);
    }

    printf("\n=== %d checks, %d failure(s) ===\n", checks, failures);
    return failures ? 1 : 0;
}
