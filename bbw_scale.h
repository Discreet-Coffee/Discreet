#pragma once
#include "bbw_config.h"

// BBW state variables: plain types so they are always available (e.g. for SD config)
extern float targetWeight;
extern bool bbwEnabled;
extern bool cutOnScaleLoss;
extern float bbwLeadTimeS;
extern float shotWeight;
extern float bbwFlowRate;
extern volatile float scaleRawWeight;
extern volatile bool scaleStable;
extern volatile bool scaleConnected;

#if DISCREET_ENABLE_BBW

#include <NimBLEDevice.h>

#define SCALE_SERVICE_UUID   "0000FFB0-0000-1000-8000-00805F9B34FB"
#define SCALE_WEIGHT_UUID    "0000FFB2-0000-1000-8000-00805F9B34FB"
#define SCALE_COMMAND_UUID   "0000FFB1-0000-1000-8000-00805F9B34FB"

#define SCALE_PKT_MINLEN     15
#define SCALE_HDR0           0xAC
#define SCALE_HDR1           0x40

#define SCALE_SILENCE_MS     15000
#define SCALE_TARE_SETTLE_MS 400
#define BBW_MAX_LEAD_G       3.0f

extern volatile uint32_t scaleLastPacketMs;
extern bool scaleCanNotify;
extern uint32_t scalePkts;
extern uint32_t scaleCbRaw;
extern bool scaleChrFound;
extern bool scaleNotifyEnabled;
extern bool scaleWasEverConnected;

extern float scaleTareOffset;
extern uint32_t scaleTareSettleAt;
extern bool scaleArmed;
extern bool pumpCutByWeight;
extern bool shotCutByWeight;

extern uint32_t bbwLastFlowMs;
extern float bbwLastFlowWeight;

float scaleNetWeight();
void scaleTareNow();
void scaleSendTare();
void scaleServiceTare();
void scaleTask(void* param);

#endif // DISCREET_ENABLE_BBW
