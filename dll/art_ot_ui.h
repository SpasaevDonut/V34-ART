#pragma once

#include "imgui.h"
#include "imgui_internal.h"
#include "art_internal.h"
#include "art_gui.h"
#include "art_hlae.h"
#include <cstdio>
#include <string>
#include <vector>
#include <algorithm>
#include <windows.h>

// Forward declarations of engine helpers defined in art_gui.cpp / dll
namespace art {
    void IssueCommand( const char *pFormat, ... );
    void IssueCommandWithSeparator( const char *pFormat, ... );
    void SeekDemoTick( int tick );
    void SetArtGuiVisible( bool visible );
    bool IsArtGuiVisible();
    bool OpenConfiguredOutputDirectory();
    bool SaveConfig( const char *pName );
    bool LoadConfig( const char *pName );
    void ResetEverythingToDefaults();
}

extern volatile LONG g_bRecording;
extern volatile LONG g_nRecordMask;
extern volatile LONG g_bArtPreviewEnabled;
extern volatile LONG g_bArtHudEnabled;
extern volatile LONG g_bArtStatisticsOverlayEnabled;
extern volatile LONG g_bArtTgaRle;
extern volatile LONG g_bNoFlashEnabled;
extern volatile LONG g_bNoSmokeEnabled;
extern volatile LONG g_bNoShadows;
extern volatile LONG g_bNoFog;
extern volatile LONG g_bPlayerChamsEnabled;
extern volatile LONG g_bViewmodelChamsEnabled;
extern volatile LONG g_bSkyboxChamsEnabled;
extern volatile LONG g_bChamsFlatShading;
extern volatile LONG g_bDebugLogging;
extern volatile LONG g_nViewmodelBackgroundRed;
extern volatile LONG g_nViewmodelBackgroundGreen;
extern volatile LONG g_nViewmodelBackgroundBlue;
extern volatile LONG g_nPlayersBackgroundRed;
extern volatile LONG g_nPlayersBackgroundGreen;
extern volatile LONG g_nPlayersBackgroundBlue;
extern volatile LONG g_nObjectIdWorldRed;
extern volatile LONG g_nObjectIdWorldGreen;
extern volatile LONG g_nObjectIdWorldBlue;
extern volatile LONG g_nObjectIdSkyboxRed;
extern volatile LONG g_nObjectIdSkyboxGreen;
extern volatile LONG g_nObjectIdSkyboxBlue;
extern volatile LONG g_nPlayerChamsRed;
extern volatile LONG g_nPlayerChamsGreen;
extern volatile LONG g_nPlayerChamsBlue;
extern volatile LONG g_nViewmodelChamsRed;
extern volatile LONG g_nViewmodelChamsGreen;
extern volatile LONG g_nViewmodelChamsBlue;
extern volatile LONG g_nSkyboxChamsRed;
extern volatile LONG g_nSkyboxChamsGreen;
extern volatile LONG g_nSkyboxChamsBlue;
extern float g_ArtViewModelFovOverride;

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

inline Theme g_Theme;
inline int g_ActiveKeybindId = -1;

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

    // Multi-layered classic Onetap borders:
    // Outer black line
    draw->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), IM_COL32(0, 0, 0, 255));
    // 1st highlight line
    draw->AddRect(ImVec2(pos.x + 1, pos.y + 1), ImVec2(pos.x + size.x - 1, pos.y + size.y - 1), IM_COL32(54, 54, 54, 255));
    // 2px dark gray band
    draw->AddRect(ImVec2(pos.x + 2, pos.y + 2), ImVec2(pos.x + size.x - 2, pos.y + size.y - 2), IM_COL32(28, 28, 28, 255));
    draw->AddRect(ImVec2(pos.x + 3, pos.y + 3), ImVec2(pos.x + size.x - 3, pos.y + size.y - 3), IM_COL32(28, 28, 28, 255));
    // 2nd highlight line
    draw->AddRect(ImVec2(pos.x + 4, pos.y + 4), ImVec2(pos.x + size.x - 4, pos.y + size.y - 4), IM_COL32(56, 56, 56, 255));
    // Inner black border
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

inline ImVec2 s_ContentBoxPos;
inline ImVec2 s_ContentBoxSize;

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

inline char s_CurrentGroupTitle[64] = {0};
inline ImVec2 s_GroupStartPos;
inline ImVec2 s_GroupSize;

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
            *val = std::max(min_val, *val - 1);
            ImGui::MarkItemEdited(id);
        }
    }

    const ImGuiID plus_id = window->GetID((std::string(label) + "_plus").c_str());
    bool plus_hovered, plus_held;
    if (ImGui::ButtonBehavior(plus_bb, plus_id, &plus_hovered, &plus_held)) {
        if (*val < max_val) {
            *val = std::min(max_val, *val + 1);
            ImGui::MarkItemEdited(id);
        }
    }

    bool track_hovered, track_held;
    ImGui::ButtonBehavior(track_bb, id, &track_hovered, &track_held);
    if (track_held) {
        float mouse_x = g.IO.MousePos.x;
        float norm = std::clamp((mouse_x - track_bb.Min.x) / (track_bb.Max.x - track_bb.Min.x), 0.0f, 1.0f);
        *val = min_val + (int)(norm * (max_val - min_val) + 0.5f);
        ImGui::MarkItemEdited(id);
    }

    ImDrawList* draw = window->DrawList;

    draw->AddText(ImVec2(minus_bb.Min.x + 1.0f, minus_bb.Min.y - 1.0f),
                  minus_hovered ? IM_COL32(255, 255, 255, 255) : IM_COL32(150, 150, 150, 255), "-");

    draw->AddRectFilled(track_bb.Min, track_bb.Max, IM_COL32(24, 24, 24, 255));
    draw->AddRect(track_bb.Min, track_bb.Max, IM_COL32(42, 42, 42, 255));

    float frac = (max_val > min_val) ? (float)(*val - min_val) / (float)(max_val - min_val) : 0.0f;
    frac = std::clamp(frac, 0.0f, 1.0f);
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

        bool currently_down = (GetAsyncKeyState(vk) & 0x8000) != 0;
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
        bool currently_down = (GetAsyncKeyState(vk) & 0x8000) != 0;
        bool just_released = !currently_down && was_down;
        return just_released;
    }
};

