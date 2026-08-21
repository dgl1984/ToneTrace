#include "tonetrace/tonetrace_realtime.h"

#include <chrono>
#include <cmath>
#include <iostream>
#include <vector>

int main() {
  try {
    constexpr int sampleRate = 192000;
    constexpr std::size_t channels = 2;
    constexpr std::size_t blockFrames = 256;
    constexpr double seconds = 3.0;

    tonetrace::CorrectionModel model;
    model.mode = tonetrace::MatchMode::FullMix;
    model.analysisLowHz = 10.0;
    model.analysisHighHz = 30000.0;
    model.resolution = 30;
    model.nodes = {
        {10.0, 0.0, 1.0}, {60.0, 5.0, 1.0}, {250.0, -4.0, 1.0},
        {1000.0, 3.0, 1.0}, {5000.0, -5.0, 1.0},
        {12000.0, 4.0, 1.0}, {30000.0, 0.0, 1.0},
    };
    tonetrace::IrRenderSettings render;
    render.sampleRate = sampleRate;
    const auto ir = tonetrace::renderMinimumPhaseIr(model, render);

    tonetrace::RealtimeConvolverConfig config;
    config.sampleRate = sampleRate;
    config.channels = channels;
    tonetrace::RealtimeConvolver processor(config);
    processor.installInitialKernel(ir);

    std::vector<float> left(blockFrames);
    std::vector<float> right(blockFrames);
    std::vector<float> outputLeft(blockFrames);
    std::vector<float> outputRight(blockFrames);
    const float* inputs[]{left.data(), right.data()};
    float* outputs[]{outputLeft.data(), outputRight.data()};
    const std::size_t totalFrames = static_cast<std::size_t>(seconds * sampleRate);
    const std::size_t blocks = (totalFrames + blockFrames - 1U) / blockFrames;

    double phase = 0.0;
    const auto begin = std::chrono::steady_clock::now();
    for (std::size_t block = 0; block < blocks; ++block) {
      for (std::size_t frame = 0; frame < blockFrames; ++frame) {
        left[frame] = static_cast<float>(0.1 * std::sin(phase));
        right[frame] = static_cast<float>(0.1 * std::cos(phase * 1.17));
        phase += 0.013;
      }
      processor.process(inputs, outputs, channels, blockFrames);
    }
    const double elapsed = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - begin).count();
    const double processed = static_cast<double>(blocks * blockFrames) / sampleRate;
    std::cout << "sample rate: " << sampleRate << " Hz\n"
              << "channels: " << channels << "\n"
              << "IR frames: " << ir.size() << "\n"
              << "processed: " << processed << " seconds\n"
              << "elapsed: " << elapsed << " seconds\n"
              << "speed: " << processed / elapsed << "x realtime\n";
    return processed > elapsed ? 0 : 1;
  } catch (const std::exception& error) {
    std::cerr << "BENCHMARK FAILURE: " << error.what() << '\n';
    return 2;
  }
}
