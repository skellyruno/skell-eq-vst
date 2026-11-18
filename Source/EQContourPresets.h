#pragma once

#include <array>
#include <cstddef>

namespace skellyeq::contour {

enum class Preset { Flat, OriginalContour };
enum class Field { Enabled, Solo, Type, Frequency, Gain, Q, Channel };

struct BandValues {
    float frequency;
    float gain;
    float q;
};

// These are the original eight-band contour values exposed by the editor.
// Both quick presets restore the same band centres and Q values; Flat changes
// only the gain to 0 dB after neutralizing each band to a stereo Peak filter.
inline constexpr std::array<BandValues, 8> originalContour {{
    {25.0f, 1.0f, 1.0f},
    {65.0f, -2.0f, 1.0f},
    {130.0f, -4.0f, 1.0f},
    {300.0f, -3.0f, 1.0f},
    {900.0f, -11.0f, 1.0f},
    {7000.0f, -2.0f, 1.0f},
    {10000.0f, 7.0f, 1.0f},
    {19000.0f, -1.5f, 1.0f}
}};

template <typename Setter>
void applyPreset(Preset preset, Setter&& set)
{
    const bool flat = preset == Preset::Flat;
    for (std::size_t band = 0; band < originalContour.size(); ++band) {
        const auto& values = originalContour[band];
        const int index = static_cast<int>(band);
        set(index, Field::Enabled, 1.0f);
        set(index, Field::Solo, 0.0f);
        set(index, Field::Type, 0.0f);       // Peak
        set(index, Field::Frequency, values.frequency);
        set(index, Field::Gain, flat ? 0.0f : values.gain);
        set(index, Field::Q, values.q);
        set(index, Field::Channel, 0.0f);    // Stereo
    }
}

} // namespace skellyeq::contour
