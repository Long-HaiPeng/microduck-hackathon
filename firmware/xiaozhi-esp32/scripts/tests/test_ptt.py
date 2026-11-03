"""Exercise the production PTT handlers with a deterministic main-loop queue."""

import os
import shlex
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def definition(source, signature):
    start = source.index(signature)
    brace = source.index("{", start)
    depth, end = 1, brace + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


class PttTest(unittest.TestCase):
    def test_hold_takes_manual_control_and_release_cancels_pending_listening(self):
        source = (ROOT / "main/application.cc").read_text()
        methods = "\n".join(definition(source, signature) for signature in (
            "void Application::HandleStartListeningEvent()",
            "void Application::HandleStopListeningEvent()",
            "void Application::StopListening(",
            "void Application::ContinueOpenAudioChannel(ListeningMode mode)",
            "void Application::SetListeningMode(ListeningMode mode)",
            "void Application::StartListeningAudio()",
        ))
        board = (ROOT / "main/boards/otto-robot/otto_robot.cc").read_text()
        ptt = "\n".join(definition(board, signature) for signature in (
            "void PttStart()", "void PttStop()",
        ))
        driver = r'''
            #include <cassert>
            #include <functional>
            #include <mutex>
            #include <optional>
            #include <string>
            #include <vector>
            #define ESP_LOGI(...)
            #define ESP_LOGE(...)
            enum State { kDeviceStateIdle, kDeviceStateListening, kDeviceStateSpeaking,
                kDeviceStateConnecting, kDeviceStateNotifying, kDeviceStateActivating,
                kDeviceStateWifiConfiguring, kDeviceStateAudioTesting };
            enum ListeningMode {
                kListeningModeRealtime, kListeningModeAutoStop, kListeningModeManualStop
            };
            constexpr int kAbortReasonNone=0;
            constexpr int MAIN_EVENT_STOP_LISTENING=1;
            void xEventGroupSetBits(bool* flag, int) { *flag=true; }
            namespace Lang { namespace Sounds { const char* OGG_POPUP="popup"; } }
            enum class PowerSaveLevel { PERFORMANCE };
            struct Board {
                static Board& GetInstance() { static Board board; return board; }
                void SetPowerSaveLevel(PowerSaveLevel) {}
            };
            struct Protocol {
                bool open=false;
                int opens=0, starts=0, stops=0;
                ListeningMode last_start=kListeningModeAutoStop;
                bool IsAudioChannelOpened() { return open; }
                bool OpenAudioChannel() { ++opens; open=true; return true; }
                void SendStartListening(ListeningMode mode) { ++starts; last_start=mode; }
                void SendStopListening() { ++stops; }
            };
            struct Audio {
                bool processing=false;
                void EnableAudioTesting(bool) {}
                void EnableVoiceProcessing(bool value) { processing=value; }
                void PlaySound(const char*) {}
            };
            struct Application {
                State state=kDeviceStateIdle;
                Protocol protocol;
                Protocol* protocol_=&protocol;
                Audio audio_service_;
                ListeningMode listening_mode_=kListeningModeRealtime;
                bool pending_listening_start_=false, play_popup_on_listening_=false;
                bool start_event=false, stop_event=false, state_changed=false;
                bool* event_group_=&stop_event;
                std::mutex mutex_;
                std::optional<ListeningMode> stop_listening_restore_mode_;
                int aborts=0;
                std::vector<std::function<void()>> scheduled;
                static Application& GetInstance() { static Application app; return app; }
                State GetDeviceState() { return state; }
                void SetDeviceState(State next) {
                    if (state!=next) { state=next; state_changed=true; }
                }
                void Schedule(std::function<void()> callback) { scheduled.push_back(callback); }
                void StartListening() { start_event=true; }
                void StopListening(std::optional<ListeningMode> restore_mode=std::nullopt);
                void SwitchListeningMode(ListeningMode mode) { listening_mode_=mode; }
                void StopNotification() { SetDeviceState(kDeviceStateIdle); }
                void AbortSpeaking(int) { ++aborts; }
                void ConfigureWakeWordForListening() {}
                void HandleStartListeningEvent();
                void HandleStopListeningEvent();
                void ContinueOpenAudioChannel(ListeningMode);
                void SetListeningMode(ListeningMode);
                void StartListeningAudio();
                void Drain() {
                    while (start_event || stop_event || state_changed || !scheduled.empty()) {
                        bool start=start_event, stop=stop_event, changed=state_changed;
                        start_event=stop_event=false;
                        state_changed=false;
                        if (changed) {
                            if (state==kDeviceStateListening && !audio_service_.processing)
                                StartListeningAudio();
                            if (state==kDeviceStateIdle) audio_service_.processing=false;
                        }
                        if (start) HandleStartListeningEvent();
                        if (stop) HandleStopListeningEvent();
                        auto tasks=std::move(scheduled);
                        scheduled.clear();
                        for (auto& task:tasks) task();
                    }
                }
            };
            bool IsOttoMoving() { return false; }
            struct Buttons {
                bool ptt_active_=false;
                ListeningMode persistent_mode_=kListeningModeRealtime;
                PTT_METHODS
            };
            APP_METHODS
            int main(int argc, char** argv) {
                assert(argc==2);
                auto& app=Application::GetInstance();
                std::string scenario=argv[1];
                if (scenario=="takeover") {
                    for (auto mode : {kListeningModeAutoStop, kListeningModeRealtime}) {
                        app.state=kDeviceStateListening;
                        app.listening_mode_=mode;
                        app.pending_listening_start_=true;
                        int starts=app.protocol.starts;
                        app.HandleStartListeningEvent();
                        assert(app.listening_mode_==kListeningModeManualStop);
                        assert(app.protocol.starts==starts+1);
                        assert(app.protocol.last_start==kListeningModeManualStop);
                        assert(app.audio_service_.processing);
                        assert(!app.pending_listening_start_);
                    }
                } else if (scenario=="cancel") {
                    app.HandleStartListeningEvent();
                    assert(app.state==kDeviceStateConnecting);
                    app.HandleStopListeningEvent();
                    app.Drain();
                    assert(app.state==kDeviceStateIdle);
                    assert(app.protocol.opens==0 && app.protocol.starts==0);
                } else if (scenario=="release" || scenario=="early_schedule") {
                    Buttons buttons;
                    app.protocol.open=(scenario=="early_schedule");
                    buttons.PttStart();
                    buttons.PttStop();
                    if (scenario=="early_schedule") {
                        // Run() may have captured an older SCHEDULE bit before
                        // START/STOP arrive. That callback must not restore early.
                        auto tasks=std::move(app.scheduled);
                        app.scheduled.clear();
                        for (auto& task:tasks) task();
                    }
                    app.Drain();
                    assert(!buttons.ptt_active_);
                    assert(app.state==kDeviceStateIdle);
                    assert(app.protocol.starts==0);
                    assert(app.listening_mode_==kListeningModeRealtime);
                } else if (scenario=="speaking") {
                    app.state=kDeviceStateSpeaking;
                    app.protocol.open=true;
                    app.audio_service_.processing=true;  // realtime keeps the processor running
                    app.HandleStartListeningEvent();
                    app.Drain();
                    assert(app.aborts==1 && app.state==kDeviceStateListening);
                    assert(app.protocol.last_start==kListeningModeManualStop);
                    app.HandleStopListeningEvent();
                    assert(app.state==kDeviceStateIdle && app.protocol.stops==1);
                } else {
                    assert(false);
                }
            }
        '''.replace("PTT_METHODS", ptt).replace("APP_METHODS", methods)
        with tempfile.TemporaryDirectory() as temporary:
            base = Path(temporary)
            source_path, executable = base / "ptt.cc", base / "ptt"
            source_path.write_text(driver)
            compiled = subprocess.run(
                shlex.split(os.environ.get("CXX", "c++"))
                + ["-std=c++20", "-fsanitize=undefined", str(source_path), "-o", str(executable)],
                capture_output=True, text=True,
            )
            self.assertEqual(compiled.returncode, 0, compiled.stderr)
            for scenario in ("takeover", "cancel", "release", "early_schedule", "speaking"):
                with self.subTest(scenario=scenario):
                    result = subprocess.run([str(executable), scenario], capture_output=True, text=True)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
