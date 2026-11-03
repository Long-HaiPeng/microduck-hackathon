#include "sdkconfig.h"

#if CONFIG_OTTO_DUCK_BODY

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <string>

#include "board.h"
#include "config.h"
#include "esp_timer.h"
#include "freertos/semphr.h"
#include "mcp_server.h"
#include "oscillator.h"
#include "otto_movements.h"
#include "power_manager.h"
#include "settings.h"
#include "wifi_manager.h"

namespace {
constexpr char kTag[] = "DuckController";
constexpr char kTrimNamespace[] = "duck_trims";
constexpr int kMouthClosed = CONFIG_OTTO_DUCK_MOUTH_CLOSED;
constexpr int kMouthOpen = CONFIG_OTTO_DUCK_MOUTH_OPEN;
constexpr int kNeckMin = CONFIG_OTTO_DUCK_NECK_MIN;
constexpr int kNeckCenter = CONFIG_OTTO_DUCK_NECK_CENTER;
constexpr int kNeckMax = CONFIG_OTTO_DUCK_NECK_MAX;
constexpr int64_t kTimeoutUs = 30LL * 1000 * 1000;
static_assert(kMouthClosed < kMouthOpen);
static_assert(kNeckMin < kNeckCenter && kNeckCenter < kNeckMax);

enum class DuckAction {
    Home,
    Walk,
    Turn,
    Jump,
    Swing,
    UpDown,
    Sit,
    HeadTurn,
    HeadShake,
    LookAround,
    Quack,
    Chatter,
    Curious,
    Preen,
    HappyWaddle,
    Stretch,
    Dance,
    Peck,
    Yawn,
    Sleepy,
    Calibrate,
};

constexpr struct {
    const char* name;
    DuckAction action;
} kActions[] = {{"home", DuckAction::Home},
                {"walk", DuckAction::Walk},
                {"turn", DuckAction::Turn},
                {"jump", DuckAction::Jump},
                {"swing", DuckAction::Swing},
                {"updown", DuckAction::UpDown},
                {"sit", DuckAction::Sit},
                {"head_turn", DuckAction::HeadTurn},
                {"head_shake", DuckAction::HeadShake},
                {"look_around", DuckAction::LookAround},
                {"quack", DuckAction::Quack},
                {"chatter", DuckAction::Chatter},
                {"curious", DuckAction::Curious},
                {"preen", DuckAction::Preen},
                {"happy_waddle", DuckAction::HappyWaddle},
                {"stretch", DuckAction::Stretch},
                {"dance", DuckAction::Dance},
                {"peck", DuckAction::Peck},
                {"yawn", DuckAction::Yawn},
                {"sleepy", DuckAction::Sleepy}};

struct DuckCommand {
    DuckAction action = DuckAction::Home;
    int steps = 1;
    int speed = 1200;
    int direction = 1;
    int amount = 30;
    int servo = 0;
    int trim = 0;
};

// Validate the actual selected board, before enabling any PWM output.
bool HasUsableServoPins(const HardwareConfig& hw) {
    const gpio_num_t servos[] = {hw.left_leg_pin,   hw.right_leg_pin, hw.left_foot_pin,
                                 hw.right_foot_pin, hw.left_hand_pin, hw.right_hand_pin};
    const gpio_num_t reserved[] = {hw.display_cs_pin,
                                   hw.display_clk_pin,
                                   hw.display_mosi_pin,
                                   hw.display_dc_pin,
                                   hw.display_rst_pin,
                                   hw.display_backlight_pin,
                                   hw.i2c_sda_pin,
                                   hw.i2c_scl_pin,
                                   hw.audio_i2s_gpio_ws,
                                   hw.audio_i2s_gpio_bclk,
                                   hw.audio_i2s_gpio_din,
                                   hw.audio_i2s_gpio_dout,
                                   hw.audio_i2s_mic_gpio_ws,
                                   hw.audio_i2s_mic_gpio_sck,
                                   hw.audio_i2s_mic_gpio_din,
                                   hw.audio_i2s_spk_gpio_dout,
                                   hw.audio_i2s_spk_gpio_bclk,
                                   hw.audio_i2s_spk_gpio_lrck,
                                   hw.power_charge_detect_pin};
    for (size_t i = 0; i < std::size(servos); ++i) {
        if (!GPIO_IS_VALID_OUTPUT_GPIO(servos[i])) {
            ESP_LOGE(kTag, "Missing or invalid servo output: %d", servos[i]);
            return false;
        }
        for (size_t j = 0; j < i; ++j) {
            if (servos[i] == servos[j]) {
                ESP_LOGE(kTag, "Duplicate servo GPIO: %d", servos[i]);
                return false;
            }
        }
        for (auto pin : reserved) {
            if (pin != GPIO_NUM_NC && pin == servos[i]) {
                ESP_LOGE(kTag, "Servo GPIO %d conflicts with a board peripheral", pin);
                return false;
            }
        }
    }
    return true;
}

class DuckController {
public:
    explicit DuckController(const HardwareConfig& hw) : hw_(hw) {
        mutex_ = xSemaphoreCreateMutex();
        if (mutex_ != nullptr && HasUsableServoPins(hw_)) {
            esp_timer_create_args_t timer_args = {};
            timer_args.callback = [](void* arg) {
                static_cast<DuckController*>(arg)->CheckDeadline();
            };
            timer_args.arg = this;
            timer_args.dispatch_method = ESP_TIMER_TASK;
            timer_args.name = "duck_watchdog";
            if (esp_timer_create(&timer_args, &watchdog_) == ESP_OK &&
                esp_timer_start_periodic(watchdog_, 50000) == ESP_OK &&
                xTaskCreate(Worker, "duck_action", 4096, this, 5, &worker_) == pdPASS) {
                ready_ = true;
                Submit({});
            }
        }
        RegisterTools();
        if (!ready_) {
            ESP_LOGE(kTag, "Duck motion disabled; check board pins and task/timer allocation");
        }
    }

