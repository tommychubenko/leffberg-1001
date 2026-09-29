/**
 * MAX98357 amp self-test (same pins as TuyaOpen your_chat_bot):
 *   BCLK=GPIO15  LRC/WS=GPIO16  DIN=GPIO7  SD=GPIO17
 *
 * Plays a rhythmic 440 Hz "beep-beep-beep" (0.2s on / 0.3s off), not a
 * continuous squeal — so you can tell real PCM from Class-D hiss.
 *
 * Hold BOOT (GPIO0) to mute. Re-flash your_chat_bot when done.
 */
#include <math.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "esp_check.h"
#include "esp_log.h"

#define TAG "tone"
#define I2S_BCLK   GPIO_NUM_15
#define I2S_WS     GPIO_NUM_16
#define I2S_DOUT   GPIO_NUM_7
#define SPK_SD     GPIO_NUM_17 /* MAX98357 SD: HIGH=unmute, LOW=mute */
#define BOOT_BTN   GPIO_NUM_0
#define SAMPLE_RATE 16000
#define TONE_HZ     440
#define AMPLITUDE   0.45f

static i2s_chan_handle_t s_tx;

static void amp_unmute(bool on)
{
    gpio_set_level(SPK_SD, on ? 1 : 0);
}

static void tone_task(void *arg)
{
    const size_t frames = 256; /* stereo frames */
    /* L+R samples per frame */
    int32_t *buf = calloc(frames * 2, sizeof(int32_t));
    assert(buf);

    double phase = 0.0;
    const double step = 2.0 * M_PI * (double)TONE_HZ / (double)SAMPLE_RATE;
    uint32_t sample_i = 0;
    const uint32_t period = SAMPLE_RATE / 2;      /* 0.5 s cycle */
    const uint32_t on_len = SAMPLE_RATE / 5;      /* 0.2 s tone */

    ESP_LOGI(TAG, "Rhythmic %d Hz (0.2s on / 0.3s off) BCLK=%d WS=%d DIN=%d SD=%d",
             TONE_HZ, I2S_BCLK, I2S_WS, I2S_DOUT, SPK_SD);
    ESP_LOGI(TAG, "Hold BOOT to mute. Expect clear beeps, not constant squeal.");

    amp_unmute(true);

    while (1) {
        bool muted = (gpio_get_level(BOOT_BTN) == 0);
        amp_unmute(!muted);

        for (size_t i = 0; i < frames; i++) {
            float s = 0.0f;
            uint32_t pos = sample_i % period;
            if (!muted && pos < on_len) {
                s = sinf((float)phase) * AMPLITUDE;
                phase += step;
                if (phase > 2.0 * M_PI) {
                    phase -= 2.0 * M_PI;
                }
            }
            sample_i++;

            int32_t s32 = (int32_t)(s * 2147483647.0f);
            buf[2 * i] = s32;
            buf[2 * i + 1] = s32; /* L=R Philips stereo */
        }
        size_t written = 0;
        ESP_ERROR_CHECK(i2s_channel_write(s_tx, buf, frames * 2 * sizeof(int32_t), &written, portMAX_DELAY));
    }
}

void app_main(void)
{
    gpio_config_t sd_cfg = {
        .pin_bit_mask = 1ULL << SPK_SD,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&sd_cfg));
    amp_unmute(false);

    gpio_config_t boot_cfg = {
        .pin_bit_mask = 1ULL << BOOT_BTN,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&boot_cfg));

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &s_tx, NULL));

    /* Match TuyaOpen tkl_i2s TX: 32-bit Philips stereo L=R for MAX98357 */
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg =
            {
                .mclk = I2S_GPIO_UNUSED,
                .bclk = I2S_BCLK,
                .ws = I2S_WS,
                .dout = I2S_DOUT,
                .din = I2S_GPIO_UNUSED,
                .invert_flags =
                    {
                        .mclk_inv = false,
                        .bclk_inv = false,
                        .ws_inv = false,
                    },
            },
    };
    std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_BOTH;
    std_cfg.slot_cfg.slot_bit_width = I2S_SLOT_BIT_WIDTH_32BIT;

    ESP_ERROR_CHECK(i2s_channel_init_std_mode(s_tx, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(s_tx));

    xTaskCreate(tone_task, "tone", 4096, NULL, 5, NULL);
}
