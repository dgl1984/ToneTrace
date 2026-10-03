#include "tone/ToneTraceOutputBypass.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace
{
void require(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}
}

int main()
{
    constexpr int sampleRate = 48000;
    constexpr std::size_t transitionFrames = 480; // released 10 ms
    std::vector<float> dry(transitionFrames + 4, 0.25F);
    std::vector<float> wet(transitionFrames + 4, 0.75F);
    float* wetPtr[] { wet.data() };
    const float* dryPtr[] { dry.data() };

    tonetrace::tone::OutputBypassMixer bypass;
    bypass.prepare(sampleRate, false);
    require(std::abs(bypass.wetMix() - 1.0) < 1.0e-12, "wet start state wrong");
    bypass.process(wetPtr, dryPtr, 1, transitionFrames, true);
    require(std::abs(bypass.wetMix()) < 1.0e-9, "10 ms bypass transition did not reach dry");
    require(std::abs(wet[transitionFrames - 1] - 0.25F) < 1.0e-5F,
            "bypass endpoint is not dry input");

    std::fill(wet.begin(), wet.end(), 0.75F);
    bypass.process(wetPtr, dryPtr, 1, transitionFrames, false);
    require(std::abs(bypass.wetMix() - 1.0) < 1.0e-9, "10 ms unbypass transition did not reach wet");
    require(std::abs(wet[transitionFrames - 1] - 0.75F) < 1.0e-5F,
            "unbypass endpoint is not processed input");

    std::cout << "PASS: released 10 ms complete-output bypass transition\n";
    return 0;
}