    bool IsMoving() const { return busy_.load(); }

    void Stop() {
        if (mutex_ == nullptr)
            return;
        xSemaphoreTake(mutex_, portMAX_DELAY);
        CancelLocked("stopped");
        xSemaphoreGive(mutex_);
    }

private:
    HardwareConfig hw_;
    Otto legs_;
    Oscillator mouth_;
    Oscillator neck_;
    SemaphoreHandle_t mutex_ = nullptr;
    TaskHandle_t worker_ = nullptr;
    esp_timer_handle_t watchdog_ = nullptr;
    bool ready_ = false;
    bool head_initialized_ = false;
    std::atomic<bool> busy_{false};
    std::atomic<bool> cancelled_{false};
    int64_t deadline_ = 0;
    const char* result_ = "idle";
    DuckCommand command_;
    std::array<int, 6> trims_{};
    static constexpr const char* kServoNames[] = {"left_leg",   "right_leg", "left_foot",
                                                  "right_foot", "mouth",     "neck"};

    const char* Submit(DuckCommand command) {
        if (!ready_)
            return "鸭形动作未启用：请检查主板舵机引脚冲突和启动日志";
        command.steps = std::clamp(command.steps, 1, 10);
        command.speed = std::clamp(command.speed, 800, 2000);
        command.direction = std::clamp(command.direction, -1, 1);
        command.amount = std::clamp(command.amount, 0, 30);
        xSemaphoreTake(mutex_, portMAX_DELAY);
        if (busy_) {
            xSemaphoreGive(mutex_);
            return "机器人正在执行或停止动作，请稍后重试";
        }
        command_ = command;
        cancelled_ = false;
        legs_.ClearStopRequest();
        result_ = "moving";
        deadline_ = esp_timer_get_time() + kTimeoutUs;
        busy_ = true;  // Reserve the slot before waking the worker.
        xTaskNotifyGive(worker_);
        xSemaphoreGive(mutex_);
        return nullptr;
    }

    void CancelLocked(const char* result) {
        if (!busy_ || cancelled_)
            return;
        cancelled_ = true;
        legs_.RequestStop();
        result_ = result;
    }

