//---------------------------------------------------------------------------

#include "rp2040_ws2812.h"
#include "javelin/hal/rgb.h"
#include "rp2040_dma.h"
#include "rp2040_ws2812.pio.h"
#include <hardware/clocks.h>
#include <hardware/pio.h>
#include <hardware/timer.h>

//---------------------------------------------------------------------------

#if JAVELIN_RGB

//---------------------------------------------------------------------------

const PIO PIO_INSTANCE = pio0;
const int STATE_MACHINE_INDEX = 2;

#if JAVELIN_SPLIT
#define JAVELIN_RGB_RIGHT_COUNT (JAVELIN_RGB_COUNT - JAVELIN_RGB_LEFT_COUNT)
#endif

//---------------------------------------------------------------------------

Ws2812::Ws2812Data Ws2812::instance;

//---------------------------------------------------------------------------

void Ws2812::Initialize() {
  const int CYCLES_PER_BIT = 10;
  const int FREQUENCY = 800000;

  uint offset = pio_add_program(PIO_INSTANCE, &ws2812_program);

  pio_gpio_init(PIO_INSTANCE, JAVELIN_RGB_PIN);
  pio_sm_set_consecutive_pindirs(PIO_INSTANCE, STATE_MACHINE_INDEX,
                                 JAVELIN_RGB_PIN, 1, true);
  pio_sm_config config = ws2812_program_get_default_config(offset);
  sm_config_set_sideset_pins(&config, JAVELIN_RGB_PIN);
  sm_config_set_out_pins(&config, JAVELIN_RGB_PIN, 1);
  sm_config_set_out_shift(&config, false, true, 24);
  sm_config_set_fifo_join(&config, PIO_FIFO_JOIN_TX);
  int div = clock_get_hz(clk_sys) / (FREQUENCY * CYCLES_PER_BIT >> 8);
  config.clkdiv = div << 8;
  pio_sm_init(PIO_INSTANCE, STATE_MACHINE_INDEX, offset, &config);
  pio_sm_set_enabled(PIO_INSTANCE, STATE_MACHINE_INDEX, true);

  dma1->destination = &PIO_INSTANCE->txf[STATE_MACHINE_INDEX];

  Rp2040DmaControl dmaControl = {
      .enable = true,
      .dataSize = Rp2040DmaControl::DataSize::WORD,
      .incrementRead = true,
      .incrementWrite = false,
      .chainToDma = 1,
      .transferRequest = Rp2040DmaTransferRequest::PIO0_TX2,
      .sniffEnable = false,
  };
  dma1->control = dmaControl;

  // clang-format off
#if JAVELIN_SPLIT
  #if JAVELIN_LEFT_COUNT == JAVELIN_RIGHT_COUNT
    dma1->count = JAVELIN_RGB_LEFT_COUNT;
  #else
    dma1->count = Split::IsLeft() ? JAVELIN_RGB_LEFT_COUNT
                                  : JAVELIN_RGB_SLAVE_COUNT;
  #endif
#else
  dma1->count = JAVELIN_RGB_COUNT;
#endif // JAVELIN_SPLIT
  // clang-format on
}

void Ws2812::Ws2812Data::Update() {
  if (!dirty) {
    return;
  }

  // Don't update more than once every 10ms.
  uint32_t now = time_us_32();
  uint32_t timeSinceLastUpdate = now - lastUpdate;
  if (timeSinceLastUpdate < 10000) {
    return;
  }

  dirty = false;
  lastUpdate = now;

#if JAVELIN_SPLIT
  dma1->sourceTrigger =
      Split::IsMaster() ? pixelValues : pixelValues + JAVELIN_RGB_LEFT_COUNT;
#else
  dma1->sourceTrigger = &pixelValues;
#endif
}

#if JAVELIN_SPLIT
void Ws2812::Ws2812Data::UpdateBuffer(TxBuffer &buffer) {
  if (!slaveDirty) {
    return;
  }

  slaveDirty = false;
  if (Split::IsLeft()) {
    buffer.Add(SplitHandlerId::RGB, pixelValues + JAVELIN_RGB_LEFT_COUNT,
               sizeof(uint32_t) * JAVELIN_RGB_RIGHT_COUNT);
  } else {
    buffer.Add(SplitHandlerId::RGB, pixelValues,
               sizeof(uint32_t) * JAVELIN_RGB_LEFT_COUNT);
  }
}

void Ws2812::Ws2812Data::OnDataReceived(const void *data, size_t length) {
  dirty = true;
  if (Split::IsLeft()) {
    memcpy(pixelValues, data, sizeof(uint32_t) * JAVELIN_RGB_LEFT_COUNT);
  } else {
    memcpy(pixelValues + JAVELIN_RGB_LEFT_COUNT, data,
           sizeof(uint32_t) * JAVELIN_RGB_RIGHT_COUNT);
  }
}
#endif

//---------------------------------------------------------------------------

void Rgb::SetRgb(size_t id, int r, int g, int b) {
  Ws2812::SetRgb(id, r, g, b);
}

size_t Rgb::GetCount() { return JAVELIN_RGB_COUNT; }

//---------------------------------------------------------------------------

#endif // JAVELIN_RGB

//---------------------------------------------------------------------------
