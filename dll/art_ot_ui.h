#pragma once

#include "imgui.h"
#include "imgui_internal.h"
#include <cstdio>
#include <string>
#include <vector>
#include <algorithm>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace OT {

struct Theme {
    ImVec4 Accent = ImVec4(77.0f / 255.0f, 123.0f / 255.0f, 255.0f / 255.0f, 1.0f); // #4d7bff (Onetap Blue)
    ImVec4 WindowBg = ImVec4(14.0f / 255.0f, 14.0f / 255.0f, 14.0f / 255.0f, 1.0f);
    ImVec4 ContentBg = ImVec4(9.0f / 255.0f, 9.0f / 255.0f, 9.0f / 255.0f, 1.0f);
    ImVec4 HeaderBg = ImVec4(16.0f / 255.0f, 16.0f / 255.0f, 16.0f / 255.0f, 1.0f);
    ImVec4 BorderDark = ImVec4(4.0f / 255.0f, 4.0f / 255.0f, 4.0f / 255.0f, 1.0f);
    ImVec4 BorderLight = ImVec4(54.0f / 255.0f, 54.0f / 255.0f, 54.0f / 255.0f, 1.0f);
    ImVec4 BorderMid = ImVec4(28.0f / 255.0f, 28.0f / 255.0f, 28.0f / 255.0f, 1.0f);
    ImVec4 BoxBorder = ImVec4(36.0f / 255.0f, 36.0f / 255.0f, 36.0f / 255.0f, 1.0f);
    ImVec4 GroupBorder = ImVec4(36.0f / 255.0f, 36.0f / 255.0f, 36.0f / 255.0f, 1.0f);
    ImVec4 WidgetBg = ImVec4(22.0f / 255.0f, 22.0f / 255.0f, 22.0f / 255.0f, 1.0f);
    ImVec4 WidgetBorder = ImVec4(42.0f / 255.0f, 42.0f / 255.0f, 42.0f / 255.0f, 1.0f);
    ImVec4 TextActive = ImVec4(220.0f / 255.0f, 220.0f / 255.0f, 220.0f / 255.0f, 1.0f);
    ImVec4 TextInactive = ImVec4(115.0f / 255.0f, 115.0f / 255.0f, 115.0f / 255.0f, 1.0f);
};

extern Theme g_Theme;
extern int g_ActiveKeybindId;

inline void InitStyle() {
    ImGuiStyle& style = ImGui::GetStyle();

    style.WindowPadding = ImVec2(0, 0);
    style.FramePadding = ImVec2(4, 2);
    style.ItemSpacing = ImVec2(0, 0);
    style.ItemInnerSpacing = ImVec2(4, 4);
    style.TouchExtraPadding = ImVec2(0, 0);
    style.IndentSpacing = 0.0f;
    style.ScrollbarSize = 6.0f;
    style.GrabMinSize = 6.0f;

    style.WindowBorderSize = 0.0f;
    style.ChildBorderSize = 0.0f;
    style.PopupBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
    style.TabBorderSize = 0.0f;

    style.WindowRounding = 0.0f;
    style.ChildRounding = 0.0f;
    style.FrameRounding = 0.0f;
    style.PopupRounding = 0.0f;
    style.ScrollbarRounding = 0.0f;
    style.GrabRounding = 0.0f;
    style.TabRounding = 0.0f;

    style.AntiAliasedLines = false;
    style.AntiAliasedFill = false;
    style.AntiAliasedLinesUseTex = false;

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_Text] = g_Theme.TextActive;
    colors[ImGuiCol_TextDisabled] = g_Theme.TextInactive;
    colors[ImGuiCol_WindowBg] = g_Theme.WindowBg;
    colors[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
    colors[ImGuiCol_PopupBg] = ImVec4(18.0f / 255.0f, 18.0f / 255.0f, 18.0f / 255.0f, 1.0f);
    colors[ImGuiCol_Border] = g_Theme.WidgetBorder;
    colors[ImGuiCol_FrameBg] = g_Theme.WidgetBg;
    colors[ImGuiCol_FrameBgHovered] = ImVec4(28.0f / 255.0f, 28.0f / 255.0f, 28.0f / 255.0f, 1.0f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(34.0f / 255.0f, 34.0f / 255.0f, 34.0f / 255.0f, 1.0f);
    colors[ImGuiCol_Header] = g_Theme.Accent;
    colors[ImGuiCol_HeaderHovered] = ImVec4(g_Theme.Accent.x, g_Theme.Accent.y, g_Theme.Accent.z, 0.70f);
    colors[ImGuiCol_HeaderActive] = g_Theme.Accent;
    colors[ImGuiCol_Button] = g_Theme.WidgetBg;
    colors[ImGuiCol_ButtonHovered] = ImVec4(28.0f / 255.0f, 28.0f / 255.0f, 28.0f / 255.0f, 1.0f);
    colors[ImGuiCol_ButtonActive] = ImVec4(34.0f / 255.0f, 34.0f / 255.0f, 34.0f / 255.0f, 1.0f);
}

inline void RenderWindowFrame(ImVec2 pos, ImVec2 size) {
    ImDrawList* draw = ImGui::GetWindowDrawList();

    draw->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), IM_COL32(0, 0, 0, 255));
    draw->AddRect(ImVec2(pos.x + 1, pos.y + 1), ImVec2(pos.x + size.x - 1, pos.y + size.y - 1), IM_COL32(54, 54, 54, 255));
    draw->AddRect(ImVec2(pos.x + 2, pos.y + 2), ImVec2(pos.x + size.x - 2, pos.y + size.y - 2), IM_COL32(28, 28, 28, 255));
    draw->AddRect(ImVec2(pos.x + 3, pos.y + 3), ImVec2(pos.x + size.x - 3, pos.y + size.y - 3), IM_COL32(28, 28, 28, 255));
    draw->AddRect(ImVec2(pos.x + 4, pos.y + 4), ImVec2(pos.x + size.x - 4, pos.y + size.y - 4), IM_COL32(56, 56, 56, 255));
    draw->AddRect(ImVec2(pos.x + 5, pos.y + 5), ImVec2(pos.x + size.x - 5, pos.y + size.y - 5), IM_COL32(0, 0, 0, 255));
}