    void CheckDeadline() {
        // A timer callback never waits behind a command handler.
        if (xSemaphoreTake(mutex_, 0) != pdTRUE)
            return;
        if (busy_ && esp_timer_get_time() >= deadline_) {
            CancelLocked("timeout");
        }
        xSemaphoreGive(mutex_);
    }

    void InitializeMotion() {
        Settings settings(kTrimNamespace, false);
        for (size_t i = 0; i < trims_.size(); ++i) {
            trims_[i] = std::clamp<int32_t>(settings.GetInt(kServoNames[i], 0), -10, 10);
        }
        legs_.Init(hw_.left_leg_pin, hw_.right_leg_pin, hw_.left_foot_pin, hw_.right_foot_pin);
        legs_.SetTrims(trims_[0], trims_[1], trims_[2], trims_[3]);
        mouth_.SetOutputRange(kMouthClosed, kMouthOpen);
        neck_.SetOutputRange(kNeckMin, kNeckMax);
        mouth_.SetTrim(trims_[4]);
        neck_.SetTrim(trims_[5]);
        mouth_.Attach(hw_.left_hand_pin);
        neck_.Attach(hw_.right_hand_pin);
    }

    static void Worker(void* arg) {
        auto& self = *static_cast<DuckController*>(arg);
        self.InitializeMotion();
        for (;;) {
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            PowerManager::PauseBatteryUpdate();
            if (!self.head_initialized_ && !self.cancelled_) {
                // Initial physical position is unknown; the first pulse targets the
                // calibrated rest pose, not the mouth's upper limit.
                self.mouth_.SetPosition(kMouthClosed);
                if (!self.cancelled_) {
                    self.neck_.SetPosition(kNeckCenter);
                    self.head_initialized_ = true;
                }
            }
            if (!self.cancelled_)
                self.Execute(self.command_);
            PowerManager::ResumeBatteryUpdate();
            xSemaphoreTake(self.mutex_, portMAX_DELAY);
            if (!self.cancelled_)
                self.result_ = "idle";
            self.busy_ = false;
            xSemaphoreGive(self.mutex_);
        }
    }

