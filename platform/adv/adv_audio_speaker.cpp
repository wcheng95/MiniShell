#include <cstdint>

#include "adv_i2c.h"
#include "adv_internal.h"

#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"

namespace {
constexpr minishell_backend_audio_t kSpeakerHandle = 2u;
constexpr uint32_t kSampleRate = 48000u;
constexpr uint32_t kChannels = 1u;
constexpr uint32_t kBitsPerSample = 16u;
constexpr int kSpeakerVolume = 80;

constexpr gpio_num_t kBclk = GPIO_NUM_41;
constexpr gpio_num_t kDout = GPIO_NUM_42;
constexpr gpio_num_t kWs = GPIO_NUM_43;
constexpr gpio_num_t kDin = GPIO_NUM_46;

i2s_chan_handle_t s_i2s_tx = nullptr;
i2s_chan_handle_t s_i2s_rx = nullptr;
const audio_codec_data_if_t *s_data_if = nullptr;
const audio_codec_ctrl_if_t *s_ctrl_if = nullptr;
const audio_codec_gpio_if_t *s_gpio_if = nullptr;
const audio_codec_if_t *s_codec_if = nullptr;
esp_codec_dev_handle_t s_codec = nullptr;
bool s_open = false;
bool s_started = false;

bool endpoint_supported(const char *endpoint)
{
    if (endpoint == nullptr) return true;
    const char expected[] = "speaker";
    for (uint32_t i = 0u;; ++i) {
        if (endpoint[i] != expected[i]) return false;
        if (expected[i] == '\0') return true;
    }
}

void cleanup_hardware()
{
    if (s_codec != nullptr) {
        (void)esp_codec_dev_close(s_codec);
        esp_codec_dev_delete(s_codec);
        s_codec = nullptr;
    }
    if (s_codec_if != nullptr) {
        audio_codec_delete_codec_if(s_codec_if);
        s_codec_if = nullptr;
    }
    if (s_ctrl_if != nullptr) {
        audio_codec_delete_ctrl_if(s_ctrl_if);
        s_ctrl_if = nullptr;
    }
    if (s_gpio_if != nullptr) {
        audio_codec_delete_gpio_if(s_gpio_if);
        s_gpio_if = nullptr;
    }
    if (s_data_if != nullptr) {
        audio_codec_delete_data_if(s_data_if);
        s_data_if = nullptr;
    }
    if (s_i2s_rx != nullptr) {
        (void)i2s_channel_disable(s_i2s_rx);
        (void)i2s_del_channel(s_i2s_rx);
        s_i2s_rx = nullptr;
    }
    if (s_i2s_tx != nullptr) {
        (void)i2s_channel_disable(s_i2s_tx);
        (void)i2s_del_channel(s_i2s_tx);
        s_i2s_tx = nullptr;
    }
    s_open = false;
    s_started = false;
}

bool prepare_i2s()
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    /* 4 x 120 frames = 10 ms total buffering at 48 kHz. This is the
     * field-tested Mini-CW sidetone setting and keeps self-monitor latency low. */
    chan_cfg.dma_desc_num = 4;
    chan_cfg.dma_frame_num = 120;
    if (i2s_new_channel(&chan_cfg, &s_i2s_tx, &s_i2s_rx) != ESP_OK) return false;

    i2s_std_config_t rx_cfg = {};
    rx_cfg.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(kSampleRate);
    rx_cfg.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
        I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO);
    rx_cfg.gpio_cfg.mclk = GPIO_NUM_NC;
    rx_cfg.gpio_cfg.bclk = kBclk;
    rx_cfg.gpio_cfg.ws = kWs;
    rx_cfg.gpio_cfg.dout = GPIO_NUM_NC;
    rx_cfg.gpio_cfg.din = kDin;
    if (i2s_channel_init_std_mode(s_i2s_rx, &rx_cfg) != ESP_OK) return false;

    i2s_std_config_t tx_cfg = {};
    tx_cfg.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(kSampleRate);
    tx_cfg.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
        I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO);
    tx_cfg.gpio_cfg.mclk = GPIO_NUM_NC;
    tx_cfg.gpio_cfg.bclk = kBclk;
    tx_cfg.gpio_cfg.ws = kWs;
    tx_cfg.gpio_cfg.dout = kDout;
    tx_cfg.gpio_cfg.din = GPIO_NUM_NC;
    return i2s_channel_init_std_mode(s_i2s_tx, &tx_cfg) == ESP_OK;
}