inline bool BeginMenuWindow(const char* name, bool* p_open, ImVec2 size = ImVec2(720, 590)) {
    ImGui::SetNextWindowSize(size, ImGuiCond_Always);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar |
                             ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoScrollbar |
                             ImGuiWindowFlags_NoCollapse;

    ImGui::GetIO().ConfigWindowsMoveFromTitleBarOnly = false;
    bool open = ImGui::Begin(name, p_open, flags);
    if (!open) return false;

    ImVec2 pos = ImGui::GetWindowPos();
    ImVec2 win_size = ImGui::GetWindowSize();

    RenderWindowFrame(pos, win_size);

    return true;
}

inline void EndMenuWindow() {
    ImGui::End();
}

inline bool TabBar(const char* const tabs[], int count, int* current_tab) {
    ImDrawList* draw = ImGui::GetWindowDrawList();
    ImVec2 win_pos = ImGui::GetWindowPos();
    ImVec2 win_size = ImGui::GetWindowSize();

    float bar_x = win_pos.x + 30.0f;
    float bar_y = win_pos.y + 14.0f;
    float bar_w = win_size.x - 60.0f;
    float bar_h = 24.0f;

    float tab_w = bar_w / (float)count;
    bool changed = false;

    for (int i = 0; i < count; ++i) {
        float current_x = bar_x + (float)i * tab_w;
        ImVec2 tab_min = ImVec2(current_x, bar_y);
        ImVec2 tab_max = ImVec2(current_x + tab_w, bar_y + bar_h);

        ImGui::SetCursorScreenPos(tab_min);
        char tab_btn_id[32];
        snprintf(tab_btn_id, sizeof(tab_btn_id), "##ot_t_%d", i);
        ImGui::InvisibleButton(tab_btn_id, ImVec2(tab_w, bar_h));
        bool hovered = ImGui::IsItemHovered();
        bool clicked = ImGui::IsItemClicked();

        if (clicked && *current_tab != i) {
            *current_tab = i;
            changed = true;
        }

        bool active = (*current_tab == i);

        if (active) {
            draw->AddRectFilled(tab_min, tab_max, IM_COL32(22, 25, 32, 180));
        } else if (hovered) {
            draw->AddRectFilled(tab_min, tab_max, IM_COL32(20, 20, 24, 100));
        }

        float text_w = ImGui::CalcTextSize(tabs[i]).x;
        ImVec2 text_pos = ImVec2(tab_min.x + (tab_w - text_w) * 0.5f,
                                 bar_y + (bar_h - 13.0f) * 0.5f);

        ImU32 text_col = active ? IM_COL32(255, 255, 255, 255)
                                : (hovered ? IM_COL32(210, 210, 210, 255) : IM_COL32(115, 115, 115, 255));

        draw->AddText(text_pos, text_col, tabs[i]);

        if (active) {
            draw->AddRectFilled(ImVec2(current_x + 1.0f, bar_y + bar_h - 1.0f),
                                ImVec2(current_x + tab_w - 1.0f, bar_y + bar_h + 1.0f),
                                ImColor(g_Theme.Accent));
        } else {
            draw->AddLine(ImVec2(current_x + 1.0f, bar_y + bar_h),
                          ImVec2(current_x + tab_w - 1.0f, bar_y + bar_h),
                          IM_COL32(36, 36, 36, 255));
        }
    }

    return changed;
}

extern ImVec2 s_ContentBoxPos;
extern ImVec2 s_ContentBoxSize;

inline void BeginContentBox(ImVec2* out_pos = nullptr, ImVec2* out_size = nullptr) {
    ImDrawList* draw = ImGui::GetWindowDrawList();
    ImVec2 win_pos = ImGui::GetWindowPos();
    ImVec2 win_size = ImGui::GetWindowSize();

    float box_x = win_pos.x + 30.0f;
    float box_y = win_pos.y + 45.0f;
    float box_w = win_size.x - 60.0f;
    float box_h = win_size.y - (box_y - win_pos.y) - 30.0f;

    s_ContentBoxPos = ImVec2(box_x, box_y);
    s_ContentBoxSize = ImVec2(box_w, box_h);

    draw->AddRectFilled(s_ContentBoxPos, ImVec2(box_x + box_w, box_y + box_h), IM_COL32(9, 9, 9, 255));
    draw->AddRect(s_ContentBoxPos, ImVec2(box_x + box_w, box_y + box_h), IM_COL32(38, 38, 38, 255));

    if (out_pos) *out_pos = s_ContentBoxPos;
    if (out_size) *out_size = s_ContentBoxSize;
}