struct MenuKeybinds {
    KeybindSetting Record = { ImGuiKey_F9, 1 };         // Record toggle (F9)
    KeybindSetting DemoPause = { ImGuiKey_P, 1 };       // Demo pause/resume toggle (P)
    KeybindSetting DemoBack = { ImGuiKey_LeftArrow, 1 };// Demo step back (-100 ticks)
    KeybindSetting DemoFwd = { ImGuiKey_RightArrow, 1 };// Demo step fwd (+100 ticks)
    KeybindSetting CampathAdd = { ImGuiKey_K, 1 };      // Campath add keyframe (K)
    KeybindSetting Preview = { ImGuiKey_F10, 1 };       // Live preview toggle (F10)
    KeybindSetting Hud = { ImGuiKey_H, 1 };             // HUD toggle (H)
    KeybindSetting Fov = { ImGuiKey_C, 1 };             // FOV toggle (C)
    KeybindSetting Thirdperson = { ImGuiKey_V, 1 };     // Thirdperson (V)
    KeybindSetting MenuToggle = { ImGuiKey_F3, 1 };     // Menu toggle (F3)
};

struct MenuState {
    int CurrentTab = 0; // Default to "capture"
    MenuKeybinds Binds;

    // Passes
    bool PassesEnable = true;
    bool VmBgActive = false;
    float VmBgColor[4] = { 0.0f, 1.0f, 0.0f, 1.0f };
    bool PlBgActive = false;
    float PlBgColor[4] = { 0.0f, 1.0f, 0.0f, 1.0f };
    bool ObjWorldActive = false;
    float ObjWorldColor[4] = { 1.0f, 1.0f, 0.0f, 1.0f };
    bool ObjSkyActive = false;
    float ObjSkyColor[4] = { 1.0f, 0.0f, 0.0f, 1.0f };
    bool WorldWeapons = true;
    int DepthMode = 0;

    // Visuals
    bool VisualsEnable = true;
    int ChamsStyle = 0;
    bool Thirdperson = false;
    int ThirdpersonDist = 120;

    // Output
    int OutputMode = 0;
    bool TakeManifest = true;
    bool AutoValidate = true;
    int QueueFiles = 500;
    int QueueMb = 256;
    int FfmpegPreset = 0;
    int FfmpegCrf = 17;

    // HLAE
    bool HlaeEnable = true;
    bool AgrAutoExport = false;

    // Configs & Theme
    bool AccentDummy = true;
    int ColorPreset = 0;
    int ConfigSlot = 0;

    // Settings
    int MenuOpacity = 100;
};

inline MenuState g_Menu;

inline void ProcessRecordingKeybinds() {
    // 1. Record hotkey (toggle or hold)
    if (g_Menu.Binds.Record.mode == 0) { // Hold
        if (g_Menu.Binds.Record.CheckPressed()) {
            if (!InterlockedCompareExchange(&g_bRecording, FALSE, FALSE)) {
                art::IssueCommand("art_start");
            }
        }
        if (g_Menu.Binds.Record.CheckReleased()) {
            if (InterlockedCompareExchange(&g_bRecording, FALSE, FALSE)) {
                art::IssueCommand("art_stop");
            }
        }
    } else { // Toggle or always
        if (g_Menu.Binds.Record.CheckPressed()) {
            art::IssueCommand("art_toggle");
        }
    }

    // 2. Demo pause / resume hotkey
    if (g_Menu.Binds.DemoPause.CheckPressed()) {
        art::IssueCommand(g_Gui.demoWasPlaying ? "demo_resume" : "demo_pause");
    }

    // 3. Demo step back 100 ticks
    if (g_Menu.Binds.DemoBack.CheckPressed()) {
        int curTick = g_Gui.currentDemoTickValid ? g_Gui.currentDemoTick : g_Gui.demoTick;
        art::SeekDemoTick(std::max(0, curTick - 100));
    }

    // 4. Demo step forward 100 ticks
    if (g_Menu.Binds.DemoFwd.CheckPressed()) {
        int curTick = g_Gui.currentDemoTickValid ? g_Gui.currentDemoTick : g_Gui.demoTick;
        art::SeekDemoTick(curTick + 100);
    }

    // 5. Campath add keyframe
    if (g_Menu.Binds.CampathAdd.CheckPressed()) {
        art::IssueCommand("mirv_campath add");
    }

    // 6. Live preview toggle
    if (g_Menu.Binds.Preview.CheckPressed()) {
        art::IssueCommand("art_preview toggle");
    }

    // 7. HUD toggle
    if (g_Menu.Binds.Hud.CheckPressed()) {
        art::IssueCommand("art_hud toggle");
    }

    // 8. Thirdperson toggle
    if (g_Menu.Binds.Thirdperson.CheckPressed()) {
        g_Menu.Thirdperson = !g_Menu.Thirdperson;
        art::IssueCommand(g_Menu.Thirdperson ? "thirdperson" : "firstperson");
    }
}

