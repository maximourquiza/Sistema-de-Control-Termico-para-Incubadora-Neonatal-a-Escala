/* Driver real con funciones de biblioteca simuladas, sin tocar la placa. */
#include "../src/speaker_sine.c"

#define CHECK(condition) do { if (!(condition)) return __LINE__; } while (0)

static unsigned int global_init_calls, setup_calls, start_calls, stop_calls;
static unsigned int graceful_calls, dac_config_calls, order, order_error;
static uint32_t dac_value, timeout_value, pclk = 25000000;
static Status setup_status = SUCCESS;
static DAC_CONVERTER_CFG_T dac_state;
static GPDMA_Channel_CFG_T configured_channel;

uint32_t CLKPWR_GetPCLK(uint32_t clock) { (void)clock; return pclk; }
void DAC_Init(void) {}
void DAC_SetBias(DAC_MAX_CURRENT bias) { (void)bias; }
void DAC_UpdateValue(uint32_t value) {
    dac_value = value;
    if (order == 3) order = 4;
}
void DAC_ConfigDAConverterControl(const DAC_CONVERTER_CFG_T *config) {
    dac_state = *config;
    ++dac_config_calls;
    if (!config->dmaRequest && order == 0) order = 1;
}
void DAC_SetDMATimeOut(uint16_t timeout) { timeout_value = timeout; }
void GPDMA_Init(void) { ++global_init_calls; }
Status GPDMA_SetupChannel(const GPDMA_Channel_CFG_T *config) {
    ++setup_calls;
    configured_channel = *config;
    return setup_status;
}
void GPDMA_ChannelStart(GPDMA_CH channel) {
    if (channel != GPDMA_CH_7 || dac_state.dmaRequest != DISABLE) order_error = 1;
    ++start_calls;
}
void GPDMA_ChannelGracefulStop(GPDMA_CH channel) {
    if (channel != GPDMA_CH_7 || order != 1 || dac_state.dmaCounter != DISABLE) order_error = 1;
    order = 2;
    ++graceful_calls;
}
void GPDMA_ChannelStop(GPDMA_CH channel) {
    if (channel != GPDMA_CH_7) order_error = 1;
    if (order == 2) order = 3;
    ++stop_calls;
}

int run_speaker_sine_tests(void) {
    CHECK(!SpeakerSine_Start());
    SpeakerSine_Stop();
    CHECK(start_calls == 0 && stop_calls == 0);
    /* Fallo de reloj: permanecer silencioso, sin inicializar globalmente DMA. */
    pclk = 0;
    CHECK(!SpeakerSine_Init());
    CHECK(dac_state.dmaRequest == DISABLE && dac_value == 512);
    CHECK(global_init_calls == 0 && !SpeakerSine_Start());
    pclk = 25000000;
    setup_status = ERROR;
    CHECK(!SpeakerSine_Init());
    CHECK(!SpeakerSine_Start() && start_calls == 0);
    setup_status = SUCCESS;
    CHECK(SpeakerSine_Init());
    CHECK(dac_state.dmaRequest == DISABLE && dac_state.dmaCounter == DISABLE);
    CHECK(dac_value == 512 && timeout_value == 250 && start_calls == 0);
    CHECK(configured_channel.channelNum == GPDMA_CH_7);
    CHECK(configured_channel.transferSize == 100);
    CHECK(configured_channel.srcMemAddr == (uint32_t)sine_samples);
    CHECK(configured_channel.linkedList == (uint32_t)&sine_descriptor);
    CHECK(sine_descriptor.nextLLI == (uint32_t)&sine_descriptor);
    CHECK(sine_samples[0] == DAC_VALUE(512) && sine_samples[25] == DAC_VALUE(639));
    const unsigned int initializations = global_init_calls;
    const unsigned int configured = setup_calls;
    CHECK(SpeakerSine_Init() && setup_calls == configured);
    CHECK(SpeakerSine_Start());
    CHECK(start_calls == 1 && setup_calls == configured + 1);
    CHECK(dac_state.dmaRequest == ENABLE && dac_state.dmaCounter == ENABLE);
    const unsigned int configs = dac_config_calls;
    CHECK(SpeakerSine_Start());
    CHECK(start_calls == 1 && setup_calls == configured + 1 && dac_config_calls == configs);
    for (unsigned int i = 0; i < 4; ++i) {
        order = 0;
        SpeakerSine_Stop();
        CHECK(order == 4 && !order_error && dac_value == 512);
        CHECK(!speaker_running && dac_state.dmaRequest == DISABLE);
        const unsigned int stopped = stop_calls;
        SpeakerSine_Stop();
        CHECK(stop_calls == stopped);
        CHECK(SpeakerSine_Start());
        CHECK(configured_channel.srcMemAddr == (uint32_t)sine_samples);
        CHECK(configured_channel.transferSize == 100);
    }
    CHECK(global_init_calls == initializations && graceful_calls == 4);
    order = 0;
    SpeakerSine_Stop();
    setup_status = ERROR;
    CHECK(!SpeakerSine_Start());
    CHECK(!speaker_initialized && !speaker_running);
    CHECK(dac_state.dmaRequest == DISABLE && dac_value == 512);
    CHECK(!order_error);
    return 0;
}
