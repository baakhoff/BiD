#ifndef _H_IMGUI_CUSTOM_H
#define _H_IMGUI_CUSTOM_H

#define IMGUI_DEFINE_PLACEMENT_NEW
#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui.h>
#include <imgui_internal.h>

namespace ImGui {

    bool VFaderFloat(const char* label, const ImVec2& size, float* v, float v_min, float v_max, const char* format = "%.3f", ImGuiSliderFlags flags = 0);
    bool VFaderInt(const char* label, const ImVec2& size, int* v, int v_min, int v_max, const char* format = "%d", ImGuiSliderFlags flags = 0);
    // Draw a label over the last item, centred on the pixels it inks
    void InkCenteredLabel(const char* text, unsigned int col);

    void StyleColorsBiD(ImGuiStyle* dst);
};

#endif