// -----------------------------------------------------------------------------
// TAB 0: CAPTURE
// -----------------------------------------------------------------------------
inline void RenderCaptureTab(float col_w, ImVec2 content_pos) {
    float pad_x = 32.0f;
    float pad_y = 16.0f;
    float col_gap = 18.0f;
    float col1_x = content_pos.x + pad_x;
    float col2_x = col1_x + col_w + col_gap;

    // LEFT COLUMN
    bool recordingActive = InterlockedCompareExchange(&g_bRecording, FALSE, FALSE) != FALSE;
    ImGui::SetCursorScreenPos(ImVec2(col1_x, content_pos.y + pad_y));
    if (Checkbox("recording active", &recordingActive)) {
        art::IssueCommand("art_toggle");
    }

    float top_group_y = content_pos.y + pad_y + 14.0f + 10.0f;

    ImGui::SetCursorScreenPos(ImVec2(col1_x, top_group_y));
    BeginGroup("recording settings", col_w, 160.0f);
    {
        if (Checkbox("auto pause demo after rec", &g_Gui.autoPauseDemoAfterRecording)) {
            art::IssueCommand(g_Gui.autoPauseDemoAfterRecording ? "art_demo_pause_after_recording on" : "art_demo_pause_after_recording off");
        }
        Checkbox("auto resume demo on rec", &g_Gui.autoResumeDemoOnRecordingStart);

        static int fps_preset = 0;
        static const char* fps_presets[] = { "default (0)", "150 fps", "300 fps", "600 fps", "2000 fps" };
        if (Combo("framerate preset", &fps_preset, fps_presets, 5)) {
            static const int rates[] = { 0, 150, 300, 600, 2000 };
            g_Gui.hostFramerate = rates[fps_preset];
            art::IssueCommand("host_framerate %d", g_Gui.hostFramerate);
        }

        if (SliderInt("framerate", &g_Gui.hostFramerate, 0, 2000, " fps")) {
            art::IssueCommand("host_framerate %d", g_Gui.hostFramerate);
        }
    }
    EndGroup();

    ImGui::SetCursorScreenPos(ImVec2(col1_x, top_group_y + 160.0f + 10.0f));
    BeginGroup("recording keybinds", col_w, 245.0f);
    {
        Keybind("record toggle key", &g_Menu.Binds.Record.key, &g_Menu.Binds.Record.mode);
        Keybind("demo pause key", &g_Menu.Binds.DemoPause.key, &g_Menu.Binds.DemoPause.mode);
        Keybind("demo step back (-100)", &g_Menu.Binds.DemoBack.key, &g_Menu.Binds.DemoBack.mode);
        Keybind("demo step fwd (+100)", &g_Menu.Binds.DemoFwd.key, &g_Menu.Binds.DemoFwd.mode);
        Keybind("preview toggle key", &g_Menu.Binds.Preview.key, &g_Menu.Binds.Preview.mode);
    }
    EndGroup();

    // RIGHT COLUMN
    ImGui::SetCursorScreenPos(ImVec2(col2_x, top_group_y));
    BeginGroup("demo playback", col_w, 205.0f);
    {
        SliderInt("demo tick", &g_Gui.demoTick, 0, 100000);
        if (Button("goto target tick")) {
            art::SeekDemoTick(g_Gui.demoTick);
        }
        if (Button(g_Gui.demoWasPlaying ? "pause demo" : "resume demo")) {
            art::IssueCommand(g_Gui.demoWasPlaying ? "demo_pause" : "demo_resume");
        }
        Checkbox("hide spectator ui", &g_Gui.spectatorUiHidden);
        if (Button("stop recording now")) {
            art::IssueCommand("art_stop");
        }
    }
    EndGroup();

    ImGui::SetCursorScreenPos(ImVec2(col2_x, top_group_y + 205.0f + 10.0f));
    BeginGroup("overlays & preview", col_w, 200.0f);
    {
        bool preview = (g_bArtPreviewEnabled != 0);
        if (Checkbox("live preview window", &preview)) {
            art::IssueCommand("art_preview %d", preview ? 1 : 0);
        }

        bool hud = (g_bArtHudEnabled != 0);
        if (Checkbox("draw game hud", &hud)) {
            art::IssueCommand("art_hud %d", hud ? 1 : 0);
        }

        bool stats = (g_bArtStatisticsOverlayEnabled != 0);
        if (Checkbox("statistics overlay", &stats)) {
            InterlockedExchange(&g_bArtStatisticsOverlayEnabled, stats ? TRUE : FALSE);
        }

        Keybind("hud toggle key", &g_Menu.Binds.Hud.key, &g_Menu.Binds.Hud.mode);
    }
    EndGroup();
}