inline void EndContentBox() {
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRect(s_ContentBoxPos, ImVec2(s_ContentBoxPos.x + s_ContentBoxSize.x, s_ContentBoxPos.y + s_ContentBoxSize.y), IM_COL32(38, 38, 38, 255));
}

inline void GetGroupMetrics(float& base_x, float& col_w) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->ParentWindow != nullptr) {
        base_x = window->Pos.x;
        col_w = window->Size.x;
    } else {
        base_x = window->DC.CursorPos.x;
        col_w = 200.0f;
    }
}

extern char s_CurrentGroupTitle[64];
extern ImVec2 s_GroupStartPos;
extern ImVec2 s_GroupSize;

inline void BeginGroup(const char* title, float width, float height) {
    ImVec2 pos = ImGui::GetCursorScreenPos();
    s_GroupStartPos = pos;
    s_GroupSize = ImVec2(width, height);
    if (title)
        snprintf(s_CurrentGroupTitle, sizeof(s_CurrentGroupTitle), "%s", title);
    else
        s_CurrentGroupTitle[0] = '\0';

    char child_id[128];
    snprintf(child_id, sizeof(child_id), "##group_%s_%f", title ? title : "", pos.y);
    ImGui::BeginChild(child_id, s_GroupSize, false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    ImGui::SetCursorPos(ImVec2(0.0f, 14.0f));
}

inline void EndGroup() {
    ImGui::EndChild();

    ImDrawList* draw = ImGui::GetWindowDrawList();
    ImVec2 min_p = s_GroupStartPos;
    ImVec2 max_p = ImVec2(min_p.x + s_GroupSize.x, min_p.y + s_GroupSize.y);
    ImU32 border_col = ImColor(g_Theme.GroupBorder);

    if (s_CurrentGroupTitle[0] != '\0') {
        ImVec2 text_size = ImGui::CalcTextSize(s_CurrentGroupTitle);
        ImVec2 title_pos = ImVec2(min_p.x + 8.0f, min_p.y - 6.0f);

        draw->AddLine(min_p, ImVec2(title_pos.x - 3.0f, min_p.y), border_col);
        draw->AddLine(ImVec2(title_pos.x + text_size.x + 3.0f, min_p.y), ImVec2(max_p.x, min_p.y), border_col);

        draw->AddLine(ImVec2(max_p.x, min_p.y), max_p, border_col);
        draw->AddLine(max_p, ImVec2(min_p.x, max_p.y), border_col);
        draw->AddLine(ImVec2(min_p.x, max_p.y), min_p, border_col);

        draw->AddText(title_pos, IM_COL32(185, 185, 185, 255), s_CurrentGroupTitle);
    } else {
        draw->AddRect(min_p, max_p, border_col);
    }
}

inline bool Checkbox(const char* label, bool* v) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;

    float base_x, col_w;
    GetGroupMetrics(base_x, col_w);

    const char* str_id = (label && label[0] != '\0') ? label : "##checkbox";
    const ImGuiID id = window->GetID(str_id);
    ImVec2 pos = window->DC.CursorPos;

    float line_height = 14.0f;
    float box_pad = 8.0f;
    float box_size = 8.0f;
    float text_gap = 8.0f;
    float box_x = base_x + box_pad;
    float text_x = box_x + box_size + text_gap;
    float text_w = ImGui::CalcTextSize(label).x;
    float total_w = (box_pad + box_size + text_gap) + text_w + 4.0f;

    const ImRect total_bb(ImVec2(box_x, pos.y), ImVec2(base_x + total_w, pos.y + line_height));
    ImGui::ItemSize(total_bb, 0.0f);
    if (!ImGui::ItemAdd(total_bb, id)) return false;

    bool hovered, held;
    bool pressed = ImGui::ButtonBehavior(total_bb, id, &hovered, &held);
    if (pressed) {
        *v = !(*v);
        ImGui::MarkItemEdited(id);
    }

    ImDrawList* draw = window->DrawList;
    ImVec2 box_pos = ImVec2(box_x, pos.y + 2.0f);

    ImU32 border_col = hovered ? IM_COL32(80, 80, 80, 255) : IM_COL32(45, 45, 45, 255);
    draw->AddRectFilled(box_pos, ImVec2(box_pos.x + box_size, box_pos.y + box_size), IM_COL32(16, 16, 16, 255));
    draw->AddRect(box_pos, ImVec2(box_pos.x + box_size, box_pos.y + box_size), border_col);

    if (*v) {
        draw->AddRectFilled(ImVec2(box_pos.x + 1.0f, box_pos.y + 1.0f),
                            ImVec2(box_pos.x + box_size - 1.0f, box_pos.y + box_size - 1.0f),
                            ImColor(g_Theme.Accent));
    }

    ImU32 text_col = hovered ? IM_COL32(255, 255, 255, 255) : IM_COL32(195, 195, 195, 255);
    draw->AddText(ImVec2(text_x, pos.y), text_col, label);

    ImGui::Dummy(ImVec2(0, 4.0f));

    return pressed;
}

