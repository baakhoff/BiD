#include "imgui-custom.h"

namespace ImGui {

bool VFaderScalar(const char* label, const ImVec2& size, ImGuiDataType data_type, void* p_data, const void* p_min, const void* p_max, const char* format, ImGuiSliderFlags flags, float meter = -1.0f, float peak = -1.0f)
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

    RenderNavCursor(frame_bb, id);

    // Slider behavior
    ImRect grab_bb;
    const bool value_changed = SliderBehavior(frame_bb, id, data_type, p_data, p_min, p_max, format, flags | ImGuiSliderFlags_Vertical, &grab_bb);
    if (value_changed)
        MarkItemEdited(id);

    // A console fader: a narrow inset slot down the middle with tick marks,
    // a soft accent fill below the cap, and a wide cap with a centre line.
    ImDrawList* dl = window->DrawList;
    const bool live = (g.ActiveId == id);
    const float cx = (frame_bb.Min.x + frame_bb.Max.x) * 0.5f;
    // wide enough to read as a meter, still narrower than the cap that rides it
    const float slot_w = ImMax(6.0f, (frame_bb.Max.x - frame_bb.Min.x) * 0.22f);
    const ImRect slot(ImVec2(cx - slot_w * 0.5f, frame_bb.Min.y + 3.0f), ImVec2(cx + slot_w * 0.5f, frame_bb.Max.y - 3.0f));
    // The ticks stand where a console prints its dB figures - unity, -10, -20,
    // -40, -60, silence - and on the cap's travel, not the slot's, so a line
    // meets the cap's centre line exactly when the fader is parked on it.
    const ImU32 tick_col = GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, 0.09f));
    const float grab_inset = 2.0f + style.GrabMinSize * 0.5f;
    const float travel = (frame_bb.Max.y - frame_bb.Min.y) - grab_inset * 2.0f;
    const float ticks[] = { 1.0f, 0.92f, 0.84f, 0.69f, 0.53f, 0.0f };
    for (float t : ticks)
    {
        float ty = frame_bb.Max.y - grab_inset - travel * t;
        dl->AddLine(ImVec2(frame_bb.Min.x + 3.0f, ty), ImVec2(frame_bb.Max.x - 3.0f, ty), tick_col, 1.0f);
    }
    dl->AddRectFilled(slot.Min, slot.Max, GetColorU32(ImVec4(0.040f, 0.045f, 0.055f, 1.0f)), slot_w * 0.5f);
    dl->AddRect(slot.Min, slot.Max, GetColorU32(ImVec4(0.0f, 0.0f, 0.0f, 0.55f)), slot_w * 0.5f);
    const float cap_c = (grab_bb.Min.y + grab_bb.Max.y) * 0.5f;
    if (meter >= 0.0f)
    {
        // The channel's own level, lit down the slot the cap rides in: signal
        // and fader share one line, and the figures printed either side read
        // for both. The shoulders are dB, not fractions of the travel - amber
        // from -12, red from -3, on the same law as the scale.
        const float amber_at = 0.9062f, red_at = 0.9765f;
        const float pitch = ImMax(4.0f, GetFontSize() * 0.35f);
        const float top = slot.Min.y + 1.0f, bot = slot.Max.y - 1.0f;
        int segs = (int)((bot - top) / pitch);
        if (segs < 8)
            segs = 8;
        const float seg_h = (bot - top) / segs;
        const int lit = (int)(ImClamp(meter, 0.0f, 1.0f) * segs + 0.5f);
        for (int sg = 0; sg < segs; sg++)
        {
            const float frac = (sg + 1.0f) / segs;
            const bool on = sg < lit;
            ImU32 col;
            if (frac > red_at)        col = on ? IM_COL32(255, 82, 72, 255)  : IM_COL32(36, 20, 19, 255);
            else if (frac > amber_at) col = on ? IM_COL32(255, 176, 46, 255) : IM_COL32(35, 29, 18, 255);
            else                      col = on ? IM_COL32(96, 222, 132, 255) : IM_COL32(17, 24, 20, 255);
            dl->AddRectFilled(ImVec2(slot.Min.x + 1.0f, bot - (sg + 1) * seg_h + 1.0f),
                ImVec2(slot.Max.x - 1.0f, bot - sg * seg_h), col, 1.5f);
        }
        if (peak > 0.02f)
        {
            const float py = bot - ImClamp(peak, 0.0f, 1.0f) * (bot - top);
            dl->AddLine(ImVec2(slot.Min.x, py), ImVec2(slot.Max.x, py), IM_COL32(255, 255, 255, 200), 1.5f);
        }
    }
    else if (slot.Max.y - cap_c > 3.0f)
        dl->AddRectFilled(ImVec2(slot.Min.x + 1.0f, cap_c), ImVec2(slot.Max.x - 1.0f, slot.Max.y - 1.0f),
            GetColorU32(ImGuiCol_SliderGrab, live ? 0.60f : 0.40f), slot_w * 0.4f);
    const float cap_h = 20.0f;
    ImRect cap(ImVec2(frame_bb.Min.x + 1.0f, cap_c - cap_h * 0.5f), ImVec2(frame_bb.Max.x - 1.0f, cap_c + cap_h * 0.5f));
    ImU32 cap_top = GetColorU32(live ? ImVec4(0.31f, 0.33f, 0.39f, 1.0f) : ImVec4(0.25f, 0.27f, 0.32f, 1.0f));
    ImU32 cap_bot = GetColorU32(live ? ImVec4(0.17f, 0.18f, 0.22f, 1.0f) : ImVec4(0.14f, 0.15f, 0.18f, 1.0f));
    dl->AddRectFilled(cap.Min, cap.Max, GetColorU32(ImVec4(0.0f, 0.0f, 0.0f, 0.55f)), g.Style.GrabRounding);
    dl->AddRectFilledMultiColor(ImVec2(cap.Min.x + 1.0f, cap.Min.y + 1.0f), ImVec2(cap.Max.x - 1.0f, cap.Max.y - 1.0f),
        cap_top, cap_top, cap_bot, cap_bot);
    dl->AddLine(ImVec2(cap.Min.x + 3.0f, cap_c), ImVec2(cap.Max.x - 3.0f, cap_c),
        GetColorU32(live ? ImGuiCol_SliderGrabActive : ImGuiCol_SliderGrab), 2.0f);

    // Display value using user-provided display format so user can add prefix/suffix/decorations to the value.
    // For the vertical slider we allow centered text to overlap the frame padding
    //char value_buf[64];
    //const char* value_buf_end = value_buf + DataTypeFormatString(value_buf, IM_ARRAYSIZE(value_buf), data_type, p_data, format);
    //RenderTextClipped(ImVec2(frame_bb.Min.x, frame_bb.Min.y + style.FramePadding.y), frame_bb.Max, value_buf, value_buf_end, NULL, ImVec2(0.5f, 0.0f));
    //if (label_size.x > 0.0f)
    //    RenderText(ImVec2(frame_bb.Max.x + style.ItemInnerSpacing.x, frame_bb.Min.y + style.FramePadding.y), label);

    return value_changed;
}