// -----------------------------------------------------------------------------
// TAB 1: PASSES
// -----------------------------------------------------------------------------
inline void RenderPassesTab(float col_w, ImVec2 content_pos) {
    float pad_x = 32.0f;
    float pad_y = 16.0f;
    float col_gap = 18.0f;
    float col1_x = content_pos.x + pad_x;
    float col2_x = col1_x + col_w + col_gap;

    ImGui::SetCursorScreenPos(ImVec2(col1_x, content_pos.y + pad_y));
    Checkbox("enable passes", &g_Menu.PassesEnable);

    float top_group_y = content_pos.y + pad_y + 14.0f + 10.0f;

    // LEFT COLUMN
    ImGui::SetCursorScreenPos(ImVec2(col1_x, top_group_y));
    BeginGroup("capture passes", col_w, 275.0f);
    {
        LONG mask = InterlockedCompareExchange(&g_nRecordMask, 0, 0);

        bool p_normal = (mask & (1 << 0)) != 0;
        if (Checkbox("normal pass", &p_normal)) {
            InterlockedXor(&g_nRecordMask, 1 << 0);
        }

        bool p_clear = (mask & (1 << 1)) != 0;
        if (Checkbox("clear pass (no hud)", &p_clear)) {
            InterlockedXor(&g_nRecordMask, 1 << 1);
        }

        bool p_noplayers = (mask & (1 << 2)) != 0;
        if (Checkbox("clear-noplayers pass", &p_noplayers)) {
            InterlockedXor(&g_nRecordMask, 1 << 2);
        }

        bool p_viewmodel = (mask & (1 << 3)) != 0;
        if (Checkbox("viewmodel pass", &p_viewmodel)) {
            InterlockedXor(&g_nRecordMask, 1 << 3);
        }

        bool p_players = (mask & (1 << 4)) != 0;
        if (Checkbox("players pass", &p_players)) {
            InterlockedXor(&g_nRecordMask, 1 << 4);
        }

        bool p_depth = (mask & (1 << 5)) != 0;
        if (Checkbox("depth pass", &p_depth)) {
            InterlockedXor(&g_nRecordMask, 1 << 5);
        }

        bool p_objectid = (mask & (1 << 6)) != 0;
        if (Checkbox("objectid pass", &p_objectid)) {
            InterlockedXor(&g_nRecordMask, 1 << 6);
        }
    }
    EndGroup();

    ImGui::SetCursorScreenPos(ImVec2(col1_x, top_group_y + 275.0f + 10.0f));
    BeginGroup("depth pass setup", col_w, 130.0f);
    {
        int d_start = (int)g_ArtRecordingStats.depthStart;
        if (SliderInt("depth start", &d_start, 0, 1000)) {
            g_ArtRecordingStats.depthStart = (float)d_start;
            art::IssueCommand("art_depth_start %d", d_start);
        }

        int d_end = (int)g_ArtRecordingStats.depthEnd;
        if (SliderInt("depth end", &d_end, 100, 8000)) {
            g_ArtRecordingStats.depthEnd = (float)d_end;
            art::IssueCommand("art_depth_end %d", d_end);
        }
    }
    EndGroup();

    // RIGHT COLUMN
    ImGui::SetCursorScreenPos(ImVec2(col2_x, top_group_y));
    BeginGroup("background colors", col_w, 220.0f);
    {
        float vm_col[4] = { g_nViewmodelBackgroundRed / 255.0f, g_nViewmodelBackgroundGreen / 255.0f, g_nViewmodelBackgroundBlue / 255.0f, 1.0f };
        if (CheckboxColor("viewmodel background", &g_Menu.VmBgActive, vm_col)) {
            art::IssueCommand("art_viewmodel_color %d %d %d", (int)(vm_col[0] * 255.0f), (int)(vm_col[1] * 255.0f), (int)(vm_col[2] * 255.0f));
        }

        float pl_col[4] = { g_nPlayersBackgroundRed / 255.0f, g_nPlayersBackgroundGreen / 255.0f, g_nPlayersBackgroundBlue / 255.0f, 1.0f };
        if (CheckboxColor("players background", &g_Menu.PlBgActive, pl_col)) {
            art::IssueCommand("art_players_color %d %d %d", (int)(pl_col[0] * 255.0f), (int)(pl_col[1] * 255.0f), (int)(pl_col[2] * 255.0f));
        }

        float obj_w[4] = { g_nObjectIdWorldRed / 255.0f, g_nObjectIdWorldGreen / 255.0f, g_nObjectIdWorldBlue / 255.0f, 1.0f };
        if (CheckboxColor("objectid world color", &g_Menu.ObjWorldActive, obj_w)) {
            art::IssueCommand("art_objectid_color world %d %d %d", (int)(obj_w[0] * 255.0f), (int)(obj_w[1] * 255.0f), (int)(obj_w[2] * 255.0f));
        }

        float obj_s[4] = { g_nObjectIdSkyboxRed / 255.0f, g_nObjectIdSkyboxGreen / 255.0f, g_nObjectIdSkyboxBlue / 255.0f, 1.0f };
        if (CheckboxColor("objectid skybox color", &g_Menu.ObjSkyActive, obj_s)) {
            art::IssueCommand("art_objectid_color skybox %d %d %d", (int)(obj_s[0] * 255.0f), (int)(obj_s[1] * 255.0f), (int)(obj_s[2] * 255.0f));
        }
    }
    EndGroup();

    ImGui::SetCursorScreenPos(ImVec2(col2_x, top_group_y + 220.0f + 10.0f));
    BeginGroup("pass options", col_w, 185.0f);
    {
        Checkbox("players world weapons", &g_Menu.WorldWeapons);

        static const char* depth_modes[] = { "linear depth", "inverse depth", "raw z-buffer" };
        Combo("depth mode", &g_Menu.DepthMode, depth_modes, 3);

        if (Button("reset colors to default")) {
            art::IssueCommand("art_viewmodel_color 0 255 0");
            art::IssueCommand("art_players_color 0 255 0");
        }
    }
    EndGroup();
}