inline bool CheckboxColor(const char* label, bool* v, float col[4]) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;

    float base_x, col_w;
    GetGroupMetrics(base_x, col_w);

    const char* str_id = (label && label[0] != '\0') ? label : "##checkcol";
    const ImGuiID id = window->GetID(str_id);
    ImVec2 pos = window->DC.CursorPos;

    float line_height = 14.0f;
    float box_pad = 8.0f;
    float box_size = 8.0f;
    float text_gap = 8.0f;
    float box_x = base_x + box_pad;
    float text_x = box_x + box_size + text_gap;
    float right_x = base_x + col_w - 24.0f;

    const ImRect total_bb(ImVec2(box_x, pos.y), ImVec2(right_x, pos.y + line_height));
    ImGui::ItemSize(total_bb, 0.0f);
    if (!ImGui::ItemAdd(total_bb, id)) return false;

    float col_box_w = 16.0f;
    float col_box_h = 7.0f;
    float text_w = ImGui::CalcTextSize(label).x;
    float check_w = (box_pad + box_size + text_gap) + text_w + 4.0f;
    ImRect check_bb(ImVec2(box_x, pos.y), ImVec2(base_x + check_w, pos.y + line_height));
    ImRect col_bb(ImVec2(right_x - col_box_w, pos.y + 3.0f),
                  ImVec2(right_x, pos.y + 3.0f + col_box_h));

    bool check_hovered, check_held;
    bool check_pressed = ImGui::ButtonBehavior(check_bb, id, &check_hovered, &check_held);
    if (check_pressed) {
        *v = !(*v);
        ImGui::MarkItemEdited(id);
    }

    const ImGuiID col_id = window->GetID((std::string(label) + "_col").c_str());
    bool col_hovered, col_held;
    bool col_pressed = ImGui::ButtonBehavior(col_bb, col_id, &col_hovered, &col_held);

    char popup_id[64];
    snprintf(popup_id, sizeof(popup_id), "##picker_%s", label);
    if (col_pressed) {
        ImGui::OpenPopup(popup_id);
    }

    ImDrawList* draw = window->DrawList;
    ImVec2 box_pos = ImVec2(box_x, pos.y + 2.0f);

    ImU32 border_col = check_hovered ? IM_COL32(80, 80, 80, 255) : IM_COL32(45, 45, 45, 255);
    draw->AddRectFilled(box_pos, ImVec2(box_pos.x + box_size, box_pos.y + box_size), IM_COL32(16, 16, 16, 255));
    draw->AddRect(box_pos, ImVec2(box_pos.x + box_size, box_pos.y + box_size), border_col);

    if (*v) {
        draw->AddRectFilled(ImVec2(box_pos.x + 1.0f, box_pos.y + 1.0f),
                            ImVec2(box_pos.x + box_size - 1.0f, box_pos.y + box_size - 1.0f),
                            ImColor(g_Theme.Accent));
    }

    ImU32 text_col = check_hovered ? IM_COL32(255, 255, 255, 255) : IM_COL32(195, 195, 195, 255);
    draw->AddText(ImVec2(text_x, pos.y), text_col, label);

    draw->AddRectFilled(col_bb.Min, col_bb.Max, ImColor(col[0], col[1], col[2], col[3]));
    draw->AddRect(col_bb.Min, col_bb.Max, col_hovered ? IM_COL32(200, 200, 200, 255) : IM_COL32(0, 0, 0, 255));

    if (ImGui::BeginPopup(popup_id)) {
        ImGui::ColorPicker4("##picker", col, ImGuiColorEditFlags_NoSidePreview | ImGuiColorEditFlags_NoInputs);
        ImGui::EndPopup();
    }

    ImGui::Dummy(ImVec2(0, 4.0f));

    return check_pressed;
}

