#pragma once
// Minimal WS2812B driver on the RMT peripheral (legacy IDF 4.4 driver, part of the Arduino
// core): a few pixels on one pin. The core's neopixelWrite() only drives one pixel.
// Non-blocking: ws2812Show() copies the frame into the RMT memory and returns, the hardware
// clocks it out (~30 us per pixel). The line idles low (R8 pull-down holds it low before).
// Uses RMT channel 0 with the memory of channel 1 too (2 x 48 items: the whole frame fits, no
// refill interrupt); nothing else in the firmware uses RMT.
#include <driver/rmt.h>

static const rmt_channel_t WS2812_CH = RMT_CHANNEL_0;
static const uint8_t WS2812_MAX = 2;   // pixels; 24 items each, must fit 2 RMT blocks (96 items)
// 40 MHz ticks (25 ns), within both the WS2812B and the WS2812B-2020 datasheets:
// 0 = 0.30 us high + 0.90 us low, 1 = 0.90 us high + 0.35 us low; reset = idle low (> 280 us)
static const uint16_t WS2812_T0H = 12, WS2812_T0L = 36, WS2812_T1H = 36, WS2812_T1L = 14;
static bool ws2812Ready = false;
static uint8_t ws2812Count = 0;

static bool ws2812Begin(uint8_t pin, uint8_t count)
{
    ws2812Count = count > WS2812_MAX ? WS2812_MAX : count;
    rmt_config_t c = RMT_DEFAULT_CONFIG_TX((gpio_num_t)pin, WS2812_CH);
    c.clk_div = 2;          // 80 MHz APB / 2
    c.mem_block_num = 2;
    c.tx_config.idle_output_en = true;
    c.tx_config.idle_level = RMT_IDLE_LEVEL_LOW;
    ws2812Ready = rmt_config(&c) == ESP_OK && rmt_driver_install(WS2812_CH, 0, 0) == ESP_OK;
    return ws2812Ready;
}

// rgb: 3 bytes per pixel (R, G, B); the pixels take G, R, B, most significant bit first
static void ws2812Show(const uint8_t* rgb)
{
    if (!ws2812Ready) return;
    static rmt_item32_t items[WS2812_MAX * 24];
    rmt_item32_t* it = items;
    for (uint8_t p = 0; p < ws2812Count; p++) {
        const uint8_t grb[3] = {rgb[p * 3 + 1], rgb[p * 3], rgb[p * 3 + 2]};
        for (uint8_t b : grb)
            for (int i = 7; i >= 0; i--, it++) {
                bool one = b & (1 << i);
                it->level0 = 1; it->duration0 = one ? WS2812_T1H : WS2812_T0H;
                it->level1 = 0; it->duration1 = one ? WS2812_T1L : WS2812_T0L;
            }
    }
    rmt_write_items(WS2812_CH, items, it - items, false);
}