// -----------------------------------------------------------------------------
// TAB 2: VISUALS
// -----------------------------------------------------------------------------
inline void RenderVisualsTab(float col_w, ImVec2 content_pos) {
    float pad_x = 32.0f;
    float pad_y = 16.0f;
    float col_gap = 18.0f;
    float col1_x = content_pos.x + pad_x;
    float col2_x = col1_x + col_w + col_gap;

    ImGui::SetCursorScreenPos(ImVec2(col1_x, content_pos.y + pad_y));
    Checkbox("enable visuals", &g_Menu.VisualsEnable);

    float top_group_y = content_pos.y + pad_y + 14.0f + 10.0f;

    // LEFT COLUMN
    ImGui::SetCursorScreenPos(ImVec2(col1_x, top_group_y));
    BeginGroup("effects suppression", col_w, 160.0f);
    {
        bool no_flash = (g_bNoFlashEnabled != 0);
        if (Checkbox("no flash effect", &no_flash)) {
            InterlockedExchange(&g_bNoFlashEnabled, no_flash ? TRUE : FALSE);
        }

        bool no_smoke = (g_bNoSmokeEnabled != 0);
        if (Checkbox("no smoke particles", &no_smoke)) {
            InterlockedExchange(&g_bNoSmokeEnabled, no_smoke ? TRUE : FALSE);
        }

        bool no_shadows = (g_bNoShadows != 0);
        if (Checkbox("suppress dynamic shadows", &no_shadows)) {
            InterlockedExchange(&g_bNoShadows, no_shadows ? TRUE : FALSE);
        }

        bool no_fog = (g_bNoFog != 0);
        if (Checkbox("disable map fog", &no_fog)) {
            InterlockedExchange(&g_bNoFog, no_fog ? TRUE : FALSE);
        }
    }
    EndGroup();

    ImGui::SetCursorScreenPos(ImVec2(col1_x, top_group_y + 160.0f + 10.0f));
    BeginGroup("field of view (fov)", col_w, 245.0f);
    {
        bool override_fov = !g_Gui.globalFovDefault;
        if (Checkbox("override global fov", &override_fov)) {
            g_Gui.globalFovDefault = !override_fov;
        }

        int fov_int = (int)g_Gui.globalFov;
        if (SliderInt("camera fov", &fov_int, 30, 150)) {
            g_Gui.globalFov = (float)fov_int;
            art::IssueCommand("art_fov %.1f", g_Gui.globalFov);
        }

        int vm_fov = (int)g_ArtViewModelFovOverride;
        if (SliderInt("viewmodel fov", &vm_fov, 30, 150)) {
            g_ArtViewModelFovOverride = (float)vm_fov;
            art::IssueCommand("art_fov_viewmodel %.1f", (float)vm_fov);
        }

        Keybind("fov toggle key", &g_Menu.Binds.Fov.key, &g_Menu.Binds.Fov.mode);
    }
    EndGroup();

    // RIGHT COLUMN
    ImGui::SetCursorScreenPos(ImVec2(col2_x, top_group_y));
    BeginGroup("chams & materials", col_w, 235.0f);
    {
        bool pl_chams = (g_bPlayerChamsEnabled != 0);
        float pl_col[4] = { g_nPlayerChamsRed / 255.0f, g_nPlayerChamsGreen / 255.0f, g_nPlayerChamsBlue / 255.0f, 1.0f };
        if (CheckboxColor("player chams", &pl_chams, pl_col)) {
            InterlockedExchange(&g_bPlayerChamsEnabled, pl_chams ? TRUE : FALSE);
            InterlockedExchange(&g_nPlayerChamsRed, (LONG)(pl_col[0] * 255.0f));
            InterlockedExchange(&g_nPlayerChamsGreen, (LONG)(pl_col[1] * 255.0f));
            InterlockedExchange(&g_nPlayerChamsBlue, (LONG)(pl_col[2] * 255.0f));
        }

        bool vm_chams = (g_bViewmodelChamsEnabled != 0);
        float vm_col[4] = { g_nViewmodelChamsRed / 255.0f, g_nViewmodelChamsGreen / 255.0f, g_nViewmodelChamsBlue / 255.0f, 1.0f };
        if (CheckboxColor("viewmodel chams", &vm_chams, vm_col)) {
            InterlockedExchange(&g_bViewmodelChamsEnabled, vm_chams ? TRUE : FALSE);
            InterlockedExchange(&g_nViewmodelChamsRed, (LONG)(vm_col[0] * 255.0f));
            InterlockedExchange(&g_nViewmodelChamsGreen, (LONG)(vm_col[1] * 255.0f));
            InterlockedExchange(&g_nViewmodelChamsBlue, (LONG)(vm_col[2] * 255.0f));
        }

        bool sky_chams = (g_bSkyboxChamsEnabled != 0);
        float sky_col[4] = { g_nSkyboxChamsRed / 255.0f, g_nSkyboxChamsGreen / 255.0f, g_nSkyboxChamsBlue / 255.0f, 1.0f };
        if (CheckboxColor("skybox tint", &sky_chams, sky_col)) {
            InterlockedExchange(&g_bSkyboxChamsEnabled, sky_chams ? TRUE : FALSE);
            InterlockedExchange(&g_nSkyboxChamsRed, (LONG)(sky_col[0] * 255.0f));
            InterlockedExchange(&g_nSkyboxChamsGreen, (LONG)(sky_col[1] * 255.0f));
            InterlockedExchange(&g_nSkyboxChamsBlue, (LONG)(sky_col[2] * 255.0f));
        }

        bool flat_chams = (g_bChamsFlatShading != 0);
        if (Checkbox("flat shading (unlit)", &flat_chams)) {
            InterlockedExchange(&g_bChamsFlatShading, flat_chams ? TRUE : FALSE);
        }

        static const char* chams_types[] = { "solid flat", "textured", "wireframe" };
        Combo("chams style", &g_Menu.ChamsStyle, chams_types, 3);
    }
    EndGroup();

    ImGui::SetCursorScreenPos(ImVec2(col2_x, top_group_y + 235.0f + 10.0f));
    BeginGroup("thirdperson camera", col_w, 170.0f);
    {
        if (Checkbox("thirdperson view", &g_Menu.Thirdperson)) {
            art::IssueCommand(g_Menu.Thirdperson ? "thirdperson" : "firstperson");
        }
        Keybind("thirdperson key", &g_Menu.Binds.Thirdperson.key, &g_Menu.Binds.Thirdperson.mode);
        SliderInt("camera distance", &g_Menu.ThirdpersonDist, 30, 300);
    }
    EndGroup();
}