inline bool Combo(const char* label, int* current_item, const char* const items[], int items_count) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;

    float base_x, col_w;
    GetGroupMetrics(base_x, col_w);

    float content_x = base_x + 24.0f;
    float right_x = base_x + col_w - 24.0f;
    float width = right_x - content_x;

    if (label && label[0] != '\0' && label[0] != '#') {
        ImGui::SetCursorScreenPos(ImVec2(content_x, window->DC.CursorPos.y));
        ImGui::TextColored(ImVec4(165.0f / 255.0f, 165.0f / 255.0f, 165.0f / 255.0f, 1.0f), "%s", label);
        ImGui::Dummy(ImVec2(0, 3.0f));
    }

    const char* str_id = (label && label[0] != '\0') ? label : "##combo";
    const ImGuiID id = window->GetID(str_id);
    float y = window->DC.CursorPos.y;
    float height = 20.0f;

    const ImRect bb(ImVec2(content_x, y), ImVec2(right_x, y + height));
    ImGui::ItemSize(bb, 0.0f);
    if (!ImGui::ItemAdd(bb, id)) return false;

    bool hovered, held;
    bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held);

    char popup_id[64];
    snprintf(popup_id, sizeof(popup_id), "##popup_%u", id);

    if (pressed) {
        ImGui::OpenPopup(popup_id);
    }

    ImDrawList* draw = window->DrawList;

    ImU32 bg_col = hovered ? IM_COL32(30, 30, 30, 255) : IM_COL32(22, 22, 22, 255);
    ImU32 border_col = hovered ? IM_COL32(65, 65, 65, 255) : IM_COL32(42, 42, 42, 255);

    draw->AddRectFilled(bb.Min, bb.Max, bg_col);
    draw->AddRect(bb.Min, bb.Max, border_col);

    const char* preview_text = (*current_item >= 0 && *current_item < items_count) ? items[*current_item] : "";
    float text_y = bb.Min.y + (height - 13.0f) * 0.5f;
    draw->AddText(ImVec2(bb.Min.x + 8.0f, text_y), IM_COL32(195, 195, 195, 255), preview_text);

    float arrow_x = bb.Max.x - 10.0f;
    float arrow_y = bb.Min.y + 8.0f;
    draw->AddTriangleFilled(ImVec2(arrow_x - 3.5f, arrow_y),
                            ImVec2(arrow_x + 3.5f, arrow_y),
                            ImVec2(arrow_x, arrow_y + 4.0f),
                            IM_COL32(140, 140, 140, 255));

    bool value_changed = false;
    ImGui::SetNextWindowPos(ImVec2(bb.Min.x, bb.Max.y + 1.0f));
    ImGui::SetNextWindowSize(ImVec2(width, 0));

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, IM_COL32(20, 20, 20, 255));
    ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(42, 42, 42, 255));

    if (ImGui::BeginPopup(popup_id, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize)) {
        ImDrawList* popup_draw = ImGui::GetWindowDrawList();
        for (int i = 0; i < items_count; ++i) {
            bool is_selected = (*current_item == i);
            ImVec2 item_pos = ImGui::GetCursorScreenPos();
            float item_h = 18.0f;
            ImRect item_bb(item_pos, ImVec2(item_pos.x + width, item_pos.y + item_h));

            ImGui::ItemSize(item_bb, 0.0f);
            char item_str_id[64];
            snprintf(item_str_id, sizeof(item_str_id), "##item_%d_%s", i, items[i]);
            ImGuiID item_id = ImGui::GetID(item_str_id);
            if (ImGui::ItemAdd(item_bb, item_id)) {
                bool item_hovered, item_held;
                bool item_pressed = ImGui::ButtonBehavior(item_bb, item_id, &item_hovered, &item_held);
                if (item_pressed) {
                    *current_item = i;
                    value_changed = true;
                    ImGui::CloseCurrentPopup();
                }

                if (item_hovered) {
                    popup_draw->AddRectFilled(item_bb.Min, item_bb.Max, IM_COL32(28, 28, 28, 255));
                }

                ImU32 text_col;
                if (is_selected) {
                    text_col = ImColor(g_Theme.Accent);
                } else if (item_hovered) {
                    text_col = IM_COL32(215, 215, 215, 255);
                } else {
                    text_col = IM_COL32(153, 153, 153, 255);
                }

                float text_row_y = item_bb.Min.y + (item_h - 13.0f) * 0.5f;
                popup_draw->AddText(ImVec2(item_bb.Min.x + 8.0f, text_row_y), text_col, items[i]);
            }
        }
        ImGui::EndPopup();
    }
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(2);

    ImGui::Dummy(ImVec2(0, 5.0f));

    return value_changed;
}

