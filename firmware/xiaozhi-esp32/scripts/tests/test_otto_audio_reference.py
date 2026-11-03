"""Exercise the real software AEC reference with streamed PCM blocks."""

import os
import shlex
import subprocess
import tempfile
import textwrap
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


class OttoAudioReferenceTest(unittest.TestCase):
    def test_reference_preserves_stream_across_chunks(self):
        header = (ROOT / "main/boards/otto-robot/otto_audio_codec.h").read_text()
        start = header.index("class OttoSoftwareReference {")
        end = header.index("\n};", start) + len("\n};")
        reference = header[start:end]
        preamble = r"""
            #include <atomic>
            #include <cassert>
            #include <cstddef>
            #include <cstdint>
            #include <string>
            #include <thread>
            #include <vector>
            constexpr int AUDIO_CODEC_DMA_DESC_NUM = 6;
            constexpr int AUDIO_CODEC_DMA_FRAME_NUM = 240;
        """
        driver = r"""
            void CheckOverflow() {
                OttoSoftwareReference reference;
                reference.InitReference(1.0);
                std::vector<int16_t> pcm(8192);
                for (int i = 0; i < 8192; ++i) pcm[i] = i + 1;
                reference.PushReference(pcm.data(), pcm.size());
                for (int i = 0; i < 2880; ++i) assert(reference.PopReference() == 0);
                for (int i = 1; i <= 2048; ++i) assert(reference.PopReference() == i);

                // Freed slots can be reused without overwriting queued audio.
                // Only the first 8192 - 2880 samples fit in the initial write.
                std::vector<int16_t> next(3000, 20000);
                reference.PushReference(next.data(), next.size());
                for (int i = 2049; i <= 5312; ++i) assert(reference.PopReference() == i);
                for (int i = 0; i < 3000; ++i) assert(reference.PopReference() == 20000);
                assert(reference.PopReference() == 0);
            }

            void CheckConcurrentOverflow() {
                OttoSoftwareReference reference;
                reference.InitReference(1.0);
                std::vector<int16_t> pcm(8192, 123);
                reference.PushReference(pcm.data(), pcm.size());
                std::atomic<bool> start{false};
                std::thread writer([&]() {
                    while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
                    for (int i = 0; i < 200000; ++i) {
                        const int16_t sample = i % 30000 + 1;
                        reference.PushReference(&sample, 1);
                    }
                });
                std::thread reader([&]() {
                    start.store(true, std::memory_order_release);
                    for (int i = 0; i < 200000; ++i) {
                        const int16_t sample = reference.PopReference();
                        assert(sample >= 0 && sample <= 30000);
                    }
                });
                writer.join();
                reader.join();
            }

            int main(int argc, char** argv) {
                assert(argc == 2);
                const std::string scenario = argv[1];
                if (scenario == "overflow") {
                    CheckOverflow();
                    return 0;
                }
                if (scenario == "concurrent") {
                    CheckConcurrentOverflow();
                    return 0;
                }
                const bool passthrough = scenario == "passthrough";
                const double ratio = passthrough ? 1.0 : 1.5;
                std::vector<int> chunks;
                if (scenario == "variable") chunks = {480, 240};
                else if (scenario == "single") chunks = {241};
                else if (scenario == "fragmented") chunks = {2, 1, 4, 1, 712};
                else if (scenario == "empty") chunks = {0, 2, 0, 718, 0};
                else if (passthrough) chunks = {2, 1, 717};
                else assert(false);

                OttoSoftwareReference reference;
                reference.InitReference(ratio);
                int sample_count = 0;
                for (int size : chunks) {
                    std::vector<int16_t> pcm(size);
                    for (int i = 0; i < size; ++i) {
                        pcm[i] = -2000 + 2 * (sample_count + i);
                    }
                    reference.PushReference(pcm.data(), size);
                    sample_count += size;
                }

                // The existing DMA delay is emitted once; empty writes must
                // not start playback or alter the resampling phase.
                const int delay = passthrough ? 2880 : 2400;
                for (int i = 0; i < delay; ++i) {
                    assert(reference.PopReference() == 0);
                }
                const int output_count = passthrough
                    ? sample_count : (2 * (sample_count - 1)) / 3 + 1;
                for (int i = 0; i < output_count; ++i) {
                    // A linear ramp has an exact output independent of how
                    // input is chunked, including fractional block boundaries.
                    const int expected = -2000 + (passthrough ? 2 : 3) * i;
                    assert(reference.PopReference() == expected);
                }
                assert(reference.PopReference() == 0);
            }
        """
        with tempfile.TemporaryDirectory(prefix="otto-audio-reference-") as temp:
            source = Path(temp) / "reference.cc"
            executable = Path(temp) / "reference"
            source.write_text(textwrap.dedent(preamble) + reference + textwrap.dedent(driver))
            compiler = shlex.split(os.environ.get("CXX", "c++"))
            compiled = subprocess.run(
                [*compiler, "-std=c++17", "-pthread", "-Wall", "-Wextra", "-Werror", str(source), "-o", str(executable)],
                capture_output=True,
                text=True,
            )
            self.assertEqual(compiled.returncode, 0, compiled.stdout + compiled.stderr)
            for scenario in ("variable", "single", "fragmented", "passthrough", "empty", "overflow", "concurrent"):
                with self.subTest(scenario=scenario):
                    result = subprocess.run([str(executable), scenario], capture_output=True, text=True)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
