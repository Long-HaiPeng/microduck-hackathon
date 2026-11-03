"""Exercise the real Otto admission/worker methods at deterministic task boundaries."""

import os
import shlex
import subprocess
import tempfile
import textwrap
import unittest
from pathlib import Path


BOARD = Path(__file__).resolve().parents[2] / "main/boards/otto-robot"


class OttoControllerAdmissionTest(unittest.TestCase):
    def test_action_stays_busy_from_enqueue_through_completion(self):
        source = (BOARD / "otto_controller.cc").read_text()
        declarations = source[source.index("    struct OttoActionParams"):
                              source.index("    static void ActionTask")]
        methods = source[source.index("    static void ActionTask"):
                         source.index("    void LoadTrimsFromNVS")]
        public_methods = source[source.index("    void StopActiveActions()"):
                                source.index("    void RegisterMcpTools()")]
        callback_start = source.index("[this](const PropertyList& properties) -> ReturnValue {")
        callback_start = source.index("{", callback_start) + 1
        callback = source[callback_start:source.index("\n            });", callback_start)]
        # The lock helper, when present, is copied with the actual implementation.
        helper_start = source.find("    class ActionControlLock")
        helper = "" if helper_start < 0 else source[helper_start:source.index(
            "    struct OttoActionParams", helper_start)]
        driver = r'''
            #include <algorithm>
            #include <atomic>
            #include <cassert>
            #include <cstdint>
            #include <cstring>
            #include <deque>
            #include <functional>
            #include <map>
            #include <string>
            #include <variant>
            #include <vector>
            #define ESP_LOGI(...)
            #define ESP_LOGW(...)
            #define ESP_LOGD(...)
            #define ESP_LOGE(...)
            #define TAG "test"
            #define pdMS_TO_TICKS(ms) (ms)
            constexpr int pdTRUE=1, pdPASS=1, portMAX_DELAY=-1, configMAX_PRIORITIES=25;
            constexpr int SERVO_COUNT=6, LEFT_LEG=0, RIGHT_LEG=1, LEFT_FOOT=2,
                          RIGHT_FOOT=3, LEFT_HAND=4, RIGHT_HAND=5;
            constexpr int kRemoteMaxSteps=10, kRemoteMinSpeedMs=800,
                          kRemoteMaxSpeedMs=2000, kRemoteMaxAmount=60;
            constexpr uint64_t kRemoteActionTimeoutUs=30000000;
            using UBaseType_t=unsigned;
            using TaskHandle_t=void*;
            using esp_timer_handle_t=void*;
            struct Mutex { bool held=false; };
            using SemaphoreHandle_t=Mutex*;
            static std::function<void()> after_receive, after_unlock;
            int xSemaphoreTake(Mutex* mutex,int) { assert(!mutex->held); mutex->held=true; return 1; }
            void xSemaphoreGive(Mutex* mutex) {
                assert(mutex->held); mutex->held=false;
                if (after_unlock) { auto hook=std::move(after_unlock); after_unlock=nullptr; hook(); }
            }
            struct Queue { size_t item_size; std::deque<std::vector<char>> items; };
            using QueueHandle_t=Queue*;
            int xQueueSend(Queue* queue,const void* item,int wait) {
                if (queue->items.size()==10) { assert(wait==0); return 0; }
                const auto* bytes=static_cast<const char*>(item);
                queue->items.emplace_back(bytes,bytes+queue->item_size); return 1;
            }
            struct WorkerIdle {};
            int xQueueReceive(Queue* queue,void* item,int) {
                if (queue->items.empty()) throw WorkerIdle{};
                std::memcpy(item,queue->items.front().data(),queue->item_size);
                queue->items.pop_front();
                if (after_receive) { auto hook=std::move(after_receive); after_receive=nullptr; hook(); }
                return 1;
            }
            unsigned uxQueueMessagesWaiting(Queue* queue) { return queue->items.size(); }
            void xQueueReset(Queue* queue) { queue->items.clear(); }
            int xTaskCreate(void(*)(void*),const char*,int,void*,int,TaskHandle_t* out) {
                *out=reinterpret_cast<void*>(1); return 1;
            }
            void vTaskDelete(TaskHandle_t) {}
            void vTaskDelay(int) {}
            void esp_timer_stop(esp_timer_handle_t) {}
            void esp_timer_start_once(esp_timer_handle_t,uint64_t) {}
            struct PowerManager { static void PauseBatteryUpdate() {} static void ResumeBatteryUpdate() {} };
            struct cJSON { int valueint=0; double valuedouble=0; };
            cJSON* cJSON_Parse(const char*) { return nullptr; }
            cJSON* cJSON_GetObjectItem(cJSON*,const char*) { return nullptr; }
            cJSON* cJSON_GetArrayItem(cJSON*,int) { return nullptr; }
            bool cJSON_IsArray(cJSON*) { return false; }
            bool cJSON_IsNumber(cJSON*) { return false; }
            bool cJSON_IsObject(cJSON*) { return false; }
            int cJSON_GetArraySize(cJSON*) { return 0; }
            void cJSON_Delete(cJSON*) {}
            const char* cJSON_GetErrorPtr() { return ""; }
            struct Otto {
                static inline std::function<void()> on_home;
                void Home(bool=true) {
                    if (on_home) { auto hook=std::move(on_home); on_home=nullptr; hook(); }
                }
            #define MOTION(name) template<class... T> void name(T...) {}
                MOTION(Execute2) MOTION(MoveServos) MOTION(Walk) MOTION(Turn)
                MOTION(Jump) MOTION(Swing) MOTION(Moonwalker) MOTION(Bend)
                MOTION(ShakeLeg) MOTION(Sit) MOTION(RadioCalisthenics)
                MOTION(MagicCircle) MOTION(Showcase) MOTION(UpDown)
                MOTION(TiptoeSwing) MOTION(Jitter) MOTION(AscendingTurn)
                MOTION(Crusaito) MOTION(Flapping) MOTION(WhirlwindLeg)
                MOTION(HandsUp) MOTION(HandsDown) MOTION(HandWave)
                MOTION(Windmill) MOTION(Takeoff) MOTION(Fitness)
                MOTION(Greeting) MOTION(Shy)
            #undef MOTION
            };
            using ReturnValue=std::variant<bool,std::string>;
            struct Property {
                std::variant<int,std::string> data;
                template<class T> const T& value() const { return std::get<T>(data); }
            };
            struct PropertyList {
                std::map<std::string,Property> values={
                    {"action",{std::string("walk")}}, {"steps",{1}}, {"speed",{1000}},
                    {"direction",{1}}, {"amount",{20}}, {"arm_swing",{20}}};
                const Property& operator[](const char* name) const { return values.at(name); }
            };
            class OttoController {
            public:
                Otto otto_;
                TaskHandle_t action_task_handle_=nullptr;
                QueueHandle_t action_queue_=nullptr;
                Mutex mutex;
                SemaphoreHandle_t action_control_mutex_=&mutex;
                esp_timer_handle_t action_watchdog_timer_=nullptr;
                bool has_hands_=true;
                std::atomic<bool> is_action_in_progress_{false};
                HELPER
                DECLARATIONS
                METHODS
                PUBLIC_METHODS
                ReturnValue Request(const PropertyList& properties) { CALLBACK }
                OttoController() { action_queue_=new Queue{sizeof(OttoActionParams),{}}; }
                ~OttoController() { delete action_queue_; }
                void Run() { try { ActionTask(this); } catch (WorkerIdle&) {} }
            };
            bool Accepted(const ReturnValue& value) {
                return std::holds_alternative<bool>(value) && std::get<bool>(value);
            }
            int main() {
                PropertyList request;
                OttoController controller;
                assert(Accepted(controller.Request(request)));
                assert(controller.IsMoving());
                after_receive=[&] {
                    // The real worker has dequeued but has not yet started execution.
                    assert(controller.IsMoving());
                    assert(!Accepted(controller.Request(request)));
                    assert(uxQueueMessagesWaiting(controller.action_queue_)==0);
                };
                controller.Run();
                assert(!controller.IsMoving());

                // Stop and watchdog replace work with home, which reserves the slot too.
                for (bool watchdog : {false,true}) {
                    assert(Accepted(controller.Request(request)));
                    if (watchdog) OttoController::ActionWatchdogCallback(&controller);
                    else controller.StopActiveActions();
                    assert(controller.IsMoving());
                    after_receive=[&] {
                        assert(controller.IsMoving());
                        assert(!Accepted(controller.Request(request)));
                    };
                    controller.Run();
                    assert(!controller.IsMoving());
                }

                // A request arriving just after completion owns busy until its own completion.
                assert(Accepted(controller.Request(request)));
                bool submitted=false;
                Otto::on_home=[&] {
                    after_unlock=[&] {
                        submitted=Accepted(controller.Request(request));
                        assert(submitted && controller.IsMoving());
                        after_receive=[&] { assert(controller.IsMoving()); };
                    };
                };
                controller.Run();
                assert(submitted && !controller.IsMoving());

                // Local/debug queue paths may enqueue more work; completion must not
                // transiently expose idle while another accepted command is queued.
                controller.QueueAction(OttoController::ACTION_HOME,1,1000,1,0);
                controller.QueueServoSequence("{}");
                int observed=0;
                after_receive=[&] {
                    assert(controller.IsMoving()); ++observed;
                    after_receive=[&] { assert(controller.IsMoving()); ++observed; };
                };
                controller.Run();
                assert(observed==2 && !controller.IsMoving());

                // A full local queue rejects immediately without dropping its busy reservation.
                for (int i=0;i<10;++i) assert(controller.QueueServoSequence("{}"));
                assert(!controller.QueueServoSequence("{}"));
                assert(controller.IsMoving());
                controller.Run();
                assert(!controller.IsMoving());
            }
        '''
        for name, content in (("HELPER", helper), ("DECLARATIONS", declarations),
                              ("PUBLIC_METHODS", public_methods), ("METHODS", methods),
                              ("CALLBACK", callback)):
            driver = driver.replace(name, content)
        with tempfile.TemporaryDirectory() as tmp:
            base = Path(tmp)
            cpp = base / "otto_controller_admission.cc"
            binary = base / "otto_controller_admission"
            cpp.write_text(textwrap.dedent(driver))
            command = shlex.split(os.environ.get("CXX", "c++")) + [
                "-std=c++20", "-fsanitize=address,undefined", str(cpp), "-o", str(binary),
            ]
            compiled = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(compiled.returncode, 0, compiled.stderr)
            ran = subprocess.run([str(binary)], capture_output=True, text=True)
            self.assertEqual(ran.returncode, 0, ran.stderr)


if __name__ == "__main__":
    unittest.main()