inline bool SliderInt(const char* label, int* val, int min_val, int max_val, const char* suffix = "") {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;

    float base_x, col_w;
    GetGroupMetrics(base_x, col_w);

    float content_x = base_x + 24.0f;
    float right_x = base_x + col_w - 24.0f;

    if (label && label[0] != '\0' && label[0] != '#') {
        ImGui::SetCursorScreenPos(ImVec2(content_x, window->DC.CursorPos.y));
        ImGui::TextColored(ImVec4(165.0f / 255.0f, 165.0f / 255.0f, 165.0f / 255.0f, 1.0f), "%s", label);
        ImGui::Dummy(ImVec2(0, 3.0f));
    }

    ImGuiContext& g = *GImGui;
    const char* str_id = (label && label[0] != '\0') ? label : "##slider";
    const ImGuiID id = window->GetID(str_id);
    float y = window->DC.CursorPos.y;
    float height = 13.0f;

    const ImRect total_bb(ImVec2(base_x + 8.0f, y), ImVec2(right_x + 16.0f, y + height));
    ImGui::ItemSize(total_bb, 0.0f);
    if (!ImGui::ItemAdd(total_bb, id)) return false;

    ImRect minus_bb(ImVec2(base_x + 8.0f, y), ImVec2(base_x + 16.0f, y + height));
    ImRect plus_bb(ImVec2(right_x + 8.0f, y), ImVec2(right_x + 16.0f, y + height));
    ImRect track_bb(ImVec2(content_x, y + 3.0f), ImVec2(right_x, y + 10.0f));

    const ImGuiID minus_id = window->GetID((std::string(label) + "_minus").c_str());
    bool minus_hovered, minus_held;
    if (ImGui::ButtonBehavior(minus_bb, minus_id, &minus_hovered, &minus_held)) {
        if (*val > min_val) {
            *val = (*val - 1 < min_val) ? min_val : (*val - 1);
            ImGui::MarkItemEdited(id);
        }
    }

    const ImGuiID plus_id = window->GetID((std::string(label) + "_plus").c_str());
    bool plus_hovered, plus_held;
    if (ImGui::ButtonBehavior(plus_bb, plus_id, &plus_hovered, &plus_held)) {
        if (*val < max_val) {
            *val = (*val + 1 > max_val) ? max_val : (*val + 1);
            ImGui::MarkItemEdited(id);
        }
    }

    bool track_hovered, track_held;
    ImGui::ButtonBehavior(track_bb, id, &track_hovered, &track_held);
    if (track_held) {
        float mouse_x = g.IO.MousePos.x;
        float norm = (track_bb.Max.x > track_bb.Min.x) ? ((mouse_x - track_bb.Min.x) / (track_bb.Max.x - track_bb.Min.x)) : 0.0f;
        if (norm < 0.0f) norm = 0.0f;
        else if (norm > 1.0f) norm = 1.0f;
        *val = min_val + (int)(norm * (max_val - min_val) + 0.5f);
        ImGui::MarkItemEdited(id);
    }

    ImDrawList* draw = window->DrawList;

    draw->AddText(ImVec2(minus_bb.Min.x + 1.0f, minus_bb.Min.y - 1.0f),
                  minus_hovered ? IM_COL32(255, 255, 255, 255) : IM_COL32(150, 150, 150, 255), "-");

    draw->AddRectFilled(track_bb.Min, track_bb.Max, IM_COL32(24, 24, 24, 255));
    draw->AddRect(track_bb.Min, track_bb.Max, IM_COL32(42, 42, 42, 255));

    float frac = (max_val > min_val) ? (float)(*val - min_val) / (float)(max_val - min_val) : 0.0f;
    if (frac < 0.0f) frac = 0.0f;
    else if (frac > 1.0f) frac = 1.0f;
    float fill_w = frac * (track_bb.Max.x - track_bb.Min.x);

    if (fill_w > 0.0f) {
        draw->AddRectFilled(track_bb.Min, ImVec2(track_bb.Min.x + fill_w, track_bb.Max.y), ImColor(g_Theme.Accent));
    }

    char val_str[32];
    snprintf(val_str, sizeof(val_str), "%d%s", *val, suffix ? suffix : "");
    ImVec2 val_size = ImGui::CalcTextSize(val_str);

    if (frac > 0.35f) {
        float text_x = track_bb.Min.x + fill_w - val_size.x - 2.0f;
        draw->AddText(ImVec2(text_x, track_bb.Min.y - 3.0f), IM_COL32(255, 255, 255, 255), val_str);
    } else {
        float text_x = track_bb.Min.x + fill_w + 3.0f;
        draw->AddText(ImVec2(text_x, track_bb.Min.y - 3.0f), IM_COL32(200, 200, 200, 255), val_str);
    }

    draw->AddText(ImVec2(plus_bb.Min.x + 1.0f, plus_bb.Min.y - 1.0f),
                  plus_hovered ? IM_COL32(255, 255, 255, 255) : IM_COL32(150, 150, 150, 255), "+");

    ImGui::Dummy(ImVec2(0, 5.0f));

    return track_held;
}

inline bool SliderPercent(const char* label, int* val, int min_val = 0, int max_val = 100) {
    return SliderInt(label, val, min_val, max_val, "%");
}

inline bool Button(const char* label, ImVec2 size = ImVec2(0, 0)) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;

    float base_x, col_w;
    GetGroupMetrics(base_x, col_w);

    float content_x = base_x + 24.0f;
    float right_x = base_x + col_w - 24.0f;
    if (size.x <= 0.0f)
        size.x = right_x - content_x;
    if (size.y <= 0.0f)
        size.y = 20.0f;

    const char* str_id = (label && label[0] != '\0') ? label : "##btn";
    const ImGuiID id = window->GetID(str_id);
    float y = window->DC.CursorPos.y;

    const ImRect bb(ImVec2(content_x, y), ImVec2(content_x + size.x, y + size.y));
    ImGui::ItemSize(bb, 0.0f);
    if (!ImGui::ItemAdd(bb, id)) return false;

    bool hovered, held;
    bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held);

    ImDrawList* draw = window->DrawList;
    ImU32 bg_col = held ? IM_COL32(34, 34, 34, 255) : (hovered ? IM_COL32(28, 28, 28, 255) : IM_COL32(22, 22, 22, 255));
    ImU32 border_col = hovered ? IM_COL32(65, 65, 65, 255) : IM_COL32(42, 42, 42, 255);

    draw->AddRectFilled(bb.Min, bb.Max, bg_col);
    draw->AddRect(bb.Min, bb.Max, border_col);

    ImVec2 text_size = ImGui::CalcTextSize(label);
    float text_x = bb.Min.x + (size.x - text_size.x) * 0.5f;
    float text_y = bb.Min.y + (size.y - text_size.y) * 0.5f;
    ImU32 text_col = hovered ? IM_COL32(255, 255, 255, 255) : IM_COL32(195, 195, 195, 255);
    draw->AddText(ImVec2(text_x, text_y), text_col, label);

    ImGui::Dummy(ImVec2(0, 5.0f));
    return pressed;
}