    void MoveHead(int mouth, int neck, int duration) {
        const int start_mouth = mouth_.GetPosition();
        const int start_neck = neck_.GetPosition();
        const int ticks = std::max(1, duration / 10);
        for (int i = 1; i <= ticks && !cancelled_; ++i) {
            mouth_.SetPosition(start_mouth + (mouth - start_mouth) * i / ticks);
            if (cancelled_)
                break;
            neck_.SetPosition(start_neck + (neck - start_neck) * i / ticks);
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }

    void Home() {
        MoveHead(kMouthClosed, kNeckCenter, 500);
        if (!cancelled_)
            legs_.Home();
    }

    void MouthPulses(int count, int opening, int neck, int period) {
        const int open = std::clamp(kMouthClosed + opening, kMouthClosed, kMouthOpen);
        for (int i = 0; i < count && !cancelled_; ++i) {
            MoveHead(open, neck, period / 2);
            MoveHead(kMouthClosed, neck, period / 2);
        }
    }

    void MoveGait(int steps, int speed, int direction, bool turning) {
        // The duck shell has a wider stance than the original Otto body.
        // Build-time starting values follow the public FlashDuck gait guide;
        // the assembled robot still needs its own calibration.
        const int hip = CONFIG_OTTO_DUCK_GAIT_HIP_AMPLITUDE;
        const int foot = CONFIG_OTTO_DUCK_GAIT_FOOT_AMPLITUDE;
        const int offset = CONFIG_OTTO_DUCK_GAIT_HIP_OFFSET;
        int amplitudes[SERVO_COUNT] = {hip, hip, foot, foot, 0, 0};
        int centers[SERVO_COUNT] = {90 + offset, 90 - offset, 95, 85, 90, 90};
        if (turning) {
            amplitudes[direction > 0 ? RIGHT_LEG : LEFT_LEG] = 0;
        }
        constexpr double kRadiansPerDegree = 3.14159265358979323846 / 180.0;
        const double foot_phase =
            -CONFIG_OTTO_DUCK_GAIT_PHASE * kRadiansPerDegree * (turning ? 1 : direction);
        double phases[SERVO_COUNT] = {0, 0, foot_phase, foot_phase, 0, 0};
        legs_.Execute2(amplitudes, centers, speed, phases, steps);
    }

    void Execute(const DuckCommand& command) {
        const int side = command.direction > 0   ? kNeckMin
                         : command.direction < 0 ? kNeckMax
                                                 : kNeckCenter;
        const int opening = std::min(command.amount, kMouthOpen - kMouthClosed);
        const int leg_amplitude = std::min(command.amount, 20);
        switch (command.action) {
            case DuckAction::Home:
                Home();
                return;
            case DuckAction::Walk:
            case DuckAction::Turn:
                MoveHead(kMouthClosed, kNeckCenter, 400);
                if (cancelled_)
                    break;
                MoveGait(command.steps, command.speed, command.direction,
                         command.action == DuckAction::Turn);
                break;
            case DuckAction::Jump:
                for (int i = 0; i < command.steps && !cancelled_; ++i)
                    legs_.Jump(1, command.speed);
                break;
            case DuckAction::Swing:
                legs_.Swing(command.steps, command.speed, leg_amplitude);
                break;
            case DuckAction::UpDown:
                legs_.UpDown(command.steps, command.speed, leg_amplitude);
                break;
            case DuckAction::Sit:
                MoveHead(kMouthClosed, kNeckCenter, 400);
                if (!cancelled_)
                    legs_.Sit();
                return;  // Keep the seated pose until a later command.
            case DuckAction::LookAround:
                for (int i = 0; i < command.steps && !cancelled_; ++i) {
                    MoveHead(kMouthClosed, kNeckMin, command.speed / 2);
                    MoveHead(kMouthClosed, kNeckMax, command.speed);
                    MoveHead(kMouthClosed, kNeckCenter, command.speed / 2);
                }
                break;
            case DuckAction::HeadTurn:
                MoveHead(kMouthClosed, side, command.speed);
                return;  // Keep looking in the requested direction.
            case DuckAction::HeadShake: {
                const int amplitude =
                    std::min({command.amount, kNeckCenter - kNeckMin, kNeckMax - kNeckCenter});
                const int ticks = command.speed / 10;
                constexpr double kFullCircle = 6.28318530717958647692;
                MoveHead(kMouthClosed, kNeckCenter, 300);
                for (int i = 0; i < command.steps && !cancelled_; ++i) {
                    for (int tick = 1; tick <= ticks && !cancelled_; ++tick) {
                        const int offset =
                            std::lround(amplitude * std::sin(kFullCircle * tick / ticks));
                        MoveHead(kMouthClosed, kNeckCenter - offset, 10);
                    }
                }
                break;
            }
            case DuckAction::Quack:
                MouthPulses(std::min(command.steps, 3), opening, kNeckCenter, command.speed);
                break;
            case DuckAction::Chatter:
                MouthPulses(std::min(command.steps, 3), std::min(opening, 6), kNeckCenter,
                            command.speed / 2);
                break;
            case DuckAction::Curious:
                MoveHead(kMouthClosed, side, command.speed / 2);
                MouthPulses(command.steps, std::min(opening, 8), side, command.speed);
                break;
            case DuckAction::Preen:
                MoveHead(kMouthClosed, side, command.speed / 2);
                MouthPulses(command.steps, std::min(opening, 6), side, command.speed / 2);
                break;
            case DuckAction::HappyWaddle:
                for (int i = 0; i < command.steps && !cancelled_; ++i) {
                    MoveGait(2, command.speed, command.direction < 0 ? -1 : 1, false);
                    MouthPulses(1, std::min(opening, 8), kNeckCenter, command.speed / 2);
                }
                break;
            case DuckAction::Stretch:
                for (int i = 0; i < command.steps && !cancelled_; ++i) {
                    legs_.UpDown(1, command.speed, std::min(leg_amplitude, 15));
                    MouthPulses(1, opening, kNeckCenter, command.speed);
                }
                break;
            case DuckAction::Dance: {
                const int period = std::clamp(command.speed, 800, 1000);
                const int left = std::max(kNeckCenter - 14, kNeckMin);
                const int right = std::min(kNeckCenter + 14, kNeckMax);
                for (int i = 0; i < command.steps && !cancelled_; ++i) {
                    MoveGait(2, period, 1, false);
                    MoveHead(kMouthClosed, left, period / 2);
                    MouthPulses(2, std::min(opening, 8), left, 2 * (period / 3));
                    MoveHead(kMouthClosed, kNeckCenter, period / 2);
                    if (!cancelled_)
                        MoveGait(2, period, -1, false);
                    MoveHead(kMouthClosed, right, period / 2);
                    MouthPulses(2, std::min(opening, 8), right, 2 * (period / 3));
                    MoveHead(kMouthClosed, kNeckCenter, period / 2);
                    MouthPulses(3, std::min(opening, 10), kNeckCenter, 2 * (period / 3));
                }
                break;
            }
            case DuckAction::Peck:
                for (int i = 0; i < command.steps && !cancelled_; ++i) {
                    legs_.UpDown(1, command.speed, std::min(leg_amplitude, 18));
                    MouthPulses(1, opening, kNeckCenter, command.speed / 2);
                }
                break;
            case DuckAction::Yawn:
                MouthPulses(command.steps, opening, kNeckCenter, command.speed * 2);
                break;
            case DuckAction::Sleepy:
                MouthPulses(command.steps, std::min(opening, 5), side, command.speed * 2);
                break;
            case DuckAction::Calibrate: {
                Settings settings(kTrimNamespace, true);
                trims_[command.servo] = std::clamp(command.trim, -10, 10);
                settings.SetInt(kServoNames[command.servo], trims_[command.servo]);
                legs_.SetTrims(trims_[0], trims_[1], trims_[2], trims_[3]);
                mouth_.SetTrim(trims_[4]);
                neck_.SetTrim(trims_[5]);
                legs_.SetRestState(false);
                Home();
                return;
            }
        }
        if (!cancelled_)
            Home();
    }

    void RegisterTools() {
        auto& mcp = McpServer::GetInstance();
        for (const std::string prefix : {"self.otto.", "self.flashduck."}) {
            mcp.AddTool(prefix + "action",
                        "控制鸭形机器人。支持 home、walk、turn、jump、swing、updown、sit、"
                        "head_turn、head_shake、look_around、quack、chatter、curious、preen、"
                        "happy_waddle、stretch、dance、peck、yawn、sleepy。steps 1-10，"
                        "quack/chatter最多3次；实际speed限制在800-2000毫秒，amount按部位限幅。"
                        "direction 1=前进/左，-1=后退/右，0用于head_turn回正。"
                        "一次一个动作，30秒超时；sit和head_turn保持姿态，home归位。",
                        PropertyList({Property("action", kPropertyTypeString, "home"),
                                      Property("steps", kPropertyTypeInteger, 1, 1, 10),
                                      Property("speed", kPropertyTypeInteger, 1200, 100, 3000),
                                      Property("direction", kPropertyTypeInteger, 1, -1, 1),
                                      Property("amount", kPropertyTypeInteger, 30, 0, 170)}),
                        [this](const PropertyList& properties) -> ToolResult {
                            DuckCommand command;
                            const auto action = properties["action"].value<std::string>();
                            const auto* match = std::find_if(
                                std::begin(kActions), std::end(kActions),
                                [&action](const auto& item) { return action == item.name; });
                            if (match == std::end(kActions))
                                return std::unexpected("不支持该动作；请使用工具列出的鸭形动作");
                            command.action = match->action;
                            command.steps = properties["steps"].value<int>();
                            command.speed = properties["speed"].value<int>();
                            command.direction = properties["direction"].value<int>();
                            command.amount = properties["amount"].value<int>();
                            if ((command.action == DuckAction::Quack ||
                                 command.action == DuckAction::Chatter) &&
                                command.steps > 3) {
                                return std::unexpected("quack/chatter 一次最多 3 次");
                            }
                            if (command.direction == 0 && (command.action == DuckAction::Walk ||
                                                           command.action == DuckAction::Turn)) {
                                return std::unexpected("walk/turn 的 direction 必须为 1 或 -1");
                            }
                            if (const char* error = Submit(command))
                                return std::unexpected(error);
                            return ReturnValue(true);
                        });
            mcp.AddTool(prefix + "stop",
                        "取消当前动作。停止后保持当前位置，不继续旧动作；需要归位请另发home。",
                        PropertyList(), [this](const PropertyList&) -> ReturnValue {
                            Stop();
                            return true;
                        });
            mcp.AddTool(prefix + "get_status",
                        "返回 moving/idle/stopped/timeout/disabled；动作结束不代表实物到位。",
                        PropertyList(), [this](const PropertyList&) -> ReturnValue {
                            if (!ready_)
                                return std::string("disabled");
                            xSemaphoreTake(mutex_, portMAX_DELAY);
                            std::string status = busy_ ? "moving" : result_;
                            xSemaphoreGive(mutex_);
                            return status;
                        });
            mcp.AddTool(prefix + "set_trim",
                        "装配校准专用：left_leg/right_leg/left_foot/right_foot/mouth/neck，"
                        "trim_value -10到10度。仅空闲时接受，独立保存并归位，最终角度仍受限。",
                        PropertyList({Property("servo_type", kPropertyTypeString),
                                      Property("trim_value", kPropertyTypeInteger, 0, -10, 10)}),
                        [this](const PropertyList& properties) -> ToolResult {
                            const auto servo = properties["servo_type"].value<std::string>();
                            for (size_t i = 0; i < std::size(kServoNames); ++i) {
                                if (servo != kServoNames[i])
                                    continue;
                                DuckCommand command;
                                command.action = DuckAction::Calibrate;
                                command.servo = i;
                                command.trim = properties["trim_value"].value<int>();
                                if (const char* error = Submit(command))
                                    return std::unexpected(error);
                                return ReturnValue(true);
                            }
                            return std::unexpected("无效舵机名称；鸭形配置使用mouth和neck");
                        });
            mcp.AddTool(prefix + "get_trims", "读取鸭形独立校准值，未沿用人形手臂校准",
                        PropertyList(), [](const PropertyList&) -> ReturnValue {
                            Settings settings(kTrimNamespace, false);
                            cJSON* result = cJSON_CreateObject();
                            for (auto name : kServoNames) {
                                cJSON_AddNumberToObject(result, name, settings.GetInt(name, 0));
                            }
                            return result;
                        });
            mcp.AddTool(prefix + "get_ip", "获取设备的Wi-Fi地址", PropertyList(),
                        [](const PropertyList&) -> ReturnValue {
                            auto ip = WifiManager::GetInstance().GetIpAddress();
                            cJSON* result = cJSON_CreateObject();
                            cJSON_AddStringToObject(result, "ip", ip.c_str());
                            cJSON_AddBoolToObject(result, "connected", !ip.empty());
                            return result;
                        });
        }
        mcp.AddTool("self.battery.get_level", "获取电池电量和充电状态", PropertyList(),
                    [](const PropertyList&) -> ReturnValue {
                        int level = 0;
                        bool charging = false, discharging = false;
                        Board::GetInstance().GetBatteryLevel(level, charging, discharging);
                        cJSON* result = cJSON_CreateObject();
                        cJSON_AddNumberToObject(result, "level", level);
                        cJSON_AddBoolToObject(result, "charging", charging);
                        return result;
                    });
    }
};

DuckController* controller = nullptr;
}  // namespace

void InitializeOttoController(const HardwareConfig& hw_config) {
    if (controller == nullptr)
        controller = new DuckController(hw_config);
}

void StopOttoActions() {
    if (controller != nullptr)
        controller->Stop();
}

bool IsOttoMoving() { return controller != nullptr && controller->IsMoving(); }

#endif  // CONFIG_OTTO_DUCK_BODY
