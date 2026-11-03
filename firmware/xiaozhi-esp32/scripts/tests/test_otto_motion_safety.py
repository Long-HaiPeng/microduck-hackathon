"""Compile real Otto motion code against a virtual clock and PWM sink."""
import os
import shlex
import subprocess
import tempfile
import textwrap
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
BOARD = ROOT / "main/boards/otto-robot"


class OttoMotionSafetyTest(unittest.TestCase):
    def test_output_limits_and_cooperative_stop(self):
        sdk = r"""
            #pragma once
            #include <cassert>
            #include <cstdint>
            using TickType_t = uint32_t;
            using ledc_channel_t = int;
            using ledc_mode_t = int;
            constexpr int LEDC_CHANNEL_0=0, LEDC_LOW_SPEED_MODE=0,
                          LEDC_TIMER_13_BIT=13, LEDC_TIMER_1=1,
                          LEDC_AUTO_CLK=0, LEDC_INTR_DISABLE=0;
            struct ledc_timer_config_t {
                int speed_mode, duty_resolution, timer_num, freq_hz, clk_cfg;
            };
            struct ledc_channel_config_t {
                int gpio_num, speed_mode, channel, intr_type, timer_sel, duty, hpoint;
            };
            #define IRAM_ATTR
            #define pdMS_TO_TICKS(ms) static_cast<TickType_t>(ms)
            #define ESP_ERROR_CHECK(result) assert((result) == 0)
            #define ESP_LOGI(...)
            #define ESP_LOGW(...)
            #define ESP_LOGD(...)
            void vTaskDelay(TickType_t ticks);
            int64_t esp_timer_get_time();
            int ledc_timer_config(const ledc_timer_config_t*);
            int ledc_channel_config(const ledc_channel_config_t*);
            int ledc_stop(ledc_mode_t, ledc_channel_t, int);
            int ledc_set_duty(ledc_mode_t, ledc_channel_t, uint32_t);
            int ledc_update_duty(ledc_mode_t, ledc_channel_t);
        """
        driver = r"""
            #include <algorithm>
            #include <cassert>
            #include <climits>
            #include <functional>
            #include <vector>
            #include "otto_movements.h"

            struct Output { int pin; uint32_t duty; int64_t ms; };
            static std::vector<Output> outputs;
            static int pins[8];
            static int64_t now_ms = 100;
            static std::function<void()> on_delay;
            int64_t esp_timer_get_time() { return now_ms * 1000; }
            void vTaskDelay(TickType_t ticks) {
                now_ms += ticks;
                if (on_delay) on_delay();
            }
            int ledc_timer_config(const ledc_timer_config_t*) { return 0; }
            int ledc_channel_config(const ledc_channel_config_t* config) {
                pins[config->channel] = config->gpio_num;
                return 0;
            }
            int ledc_stop(ledc_mode_t, ledc_channel_t, int) { return 0; }
            int ledc_set_duty(ledc_mode_t, ledc_channel_t channel, uint32_t duty) {
                outputs.push_back({pins[channel], duty, now_ms});
                return 0;
            }
            int ledc_update_duty(ledc_mode_t, ledc_channel_t) { return 0; }
            uint32_t Duty(int angle) {
                return static_cast<uint32_t>(((angle / 180.0) * 2.0 + 0.5) * 8191 / 20.0);
            }
            void CheckOutput(int min_angle, int max_angle) {
                assert(!outputs.empty());
                for (const auto& output : outputs) {
                    assert(output.duty >= Duty(min_angle));
                    assert(output.duty <= Duty(max_angle));
                }
            }

            int main() {
                {
                    Oscillator legacy;
                    legacy.Attach(4);
                    legacy.SetTrim(20);
                    legacy.SetPosition(180);
                    assert(legacy.GetPosition() == 180);
                    assert(outputs.back().duty == Duty(180));
                    legacy.SetTrim(0);
                    assert(legacy.GetPosition() == 180);
                }
                outputs.clear();
                {
                    Oscillator mouth;
                    assert(mouth.SetOutputRange(45, 60));
                    assert(!mouth.SetOutputRange(80, 40));
                    assert(!mouth.SetOutputRange(-1, 60));
                    assert(!mouth.SetOutputRange(45, 181));
                    mouth.SetTrim(10);
                    mouth.Attach(4);
                    assert(!mouth.SetOutputRange(0, 180));
                    for (int target : {INT_MIN, -1, 0, 45, 52, 60, 180, INT_MAX}) {
                        mouth.SetPosition(target);
                        assert(outputs.back().duty ==
                               Duty(std::clamp(target, 35, 50) + 10));
                        assert(outputs.back().duty == Duty(mouth.GetPosition() + 10));
                    }
                    CheckOutput(45, 60);
                    mouth.SetTrim(-10);
                    assert(Duty(mouth.GetPosition() - 10) == outputs.back().duty);
                    mouth.SetLimiter(60);
                    now_ms += 1000;
                    mouth.SetPosition(INT_MIN);
                    assert(outputs.back().duty == Duty(45));
                    now_ms += 10;
                    mouth.SetPosition(INT_MAX);
                    assert(outputs.back().duty == Duty(46));
                    CheckOutput(45, 60);
                    mouth.Detach();
                    assert(mouth.SetOutputRange(0, 180));
                    mouth.DisableLimiter();
                    mouth.SetTrim(0);
                    mouth.Attach(4);
                    mouth.SetPosition(180);
                    assert(outputs.back().duty == Duty(180));
                }

                {
                    Otto original;
                    original.Init(1, 2, 3, 4, 5, 6);
                    outputs.clear();
                    original.Home();
                    assert(outputs.size() >= 6);
                    for (auto i = outputs.end() - 6; i != outputs.end(); ++i) {
                        assert(i->duty == Duty(i->pin == 5 ? 45 : i->pin == 6 ? 135 : 90));
                    }
                }

                Otto legs;
                legs.Init(17, 39, 18, 38);
                int target[SERVO_COUNT] = {120, 60, 110, 70, 0, 180};
                int amplitude[SERVO_COUNT] = {20, 20, 20, 20, 20, 20};
                int center[SERVO_COUNT] = {90, 90, 90, 90, 90, 90};
                double phase[SERVO_COUNT] = {};
                const std::vector<std::function<void()>> moves = {
                    [&] { legs.MoveServos(1000, target); },
                    [&] { legs.Walk(10, 1000); },
                    [&] { legs.Turn(10, 1000); },
                    [&] { legs.Execute2(amplitude, center, 1000, phase, 10); },
                    [&] { legs.Home(); },
                    [&] { legs.Sit(); },
                    [&] { legs.Swing(10, 1000); },
                };
                for (const auto& move : moves) {
                    legs.ClearStopRequest();
                    legs.MoveServos(0, target);
                    outputs.clear();
                    const int64_t cutoff = now_ms + 55;
                    size_t writes_at_stop = 0;
                    int64_t stop_seen = 0;
                    on_delay = [&] {
                        if (now_ms >= cutoff && !legs.IsStopRequested()) {
                            legs.RequestStop();
                            writes_at_stop = outputs.size();
                            stop_seen = now_ms;
                        }
                    };
                    move();
                    assert(legs.IsStopRequested());
                    assert(stop_seen - cutoff <= 10);
                    assert(now_ms == stop_seen);
                    assert(outputs.size() == writes_at_stop);
                    assert(!outputs.empty());
                    for (const auto& output : outputs) {
                        assert(output.pin == 17 || output.pin == 39 ||
                               output.pin == 18 || output.pin == 38);
                    }
                    on_delay = nullptr;
                    legs.Home();
                    legs.MoveSingle(90, LEFT_LEG);
                    assert(outputs.size() == writes_at_stop);
                    legs.ClearStopRequest();
                    legs.Home();
                    assert(outputs.size() > writes_at_stop);
                    assert(legs.GetRestState());
                }
            }
        """
        with tempfile.TemporaryDirectory() as tmp:
            base = Path(tmp)
            (base / "test_sdk.h").write_text(textwrap.dedent(sdk))
            for name in (
                "driver/gpio.h", "driver/ledc.h", "esp_timer.h", "esp_log.h",
                "freertos/FreeRTOS.h", "freertos/task.h", "freertos/idf_additions.h",
            ):
                header = base / name
                header.parent.mkdir(parents=True, exist_ok=True)
                header.write_text('#include "test_sdk.h"\n')
            source = base / "motion_test.cc"
            source.write_text(textwrap.dedent(driver))
            binary = base / "motion_test"
            command = shlex.split(os.environ.get("CXX", "c++")) + [
                "-std=c++20", "-fsanitize=undefined", "-fno-sanitize-recover=all",
                "-I", str(base), "-I", str(BOARD),
                str(source), str(BOARD / "oscillator.cc"), str(BOARD / "otto_movements.cc"),
                "-o", str(binary),
            ]
            subprocess.run(command, check=True, capture_output=True, text=True)
            subprocess.run([str(binary)], check=True, capture_output=True, text=True)


if __name__ == "__main__":
    unittest.main()