inline const char* GetKeyName(int key) {
    switch (key) {
        case 0: return "none";
        case 1: return "mouse1";
        case 2: return "mouse2";
        case 3: return "mouse3";
        case 4: return "mouse4";
        case 5: return "mouse5";
        case ImGuiKey_LeftShift:
        case ImGuiKey_RightShift: return "shift";
        case ImGuiKey_LeftCtrl:
        case ImGuiKey_RightCtrl: return "ctrl";
        case ImGuiKey_LeftAlt:
        case ImGuiKey_RightAlt: return "alt";
        case ImGuiKey_Space: return "space";
        case ImGuiKey_F: return "f";
        case ImGuiKey_E: return "e";
        case ImGuiKey_C: return "c";
        case ImGuiKey_V: return "v";
        case ImGuiKey_X: return "x";
        case ImGuiKey_Z: return "z";
        case ImGuiKey_Q: return "q";
        case ImGuiKey_R: return "r";
        case ImGuiKey_T: return "t";
        default: return ImGui::GetKeyName((ImGuiKey)key);
    }
}

inline const char* GetKeyModeName(int mode) {
    switch (mode) {
        case 0: return "hold";
        case 1: return "toggle";
        case 2: return "always";
        default: return "hold";
    }
}

inline bool Keybind(const char* label, int* key, int* mode) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;

    float base_x, col_w;
    GetGroupMetrics(base_x, col_w);

    float content_x = base_x + 24.0f;
    float right_x = base_x + col_w - 24.0f;

    if (label && label[0] != '\0' && label[0] != '#') {
        ImGui::SetCursorScreenPos(ImVec2(content_x, window->DC.CursorPos.y));
        ImGui::TextColored(ImVec4(165.0f / 255.0f, 165.0f / 255.0f, 165.0f / 255.0f, 1.0f), "%s", label);
        ImGui::Dummy(ImVec2(0, 3.0f));
    }

    const char* str_id = (label && label[0] != '\0') ? label : "##keybind";
    const ImGuiID id = window->GetID(str_id);
    float y = window->DC.CursorPos.y;
    float height = 18.0f;

    const ImRect bb(ImVec2(content_x, y), ImVec2(right_x, y + height));
    ImGui::ItemSize(bb, 0.0f);
    if (!ImGui::ItemAdd(bb, id)) return false;

    bool hovered, held;
    bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held);

    bool is_listening = (g_ActiveKeybindId == (int)id);

    if (pressed) {
        if (is_listening) {
            g_ActiveKeybindId = -1;
        } else {
            g_ActiveKeybindId = (int)id;
        }
    }

    if (is_listening) {
        for (int mb = 0; mb < 5; ++mb) {
            if (ImGui::IsMouseClicked(mb) && !hovered) {
                *key = mb + 1;
                g_ActiveKeybindId = -1;
                ImGui::MarkItemEdited(id);
                break;
            }
        }
        for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_NamedKey_END; ++k) {
            if (ImGui::IsKeyPressed((ImGuiKey)k)) {
                if (k == ImGuiKey_Escape || k == ImGuiKey_Delete) {
                    *key = 0;
                } else {
                    *key = k;
                }
                g_ActiveKeybindId = -1;
                ImGui::MarkItemEdited(id);
                break;
            }
        }
    }

    char popup_id[64];
    snprintf(popup_id, sizeof(popup_id), "##mode_popup_%u", id);
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        ImGui::OpenPopup(popup_id);
    }

    ImDrawList* draw = window->DrawList;
    ImU32 bg_col = hovered ? IM_COL32(30, 30, 30, 255) : IM_COL32(22, 22, 22, 255);
    ImU32 border_col = hovered ? IM_COL32(65, 65, 65, 255) : IM_COL32(42, 42, 42, 255);

    draw->AddRectFilled(bb.Min, bb.Max, bg_col);
    draw->AddRect(bb.Min, bb.Max, border_col);

    float text_y = bb.Min.y + (height - 13.0f) * 0.5f;

    const char* key_text = is_listening ? "..." : GetKeyName(*key);
    ImU32 key_col = is_listening ? ImGui::ColorConvertFloat4ToU32(g_Theme.Accent) : IM_COL32(195, 195, 195, 255);
    draw->AddText(ImVec2(bb.Min.x + 8.0f, text_y), key_col, key_text);

    const char* mode_text = GetKeyModeName(*mode);
    ImVec2 mode_size = ImGui::CalcTextSize(mode_text);
    draw->AddText(ImVec2(bb.Max.x - mode_size.x - 8.0f, text_y),
                  IM_COL32(130, 130, 130, 255),
                  mode_text);

    if (ImGui::BeginPopup(popup_id)) {
        if (ImGui::Selectable("Hold", *mode == 0)) *mode = 0;
        if (ImGui::Selectable("Toggle", *mode == 1)) *mode = 1;
        if (ImGui::Selectable("Always on", *mode == 2)) *mode = 2;
        ImGui::EndPopup();
    }

    ImGui::Dummy(ImVec2(0, 5.0f));

    return true;
}

#ifndef _WIN32
#define VK_LBUTTON 0x01
#define VK_RBUTTON 0x02
#define VK_MBUTTON 0x04
#define VK_XBUTTON1 0x05
#define VK_XBUTTON2 0x06
#define VK_BACK 0x08
#define VK_TAB 0x09
#define VK_RETURN 0x0D
#define VK_SHIFT 0x10
#define VK_CONTROL 0x11
#define VK_MENU 0x12
#define VK_ESCAPE 0x1B
#define VK_SPACE 0x20
#define VK_PRIOR 0x21
#define VK_NEXT 0x22
#define VK_END 0x23
#define VK_HOME 0x24
#define VK_LEFT 0x25
#define VK_UP 0x26
#define VK_RIGHT 0x27
#define VK_DOWN 0x28
#define VK_INSERT 0x2D
#define VK_DELETE 0x2E
#define VK_F1 0x70
#endif

