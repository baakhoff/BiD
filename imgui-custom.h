#ifndef _H_IMGUI_CUSTOM_H
#define _H_IMGUI_CUSTOM_H

#define IMGUI_DEFINE_PLACEMENT_NEW
#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui.h>
#include <imgui_internal.h>

namespace ImGui {

    // meter and peak, when given, light the fader's own slot as a ladder in
    // place of the level fill: both are 0..1 on the same scale as the fader,
    // so a signal stands where the figures beside it say it does. Pass a
    // negative meter for a fader with nothing to show.
    bool VFaderFloat(const char* label, const ImVec2& size, float* v, float v_min, float v_max, const char* format = "%.3f", ImGuiSliderFlags flags = 0, float meter = -1.0f, float peak = -1.0f);
    bool VFaderInt(const char* label, const ImVec2& size, int* v, int v_min, int v_max, const char* format = "%d", ImGuiSliderFlags flags = 0);
    // Draw a label over the last item, centred on the pixels it inks
    void InkCenteredLabel(const char* text, unsigned int col);

    void StyleColorsBiD(ImGuiStyle* dst);
};

#endif