// Centre a label over the last item by the pixels it actually inks, not
// its advance box. This face's letters ride low in their line box and the
// icon font's ride high, so stock box-centring reads as misplaced in small
// buttons; ink-centring is what "centred" means to the eye.
void InkCenteredLabel(const char* text, unsigned int col)
{
    ImFont* f = GetFont();
    const float fsize = GetFontSize();
    ImFontBaked* baked = f->GetFontBaked(fsize);
    ImVec2 mn = GetItemRectMin(), mx = GetItemRectMax();
    float pen = 0.0f, x0 = 0.0f, x1 = 0.0f, y0 = 1e9f, y1 = -1e9f;
    bool any = false;
    for (const char* p = text; *p; ) {
        unsigned int ch = 0;
        p += ImTextCharFromUtf8(&ch, p, NULL);
        const ImFontGlyph* g = baked ? baked->FindGlyph((ImWchar)ch) : NULL;
        if (!g) continue;
        if (g->X1 > g->X0) {
            if (!any) x0 = pen + g->X0;
            any = true;
            x1 = pen + g->X1;
            y0 = ImMin(y0, g->Y0);
            y1 = ImMax(y1, g->Y1);
        }
        pen += g->AdvanceX;
    }
    ImVec2 pos;
    if (any)
        pos = ImVec2((mn.x + mx.x) * 0.5f - (x0 + x1) * 0.5f, (mn.y + mx.y) * 0.5f - (y0 + y1) * 0.5f);
    else {
        ImVec2 ts = CalcTextSize(text);
        pos = ImVec2((mn.x + mx.x) * 0.5f - ts.x * 0.5f, (mn.y + mx.y) * 0.5f - ts.y * 0.5f);
    }
    pos.x = (float)(int)(pos.x + 0.5f);
    pos.y = (float)(int)(pos.y + 0.5f);
    GetWindowDrawList()->AddText(f, fsize, pos, col, text);
}

bool VFaderFloat(const char* label, const ImVec2& size, float* v, float v_min, float v_max, const char* format, ImGuiSliderFlags flags, float meter, float peak)
{
    return VFaderScalar(label, size, ImGuiDataType_Float, v, &v_min, &v_max, format, flags, meter, peak);
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
    style->ButtonTextAlign      = ImVec2(0.50f, 0.50f);

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