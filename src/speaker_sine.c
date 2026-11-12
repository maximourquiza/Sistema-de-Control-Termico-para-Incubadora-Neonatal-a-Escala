#include "speaker_sine.h"
#include "LPC17xx.h"
#include "lpc17xx_clkpwr.h"
#include "lpc17xx_dac.h"
#include "lpc17xx_gpdma.h"

#include <cr_section_macros.h>
#include <math.h>
#include <stdint.h>

#define SINE_SAMPLE_COUNT 100U
#define SINE_FREQUENCY_HZ 1000U
#define SINE_SAMPLE_RATE_HZ (SINE_SAMPLE_COUNT * SINE_FREQUENCY_HZ)
#define SINE_PI 3.14159265f

/* El enlazador reserva ambas regiones en RAM accesible por el DMA. */
__BSS(RAM2) __attribute__((aligned(4))) static uint32_t sine_samples[SINE_SAMPLE_COUNT];
__BSS(RAM2) __attribute__((aligned(4))) static GPDMA_LLI_T sine_descriptor;

bool SpeakerSine_Init(void) {
    const DAC_CONVERTER_CFG_T dac_disabled = {DISABLE, DISABLE, DISABLE};
    const DAC_CONVERTER_CFG_T dac_running = {DISABLE, ENABLE, ENABLE};
    DAC_Init(); /* P0.26 / AOUT, PCLK = CCLK / 4. */
    DAC_ConfigDAConverterControl(&dac_disabled);
    DAC_SetBias(DAC_700uA);
    DAC_UpdateValue(512U);

    const uint32_t timeout = CLKPWR_GetPCLK(CLKPWR_PCLKSEL_DAC) / SINE_SAMPLE_RATE_HZ;
    if (timeout == 0U || timeout > UINT16_MAX) {
        return false;
    }

    for (uint32_t i = 0; i < SINE_SAMPLE_COUNT; ++i) {
        const float value = 511.5f + 127.9f * sinf(2.0f * SINE_PI * i / SINE_SAMPLE_COUNT);
        sine_samples[i] = DAC_VALUE((uint32_t)(value + 0.5f));
    }

    sine_descriptor.srcAddr = (uint32_t)sine_samples;
    sine_descriptor.dstAddr = (uint32_t)&LPC_DAC->DACR;
    sine_descriptor.nextLLI = (uint32_t)&sine_descriptor;
    sine_descriptor.control = GPDMA_DMACCxControl_TransferSize(SINE_SAMPLE_COUNT) |
        GPDMA_DMACCxControl_SBSize(GPDMA_BSIZE_1) |
        GPDMA_DMACCxControl_DBSize(GPDMA_BSIZE_1) |
        GPDMA_DMACCxControl_SWidth(GPDMA_WORD) |
        GPDMA_DMACCxControl_DWidth(GPDMA_WORD) |
        GPDMA_DMACCxControl_SI;

    const GPDMA_Channel_CFG_T dma_config = {
        .channelNum = GPDMA_CH_7,
        .transferSize = SINE_SAMPLE_COUNT,
        .type = GPDMA_M2P,
        .srcMemAddr = (uint32_t)sine_samples,
        .dstConn = GPDMA_DAC,
        .src = {GPDMA_WORD, GPDMA_BSIZE_1, ENABLE},
        .dst = {GPDMA_WORD, GPDMA_BSIZE_1, DISABLE},
        .intTC = DISABLE,
        .intErr = DISABLE,
        .linkedList = (uint32_t)&sine_descriptor
    };

    /* Unica inicializacion global de GPDMA: sensor y UART usan sondeo. */
    GPDMA_Init();
    if (GPDMA_SetupChannel(&dma_config) != SUCCESS) {
        GPDMA_ChannelStop(GPDMA_CH_7);
        return false;
    }
    DAC_SetDMATimeOut((uint16_t)timeout);
    __DMB(); /* Publicar tabla y descriptor antes de habilitar el canal. */
    GPDMA_ChannelStart(GPDMA_CH_7);
    DAC_ConfigDAConverterControl(&dac_running);
    return true;
}