// -----------------------------------------------------------------------------
// TAB 3: OUTPUT
// -----------------------------------------------------------------------------
inline void RenderOutputTab(float col_w, ImVec2 content_pos) {
    float pad_x = 32.0f;
    float pad_y = 16.0f;
    float col_gap = 18.0f;
    float col1_x = content_pos.x + pad_x;
    float col2_x = col1_x + col_w + col_gap;
    float top_group_y = content_pos.y + pad_y + 10.0f;

    // LEFT COLUMN
    ImGui::SetCursorScreenPos(ImVec2(col1_x, top_group_y));
    BeginGroup("recording pipeline", col_w, 205.0f);
    {
        static const char* output_modes[] = { "tga image sequence", "ffmpeg direct pipe" };
        if (Combo("pipeline mode", &g_Menu.OutputMode, output_modes, 2)) {
            art::IssueCommand("art_output_mode %s", g_Menu.OutputMode == 1 ? "ffmpeg" : "tga");
        }

        bool tga_rle = (g_bArtTgaRle != 0);
        if (Checkbox("tga rle compression", &tga_rle)) {
            InterlockedExchange(&g_bArtTgaRle, tga_rle ? TRUE : FALSE);
        }

        Checkbox("take json manifest", &g_Menu.TakeManifest);
        Checkbox("auto validate finished takes", &g_Menu.AutoValidate);
    }
    EndGroup();

    ImGui::SetCursorScreenPos(ImVec2(col1_x, top_group_y + 205.0f + 10.0f));
    BeginGroup("queue & memory", col_w, 200.0f);
    {
        SliderInt("max queue files", &g_Menu.QueueFiles, 50, 5000);
        SliderInt("max queue memory", &g_Menu.QueueMb, 64, 2048, " mb");
        if (Button("flush writer queue")) {
            art::IssueCommand("art_flush");
        }
    }
    EndGroup();

    // RIGHT COLUMN
    ImGui::SetCursorScreenPos(ImVec2(col2_x, top_group_y));
    BeginGroup("ffmpeg video encoding", col_w, 235.0f);
    {
        static const char* presets[] = { "prores 422", "prores 4444", "x264 lossless", "x264 high quality", "nvenc h.264", "nvenc hevc", "custom preset" };
        if (Combo("encoder preset", &g_Menu.FfmpegPreset, presets, 7)) {
            art::IssueCommand("art_ffmpeg_preset %s", presets[g_Menu.FfmpegPreset]);
        }

        SliderInt("ffmpeg crf / quality", &g_Menu.FfmpegCrf, 0, 51);

        if (Button("test ffmpeg installation")) {
            art::IssueCommand("art_ffmpeg_test");
        }
    }
    EndGroup();

    ImGui::SetCursorScreenPos(ImVec2(col2_x, top_group_y + 235.0f + 10.0f));
    BeginGroup("folders & exploration", col_w, 170.0f);
    {
        if (Button("open output / takes folder")) {
            art::OpenConfiguredOutputDirectory();
        }
    }
    EndGroup();
}

// -----------------------------------------------------------------------------
// TAB 4: INFO
// -----------------------------------------------------------------------------
inline void RenderInfoTab(float col_w, ImVec2 content_pos) {
    float pad_x = 32.0f;
    float pad_y = 16.0f;
    float col_gap = 18.0f;
    float col1_x = content_pos.x + pad_x;
    float col2_x = col1_x + col_w + col_gap;
    float top_group_y = content_pos.y + pad_y + 10.0f;

    // LEFT COLUMN
    ImGui::SetCursorScreenPos(ImVec2(col1_x, top_group_y));
    BeginGroup("take session stats", col_w, 205.0f);
    {
        ImGui::Text("take: %s", g_ArtRecordingStats.takeName[0] ? g_ArtRecordingStats.takeName : "none");
        ImGui::Dummy(ImVec2(0, 4.0f));
        ImGui::Text("frames: %d", (int)g_ArtRecordingStats.takeFrames);
        ImGui::Dummy(ImVec2(0, 4.0f));
        ImGui::Text("files: %lu", g_ArtRecordingStats.takeFiles);
        ImGui::Dummy(ImVec2(0, 6.0f));
        if (Button("run integrity validation")) {
            art::IssueCommand("art_validation");
        }
    }
    EndGroup();

    ImGui::SetCursorScreenPos(ImVec2(col1_x, top_group_y + 205.0f + 10.0f));
    BeginGroup("pipeline performance", col_w, 200.0f);
    {
        ImGui::Text("render: %.2f ms", g_ArtPipelineStats.stages[0].takeSamples ? (double)g_ArtPipelineStats.stages[0].takeTotalMicroseconds / g_ArtPipelineStats.stages[0].takeSamples / 1000.0 : 0.0);
        ImGui::Dummy(ImVec2(0, 3.0f));
        ImGui::Text("read: %.2f ms", g_ArtPipelineStats.stages[1].takeSamples ? (double)g_ArtPipelineStats.stages[1].takeTotalMicroseconds / g_ArtPipelineStats.stages[1].takeSamples / 1000.0 : 0.0);
        ImGui::Dummy(ImVec2(0, 3.0f));
        ImGui::Text("encode: %.2f ms", g_ArtPipelineStats.stages[2].takeSamples ? (double)g_ArtPipelineStats.stages[2].takeTotalMicroseconds / g_ArtPipelineStats.stages[2].takeSamples / 1000.0 : 0.0);
        ImGui::Dummy(ImVec2(0, 3.0f));
        ImGui::Text("write: %.2f ms", g_ArtPipelineStats.stages[3].takeSamples ? (double)g_ArtPipelineStats.stages[3].takeTotalMicroseconds / g_ArtPipelineStats.stages[3].takeSamples / 1000.0 : 0.0);
    }
    EndGroup();

    // RIGHT COLUMN
    ImGui::SetCursorScreenPos(ImVec2(col2_x, top_group_y));
    BeginGroup("engine & platform", col_w, 205.0f);
    {
        ImGui::Text("CS:S v34 Engine Build 4044");
        ImGui::Dummy(ImVec2(0, 4.0f));
        ImGui::Text("Direct3D 9 (Win32 x86)");
        ImGui::Dummy(ImVec2(0, 4.0f));
        MEMORYSTATUSEX memStatus = {};
        memStatus.dwLength = sizeof(memStatus);
        GlobalMemoryStatusEx(&memStatus);
        ImGui::Text("free RAM: %llu MB", memStatus.ullAvailPhys / (1024 * 1024));
    }
    EndGroup();

    ImGui::SetCursorScreenPos(ImVec2(col2_x, top_group_y + 205.0f + 10.0f));
    BeginGroup("about art", col_w, 200.0f);
    {
        ImGui::Text("CS:S V34 ART V1.0");
        ImGui::Dummy(ImVec2(0, 3.0f));
        ImGui::Text("Created by Contrastniy");
        ImGui::Dummy(ImVec2(0, 6.0f));
        if (Button("YouTube: @Contrastniy")) {
            ShellExecuteA(NULL, "open", "https://www.youtube.com/@Contrastniy", NULL, NULL, SW_SHOWNORMAL);
        }
    }
    EndGroup();
}