inline int ImGuiKeyToVK(int key) {
    if (key <= 0) return 0;
    if (key >= 1 && key <= 5) {
        static const int mouseVK[] = { 0, VK_LBUTTON, VK_RBUTTON, VK_MBUTTON, VK_XBUTTON1, VK_XBUTTON2 };
        return mouseVK[key];
    }
    if (key >= ImGuiKey_0 && key <= ImGuiKey_9) return '0' + (key - ImGuiKey_0);
    if (key >= ImGuiKey_A && key <= ImGuiKey_Z) return 'A' + (key - ImGuiKey_A);
    if (key >= ImGuiKey_F1 && key <= ImGuiKey_F12) return VK_F1 + (key - ImGuiKey_F1);
    switch (key) {
        case ImGuiKey_Tab: return VK_TAB;
        case ImGuiKey_LeftArrow: return VK_LEFT;
        case ImGuiKey_RightArrow: return VK_RIGHT;
        case ImGuiKey_UpArrow: return VK_UP;
        case ImGuiKey_DownArrow: return VK_DOWN;
        case ImGuiKey_PageUp: return VK_PRIOR;
        case ImGuiKey_PageDown: return VK_NEXT;
        case ImGuiKey_Home: return VK_HOME;
        case ImGuiKey_End: return VK_END;
        case ImGuiKey_Insert: return VK_INSERT;
        case ImGuiKey_Delete: return VK_DELETE;
        case ImGuiKey_Backspace: return VK_BACK;
        case ImGuiKey_Space: return VK_SPACE;
        case ImGuiKey_Enter: return VK_RETURN;
        case ImGuiKey_Escape: return VK_ESCAPE;
        case ImGuiKey_LeftCtrl: case ImGuiKey_RightCtrl: return VK_CONTROL;
        case ImGuiKey_LeftShift: case ImGuiKey_RightShift: return VK_SHIFT;
        case ImGuiKey_LeftAlt: case ImGuiKey_RightAlt: return VK_MENU;
        default: return 0;
    }
}

struct KeybindSetting {
    int key = 0;
    int mode = 1; // 0 = hold, 1 = toggle, 2 = always
    bool was_down = false;

    bool CheckPressed() {
        if (mode == 2) return true;
        int vk = ImGuiKeyToVK(key);
        if (vk <= 0) return false;

#ifdef _WIN32
        bool currently_down = (GetAsyncKeyState(vk) & 0x8000) != 0;
#else
        bool currently_down = false;
#endif
        bool just_pressed = currently_down && !was_down;
        was_down = currently_down;

        if (mode == 1) { // Toggle
            return just_pressed;
        } else if (mode == 0) { // Hold
            return currently_down;
        }
        return false;
    }

    bool CheckReleased() {
        if (mode != 0) return false;
        int vk = ImGuiKeyToVK(key);
        if (vk <= 0) return false;
#ifdef _WIN32
        bool currently_down = (GetAsyncKeyState(vk) & 0x8000) != 0;
#else
        bool currently_down = false;
#endif
        bool just_released = !currently_down && was_down;
        return just_released;
    }
};

struct MenuKeybinds {
    KeybindSetting Record = { ImGuiKey_F9, 1 };
    KeybindSetting DemoPause = { ImGuiKey_P, 1 };
    KeybindSetting DemoBack = { ImGuiKey_LeftArrow, 1 };
    KeybindSetting DemoFwd = { ImGuiKey_RightArrow, 1 };
    KeybindSetting CampathAdd = { ImGuiKey_K, 1 };
    KeybindSetting Preview = { ImGuiKey_F10, 1 };
    KeybindSetting Hud = { ImGuiKey_H, 1 };
    KeybindSetting Fov = { ImGuiKey_C, 1 };
    KeybindSetting Thirdperson = { ImGuiKey_V, 1 };
    KeybindSetting MenuToggle = { ImGuiKey_F3, 1 };
};

struct MenuState {
    int CurrentTab = 0;
    MenuKeybinds Binds;

    // Tab state
    bool PassesEnable = true;
    bool VmBgActive = false;
    bool PlBgActive = false;
    bool ObjWorldActive = false;
    bool ObjSkyActive = false;
    bool WorldWeapons = true;
    int DepthMode = 0;

    bool VisualsEnable = true;
    int ChamsStyle = 0;
    bool Thirdperson = false;
    int ThirdpersonDist = 120;

    int OutputMode = 0;
    bool TakeManifest = true;
    bool AutoValidate = true;
    int QueueFiles = 500;
    int QueueMb = 256;
    int FfmpegPreset = 0;
    int FfmpegCrf = 17;

    bool HlaeEnable = true;
    bool AgrAutoExport = false;

    bool AccentDummy = true;
    int ColorPreset = 0;
    int ConfigSlot = 0;

    int MenuOpacity = 100;
};

extern MenuState g_Menu;

} // namespace OT
