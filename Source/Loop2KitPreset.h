#pragma once
#include <vector>

namespace Loop2KitPreset
{
    struct StepEvent
    {
        int midiNote;
        int stepIndex;
        int velocity;
    };

    inline constexpr float defaultSensitivity = 0.62f;
    inline constexpr float defaultMinGapMs    = 65.0f;
    inline constexpr float defaultBpm         = 96.0f;
    inline constexpr int   defaultBars        = 2;
    inline constexpr int   defaultGridDiv     = 16;
    inline constexpr float defaultSwing       = 0.0f;

    inline std::vector<StepEvent> getInitialPattern()
    {
        return {
        { 36, 0, 127 },
        { 44, 2, 37 },
        { 41, 4, 99 },
        { 36, 6, 127 },
        { 41, 7, 52 },
        { 42, 8, 63 },
        { 41, 9, 47 },
        { 36, 10, 127 },
        { 41, 12, 100 },
        { 42, 14, 59 },
        { 36, 16, 127 },
        { 42, 18, 37 },
        { 41, 20, 98 },
        { 36, 22, 127 },
        { 43, 23, 54 },
        { 42, 24, 63 },
        { 45, 25, 53 },
        { 36, 26, 127 },
        { 41, 28, 98 },
        { 42, 30, 34 },
        { 36, 31, 127 }
        };
    }
}