// -----------------------------------------------------------------------------
// TAB 5: HLAE
// -----------------------------------------------------------------------------
inline void RenderHlaeTab(float col_w, ImVec2 content_pos) {
    float pad_x = 32.0f;
    float pad_y = 16.0f;
    float col_gap = 18.0f;
    float col1_x = content_pos.x + pad_x;
    float col2_x = col1_x + col_w + col_gap;

    ImGui::SetCursorScreenPos(ImVec2(col1_x, content_pos.y + pad_y));
    Checkbox("enable hlae features", &g_Menu.HlaeEnable);

    float top_group_y = content_pos.y + pad_y + 14.0f + 10.0f;

    // LEFT COLUMN
    ImGui::SetCursorScreenPos(ImVec2(col1_x, top_group_y));
    BeginGroup("camera path (campath)", col_w, 220.0f);
    {
        Checkbox("draw camera path", &g_Hlae.campathDraw);
        Keybind("add keyframe key", &g_Menu.Binds.CampathAdd.key, &g_Menu.Binds.CampathAdd.mode);

        if (Button("add keyframe (mirv_campath add)")) {
            art::IssueCommand("mirv_campath add");
        }
        if (Button("remove keyframe (mirv_campath remove)")) {
            art::IssueCommand("mirv_campath remove");
        }
        if (Button("clear all (mirv_campath clear)")) {
            art::IssueCommand("mirv_campath clear");
        }
    }
    EndGroup();

    ImGui::SetCursorScreenPos(ImVec2(col1_x, top_group_y + 220.0f + 10.0f));
    BeginGroup("campath playback", col_w, 185.0f);
    {
        if (Button("start at current time")) {
            art::IssueCommand("mirv_campath start");
        }
        if (Button(g_Gui.demoWasPlaying ? "pause demo" : "resume demo")) {
            art::IssueCommand(g_Gui.demoWasPlaying ? "demo_pause" : "demo_resume");
        }
        SliderInt("demo tick", &g_Gui.demoTick, 0, 100000);
    }
    EndGroup();

    // RIGHT COLUMN
    ImGui::SetCursorScreenPos(ImVec2(col2_x, top_group_y));
    BeginGroup("camera import & export", col_w, 220.0f);
    {
        if (Button("export .cam (camera.cam)")) {
            art::IssueCommand("mirv_camio export camera.cam");
        }
        if (Button("import .cam (camera.cam)")) {
            art::IssueCommand("mirv_camio import camera.cam");
        }
        if (Button("export .bvh (camera.bvh)")) {
            art::IssueCommand("mirv_camexport bvh camera.bvh");
        }
        if (Button("import .bvh (camera.bvh)")) {
            art::IssueCommand("mirv_camimport bvh camera.bvh");
        }
    }
    EndGroup();

    ImGui::SetCursorScreenPos(ImVec2(col2_x, top_group_y + 220.0f + 10.0f));
    BeginGroup("game record (.agr)", col_w, 185.0f);
    {
        Checkbox("auto-export with art takes", &g_Menu.AgrAutoExport);

        if (Button("start agr recording")) {
            art::IssueCommand("mirv_agr start");
        }
        if (Button("stop agr recording")) {
            art::IssueCommand("mirv_agr stop");
        }
    }
    EndGroup();
}

// -----------------------------------------------------------------------------
// TAB 6: CONFIGS
// -----------------------------------------------------------------------------
inline void RenderConfigsTab(float col_w, ImVec2 content_pos) {
    float pad_x = 32.0f;
    float pad_y = 16.0f;
    float col_gap = 18.0f;
    float col1_x = content_pos.x + pad_x;
    float col2_x = col1_x + col_w + col_gap;
    float top_group_y = content_pos.y + pad_y + 10.0f;

    // LEFT COLUMN
    ImGui::SetCursorScreenPos(ImVec2(col1_x, top_group_y));
    BeginGroup("theme & colors", col_w, 210.0f);
    {
        float accent_col[4] = { g_Theme.Accent.x, g_Theme.Accent.y, g_Theme.Accent.z, g_Theme.Accent.w };
        if (CheckboxColor("accent color", &g_Menu.AccentDummy, accent_col)) {
            g_Theme.Accent = ImVec4(accent_col[0], accent_col[1], accent_col[2], accent_col[3]);
        }

        static const char* color_presets[] = { "onetap v1 blue", "onetap v3 purple", "skeet green", "crimson red" };
        if (Combo("color presets", &g_Menu.ColorPreset, color_presets, 4)) {
            if (g_Menu.ColorPreset == 0) g_Theme.Accent = ImVec4(77.f/255.f, 123.f/255.f, 255.f/255.f, 1.f);
            else if (g_Menu.ColorPreset == 1) g_Theme.Accent = ImVec4(147.f/255.f, 112.f/255.f, 219.f/255.f, 1.f);
            else if (g_Menu.ColorPreset == 2) g_Theme.Accent = ImVec4(140.f/255.f, 200.f/255.f, 70.f/255.f, 1.f);
            else if (g_Menu.ColorPreset == 3) g_Theme.Accent = ImVec4(230.f/255.f, 60.f/255.f, 60.f/255.f, 1.f);
        }
    }
    EndGroup();

    ImGui::SetCursorScreenPos(ImVec2(col1_x, top_group_y + 210.0f + 10.0f));
    BeginGroup("actions", col_w, 195.0f);
    {
        if (Button("reset to default blue")) {
            g_Theme.Accent = ImVec4(77.f/255.f, 123.f/255.f, 255.f/255.f, 1.f);
        }
        if (Button("reset everything to defaults")) {
            art::ResetEverythingToDefaults();
        }
    }
    EndGroup();

    // RIGHT COLUMN
    ImGui::SetCursorScreenPos(ImVec2(col2_x, top_group_y));
    BeginGroup("configuration slots", col_w, 210.0f);
    {
        static const char* cfg_slots[] = { "default", "high_fps", "quality_prores", "depth_isolated", "hlae_cinematic" };
        Combo("config slot", &g_Menu.ConfigSlot, cfg_slots, 5);

        if (Button("load selected config")) {
            art::LoadConfig(cfg_slots[g_Menu.ConfigSlot]);
        }
        if (Button("save to selected config")) {
            art::SaveConfig(cfg_slots[g_Menu.ConfigSlot]);
        }
    }
    EndGroup();

    ImGui::SetCursorScreenPos(ImVec2(col2_x, top_group_y + 210.0f + 10.0f));
    BeginGroup("preset management", col_w, 195.0f);
    {
        if (Button("save as startup default")) {
            art::SaveConfig("default");
        }
        if (Button("reload active config")) {
            art::LoadConfig("default");
        }
    }
    EndGroup();
}

