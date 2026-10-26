#ifndef OTTO_AUDIO_CODEC_H
#define OTTO_AUDIO_CODEC_H

#include "codecs/no_audio_codec.h"
#include "settings.h"

#include <atomic>
#include <vector>

// 软件回采参考 mixin：Otto 板无 codec 芯片（INMP441 + I2S 功放直连），
// 没有硬件回采通道。参考 esp32-s3-box-lite 的做法，把播放数据缓存为
// AEC 参考流，使 CONFIG_USE_DEVICE_AEC 的 AFE 回声消除真正生效，
// realtime 全双工模式不再被自己的 TTS 打断。
//
// 播放率与拾音率不同时（无摄像头版 24k 播放 / 16k 拾音），写入侧先做
// 线性插值降采样，保证参考流与麦克风同采样率。
class OttoSoftwareReference {
public:
    void InitReference(double rate_step) {
        step_ = rate_step;
        // 循环延迟（折算成拾音率样本数）：Write 入队的数据经 TX DMA 队列
        // （6x240 帧 @播放率）才出声，回声又经 RX DMA 队列（6x240 帧 @拾音率）
        // 才被读走。参考流在 Write 时即入缓冲、Read 时即被消费，若不垫入
        // 等长静音，会领先麦克风听到的回声约 180ms，AEC 无法对齐收敛，
        // 残余回声被 VAD 误判为用户说话（自打断）。
        constexpr int kDmaFrames = AUDIO_CODEC_DMA_DESC_NUM * AUDIO_CODEC_DMA_FRAME_NUM;
        playout_delay_samples_ =
            (int64_t)kDmaFrames + (int64_t)(kDmaFrames / rate_step);
        ref_buffer_.assign(kRefBufferSize, 0);
    }

    void PushReference(const int16_t* data, int samples) {
        // 新一轮播放开始（缓冲已排空）时，先垫入出声延迟等长的静音，
        // 使参考流与麦克风回声时间对齐（见 InitReference 注释）
        size_t wp0 = ref_write_pos_.load(std::memory_order_relaxed);
        size_t rp0 = ref_read_pos_.load(std::memory_order_relaxed);
        if (wp0 == rp0 && playout_delay_samples_ > 0) {
            for (int64_t i = 0; i < playout_delay_samples_; i++) {
                PushRaw(0);
            }
        }
        if (step_ <= 1.0) {
            for (int i = 0; i < samples; i++) {
                PushRaw(data[i]);
            }
            return;
        }
        // 播放率 > 拾音率（如 24k -> 16k）：按全局输入轴做线性插值降采样
        double base = in_count_ - samples;
        while (next_in_pos_ <= in_count_ - 1.0) {
            int idx = (int)(next_in_pos_ - base);
            double frac = next_in_pos_ - base - idx;
            int16_t a = (idx < 0) ? prev_last_ : data[idx];
            int16_t b = data[idx + 1];
            PushRaw((int16_t)(a + (int32_t)((b - a) * frac)));
            next_in_pos_ += step_;
        }
        prev_last_ = data[samples - 1];
        in_count_ += samples;
    }

    int16_t PopReference() {
        size_t rp = ref_read_pos_.load(std::memory_order_relaxed);
        size_t wp = ref_write_pos_.load(std::memory_order_acquire);
        if (rp >= wp) {
            return 0;  // 参考欠载（如刚开始播放），填静音
        }
        int16_t v = ref_buffer_[rp & (kRefBufferSize - 1)];
        ref_read_pos_.store(rp + 1, std::memory_order_release);
        return v;
    }

private:
    void PushRaw(int16_t v) {
        size_t wp = ref_write_pos_.load(std::memory_order_relaxed);
        size_t rp = ref_read_pos_.load(std::memory_order_relaxed);
        if (wp - rp >= kRefBufferSize) {
            // 写溢出：丢弃最旧的参考数据，保留最新波形
            ref_read_pos_.store(wp - kRefBufferSize + 1, std::memory_order_release);
        }
        ref_buffer_[wp & (kRefBufferSize - 1)] = v;
        ref_write_pos_.store(wp + 1, std::memory_order_release);
    }