bool prepare_codec()
{
    if (adv_i2c_prepare() != 0 || adv_i2c_bus() == nullptr) return false;

    audio_codec_i2s_cfg_t i2s_cfg = {};
    i2s_cfg.rx_handle = s_i2s_rx;
    i2s_cfg.tx_handle = s_i2s_tx;
    s_data_if = audio_codec_new_i2s_data(&i2s_cfg);
    if (s_data_if == nullptr) return false;

    audio_codec_i2c_cfg_t i2c_cfg = {};
    i2c_cfg.addr = ES8311_CODEC_DEFAULT_ADDR;
    i2c_cfg.bus_handle = adv_i2c_bus();
    s_ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
    if (s_ctrl_if == nullptr) return false;

    s_gpio_if = audio_codec_new_gpio();
    if (s_gpio_if == nullptr) return false;

    es8311_codec_cfg_t es8311_cfg = {};
    es8311_cfg.codec_mode = ESP_CODEC_DEV_WORK_MODE_BOTH;
    es8311_cfg.ctrl_if = s_ctrl_if;
    es8311_cfg.gpio_if = s_gpio_if;
    es8311_cfg.pa_pin = GPIO_NUM_NC;
    es8311_cfg.use_mclk = false;
    s_codec_if = es8311_codec_new(&es8311_cfg);
    if (s_codec_if == nullptr) return false;

    esp_codec_dev_cfg_t dev_cfg = {};
    dev_cfg.codec_if = s_codec_if;
    dev_cfg.data_if = s_data_if;
    dev_cfg.dev_type = ESP_CODEC_DEV_TYPE_IN_OUT;
    s_codec = esp_codec_dev_new(&dev_cfg);
    if (s_codec == nullptr) return false;

    esp_codec_dev_sample_info_t fs = {};
    fs.sample_rate = static_cast<int>(kSampleRate);
    fs.channel = static_cast<int>(kChannels);
    fs.bits_per_sample = static_cast<int>(kBitsPerSample);
    if (esp_codec_dev_open(s_codec, &fs) != ESP_CODEC_DEV_OK) return false;
    if (esp_codec_dev_set_out_vol(s_codec, kSpeakerVolume) != ESP_CODEC_DEV_OK) return false;
    if (esp_codec_dev_set_out_mute(s_codec, true) != ESP_CODEC_DEV_OK) return false;
    return true;
}

mini_result_t speaker_open(void *ctx, const char *endpoint,
                           uint32_t sample_rate_hz, uint32_t sample_format,
                           uint32_t channels,
                           minishell_backend_audio_t *out_audio)
{
    (void)ctx;
    if (out_audio == nullptr) return MINI_ERR_INVALID;
    *out_audio = MINISHELL_BACKEND_AUDIO_INVALID;
    if (s_open) return MINI_ERR_TOO_MANY_OPEN;
    if (!endpoint_supported(endpoint)) return MINI_ERR_NOT_FOUND;
    if (sample_rate_hz != kSampleRate || sample_format != MINI_AUDIO_SAMPLE_S16 ||
        channels != kChannels) return MINI_ERR_UNSUPPORTED;

    if (!prepare_i2s() || !prepare_codec()) {
        cleanup_hardware();
        return MINI_ERR_IO;
    }

    s_open = true;
    s_started = false;
    *out_audio = kSpeakerHandle;
    return MINI_OK;
}

bool valid_handle(minishell_backend_audio_t audio)
{
    return s_open && audio == kSpeakerHandle && s_codec != nullptr;
}

mini_result_t speaker_start(void *ctx, minishell_backend_audio_t audio)
{
    (void)ctx;
    if (!valid_handle(audio)) return MINI_ERR_BAD_HANDLE;
    if (s_started) return MINI_OK;
    if (esp_codec_dev_set_out_mute(s_codec, false) != ESP_CODEC_DEV_OK) return MINI_ERR_IO;
    s_started = true;
    return MINI_OK;
}

mini_result_t speaker_write(void *ctx, minishell_backend_audio_t audio,
                            const void *frames, uint32_t frame_count,
                            uint32_t *out_frames, uint32_t timeout_ms)
{
    (void)ctx;
    (void)timeout_ms;
    if (!valid_handle(audio)) return MINI_ERR_BAD_HANDLE;
    if (out_frames == nullptr) return MINI_ERR_INVALID;
    *out_frames = 0u;
    if (!s_started) return MINI_ERR_NOT_READY;
    if (frame_count == 0u) return MINI_OK;
    if (frames == nullptr || frame_count > 0x3fffffffu) return MINI_ERR_INVALID;

    int bytes = static_cast<int>(frame_count * sizeof(int16_t));
    if (esp_codec_dev_write(s_codec, const_cast<void *>(frames), bytes) != ESP_CODEC_DEV_OK) {
        return MINI_ERR_IO;
    }
    *out_frames = frame_count;
    return MINI_OK;
}

mini_result_t speaker_stop(void *ctx, minishell_backend_audio_t audio)
{
    (void)ctx;
    if (!valid_handle(audio)) return MINI_ERR_BAD_HANDLE;
    if (!s_started) return MINI_OK;
    if (esp_codec_dev_set_out_mute(s_codec, true) != ESP_CODEC_DEV_OK) return MINI_ERR_IO;
    s_started = false;
    return MINI_OK;
}

mini_result_t speaker_abort(void *ctx, minishell_backend_audio_t audio)
{
    return speaker_stop(ctx, audio);
}

mini_result_t speaker_close(void *ctx, minishell_backend_audio_t audio)
{
    (void)ctx;
    if (!valid_handle(audio)) return MINI_ERR_BAD_HANDLE;
    if (s_started) (void)esp_codec_dev_set_out_mute(s_codec, true);
    cleanup_hardware();
    return MINI_OK;
}
}  // namespace

extern "C" void adv_audio_speaker_configure(minishell_services_port_t *port)
{
    if (port == nullptr) return;
    port->audio_capabilities |= MINI_AUDIO_CAP_TX;
    port->audio_tx_open = speaker_open;
    port->audio_tx_start = speaker_start;
    port->audio_tx_write = speaker_write;
    port->audio_tx_stop = speaker_stop;
    port->audio_tx_abort = speaker_abort;
    port->audio_tx_close = speaker_close;
}
