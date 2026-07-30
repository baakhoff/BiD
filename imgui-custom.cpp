#include "imgui-custom.h"

namespace ImGui {

bool VFaderScalar(const char* label, const ImVec2& size, ImGuiDataType data_type, void* p_data, const void* p_min, const void* p_max, const char* format, ImGuiSliderFlags flags)
{
    ImGuiWindow* window = GetCurrentWindow();
    if (window->SkipItems)
        return false;

    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;
    const ImGuiID id = window->GetID(label);

    const ImVec2 label_size = CalcTextSize(label, NULL, true);
    ImRect frame_bb(window->DC.CursorPos, window->DC.CursorPos + size);
    ImRect bb(frame_bb.Min, frame_bb.Max + ImVec2(label_size.x > 0.0f ? style.ItemInnerSpacing.x + label_size.x : 0.0f, 0.0f));

    ItemSize(bb, style.FramePadding.y);
    if (!ItemAdd(frame_bb, id))
        return false;

    // Default format string when passing NULL
    if (format == NULL)
        format = DataTypeGetInfo(data_type)->PrintFmt;

    const bool hovered = ItemHoverable(frame_bb, id, g.LastItemData.ItemFlags);
    const bool clicked = hovered && IsMouseClicked(0, ImGuiInputFlags_None, id);
    if (clicked || g.NavActivateId == id)
    {
        if (clicked)
            SetKeyOwner(ImGuiKey_MouseLeft, id);
        SetActiveID(id, window);
        SetFocusID(id, window);
        FocusWindow(window);
        g.ActiveIdUsingNavDirMask |= (1 << ImGuiDir_Up) | (1 << ImGuiDir_Down);
    }

    // Draw frame
    //const ImU32 frame_col = GetColorU32(g.ActiveId == id ? ImGuiCol_FrameBgActive : hovered ? ImGuiCol_FrameBgHovered : ImGuiCol_FrameBg);
    float contrast = 0.1;
    const ImU32 frame_col = GetColorU32(g.ActiveId == id ? ImGuiCol_FrameBgActive : g.HoveredId == id ? ImGuiCol_FrameBgHovered : ImGuiCol_FrameBg, 1.0f + contrast);
    const ImU32 frame_col_after = GetColorU32(g.ActiveId == id ? ImGuiCol_FrameBgActive : g.HoveredId == id ? ImGuiCol_FrameBgHovered : ImGuiCol_FrameBg, 1.0f - contrast);
    RenderNavCursor(frame_bb, id);
    //RenderFrame(frame_bb.Min, frame_bb.Max, frame_col, true, g.Style.FrameRounding);

    // Slider behavior
    ImRect grab_bb;
    const bool value_changed = SliderBehavior(frame_bb, id, data_type, p_data, p_min, p_max, format, flags | ImGuiSliderFlags_Vertical, &grab_bb);
    if (value_changed)
        MarkItemEdited(id);

    float thickness = 0.4;
    ImRect draw_bb = frame_bb;
    if (thickness != 1.0f)
    {
        float shrink_amount = (float)(int)((frame_bb.Max.x - frame_bb.Min.x) * 0.5f * (1.0f - thickness));
        draw_bb.Min.x += shrink_amount;
        draw_bb.Max.x -= shrink_amount;
    }

    // Render track
    window->DrawList->AddRectFilled(ImVec2(draw_bb.Min.x, grab_bb.Max.y - (grab_bb.Max.y - grab_bb.Min.y) * 0.65f), draw_bb.Max, frame_col, style.FrameRounding, ImDrawFlags_RoundCornersBottom);
    window->DrawList->AddRectFilled(draw_bb.Min, ImVec2(draw_bb.Max.x, grab_bb.Min.y + (grab_bb.Max.y - grab_bb.Min.y) * 0.35f), frame_col_after, style.FrameRounding, ImDrawFlags_RoundCornersTop);

    // Render grab
    ImRect modgrab_bb = grab_bb;
    modgrab_bb.Min.y -= 20;
    modgrab_bb.Max.y += 20;
    grab_bb.Min.y += 4;
    grab_bb.Max.y -= 4;
    if (grab_bb.Max.y > grab_bb.Min.y) {
        window->DrawList->AddRectFilled(modgrab_bb.Min, modgrab_bb.Max, GetColorU32(ImVec4(0,0,0.1,255)), style.GrabRounding);
        window->DrawList->AddRectFilled(grab_bb.Min, grab_bb.Max, GetColorU32(g.ActiveId == id ? ImGuiCol_SliderGrabActive : ImGuiCol_SliderGrab), style.GrabRounding);
    }

    // Display value using user-provided display format so user can add prefix/suffix/decorations to the value.
    // For the vertical slider we allow centered text to overlap the frame padding
    //char value_buf[64];
    //const char* value_buf_end = value_buf + DataTypeFormatString(value_buf, IM_ARRAYSIZE(value_buf), data_type, p_data, format);
    //RenderTextClipped(ImVec2(frame_bb.Min.x, frame_bb.Min.y + style.FramePadding.y), frame_bb.Max, value_buf, value_buf_end, NULL, ImVec2(0.5f, 0.0f));
    //if (label_size.x > 0.0f)
    //    RenderText(ImVec2(frame_bb.Max.x + style.ItemInnerSpacing.x, frame_bb.Min.y + style.FramePadding.y), label);

    return value_changed;
}

bool VFaderFloat(const char* label, const ImVec2& size, float* v, float v_min, float v_max, const char* format, ImGuiSliderFlags flags)
{
    return VFaderScalar(label, size, ImGuiDataType_Float, v, &v_min, &v_max, format, flags);
}

bool VFaderInt(const char* label, const ImVec2& size, int* v, int v_min, int v_max, const char* format, ImGuiSliderFlags flags)
{
    return VFaderScalar(label, size, ImGuiDataType_S32, v, &v_min, &v_max, format, flags);
}

void StyleColorsBiD(ImGuiStyle* dst)
{
    ImGuiStyle* style = dst ? dst : &ImGui::GetStyle();
    ImVec4* colors = style->Colors;

    // BiD console theme: charcoal panels, one warm amber accent, soft
    // corners. The fader and knob widgets read these same colors, so the
    // accent runs through every moving part.
    const ImVec4 accent    (1.00f, 0.64f, 0.16f, 1.00f);
    const ImVec4 accent_hi (1.00f, 0.74f, 0.32f, 1.00f);
    const ImVec4 accent_dim(1.00f, 0.64f, 0.16f, 0.28f);
    const ImVec4 bg        (0.075f, 0.080f, 0.094f, 1.00f);
    const ImVec4 panel     (0.105f, 0.115f, 0.135f, 1.00f);
    const ImVec4 frame     (0.150f, 0.160f, 0.188f, 1.00f);
    const ImVec4 frame_hi  (0.190f, 0.205f, 0.240f, 1.00f);
    const ImVec4 text      (0.93f, 0.93f, 0.95f, 1.00f);
    const ImVec4 text_dim  (0.52f, 0.54f, 0.60f, 1.00f);

    style->WindowRounding    = 10.0f;
    style->ChildRounding     = 9.0f;
    style->FrameRounding     = 6.0f;
    style->GrabRounding      = 6.0f;
    style->PopupRounding     = 8.0f;
    style->TabRounding       = 6.0f;
    style->ScrollbarRounding = 8.0f;
    style->ScrollbarSize     = 10.0f;
    style->WindowPadding     = ImVec2(12.0f, 10.0f);
    style->FramePadding      = ImVec2(9.0f, 5.0f);
    style->ItemSpacing       = ImVec2(8.0f, 7.0f);
    style->CellPadding       = ImVec2(7.0f, 5.0f);
    style->WindowBorderSize  = 1.0f;
    style->ChildBorderSize   = 0.0f;
    style->PopupBorderSize   = 1.0f;
    style->WindowTitleAlign  = ImVec2(0.50f, 0.50f);
    style->SeparatorTextBorderSize = 2.0f;

    colors[ImGuiCol_Text]                   = text;
    colors[ImGuiCol_TextDisabled]           = text_dim;
    colors[ImGuiCol_WindowBg]               = bg;
    colors[ImGuiCol_ChildBg]                = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_PopupBg]                = ImVec4(0.090f, 0.097f, 0.113f, 0.98f);
    colors[ImGuiCol_Border]                 = ImVec4(1.00f, 1.00f, 1.00f, 0.07f);
    colors[ImGuiCol_BorderShadow]           = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_FrameBg]                = frame;
    colors[ImGuiCol_FrameBgHovered]         = frame_hi;
    colors[ImGuiCol_FrameBgActive]          = ImVec4(0.225f, 0.240f, 0.280f, 1.00f);
    colors[ImGuiCol_TitleBg]                = bg;
    colors[ImGuiCol_TitleBgActive]          = panel;
    colors[ImGuiCol_TitleBgCollapsed]       = ImVec4(0.05f, 0.05f, 0.06f, 0.75f);
    colors[ImGuiCol_MenuBarBg]              = panel;
    colors[ImGuiCol_ScrollbarBg]            = ImVec4(0.00f, 0.00f, 0.00f, 0.25f);
    colors[ImGuiCol_ScrollbarGrab]          = frame_hi;
    colors[ImGuiCol_ScrollbarGrabHovered]   = ImVec4(0.28f, 0.30f, 0.35f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive]    = accent;
    colors[ImGuiCol_CheckMark]              = accent;
    colors[ImGuiCol_SliderGrab]             = accent;
    colors[ImGuiCol_SliderGrabActive]       = accent_hi;
    colors[ImGuiCol_Button]                 = frame;
    colors[ImGuiCol_ButtonHovered]          = accent;   // lit toggles borrow this
    colors[ImGuiCol_ButtonActive]           = accent_hi;
    colors[ImGuiCol_Header]                 = accent_dim;
    colors[ImGuiCol_HeaderHovered]          = ImVec4(1.00f, 0.64f, 0.16f, 0.45f);
    colors[ImGuiCol_HeaderActive]           = ImVec4(1.00f, 0.64f, 0.16f, 0.62f);
    colors[ImGuiCol_Separator]              = colors[ImGuiCol_Border];
    colors[ImGuiCol_SeparatorHovered]       = accent_dim;
    colors[ImGuiCol_SeparatorActive]        = accent;
    colors[ImGuiCol_ResizeGrip]             = ImVec4(1.00f, 1.00f, 1.00f, 0.06f);
    colors[ImGuiCol_ResizeGripHovered]      = accent_dim;
    colors[ImGuiCol_ResizeGripActive]       = accent;
    colors[ImGuiCol_InputTextCursor]        = accent;
    colors[ImGuiCol_TabHovered]             = frame_hi;
    colors[ImGuiCol_Tab]                    = ImVec4(0.120f, 0.128f, 0.150f, 1.00f);
    colors[ImGuiCol_TabSelected]            = frame;
    colors[ImGuiCol_TabSelectedOverline]    = accent;
    colors[ImGuiCol_TabDimmed]              = ImVec4(0.095f, 0.100f, 0.117f, 1.00f);
    colors[ImGuiCol_TabDimmedSelected]      = ImVec4(0.120f, 0.128f, 0.150f, 1.00f);
    colors[ImGuiCol_TabDimmedSelectedOverline] = ImVec4(1.00f, 0.64f, 0.16f, 0.35f);
    colors[ImGuiCol_PlotLines]              = accent;
    colors[ImGuiCol_PlotLinesHovered]       = accent_hi;
    colors[ImGuiCol_PlotHistogram]          = accent;
    colors[ImGuiCol_PlotHistogramHovered]   = accent_hi;
    colors[ImGuiCol_TableHeaderBg]          = panel;
    colors[ImGuiCol_TableBorderStrong]      = ImVec4(1.00f, 1.00f, 1.00f, 0.10f);
    colors[ImGuiCol_TableBorderLight]       = ImVec4(1.00f, 1.00f, 1.00f, 0.05f);
    colors[ImGuiCol_TableRowBg]             = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_TableRowBgAlt]          = ImVec4(1.00f, 1.00f, 1.00f, 0.03f);
    colors[ImGuiCol_TextLink]               = accent_hi;
    colors[ImGuiCol_TextSelectedBg]         = accent_dim;
    colors[ImGuiCol_TreeLines]              = colors[ImGuiCol_Border];
    colors[ImGuiCol_DragDropTarget]         = accent_hi;
    colors[ImGuiCol_UnsavedMarker]          = accent;
    colors[ImGuiCol_NavCursor]              = accent;
    colors[ImGuiCol_NavWindowingHighlight]  = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
    colors[ImGuiCol_NavWindowingDimBg]      = ImVec4(0.10f, 0.10f, 0.12f, 0.50f);
    colors[ImGuiCol_ModalWindowDimBg]       = ImVec4(0.03f, 0.03f, 0.04f, 0.55f);
};
}