#include "EQContourPresets.h"

#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>

namespace {
using skellyeq::contour::Field;
using skellyeq::contour::Preset;

void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

using BandState = std::array<float, 7>;
using State = std::array<BandState, 8>;

State apply(Preset preset)
{
    State state {};
    std::array<int, 8> writes {};
    skellyeq::contour::applyPreset(preset, [&](int band, Field field, float value) {
        const auto index = static_cast<std::size_t>(field);
        require(band >= 0 && band < 8, "preset writes only the eight stable band IDs");
        require(index < state[0].size(), "preset field is valid");
        state[static_cast<std::size_t>(band)][index] = value;
        ++writes[static_cast<std::size_t>(band)];
    });
    for (int count : writes)
        require(count == 7, "each band receives a complete seven-field state");
    return state;
}
}

int main()
{
    const float expectedFrequency[8] {25.0f, 65.0f, 130.0f, 300.0f,
                                      900.0f, 7000.0f, 10000.0f, 19000.0f};
    const float expectedGain[8] {1.0f, -2.0f, -4.0f, -3.0f,
                                 -11.0f, -2.0f, 7.0f, -1.5f};

    const auto flat = apply(Preset::Flat);
    const auto original = apply(Preset::OriginalContour);

    for (std::size_t band = 0; band < 8; ++band) {
        for (const auto* state : {&flat, &original}) {
            require((*state)[band][static_cast<std::size_t>(Field::Enabled)] == 1.0f,
                    "quick contour enables all eight bands");
            require((*state)[band][static_cast<std::size_t>(Field::Solo)] == 0.0f,
                    "quick contour clears stale solo state");
            require((*state)[band][static_cast<std::size_t>(Field::Type)] == 0.0f,
                    "quick contour restores neutral Peak filter type");
            require((*state)[band][static_cast<std::size_t>(Field::Frequency)] == expectedFrequency[band],
                    "quick contour restores the original band frequency");
            require((*state)[band][static_cast<std::size_t>(Field::Q)] == 1.0f,
                    "quick contour restores Q=1");
            require((*state)[band][static_cast<std::size_t>(Field::Channel)] == 0.0f,
                    "quick contour restores stereo routing");
        }
        require(flat[band][static_cast<std::size_t>(Field::Gain)] == 0.0f,
                "Flat is genuinely flat even after cut/notch/band-pass edits");
        require(original[band][static_cast<std::size_t>(Field::Gain)] == expectedGain[band],
                "Original contour restores all eight authored gain values");
    }

    std::cout << "PASS: Flat and Original contour restore complete eight-band state\n";
    return 0;
}