// -----------------------------------------------------------------------------
// TAB 7: SETTINGS
// -----------------------------------------------------------------------------
inline void RenderSettingsTab(float col_w, ImVec2 content_pos) {
    float pad_x = 32.0f;
    float pad_y = 16.0f;
    float col_gap = 18.0f;
    float col1_x = content_pos.x + pad_x;
    float col2_x = col1_x + col_w + col_gap;
    float top_group_y = content_pos.y + pad_y + 10.0f;

    // LEFT COLUMN
    ImGui::SetCursorScreenPos(ImVec2(col1_x, top_group_y));
    BeginGroup("menu & input", col_w, 210.0f);
    {
        Keybind("menu toggle key", &g_Menu.Binds.MenuToggle.key, &g_Menu.Binds.MenuToggle.mode);
        Checkbox("hlae input while gui open", &g_Gui.hlaeInputWhileGui);
        SliderPercent("menu opacity", &g_Menu.MenuOpacity, 30, 100);
    }
    EndGroup();

    ImGui::SetCursorScreenPos(ImVec2(col1_x, top_group_y + 210.0f + 10.0f));
    BeginGroup("logging", col_w, 195.0f);
    {
        bool dev_log = (g_bDebugLogging != 0);
        if (Checkbox("developer log to file", &dev_log)) {
            art::IssueCommand("art_debug %d", dev_log ? 1 : 0);
        }
        if (Button("flush log to disk")) {
            art::IssueCommand("art_flush");
        }
    }
    EndGroup();

    // RIGHT COLUMN
    ImGui::SetCursorScreenPos(ImVec2(col2_x, top_group_y));
    BeginGroup("experimental features", col_w, 210.0f);
    {
        Checkbox("experimental options enabled", &g_Gui.experimentalOptionsEnabled);

        if (Checkbox("pause demo after recording stops", &g_Gui.autoPauseDemoAfterRecording)) {
            art::IssueCommand(g_Gui.autoPauseDemoAfterRecording ? "art_demo_pause_after_recording on" : "art_demo_pause_after_recording off");
        }

        Checkbox("resume demo on recording start", &g_Gui.autoResumeDemoOnRecordingStart);
    }
    EndGroup();

    ImGui::SetCursorScreenPos(ImVec2(col2_x, top_group_y + 210.0f + 10.0f));
    BeginGroup("quick tools", col_w, 195.0f);
    {
        if (Button("open output folder")) {
            art::OpenConfiguredOutputDirectory();
        }
        if (Button("print full console help")) {
            art::IssueCommandWithSeparator("art_help");
        }
    }
    EndGroup();
}

// -----------------------------------------------------------------------------
// MAIN RENDER MENU
// -----------------------------------------------------------------------------
inline void RenderMenu(bool* p_open) {
    ImVec2 menu_size(720.0f, 590.0f);

    if (!BeginMenuWindow(V34_ART_PRODUCT_NAME " V" V34_ART_VERSION_STRING, p_open, menu_size)) {
        return;
    }

    static const char* s_Tabs[] = {
        "capture", "passes", "visuals", "output", "info", "hlae", "configs", "settings"
    };

    // Top tab bar with 8 tabs
    TabBar(s_Tabs, 8, &g_Menu.CurrentTab);

    // Inner separate content box enclosing all lower tab content
    ImVec2 content_pos, content_size;
    BeginContentBox(&content_pos, &content_size);

    float pad_x = 32.0f;
    float col_gap = 18.0f;
    float col_w = (content_size.x - pad_x * 2.0f - col_gap) * 0.5f;

    switch (g_Menu.CurrentTab) {
        case 0: RenderCaptureTab(col_w, content_pos); break;
        case 1: RenderPassesTab(col_w, content_pos); break;
        case 2: RenderVisualsTab(col_w, content_pos); break;
        case 3: RenderOutputTab(col_w, content_pos); break;
        case 4: RenderInfoTab(col_w, content_pos); break;
        case 5: RenderHlaeTab(col_w, content_pos); break;
        case 6: RenderConfigsTab(col_w, content_pos); break;
        case 7: RenderSettingsTab(col_w, content_pos); break;
        default: RenderCaptureTab(col_w, content_pos); break;
    }

    EndContentBox();

    EndMenuWindow();
}

} // namespace OT
