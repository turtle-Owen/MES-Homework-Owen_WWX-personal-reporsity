// Apple 风格主题：颜色、字号、圆角、轻毛玻璃质感。
#pragma once

#include "imgui.h"
#include <string>

namespace mes::ui {

struct Palette {
  ImU32 backgroundTop;
  ImU32 backgroundBottom;
  ImU32 glass;
  ImU32 glassStrong;
  ImU32 glassBorder;
  ImU32 glassShadow;
  ImU32 text;
  ImU32 textSecondary;
  ImU32 textTertiary;
  ImU32 accent;
  ImU32 accentHover;
  ImU32 accentSoft;
  ImU32 success;
  ImU32 warning;
  ImU32 danger;
  ImU32 separator;
  ImU32 windowClose;
  ImU32 windowMinimize;
  // ---- 扩展 ----
  ImU32 textInverse;    // 深色按钮上的文字
  ImU32 successSoft;    // 成功色调的半透明底
  ImU32 warningSoft;
  ImU32 dangerSoft;
  ImU32 accentDeep;     // 主色渐变末端
  ImU32 accentLight;    // 主色渐变起点
  ImU32 glassTint;      // 卡片内额外的冷色调
  ImU32 scrollbar;      // 滚动条
  ImU32 shadowTint;     // 阴影基色（含 alpha）
};

struct Theme {
  Palette colors;
  float scale = 1.0f;
  ImFont* fontSmall = nullptr;
  ImFont* fontBody = nullptr;
  ImFont* fontMedium = nullptr;
  ImFont* fontTitle = nullptr;
  ImFont* fontHero = nullptr;
  ImFont* fontIcon = nullptr;
  std::string cjkFontPath;
  std::string boldFontPath;

  // 已按 DPI 缩放的布局尺寸（逻辑像素）。
  float topBarHeight = 52.0f;
  float footerHeight = 34.0f;
  float margin = 40.0f;
  float radiusCard = 20.0f;
  float radiusControl = 12.0f;
  float gap = 16.0f;
  float rowHeight = 40.0f;
};

Theme& theme();
void applyStyle();
// 由 main 在 ImGui 上下文创建后调用；加载系统中文字体与 Apple 风格字号层级。
void loadFonts(float dpiScale);
// loadFonts 之后每帧调用：按 ImGui framebuffer 比例修正字体显示大小。
void syncFontScale();

// ---------------------------------------------------------------- 小工具
float uiScale();
// 逻辑字号（AddText 显式字号时使用，已抵消 FontGlobalScale）。
float textSize(const ImFont* font);
ImU32 withAlpha(ImU32 color, float alpha);
ImU32 mixColor(ImU32 a, ImU32 b, float t);
ImU32 lighten(ImU32 color, float amount);
ImU32 darken(ImU32 color, float amount);
ImVec2 measureText(const ImFont* font, const char* text);
float lineHeight(const ImFont* font = nullptr);
const ImWchar* cjkGlyphRanges();
// 文本绘制小工具（显式字体/颜色，绕过默认皮肤）。
void drawLabel(ImDrawList* drawList, ImFont* font, const ImVec2& pos,
               ImU32 color, const char* text);
void drawLabelCenter(ImDrawList* drawList, ImFont* font, const ImVec2& min,
                     const ImVec2& max, ImU32 color, const char* text);
void drawLabelWrapped(ImDrawList* drawList, ImFont* font, const ImVec2& pos,
                      ImU32 color, const char* text, float wrapWidth);
float measureWrappedHeight(ImFont* font, const char* text, float wrapWidth);

// 圆角、阴影、分隔线等可复用绘制。
namespace paint {
void shadow(ImDrawList* drawList, const ImVec2& min, const ImVec2& max,
            float rounding, float alpha = 1.0f, float spread = 18.0f);
void glassPanel(ImDrawList* drawList, const ImVec2& min, const ImVec2& max,
                float rounding, bool strong = false);
void border(ImDrawList* drawList, const ImVec2& min, const ImVec2& max,
            float rounding, ImU32 color);
void verticalGradient(ImDrawList* drawList, const ImVec2& min,
                      const ImVec2& max, ImU32 top, ImU32 bottom);
void spotlight(ImDrawList* drawList, const ImVec2& center, float radius,
               ImU32 color);
// 圆角矩形外发光（选中态/焦点态）。
void glow(ImDrawList* drawList, const ImVec2& min, const ImVec2& max,
          float rounding, ImU32 color, float spread = 10.0f);
// 1px 内高光（卡片顶部丝光）。
void highlight(ImDrawList* drawList, const ImVec2& min, const ImVec2& max,
               float rounding, float alpha = 0.85f);
// 顶部/底部分隔线。
void hairline(ImDrawList* drawList, float x0, float x1, float y, float alpha);
// 勾 / 叉 / 圆环。
void checkmark(ImDrawList* drawList, const ImVec2& center, float size,
               ImU32 color, float thickness);
void cross(ImDrawList* drawList, const ImVec2& center, float size, ImU32 color,
           float thickness);
}  // namespace paint

}  // namespace mes::ui
