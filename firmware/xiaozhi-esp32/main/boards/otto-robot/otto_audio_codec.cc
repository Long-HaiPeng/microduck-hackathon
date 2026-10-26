#include "otto_audio_codec.h"

#include <cmath>
#include <cstdint>
#include <vector>

// Write：音量缩放后写 I2S，同时把缩放后的实际出声波形缓存为 AEC 参考
// （Read 侧从缓冲头部消费，天然滞后于播放，与真实声路径方向一致）。
template <typename Codec>
static int OttoWrite(Codec* codec, const int16_t* data, int samples) {
    std::lock_guard<std::mutex> lock(codec->data_if_mutex_);
    std::vector<int32_t> buffer(samples);
    std::vector<int16_t> ref_in(samples);
    int32_t volume_factor = pow(double(codec->output_volume()) / 100.0, 2) * 65536;
    for (int i = 0; i < samples; i++) {
        int64_t temp = int64_t(data[i]) * volume_factor;
        int32_t scaled;
        if (temp > INT32_MAX) {
            scaled = INT32_MAX;
        } else if (temp < INT32_MIN) {
            scaled = INT32_MIN;
        } else {
            scaled = static_cast<int32_t>(temp);
        }
        buffer[i] = scaled;
        ref_in[i] = static_cast<int16_t>(scaled >> 16);
    }
    codec->PushReference(ref_in.data(), samples);

    size_t bytes_written;
    ESP_ERROR_CHECK(i2s_channel_write(codec->tx_handle_, buffer.data(), samples * sizeof(int32_t),
                                      &bytes_written, portMAX_DELAY));
    return bytes_written / sizeof(int32_t);
}

// Read：读麦克风并与参考流交错输出（MR 双通道），供 AFE 做回声消除
template <typename Codec>
static int OttoRead(Codec* codec, int16_t* dest, int samples) {
    size_t bytes_read;
    constexpr uint32_t kReadTimeoutMs = 200;
    int frames = samples / codec->input_channels();
    std::vector<int32_t> bit32_buffer(frames);
    if (i2s_channel_read(codec->rx_handle_, bit32_buffer.data(), frames * sizeof(int32_t),
                         &bytes_read, kReadTimeoutMs) != ESP_OK) {
        return 0;
    }
    frames = bytes_read / sizeof(int32_t);
    int i = 0;
    for (int f = 0; f < frames; f++) {
        int32_t value = bit32_buffer[f] >> 12;
        dest[i++] = (value > INT16_MAX) ? INT16_MAX
                    : (value < -INT16_MAX) ? -INT16_MAX
                                           : (int16_t)value;
        dest[i++] = codec->PopReference();
    }
    return i;
}

int OttoAudioCodecDuplex::Write(const int16_t* data, int samples) {
    return OttoWrite(this, data, samples);
}

int OttoAudioCodecDuplex::Read(int16_t* dest, int samples) {
    return OttoRead(this, dest, samples);
}

int OttoAudioCodecSimplex::Write(const int16_t* data, int samples) {
    return OttoWrite(this, data, samples);
}

int OttoAudioCodecSimplex::Read(int16_t* dest, int samples) {
    return OttoRead(this, dest, samples);
}
