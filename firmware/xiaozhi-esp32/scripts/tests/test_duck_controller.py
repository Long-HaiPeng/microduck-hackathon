"""Run the real duck controller and MCP callbacks with deterministic hardware stubs."""

import os
import shlex
import subprocess
import tempfile
import textwrap
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
BOARD = ROOT / "main/boards/otto-robot"


class DuckControllerTest(unittest.TestCase):
    def test_press_stop_consumes_the_whole_click_gesture(self):
        board_source = (BOARD / "otto_robot.cc").read_text()

        def callback_body(name):
            start = board_source.index(f"boot_button_.{name}([this]() {{")
            start = board_source.index("{", start) + 1
            ending = "\n        }, 3);" if name == "OnMultipleClick" else "\n        });"
            end = board_source.index(ending, start)
            return board_source[start:end]

        down = callback_body("OnPressDown")
        click = callback_body("OnClick")
        double_click = callback_body("OnDoubleClick")
        long_press = callback_body("OnLongPress")
        up = callback_body("OnPressUp")
        multiple_click = callback_body("OnMultipleClick")
        driver = r"""
            #include <cassert>
            #include <functional>
            #include <string>
            #define ESP_LOGI(...)
            #define ESP_ERROR_CHECK(expr) assert((expr)==0)
            enum State { kDeviceStateStarting, kDeviceStateIdle };
            enum ListeningMode {
                kListeningModeRealtime, kListeningModeAutoStop, kListeningModeManualStop
            };
            static bool moving=false;
            static int stops=0, ptt_starts=0, ptt_stops=0;
            bool IsOttoMoving() { return moving; }
            void StopOttoActions() { ++stops; moving=false; }
            static int env_changes=0, restart_timers=0;
            std::string ota_url;
            struct Settings {
                Settings(const char*, bool) {}
                std::string GetString(const char*) { return ota_url; }
                void SetString(const char*, const char* value) { ota_url=value; ++env_changes; }
            };
            struct Display { void SetStatus(const char*) {} };
            struct Timer {};
            using esp_timer_handle_t=Timer*;
            constexpr int ESP_TIMER_TASK=0;
            struct esp_timer_create_args_t {
                void (*callback)(void*);
                void* arg;
                int dispatch_method;
                const char* name;
                bool skip_unhandled_events;
            };
            void esp_restart() {}
            int esp_timer_create(const esp_timer_create_args_t*, esp_timer_handle_t*) { return 0; }
            void esp_timer_start_once(esp_timer_handle_t, int delay) {
                assert(delay==2000000);
                ++restart_timers;
            }
            struct Application {
                int toggles=0, mode_changes=0;
                State state=kDeviceStateIdle;
                static Application& GetInstance() { static Application app; return app; }
                State GetDeviceState() { return state; }
                void SwitchListeningMode(ListeningMode) { ++mode_changes; }
                void ToggleChatState() { ++toggles; }
                void Schedule(std::function<void()> task) { task(); }
            };
            struct Buttons {
                int config_entries=0;
                Display* display_=nullptr;
                bool ptt_active_=false, duck_stop_press_=false;
                ListeningMode persistent_mode_=kListeningModeRealtime;
                void EnterWifiConfigMode() { ++config_entries; }
                void PttStart() { ptt_active_=true; ++ptt_starts; }
                void PttStop() { ptt_active_=false; ++ptt_stops; }
                void Down() { DOWN_BODY
                }
                void Click() { CLICK_BODY
                }
                void DoubleClick() { DOUBLE_BODY
                }
                void LongPress() { LONG_BODY
                }
                void Up() { UP_BODY
                }
                void MultipleClick() { MULTIPLE_BODY
                }
            };
            int main() {
                auto& app=Application::GetInstance();
                Buttons button;
                moving=true;
                button.Down();
                assert(ptt_starts==0);
            #if CONFIG_OTTO_DUCK_BODY
                assert(stops==1 && !moving);
                button.Click();
                assert(stops==1 && app.toggles==0 && app.mode_changes==0);
                // A second press can happen after motion has stopped but before
                // the button driver decides this is a double-click gesture.
                moving=true;
                button.Down();
                assert(stops==2);
                button.Down();
                button.DoubleClick();
                assert(ptt_starts==0 && app.mode_changes==0 && app.toggles==0);
                assert(!button.duck_stop_press_);
                moving=true;
                button.Down();
                button.LongPress();
                assert(stops==3 && !button.duck_stop_press_ && ptt_starts==0);
                button.Up();
                assert(ptt_stops==0);
                button.Down();
                button.Click();
                assert(ptt_starts==0 && app.toggles==1);
                // Boot-time homing must remain stoppable before network setup.
                app.state=kDeviceStateStarting;
                moving=true;
                button.Down();
                assert(stops==4 && !moving && ptt_starts==0);
                button.Click();
                assert(button.config_entries==0 && app.toggles==1);
                button.Down();
                button.Click();
                assert(button.config_entries==1 && ptt_starts==0);
            #else
                assert(stops==0 && moving);
                button.Click();
                assert(stops==1 && !moving && app.toggles==0);
                button.Down();
                button.Click();
                assert(ptt_starts==0 && app.toggles==1);
            #endif
                app.state=kDeviceStateIdle;
                button.Down();
                button.LongPress();
                assert(ptt_starts==1 && button.ptt_active_);
                button.Up();
                assert(ptt_stops==1 && !button.ptt_active_ && app.toggles==1);
                // The next short gesture remains a normal click.
                button.Down();
                button.Up();
                button.Click();
                assert(app.toggles==2);
                int mode_changes=app.mode_changes;
                button.Down();
                button.Up();
                button.Down();
                button.Up();
                button.DoubleClick();
                assert(app.mode_changes==mode_changes+1 && app.toggles==2);
                button.MultipleClick();
                assert(env_changes==1 && restart_timers==1);
            #if CONFIG_OTTO_DUCK_BODY
                moving=true;
                button.Down();
                button.MultipleClick();
                assert(env_changes==1 && restart_timers==1);
            #endif
            }
        """
        driver = driver.replace("DOWN_BODY", down).replace("CLICK_BODY", click)
        driver = driver.replace("DOUBLE_BODY", double_click)
        driver = driver.replace("LONG_BODY", long_press)
        driver = driver.replace("UP_BODY", up)
        driver = driver.replace("MULTIPLE_BODY", multiple_click)
        with tempfile.TemporaryDirectory() as tmp:
            base = Path(tmp)
            source = base / "buttons.cc"
            source.write_text(textwrap.dedent(driver))
            for enabled in (0, 1):
                with self.subTest(duck_enabled=enabled):
                    binary = base / f"buttons-{enabled}"
                    command = shlex.split(os.environ.get("CXX", "c++")) + [
                        "-std=c++20", "-fsanitize=undefined",
                        f"-DCONFIG_OTTO_DUCK_BODY={enabled}",
                        str(source), "-o", str(binary),
                    ]
                    subprocess.run(command, check=True, capture_output=True, text=True)
                    subprocess.run([str(binary)], check=True, capture_output=True, text=True)

    def test_action_admission_stop_and_calibration(self):
        platform = r"""
            #pragma once
            #define _BOARD_CONFIG_H_
            #define __OSCILLATOR_H__
            #define __OTTO_MOVEMENTS_H__
            #define __POWER_MANAGER_H__
            #define SERVO_COUNT 6
            #define LEFT_LEG 0
            #define RIGHT_LEG 1
            #include <algorithm>
            #include <atomic>
            #include <cassert>
            #include <condition_variable>
            #include <cstdint>
            #include <expected>
            #include <functional>
            #include <map>
            #include <mutex>
            #include <string>
            #include <thread>
            #include <variant>
            #include <vector>
            #define ESP_LOGE(...)
            #define GPIO_IS_VALID_OUTPUT_GPIO(pin) ((pin) >= 0 && (pin) < 49)
            #define GPIO_NUM_NC -1
            #define ESP_OK 0
            #define ESP_TIMER_TASK 0
            #define pdPASS 1
            #define pdTRUE 1
            #define portMAX_DELAY 0xffffffff
            #define pdMS_TO_TICKS(ms) (ms)
            using gpio_num_t = int;
            struct HardwareConfig {
                int left_leg_pin=1, right_leg_pin=2, left_foot_pin=3, right_foot_pin=4;
                int left_hand_pin=5, right_hand_pin=6;
                int display_cs_pin=-1, display_clk_pin=-1, display_mosi_pin=-1;
                int display_dc_pin=-1, display_rst_pin=-1, display_backlight_pin=-1;
                int i2c_sda_pin=-1, i2c_scl_pin=-1;
                int audio_i2s_gpio_ws=-1, audio_i2s_gpio_bclk=-1;
                int audio_i2s_gpio_din=-1, audio_i2s_gpio_dout=-1;
                int audio_i2s_mic_gpio_ws=-1, audio_i2s_mic_gpio_sck=-1;
                int audio_i2s_mic_gpio_din=-1, audio_i2s_spk_gpio_dout=-1;
                int audio_i2s_spk_gpio_bclk=-1, audio_i2s_spk_gpio_lrck=-1;
                int power_charge_detect_pin=-1;
            };
            using SemaphoreHandle_t = std::mutex*;
            inline SemaphoreHandle_t xSemaphoreCreateMutex() { return new std::mutex; }
            inline int xSemaphoreTake(SemaphoreHandle_t mutex, uint32_t ticks) {
                if (ticks == 0) return mutex->try_lock();
                mutex->lock();
                return pdTRUE;
            }
            inline void xSemaphoreGive(SemaphoreHandle_t mutex) { mutex->unlock(); }
            struct EndWorker {};
            struct Task {
                std::mutex mutex;
                std::condition_variable changed;
                std::thread thread;
                int pending=0, permits=0, completed=0;
                bool initialized=false, shutdown=false, consumed=false;
            };
            using TaskHandle_t = Task*;
            inline Task* task=nullptr;
            inline uint32_t ulTaskNotifyTake(int, uint32_t) {
                std::unique_lock lock(task->mutex);
                task->initialized=true;
                if (task->consumed) ++task->completed;
                task->consumed=false;
                task->changed.notify_all();
                task->changed.wait(lock, [] {
                    return task->shutdown || (task->pending && task->permits);
                });
                if (task->shutdown) throw EndWorker{};
                task->pending=0;
                --task->permits;
                task->consumed=true;
                return 1;
            }
            inline int xTaskCreate(void (*fn)(void*), const char*, int, void* arg, int,
                                   TaskHandle_t* output) {
                task=new Task;
                *output=task;
                task->thread=std::thread([=] { try { fn(arg); } catch (EndWorker&) {} });
                return pdPASS;
            }
            inline void xTaskNotifyGive(TaskHandle_t target) {
                std::lock_guard lock(target->mutex);
                ++target->pending;
                target->changed.notify_all();
            }
            inline void RunOne() {
                std::unique_lock lock(task->mutex);
                const int target=task->completed+1;
                ++task->permits;
                task->changed.notify_all();
                task->changed.wait(lock, [=] { return task->completed >= target; });
            }
            inline void JoinWorker() {
                {
                    std::lock_guard lock(task->mutex);
                    task->shutdown=true;
                    task->changed.notify_all();
                }
                task->thread.join();
                delete task;
                task=nullptr;
            }
            struct esp_timer_create_args_t {
                void (*callback)(void*)=nullptr;
                void* arg=nullptr;
                int dispatch_method=0;
                const char* name=nullptr;
            };
            using esp_timer_handle_t=esp_timer_create_args_t*;
            inline esp_timer_handle_t timer=nullptr;
            inline int esp_timer_create(const esp_timer_create_args_t* args,
                                        esp_timer_handle_t* output) {
                timer=new esp_timer_create_args_t(*args);
                *output=timer;
                return ESP_OK;
            }
            inline int esp_timer_start_periodic(esp_timer_handle_t, int) { return ESP_OK; }
            inline std::atomic<int64_t> clock_us{100000};
            inline int64_t esp_timer_get_time() { return clock_us; }
            inline std::function<void()> on_delay;
            inline void vTaskDelay(int ticks) {
                clock_us += ticks*1000;
                if (on_delay) on_delay();
            }
            struct Motion { std::string kind; int value=0, speed=0, direction=0, servo=-1; };
            inline std::vector<Motion> motion;
            class Otto {
                std::atomic<bool> cancelled{false};
            public:
                void Init(int,int,int,int) {}
                void SetTrims(int,int,int,int) {}
                void ClearStopRequest() { cancelled=false; }
                void RequestStop() { cancelled=true; }
                void SetRestState(bool) {}
                void Home() { if (!cancelled) motion.push_back({"home"}); }
                void Execute2(int amplitude[],int center[],int speed,double phase[],float steps) {
                    assert(center[0]==115 && center[1]==65);
                    assert(amplitude[2]==34 && amplitude[3]==34);
                    const bool turning=amplitude[0]==0 || amplitude[1]==0;
                    const int direction=turning ? (amplitude[1]==0 ? 1 : -1)
                                                : (phase[2] > 0 ? -1 : 1);
                    if (!cancelled) motion.push_back(
                        {turning ? "turn" : "walk",static_cast<int>(steps),speed,direction});
                }
                void Jump(int steps,int speed) {
                    if (!cancelled) motion.push_back({"jump",steps,speed});
                }
                void Swing(int steps,int speed,int) {
                    if (!cancelled) motion.push_back({"swing",steps,speed});
                }
                void UpDown(int steps,int speed,int) {
                    if (!cancelled) motion.push_back({"updown",steps,speed});
                }
                void Sit() { if (!cancelled) motion.push_back({"sit"}); }
            };
            class Oscillator {
                int position=90, pin=-1;
            public:
                bool SetOutputRange(int,int) { return true; }
                void SetTrim(int) {}
                void Attach(int value) { pin=value; }
                int GetPosition() { return position; }
                void SetPosition(int value) {
                    position=value;
                    motion.push_back({"head", value, 0, 0, pin});
                }
            };
            class PowerManager {
            public:
                static void PauseBatteryUpdate() {}
                static void ResumeBatteryUpdate() {}
            };
            class Settings {
                std::string name;
            public:
                inline static std::map<std::string,std::map<std::string,int>> data;
                Settings(const std::string& key,bool=false):name(key) {}
                int GetInt(const std::string& key,int fallback) {
                    auto& values=data[name];
                    auto found=values.find(key);
                    return found==values.end()?fallback:found->second;
                }
                void SetInt(const std::string& key,int value) { data[name][key]=value; }
            };
            struct cJSON { std::map<std::string,int> numbers; };
            inline cJSON* cJSON_CreateObject() { return new cJSON; }
            inline void cJSON_AddNumberToObject(cJSON* p,const char* k,int v) { p->numbers[k]=v; }
            inline void cJSON_AddBoolToObject(cJSON*,const char*,bool) {}
            inline void cJSON_AddStringToObject(cJSON*,const char*,const char*) {}
            using ReturnValue=std::variant<bool,int,std::string,cJSON*>;
            using ToolResult=std::expected<ReturnValue,std::string>;
            enum PropertyType { kPropertyTypeInteger, kPropertyTypeString };
            struct Property {
                std::string name;
                std::variant<int,std::string> data{0};
                Property(std::string n,PropertyType):name(n) {}
                Property(std::string n,PropertyType,const char* v):name(n),data(std::string(v)) {}
                Property(std::string n,PropertyType,int v,int,int):name(n),data(v) {}
                template<class T> T value() const { return std::get<T>(data); }
            };
            struct PropertyList {
                std::map<std::string,Property> values;
                PropertyList()=default;
                PropertyList(std::initializer_list<Property> props) {
                    for (auto& p:props) values.emplace(p.name,p);
                }
                const Property& operator[](const std::string& key) const { return values.at(key); }
            };
            class McpServer {
            public:
                std::map<std::string,std::function<ToolResult(const PropertyList&)>> callbacks;
                static McpServer& GetInstance() { static McpServer server; return server; }
                template<class Callback>
                void AddTool(const std::string& name,const char*,PropertyList,Callback fn) {
                    callbacks[name]=[fn](const PropertyList& props) -> ToolResult {
                        return fn(props);
                    };
                }
            };
            class Board {
            public:
                static Board& GetInstance() { static Board board; return board; }
                void GetBatteryLevel(int& level,bool&,bool&) { level=80; }
            };
            class WifiManager {
            public:
                static WifiManager& GetInstance() { static WifiManager wifi; return wifi; }
                std::string GetIpAddress() { return "192.0.2.1"; }
            };
        """
        driver = r"""
            #include "duck_controller.cc"
            ToolResult Call(const char* name,PropertyList params={}) {
                return McpServer::GetInstance().callbacks.at(name)(params);
            }
            ToolResult Act(const char* action,int steps=1,int speed=1200,int direction=1,
                           const char* tool="self.otto.action",int amount=30) {
                return Call(tool,
                    {Property("action",kPropertyTypeString,action),
                     Property("steps",kPropertyTypeInteger,steps,1,10),
                     Property("speed",kPropertyTypeInteger,speed,800,2000),
                     Property("direction",kPropertyTypeInteger,direction,-1,1),
                     Property("amount",kPropertyTypeInteger,amount,0,170)});
            }
            std::string Status() {
                return std::get<std::string>(*Call("self.otto.get_status"));
            }
            int main() {
                HardwareConfig hw;
                Settings::data["otto_trims"]["left_hand"]=37;
                auto duck=new DuckController(hw);
                assert(duck->IsMoving()); // Boot home already reserves the only slot.
                assert(!Act("walk"));
                duck->Stop();
                clock_us += 31000000;
                timer->callback(timer->arg);
                RunOne();
                assert(motion.empty());
                assert(Status()=="stopped");
                assert(!duck->IsMoving());

                for (const auto* name:{"hand_wave","greeting","servo_sequences","unknown"}) {
                    assert(!Act(name));
                }
                assert(!Act("walk",1,1200,0));
                assert(!Act("quack",4));
                assert(!McpServer::GetInstance().callbacks.contains("self.otto.servo_sequences"));
                assert(Act("walk",100,1));
                assert(!Act("quack"));
                RunOne();
                assert(motion.size() >= 2);
                assert(motion[0].kind=="head" && motion[0].value==45);
                assert(motion[1].kind=="head" && motion[1].value==135);
                auto walk=std::find_if(motion.begin(),motion.end(),
                                     [](const Motion& m){return m.kind=="walk";});
                assert(walk!=motion.end() && walk->value==10 && walk->speed==800);
                assert(Status()=="idle");

                // The video prompt can use FlashDuck tool names while the web
                // interface continues to use the existing Otto names.
                motion.clear();
                assert(Act("sit",1,1200,1,"self.flashduck.action"));
                assert(!Act("home")); // Both names reserve the same motion slot.
                RunOne();
                assert(!motion.empty() && motion.back().kind=="sit");
                assert(std::get<std::string>(*Call("self.flashduck.get_status"))=="idle");
                motion.clear();
                assert(Act("home",1,1200,1,"self.flashduck.action"));
                RunOne();
                assert(!motion.empty() && motion.back().kind=="home");

                motion.clear();
                assert(Act("dance",1,1200,1,"self.flashduck.action"));
                RunOne();
                auto forward=std::find_if(motion.begin(),motion.end(),
                    [](const Motion& m){return m.kind=="walk" && m.direction==1;});
                auto backward=std::find_if(forward,motion.end(),
                    [](const Motion& m){return m.kind=="walk" && m.direction==-1;});
                assert(forward!=motion.end() && backward!=motion.end() && forward<backward);
                assert(forward->value==2 && backward->value==2);
                assert(forward->speed==1000 && backward->speed==1000);
                int neck=135, mouth=45, centered_pulses=0;
                for (const auto& m:motion) {
                    if (m.servo==hw.right_hand_pin) {
                        assert(m.value>=121 && m.value<=149);
                        neck=m.value;
                    } else if (m.servo==hw.left_hand_pin) {
                        if (m.value==55 && mouth<55 && neck==135) ++centered_pulses;
                        mouth=m.value;
                    }
                }
                assert(centered_pulses==3 && neck==135 && mouth==45);
                assert(motion.back().kind=="home");

                motion.clear();
                assert(Act("dance",1,100,1,"self.flashduck.action"));
                RunOne();
                assert(std::count_if(motion.begin(),motion.end(),[](const Motion& m) {
                    return m.kind=="walk" && m.speed==800;
                })==2);

                for (int direction:{-1,1}) {
                    motion.clear();
                    assert(Act("turn",1,1200,direction,"self.flashduck.action"));
                    RunOne();
                    assert(std::any_of(motion.begin(),motion.end(),[=](const Motion& m) {
                        return m.kind=="turn" && m.direction==direction;
                    }));
                }

                motion.clear();
                assert(Act("dance",1,1200,1,"self.flashduck.action"));
                size_t dance_writes_at_stop=0;
                on_delay=[&] {
                    const bool walked=std::any_of(motion.begin(),motion.end(),
                        [](const Motion& m){return m.kind=="walk" && m.direction==1;});
                    if (walked && !dance_writes_at_stop) {
                        Call("self.flashduck.stop");
                        dance_writes_at_stop=motion.size();
                    }
                };
                RunOne();
                on_delay=nullptr;
                assert(dance_writes_at_stop>0 && motion.size()==dance_writes_at_stop);
                assert(std::none_of(motion.begin(),motion.end(),
                    [](const Motion& m){return m.kind=="walk" && m.direction==-1;}));
                assert(Status()=="stopped");

                for (const auto* name:{"jump","swing","updown","head_turn","head_shake",
                                       "look_around","quack","chatter","curious","preen",
                                       "happy_waddle","stretch","peck","yawn","sleepy"}) {
                    motion.clear();
                    assert(Act(name,1,1200,1,"self.flashduck.action"));
                    RunOne();
                    assert(!motion.empty() && Status()=="idle");
                }

                motion.clear();
                assert(Act("head_shake",1,1200,1,"self.flashduck.action"));
                RunOne();
                for (int angle:{105,165}) {
                    assert(std::any_of(motion.begin(),motion.end(),[=](const Motion& m) {
                        return m.servo==hw.right_hand_pin && m.value==angle;
                    }));
                }
                int previous_neck=135;
                for (const auto& m:motion) {
                    if (m.servo==hw.right_hand_pin) {
                        assert(std::abs(m.value-previous_neck)<=2);
                        previous_neck=m.value;
                    }
                }
                assert(previous_neck==135);

                for (int amount:{0,5}) {
                    motion.clear();
                    assert(Act("head_shake",1,1200,1,"self.flashduck.action",amount));
                    RunOne();
                    int minimum=135, maximum=135;
                    for (const auto& m:motion) {
                        if (m.servo==hw.right_hand_pin) {
                            minimum=std::min(minimum,m.value);
                            maximum=std::max(maximum,m.value);
                        }
                    }
                    assert(minimum==135-amount && maximum==135+amount);
                }

                int64_t previous_duration=0;
                for (int period:{800,1600}) {
                    const int64_t start=clock_us;
                    assert(Act("head_shake",1,period,1,"self.flashduck.action",5));
                    RunOne();
                    const int64_t duration=clock_us-start;
                    if (previous_duration) assert(duration-previous_duration==800000);
                    previous_duration=duration;
                }

                motion.clear();
                assert(Act("head_shake",10));
                size_t writes_at_stop=0;
                on_delay=[&] {
                    const bool shaking=std::any_of(motion.begin(),motion.end(),[&](const Motion& m) {
                        return m.servo==hw.right_hand_pin && m.value!=135;
                    });
                    if (shaking && !writes_at_stop) {
                        Call("self.otto.stop");
                        writes_at_stop=motion.size();
                    }
                };
                RunOne();
                on_delay=nullptr;
                assert(writes_at_stop>0 && motion.size()==writes_at_stop);
                assert(Status()=="stopped");
                motion.clear();
                assert(Act("home"));
                RunOne();
                assert(!motion.empty() && motion.back().kind=="home");
                assert(Status()=="idle");

                motion.clear();
                assert(Act("walk"));
                clock_us += 31000000;
                timer->callback(timer->arg);
                RunOne();
                assert(motion.empty() && Status()=="timeout");
                assert(Act("home"));
                RunOne();

                auto trim=[](const char* servo,int value) {
                    return Call("self.otto.set_trim",
                        {Property("servo_type",kPropertyTypeString,servo),
                         Property("trim_value",kPropertyTypeInteger,value,-10,10)});
                };
                assert(!trim("left_hand",5));
                assert(trim("mouth",100));
                assert(!trim("neck",1));
                RunOne();
                assert(Settings::data["duck_trims"]["mouth"]==10);
                assert(Settings::data["otto_trims"]["left_hand"]==37);
                auto saved=std::get<cJSON*>(*Call("self.otto.get_trims"));
                assert(saved->numbers.at("mouth")==10);
                assert(saved->numbers.contains("neck") && !saved->numbers.contains("left_hand"));
                delete saved;
                JoinWorker();
                delete timer;
                delete duck;

                hw.display_cs_pin=hw.right_hand_pin;
                auto disabled=new DuckController(hw);
                assert(task==nullptr);
                assert(Status()=="disabled" && !Act("home"));
                Call("self.otto.stop");
                assert(!disabled->IsMoving());
                delete disabled;
            }
        """
        with tempfile.TemporaryDirectory() as tmp:
            base = Path(tmp)
            (base / "platform.h").write_text(textwrap.dedent(platform))
            (base / "sdkconfig.h").write_text(
                "#define CONFIG_OTTO_DUCK_BODY 1\n"
                "#define CONFIG_OTTO_DUCK_MOUTH_CLOSED 45\n"
                "#define CONFIG_OTTO_DUCK_MOUTH_OPEN 60\n"
                "#define CONFIG_OTTO_DUCK_NECK_MIN 105\n"
                "#define CONFIG_OTTO_DUCK_NECK_CENTER 135\n"
                "#define CONFIG_OTTO_DUCK_NECK_MAX 165\n"
                "#define CONFIG_OTTO_DUCK_GAIT_HIP_OFFSET 25\n"
                "#define CONFIG_OTTO_DUCK_GAIT_HIP_AMPLITUDE 44\n"
                "#define CONFIG_OTTO_DUCK_GAIT_FOOT_AMPLITUDE 34\n"
                "#define CONFIG_OTTO_DUCK_GAIT_PHASE 108\n"
            )
            for name in ("board.h", "esp_timer.h", "freertos/semphr.h",
                         "mcp_server.h", "settings.h", "wifi_manager.h"):
                header = base / name
                header.parent.mkdir(parents=True, exist_ok=True)
                header.write_text('#include "platform.h"\n')
            source = base / "controller_test.cc"
            source.write_text(textwrap.dedent(driver))
            binary = base / "controller_test"
            command = shlex.split(os.environ.get("CXX", "c++")) + [
                "-std=c++23", "-pthread", "-fsanitize=undefined",
                "-fno-sanitize-recover=all", "-include", str(base / "platform.h"),
                "-I", str(base), "-I", str(BOARD), str(source), "-o", str(binary),
            ]
            subprocess.run(command, check=True, capture_output=True, text=True)
            subprocess.run([str(binary)], check=True, capture_output=True, text=True,
                           timeout=20)


if __name__ == "__main__":
    unittest.main()