    static constexpr size_t kRefBufferSize = 8192;  // 2 的幂，约 512ms @16kHz
    std::vector<int16_t> ref_buffer_;
    // 单生产者(Write)/单消费者(Read)，各自只写自己的游标，原子读取对方
    std::atomic<size_t> ref_write_pos_{0};
    std::atomic<size_t> ref_read_pos_{0};
    double step_ = 1.0;         // 播放采样率 / 拾音采样率
    int64_t playout_delay_samples_ = 0;  // DMA 循环延迟（拾音率样本数），参考流对齐用
    double next_in_pos_ = 0.0;  // 下一参考输出点在全局输入轴上的位置
    int64_t in_count_ = 0;      // 已收到的播放样本总数
    int16_t prev_last_ = 0;     // 上一播放帧最后一个样本（跨帧插值用）
};

// 摄像头版：单 I2S 端口全双工（16k/16k）
class OttoAudioCodecDuplex : public NoAudioCodecDuplex, public OttoSoftwareReference {
public:
    OttoAudioCodecDuplex(int input_sample_rate, int output_sample_rate, gpio_num_t bclk,
                         gpio_num_t ws, gpio_num_t dout, gpio_num_t din)
        : NoAudioCodecDuplex(input_sample_rate, output_sample_rate, bclk, ws, dout, din) {
        input_reference_ = true;
        input_channels_ = 2;  // 麦克风 + 软件参考
        // NVS 无用户音量记录时默认 30%（低音量降低回声耦合）；
        // 有记录则保留用户值，不能每次开机覆盖
        Settings volume_settings("audio", false);
        if (volume_settings.GetInt("output_volume", -1) < 0) {
            output_volume_ = 30;
        }
        InitReference((double)output_sample_rate / input_sample_rate);
    }

protected:
    int Write(const int16_t* data, int samples) override;
    int Read(int16_t* dest, int samples) override;

    template <typename Codec>
    friend int OttoWrite(Codec* codec, const int16_t* data, int samples);
    template <typename Codec>
    friend int OttoRead(Codec* codec, int16_t* dest, int samples);
};

// 无摄像头版：喇叭/麦克风独立 I2S 端口（16k 入 / 24k 出）
class OttoAudioCodecSimplex : public NoAudioCodecSimplex, public OttoSoftwareReference {
public:
    OttoAudioCodecSimplex(int input_sample_rate, int output_sample_rate, gpio_num_t spk_bclk,
                          gpio_num_t spk_ws, gpio_num_t spk_dout, gpio_num_t mic_sck,
                          gpio_num_t mic_ws, gpio_num_t mic_din)
        : NoAudioCodecSimplex(input_sample_rate, output_sample_rate, spk_bclk, spk_ws, spk_dout,
                              mic_sck, mic_ws, mic_din) {
        input_reference_ = true;
        input_channels_ = 2;  // 麦克风 + 软件参考
        // NVS 无用户音量记录时默认 30%（低音量降低回声耦合）；
        // 有记录则保留用户值，不能每次开机覆盖
        Settings volume_settings("audio", false);
        if (volume_settings.GetInt("output_volume", -1) < 0) {
            output_volume_ = 30;
        }
        InitReference((double)output_sample_rate / input_sample_rate);
    }

protected:
    int Write(const int16_t* data, int samples) override;
    int Read(int16_t* dest, int samples) override;

    template <typename Codec>
    friend int OttoWrite(Codec* codec, const int16_t* data, int samples);
    template <typename Codec>
    friend int OttoRead(Codec* codec, int16_t* dest, int samples);
};

#endif  // OTTO_AUDIO_CODEC_H
