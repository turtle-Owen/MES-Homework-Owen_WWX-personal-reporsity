// 轻量组件库：所有控件用 ImDrawList 自绘，保持 Apple 风格一致。
#include "ui/widgets.h"
#include "ui/theme.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace mes::ui {
namespace {

constexpr float kPi = 3.14159265358979323846f;

float gLayoutWidth = 0.0f;
bool gBlocked = false;
std::unordered_map<ImGuiID, float> gAnims;

struct FieldVisual {
  bool hover = false;
  bool focus = false;
};
std::unordered_map<ImGuiID, FieldVisual> gFields;

inline float sc() { return theme().scale > 0.01f ? theme().scale : 1.0f; }
inline float sp(float value) { return value * sc(); }

inline ImFont* fontOf(ImFont* font, ImFont* fallback) {
  if (font) return font;
  if (fallback) return fallback;
  return ImGui::GetFont();
}

inline ImFont* fSmall() { return fontOf(theme().fontSmall, theme().fontBody); }
inline ImFont* fBody() { return fontOf(theme().fontBody, nullptr); }
inline ImFont* fMedium() { return fontOf(theme().fontMedium, theme().fontBody); }
inline ImFont* fTitle() { return fontOf(theme().fontTitle, theme().fontMedium); }
inline ImFont* fHero() { return fontOf(theme().fontHero, theme().fontTitle); }

float animate(ImGuiID id, float target, float speed = 16.0f) {
  auto it = gAnims.find(id);
  if (it == gAnims.end()) {
    gAnims[id] = target;
    return target;
  }
  const float dt = std::min(ImGui::GetIO().DeltaTime, 0.1f);
  it->second += (target - it->second) * std::min(1.0f, dt * speed);
  if (std::fabs(target - it->second) < 0.002f) it->second = target;
  return it->second;
}

ImVec2 measure(ImFont* font, const std::string& text) {
  return measureText(font, text.c_str());
}

void drawText(ImDrawList* dl, ImFont* font, const ImVec2& pos, ImU32 color,
              const std::string& text) {
  if (!dl || text.empty()) return;
  dl->AddText(font, textSize(font), pos, color, text.c_str());
}

void drawTextCenter(ImDrawList* dl, ImFont* font, const Rect& rect, ImU32 color,
                    const std::string& text) {
  if (!dl || text.empty()) return;
  const ImVec2 size = measure(font, text);
  dl->AddText(font, textSize(font),
              ImVec2(rect.min.x + (rect.size().x - size.x) * 0.5f,
                     rect.min.y + (rect.size().y - size.y) * 0.5f),
              color, text.c_str());
}

float measureWrappedHeight(ImFont* font, const std::string& text,
                           float wrapLogical) {
  if (!font || text.empty()) return 0.0f;
  const float gs = ImGui::GetIO().FontGlobalScale;
  const float wrapRaster = gs > 0.0001f ? wrapLogical / gs : wrapLogical;
  const ImVec2 size =
      font->CalcTextSizeA(font->FontSize, FLT_MAX, wrapRaster, text.c_str());
  return size.y * (gs > 0.0001f ? gs : 1.0f);
}

void drawTextWrapped(ImDrawList* dl, ImFont* font, const ImVec2& pos, ImU32 color,
                     const std::string& text, float wrapLogical) {
  if (!dl || text.empty()) return;
  dl->AddText(font, textSize(font), pos, color, text.c_str(), nullptr,
              wrapLogical);
}

void advanceY(float dy) {
  const ImVec2 p = ImGui::GetCursorScreenPos();
  ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + dy));
}

void strokeEllipse(ImDrawList* dl, const ImVec2& center, float rx, float ry,
                   ImU32 color, float thickness) {
  const int segments = 24;
  for (int i = 0; i <= segments; ++i) {
    const float t = (static_cast<float>(i) / segments) * 2.0f * kPi;
    const ImVec2 p(center.x + std::cos(t) * rx, center.y + std::sin(t) * ry);
    if (i == 0)
      dl->PathLineTo(p);
    else
      dl->PathLineTo(p);
  }
  dl->PathStroke(color, 0, thickness);
}

// ---------------------------------------------------------------- 按钮
enum class Tone { Primary, Secondary, Quiet, Danger, DangerSolid };

bool buttonImpl(const char* id, const char* label, Icon icon, const ImVec2& size,
                Tone tone, bool disabled) {
  Theme& t = theme();
  const Palette& c = t.colors;
  ImDrawList* dl = ImGui::GetWindowDrawList();
  ImFont* font = fMedium();
  const std::string text = label ? label : "";
  const ImVec2 labelSize = measure(font, text);
  const float iconSize = labelSize.y > 1.0f ? labelSize.y * 1.05f : sp(16);
  const float iconGap = icon != Icon::None ? sp(8.0f) : 0.0f;
  float w = size.x > 0.5f
                ? size.x
                : labelSize.x + (icon != Icon::None ? iconSize : 0.0f) + iconGap +
                      sp(40.0f);
  float h = size.y > 0.5f ? size.y : sp(44.0f);
  w = std::max(w, sp(40.0f));
  h = std::max(h, sp(24.0f));

  const ImVec2 p = ImGui::GetCursorScreenPos();
  const Rect rect{p, ImVec2(p.x + w, p.y + h)};
  const bool blocked = gBlocked;
  bool hovered = false;
  bool active = false;
  bool clicked = false;
  bool focused = false;
  float anim = 0.0f;

  ImGui::PushID(id ? id : label);
  const ImGuiID aid = ImGui::GetID("##btn");
  if (!disabled && !blocked) {
    ImGui::SetCursorScreenPos(p);
    ImGui::InvisibleButton("##btn", ImVec2(w, h));
    hovered = ImGui::IsItemHovered();
    active = ImGui::IsItemActive();
    clicked = ImGui::IsItemClicked();
    focused = ImGui::IsItemFocused();
    anim = animate(aid, hovered ? 1.0f : 0.0f, 18.0f);
  }
  ImGui::PopID();

  const float press = active ? 1.0f : 0.0f;
  const float rounding = sp(tone == Tone::Quiet ? 10.0f : 12.0f);
  const bool interactive = !disabled;

  if (tone == Tone::Primary) {
    ImU32 top = mixColor(c.accentLight, IM_COL32(255, 255, 255, 255),
                         0.10f + 0.10f * anim);
    ImU32 bottom = mixColor(c.accentDeep, c.accent, 0.25f);
    if (press > 0.5f) {
      top = darken(top, 0.05f);
      bottom = darken(bottom, 0.05f);
    }
    if (disabled) {
      top = IM_COL32(148, 158, 176, 120);
      bottom = IM_COL32(128, 138, 158, 120);
    } else {
      // 主色柔光
      const int layers = 4;
      for (int i = layers; i >= 1; --i) {
        const float off = sp(2.0f) * static_cast<float>(i);
        dl->AddRectFilled(ImVec2(rect.min.x - off * 0.6f, rect.min.y + sp(1.5f)),
                          ImVec2(rect.max.x + off * 0.6f, rect.max.y + off * 0.8f),
                          withAlpha(c.accent, 0.05f), rounding + off * 0.5f);
      }
    }
    dl->AddRectFilled(rect.min, rect.max, bottom, rounding);
    if (!disabled) {
      dl->AddRectFilled(ImVec2(rect.min.x + 1.0f, rect.min.y + 1.0f),
                        ImVec2(rect.max.x - 1.0f, rect.min.y + h * 0.55f),
                        withAlpha(top, 0.55f), rounding,
                        ImDrawFlags_RoundCornersTop);
    }
    paint::highlight(dl, rect.min, rect.max, rounding, 0.45f);
    const ImU32 textColor =
        disabled ? withAlpha(c.textInverse, 0.72f) : c.textInverse;
    if (icon != Icon::None) {
      const float cx = rect.center().x - (labelSize.x + iconSize + iconGap) * 0.5f +
                       iconSize * 0.5f;
      paintIcon(dl, ImVec2(cx, rect.center().y), iconSize, icon, textColor, 1.5f);
      drawText(dl, font,
               ImVec2(cx + iconSize * 0.5f + iconGap,
                      rect.center().y - labelSize.y * 0.5f),
               textColor, text);
    } else {
      drawTextCenter(dl, font, rect, textColor, text);
    }
    if (focused) {
      dl->AddRect(rect.min, rect.max, withAlpha(IM_COL32(255, 255, 255, 255), 0.9f),
                  rounding, 0, sp(1.5f));
    }
  } else if (tone == Tone::Secondary || tone == Tone::Quiet ||
             tone == Tone::Danger) {
    const bool dangerTone = tone == Tone::Danger;
    ImU32 bg = IM_COL32(255, 255, 255, 0);
    ImU32 borderColor = 0;
    ImU32 textColor = c.text;
    if (tone == Tone::Secondary) {
      bg = withAlpha(IM_COL32(255, 255, 255, 255), 0.62f + 0.22f * anim);
      borderColor = withAlpha(
          dangerTone ? c.danger : IM_COL32(120, 132, 152, 255),
          (dangerTone ? 0.35f : 0.26f) + 0.24f * anim);
      textColor = dangerTone ? c.danger : c.text;
    } else if (tone == Tone::Quiet) {
      bg = withAlpha(IM_COL32(255, 255, 255, 255), 0.05f + 0.42f * anim);
      borderColor = withAlpha(IM_COL32(255, 255, 255, 255), 0.0f + 0.35f * anim);
      textColor = anim > 0.5f ? c.text : c.textSecondary;
    } else {
      bg = withAlpha(c.dangerSoft, 0.55f + 0.45f * anim);
      borderColor = withAlpha(c.danger, 0.30f + 0.30f * anim);
      textColor = c.danger;
    }
    if (disabled) {
      bg = withAlpha(IM_COL32(255, 255, 255, 255), 0.20f);
      borderColor = withAlpha(IM_COL32(120, 132, 152, 255), 0.16f);
      textColor = withAlpha(textColor, 0.45f);
    }
    if (press > 0.5f && !disabled) bg = darken(bg, 0.04f);
    if (bg != 0) dl->AddRectFilled(rect.min, rect.max, bg, rounding);
    if (borderColor != 0) dl->AddRect(rect.min, rect.max, borderColor, rounding, 0,
                                      1.0f);
    if (tone == Tone::Secondary || tone == Tone::Danger) {
      paint::highlight(dl, rect.min, rect.max, rounding, disabled ? 0.2f : 0.55f);
    }
    if (icon != Icon::None) {
      const ImU32 iconColor = textColor;
      const float cx = rect.center().x - (labelSize.x + iconSize + iconGap) * 0.5f +
                       iconSize * 0.5f;
      paintIcon(dl, ImVec2(cx, rect.center().y), iconSize, icon, iconColor, 1.5f);
      drawText(dl, font,
               ImVec2(cx + iconSize * 0.5f + iconGap,
                      rect.center().y - labelSize.y * 0.5f),
               textColor, text);
    } else {
      drawTextCenter(dl, font, rect, textColor, text);
    }
    if (focused) {
      dl->AddRect(rect.min, rect.max, withAlpha(c.accent, 0.65f), rounding, 0,
                  sp(1.5f));
    }
  } else {  // DangerSolid
    ImU32 top = mixColor(lighten(c.danger, 0.15f), IM_COL32(255, 255, 255, 255),
                         0.10f * anim);
    ImU32 bottom = darken(c.danger, 0.10f);
    if (disabled) {
      top = bottom = IM_COL32(150, 150, 156, 120);
    }
    dl->AddRectFilled(rect.min, rect.max, bottom, rounding);
    if (!disabled) {
      dl->AddRectFilled(ImVec2(rect.min.x + 1.0f, rect.min.y + 1.0f),
                        ImVec2(rect.max.x - 1.0f, rect.min.y + h * 0.55f),
                        withAlpha(top, 0.55f), rounding,
                        ImDrawFlags_RoundCornersTop);
    }
    const ImU32 textColor =
        disabled ? withAlpha(c.textInverse, 0.72f) : c.textInverse;
    drawTextCenter(dl, font, rect, textColor, text);
  }

  ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + h));
  (void)interactive;
  return clicked;
}

// ---------------------------------------------------------------- 卡片
std::vector<std::string> splitLines(const std::string& text) {
  std::vector<std::string> out;
  std::string current;
  for (char ch : text) {
    if (ch == '\n') {
      out.push_back(current);
      current.clear();
    } else {
      current.push_back(ch);
    }
  }
  if (!current.empty()) out.push_back(current);
  return out;
}

// ---------------------------------------------------------------- PowerShell
#if defined(_WIN32)
std::string base64Encode(const std::string& data) {
  static const char* table =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  out.reserve(((data.size() + 2) / 3) * 4);
  size_t i = 0;
  while (i + 3 <= data.size()) {
    const unsigned int v = (static_cast<unsigned char>(data[i]) << 16) |
                           (static_cast<unsigned char>(data[i + 1]) << 8) |
                           static_cast<unsigned char>(data[i + 2]);
    out.push_back(table[(v >> 18) & 0x3F]);
    out.push_back(table[(v >> 12) & 0x3F]);
    out.push_back(table[(v >> 6) & 0x3F]);
    out.push_back(table[v & 0x3F]);
    i += 3;
  }
  if (i < data.size()) {
    unsigned int v = static_cast<unsigned char>(data[i]) << 16;
    if (i + 1 < data.size()) v |= static_cast<unsigned char>(data[i + 1]) << 8;
    out.push_back(table[(v >> 18) & 0x3F]);
    out.push_back(table[(v >> 12) & 0x3F]);
    out.push_back(i + 1 < data.size() ? table[(v >> 6) & 0x3F] : '=');
    out.push_back('=');
  }
  return out;
}

std::string utf8ToUtf16Le(const std::string& utf8) {
  if (utf8.empty()) return {};
  const int length = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(),
                                         static_cast<int>(utf8.size()), nullptr, 0);
  if (length <= 0) return {};
  std::wstring wide(static_cast<size_t>(length), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()),
                      wide.data(), length);
  std::string out(static_cast<size_t>(length) * 2, '\0');
  std::memcpy(out.data(), wide.data(), static_cast<size_t>(length) * 2);
  return out;
}
#endif

std::string runCapture(const std::string& command) {
  std::string out;
#if defined(_WIN32)
  FILE* pipe = _popen(command.c_str(), "rb");
#else
  FILE* pipe = popen(command.c_str(), "r");
#endif
  if (!pipe) return out;
  char buffer[4096];
  size_t read = 0;
  while ((read = std::fread(buffer, 1, sizeof(buffer), pipe)) > 0) {
    out.append(buffer, read);
    if (out.size() > 65536) break;
  }
#if defined(_WIN32)
  _pclose(pipe);
#else
  pclose(pipe);
#endif
  return out;
}

std::string trimDialogResult(std::string text) {
  // UTF-8 BOM
  if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
      static_cast<unsigned char>(text[1]) == 0xBB &&
      static_cast<unsigned char>(text[2]) == 0xBF) {
    text.erase(0, 3);
  }
  // 只取第一行（PowerShell 在异常时可能输出多行）
  const size_t lineEnd = text.find_first_of("\r\n");
  if (lineEnd != std::string::npos) text = text.substr(0, lineEnd);
  while (!text.empty() && (text.front() == ' ' || text.front() == '\t'))
    text.erase(text.begin());
  while (!text.empty() && (text.back() == ' ' || text.back() == '\t'))
    text.pop_back();
  return text;
}

std::string replaceAll(std::string text, const std::string& from,
                       const std::string& to) {
  if (from.empty()) return text;
  size_t pos = 0;
  while ((pos = text.find(from, pos)) != std::string::npos) {
    text.replace(pos, from.size(), to);
    pos += to.size();
  }
  return text;
}

std::string quoteSingle(const std::string& text) {
  return "\"" + replaceAll(text, "\"", "\\\"") + "\"";
}

std::string powerShellCommand(const std::string& script) {
#if defined(_WIN32)
  const std::string encoded = base64Encode(utf8ToUtf16Le(script));
  return "powershell.exe -NoProfile -NonInteractive -STA -ExecutionPolicy Bypass "
         "-EncodedCommand " +
         encoded;
#else
  (void)script;
  return {};
#endif
}

}  // namespace

float layoutWidth() {
  if (gLayoutWidth > 1.0f) return gLayoutWidth;
  const ImVec2 display = ImGui::GetIO().DisplaySize;
  return std::max(sp(320.0f), display.x - 2.0f * theme().margin);
}

void setLayoutWidth(float width) { gLayoutWidth = width; }
void setInteractionBlocked(bool blocked) { gBlocked = blocked; }
bool interactionBlocked() { return gBlocked; }

void modalScrim(float alpha) {
  ImDrawList* dl = ImGui::GetWindowDrawList();
  if (!dl) return;
  const ImVec2 display = ImGui::GetIO().DisplaySize;
  dl->AddRectFilled(ImVec2(0, 0), display,
                    IM_COL32(18, 26, 44, static_cast<int>(alpha * 255.0f)));
}

std::string elideText(const std::string& text, float maxWidth, ImFont* font) {
  ImFont* f = fontOf(font, fBody());
  if (measureText(f, text.c_str()).x <= maxWidth) return text;
  std::string current;
  const std::string ellipsis = "…";
  size_t i = 0;
  while (i < text.size()) {
    unsigned char byte = static_cast<unsigned char>(text[i]);
    size_t length = 1;
    if (byte >= 0xF0)
      length = 4;
    else if (byte >= 0xE0)
      length = 3;
    else if (byte >= 0xC0)
      length = 2;
    length = std::min(length, text.size() - i);
    std::string candidate = current + text.substr(i, length) + ellipsis;
    if (measureText(f, candidate.c_str()).x > maxWidth) break;
    current += text.substr(i, length);
    i += length;
  }
  return current.empty() ? ellipsis : current + ellipsis;
}

// ---------------------------------------------------------------- 图标
void paintIcon(ImDrawList* dl, const ImVec2& center, float size, Icon icon,
               ImU32 color, float thickness) {
  if (!dl || icon == Icon::None) return;
  const float r = size * 0.5f;
  const float t = std::max(sp(1.0f), thickness * size / 20.0f);
  auto P = [&](float x, float y) {
    return ImVec2(center.x + x * r, center.y + y * r);
  };
  switch (icon) {
    case Icon::House: {
      dl->PathLineTo(P(-0.88f, 0.06f));
      dl->PathLineTo(P(0.0f, -0.82f));
      dl->PathLineTo(P(0.88f, 0.06f));
      dl->PathStroke(color, 0, t);
      dl->PathLineTo(P(-0.62f, 0.16f));
      dl->PathLineTo(P(-0.62f, 0.80f));
      dl->PathLineTo(P(0.62f, 0.80f));
      dl->PathLineTo(P(0.62f, 0.16f));
      dl->PathStroke(color, 0, t);
      dl->AddRect(P(-0.20f, 0.34f), P(0.20f, 0.80f), color, 0.0f, 0, t * 0.85f);
      break;
    }
    case Icon::Server: {
      dl->AddRect(P(-0.78f, -0.78f), P(0.78f, -0.16f), color, r * 0.16f, 0, t);
      dl->AddRect(P(-0.78f, 0.16f), P(0.78f, 0.78f), color, r * 0.16f, 0, t);
      dl->AddCircleFilled(P(-0.48f, -0.47f), t * 1.05f, color, 8);
      dl->AddCircleFilled(P(-0.48f, 0.47f), t * 1.05f, color, 8);
      dl->AddLine(P(-0.16f, -0.47f), P(0.52f, -0.47f), color, t * 0.8f);
      dl->AddLine(P(-0.16f, 0.47f), P(0.52f, 0.47f), color, t * 0.8f);
      break;
    }
    case Icon::Check:
      paint::checkmark(dl, center, size * 0.95f, color, t);
      break;
    case Icon::Cross:
      paint::cross(dl, center, size, color, t);
      break;
    case Icon::ArrowRight: {
      dl->AddLine(P(-0.70f, 0.0f), P(0.58f, 0.0f), color, t);
      dl->PathLineTo(P(0.16f, -0.44f));
      dl->PathLineTo(P(0.64f, 0.0f));
      dl->PathLineTo(P(0.16f, 0.44f));
      dl->PathStroke(color, 0, t);
      break;
    }
    case Icon::ChevronLeft: {
      dl->PathLineTo(P(0.22f, -0.72f));
      dl->PathLineTo(P(-0.34f, 0.0f));
      dl->PathLineTo(P(0.22f, 0.72f));
      dl->PathStroke(color, 0, t);
      break;
    }
    case Icon::Folder: {
      dl->PathLineTo(P(-0.82f, -0.44f));
      dl->PathLineTo(P(-0.82f, 0.62f));
      dl->PathLineTo(P(0.82f, 0.62f));
      dl->PathLineTo(P(0.82f, -0.26f));
      dl->PathLineTo(P(-0.12f, -0.26f));
      dl->PathLineTo(P(-0.34f, -0.52f));
      dl->PathLineTo(P(-0.82f, -0.52f));
      dl->PathLineTo(P(-0.82f, -0.44f));
      dl->PathStroke(color, 0, t);
      break;
    }
    case Icon::Globe: {
      dl->AddCircle(center, r * 0.78f, color, 32, t);
      dl->AddLine(P(0.0f, -0.78f), P(0.0f, 0.78f), color, t * 0.8f);
      dl->AddLine(P(-0.78f, 0.0f), P(0.78f, 0.0f), color, t * 0.8f);
      strokeEllipse(dl, center, r * 0.36f, r * 0.78f, color, t * 0.8f);
      break;
    }
    case Icon::Info: {
      dl->AddCircle(center, r * 0.78f, color, 32, t);
      dl->AddCircleFilled(P(0.0f, -0.36f), t * 1.15f, color, 8);
      dl->AddLine(P(0.0f, -0.10f), P(0.0f, 0.46f), color, t);
      break;
    }
    case Icon::Warning: {
      dl->PathLineTo(P(-0.86f, 0.62f));
      dl->PathLineTo(P(0.0f, -0.74f));
      dl->PathLineTo(P(0.86f, 0.62f));
      dl->PathLineTo(P(-0.86f, 0.62f));
      dl->PathStroke(color, 0, t);
      dl->AddLine(P(0.0f, -0.20f), P(0.0f, 0.22f), color, t);
      dl->AddCircleFilled(P(0.0f, 0.44f), t * 1.05f, color, 8);
      break;
    }
    case Icon::Download: {
      dl->AddLine(P(0.0f, -0.76f), P(0.0f, 0.20f), color, t);
      dl->PathLineTo(P(-0.42f, -0.22f));
      dl->PathLineTo(P(0.0f, 0.18f));
      dl->PathLineTo(P(0.42f, -0.22f));
      dl->PathStroke(color, 0, t);
      dl->PathLineTo(P(-0.70f, 0.36f));
      dl->PathLineTo(P(-0.70f, 0.70f));
      dl->PathLineTo(P(0.70f, 0.70f));
      dl->PathLineTo(P(0.70f, 0.36f));
      dl->PathStroke(color, 0, t);
      break;
    }
    case Icon::Refresh: {
      dl->PathArcTo(center, r * 0.70f, -kPi * 0.85f, kPi * 0.72f, 28);
      dl->PathStroke(color, 0, t);
      const ImVec2 tip(center.x + std::cos(kPi * 0.72f) * r * 0.70f,
                       center.y + std::sin(kPi * 0.72f) * r * 0.70f);
      dl->AddLine(tip, ImVec2(tip.x - r * 0.30f, tip.y - r * 0.06f), color, t);
      dl->AddLine(tip, ImVec2(tip.x - r * 0.02f, tip.y + r * 0.30f), color, t);
      break;
    }
    case Icon::Play: {
      dl->AddTriangleFilled(P(-0.52f, -0.68f), P(-0.52f, 0.68f), P(0.72f, 0.0f),
                            color);
      break;
    }
    case Icon::Stop: {
      dl->AddRectFilled(P(-0.52f, -0.52f), P(0.52f, 0.52f), color, r * 0.18f);
      break;
    }
    case Icon::Trash: {
      dl->AddLine(P(-0.72f, -0.52f), P(0.72f, -0.52f), color, t);
      dl->AddLine(P(-0.26f, -0.76f), P(0.26f, -0.76f), color, t);
      dl->PathLineTo(P(-0.52f, -0.34f));
      dl->PathLineTo(P(-0.38f, 0.72f));
      dl->PathLineTo(P(0.38f, 0.72f));
      dl->PathLineTo(P(0.52f, -0.34f));
      dl->PathStroke(color, 0, t);
      dl->AddLine(P(-0.16f, -0.16f), P(-0.10f, 0.52f), color, t * 0.8f);
      dl->AddLine(P(0.16f, -0.16f), P(0.10f, 0.52f), color, t * 0.8f);
      break;
    }
    case Icon::Copy: {
      dl->AddRect(P(-0.74f, -0.74f), P(0.20f, 0.18f), color, r * 0.14f, 0, t);
      dl->AddRect(P(-0.16f, -0.16f), P(0.74f, 0.74f), color, r * 0.14f, 0, t);
      break;
    }
    case Icon::External: {
      dl->PathLineTo(P(-0.30f, -0.70f));
      dl->PathLineTo(P(-0.74f, -0.70f));
      dl->PathLineTo(P(-0.74f, 0.74f));
      dl->PathLineTo(P(0.70f, 0.74f));
      dl->PathLineTo(P(0.70f, 0.30f));
      dl->PathStroke(color, 0, t);
      dl->AddLine(P(-0.10f, 0.10f), P(0.72f, -0.72f), color, t);
      dl->AddLine(P(0.24f, -0.72f), P(0.72f, -0.72f), color, t);
      dl->AddLine(P(0.72f, -0.72f), P(0.72f, -0.22f), color, t);
      break;
    }
    case Icon::Sparkle: {
      dl->AddQuadFilled(P(0.0f, -0.88f), P(0.17f, 0.0f), P(0.0f, 0.88f),
                        P(-0.17f, 0.0f), color);
      dl->AddQuadFilled(P(-0.88f, 0.0f), P(0.0f, -0.17f), P(0.88f, 0.0f),
                        P(0.0f, 0.17f), color);
      break;
    }
    case Icon::Shield: {
      dl->PathLineTo(P(-0.70f, -0.58f));
      dl->PathLineTo(P(0.0f, -0.78f));
      dl->PathLineTo(P(0.70f, -0.58f));
      dl->PathLineTo(P(0.58f, 0.18f));
      dl->PathLineTo(P(0.0f, 0.76f));
      dl->PathLineTo(P(-0.58f, 0.18f));
      dl->PathLineTo(P(-0.70f, -0.58f));
      dl->PathStroke(color, 0, t);
      paint::checkmark(dl, ImVec2(center.x, center.y + r * 0.02f), size * 0.42f,
                       color, t * 0.9f);
      break;
    }
    case Icon::Clock: {
      dl->AddCircle(center, r * 0.78f, color, 32, t);
      dl->AddLine(center, ImVec2(center.x, center.y - r * 0.52f), color, t);
      dl->AddLine(center, ImVec2(center.x + r * 0.40f, center.y + r * 0.10f), color,
                  t);
      break;
    }
    case Icon::Lock: {
      dl->AddRect(P(-0.56f, -0.06f), P(0.56f, 0.70f), color, r * 0.16f, 0, t);
      dl->PathArcTo(ImVec2(center.x, center.y - r * 0.10f), r * 0.34f, kPi, 2.0f * kPi,
                    18);
      dl->PathStroke(color, 0, t);
      dl->AddCircleFilled(ImVec2(center.x, center.y + r * 0.28f), t * 1.1f, color, 8);
      break;
    }
    case Icon::Terminal: {
      dl->AddRect(P(-0.82f, -0.62f), P(0.82f, 0.62f), color, r * 0.16f, 0, t);
      dl->PathLineTo(P(-0.48f, -0.20f));
      dl->PathLineTo(P(-0.18f, 0.06f));
      dl->PathLineTo(P(-0.48f, 0.32f));
      dl->PathStroke(color, 0, t * 0.9f);
      dl->AddLine(P(0.04f, 0.34f), P(0.46f, 0.34f), color, t * 0.9f);
      break;
    }
    case Icon::Cpu: {
      dl->AddRect(P(-0.48f, -0.48f), P(0.48f, 0.48f), color, r * 0.12f, 0, t);
      dl->AddRect(P(-0.18f, -0.18f), P(0.18f, 0.18f), color, r * 0.06f, 0, t * 0.85f);
      for (int i = -1; i <= 1; ++i) {
        const float offset = static_cast<float>(i) * r * 0.30f;
        dl->AddLine(ImVec2(center.x + offset, center.y - r * 0.48f),
                    ImVec2(center.x + offset, center.y - r * 0.74f), color, t * 0.8f);
        dl->AddLine(ImVec2(center.x + offset, center.y + r * 0.48f),
                    ImVec2(center.x + offset, center.y + r * 0.74f), color, t * 0.8f);
        dl->AddLine(ImVec2(center.x - r * 0.48f, center.y + offset),
                    ImVec2(center.x - r * 0.74f, center.y + offset), color, t * 0.8f);
        dl->AddLine(ImVec2(center.x + r * 0.48f, center.y + offset),
                    ImVec2(center.x + r * 0.74f, center.y + offset), color, t * 0.8f);
      }
      break;
    }
    case Icon::Minus: {
      dl->AddLine(P(-0.58f, 0.0f), P(0.58f, 0.0f), color, t);
      break;
    }
    case Icon::None:
      break;
  }
}

void iconBadge(ImDrawList* dl, const ImVec2& center, float radius, Icon icon,
               ImU32 from, ImU32 to, float iconScale) {
  if (!dl) return;
  // 柔光投影
  for (int i = 3; i >= 1; --i) {
    const float off = sp(2.0f) * static_cast<float>(i);
    dl->AddCircleFilled(ImVec2(center.x, center.y + off + sp(2.0f)),
                        radius + sp(1.0f) * static_cast<float>(i),
                        withAlpha(theme().colors.shadowTint, 0.05f), 48);
  }
  dl->AddCircleFilled(center, radius, to, 56);
  dl->AddCircleFilled(ImVec2(center.x, center.y - radius * 0.16f), radius * 0.94f,
                      from, 56);
  dl->AddCircleFilled(ImVec2(center.x, center.y - radius * 0.44f), radius * 0.62f,
                      withAlpha(lighten(from, 0.45f), 0.35f), 48);
  dl->AddCircle(center, radius - sp(0.5f),
                withAlpha(IM_COL32(255, 255, 255, 255), 0.30f), 56, sp(1.0f));
  paintIcon(dl, center, radius * 2.0f * iconScale, icon,
            IM_COL32(255, 255, 255, 248), 1.8f);
}

// ---------------------------------------------------------------- 卡片
Rect card(const char* id, const ImVec2& size, float rounding, bool strong) {
  (void)id;
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const ImVec2 p = ImGui::GetCursorScreenPos();
  const ImVec2 max(p.x + size.x, p.y + size.y);
  paint::glassPanel(dl, p, max, rounding, strong);
  const float padX = sp(24.0f);
  const float padY = sp(20.0f);
  ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + size.y));
  return Rect{ImVec2(p.x + padX, p.y + padY),
              ImVec2(max.x - padX, max.y - padY)};
}

void cardHeader(const char* eyebrow, const char* title, const char* desc) {
  Theme& t = theme();
  const Palette& c = t.colors;
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const float startX = ImGui::GetCursorScreenPos().x;
  float y = ImGui::GetCursorScreenPos().y;
  if (eyebrow && eyebrow[0]) {
    drawText(dl, fSmall(), ImVec2(startX, y), c.textTertiary, eyebrow);
    y += textSize(fSmall()) + sp(6.0f);
  }
  if (title && title[0]) {
    drawText(dl, fTitle(), ImVec2(startX, y), c.text, title);
    y += textSize(fTitle()) + sp(6.0f);
  }
  if (desc && desc[0]) {
    const float wrap = layoutWidth();
    drawTextWrapped(dl, fBody(), ImVec2(startX, y), c.textSecondary, desc, wrap);
    y += measureWrappedHeight(fBody(), desc, wrap) + sp(8.0f);
  }
  ImGui::SetCursorScreenPos(ImVec2(startX, y));
}

bool primaryButton(const char* label, const ImVec2& size, bool disabled) {
  return buttonImpl(label, label, Icon::None, size, Tone::Primary, disabled);
}
bool secondaryButton(const char* label, const ImVec2& size, bool disabled) {
  return buttonImpl(label, label, Icon::None, size, Tone::Secondary, disabled);
}
bool quietButton(const char* label, const ImVec2& size, bool disabled) {
  return buttonImpl(label, label, Icon::None, size, Tone::Quiet, disabled);
}
bool dangerButton(const char* label, const ImVec2& size, bool disabled) {
  return buttonImpl(label, label, Icon::None, size, Tone::Danger, disabled);
}

bool actionButton(const char* id, Icon icon, const char* label, const ImVec2& size,
                  bool primary, bool danger) {
  const Tone tone = primary ? Tone::Primary
                            : (danger ? Tone::Danger : Tone::Secondary);
  return buttonImpl(id, label, icon, size, tone, false);
}

bool iconButton(const char* id, const char* glyph, const ImVec2& size,
                const char* tooltip) {
  Theme& t = theme();
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const ImVec2 p = ImGui::GetCursorScreenPos();
  const Rect rect{p, ImVec2(p.x + size.x, p.y + size.y)};
  bool hovered = false;
  bool clicked = false;
  if (!gBlocked) {
    ImGui::PushID(id);
    ImGui::SetCursorScreenPos(p);
    ImGui::InvisibleButton("##icon", size);
    hovered = ImGui::IsItemHovered();
    clicked = ImGui::IsItemClicked();
    ImGui::PopID();
    if (hovered && tooltip) ImGui::SetTooltip("%s", tooltip);
  }
  if (hovered) {
    dl->AddRectFilled(rect.min, rect.max,
                      withAlpha(IM_COL32(255, 255, 255, 255), 0.55f),
                      sp(9.0f));
  }
  ImFont* font = theme().fontIcon ? theme().fontIcon : fBody();
  drawTextCenter(dl, font, rect,
                 hovered ? t.colors.text : t.colors.textSecondary,
                 glyph ? glyph : "");
  ImGui::SetCursorScreenPos(ImVec2(p.x + size.x, p.y));
  return clicked;
}

bool linkText(const char* id, const std::string& text, float maxWidth,
              const char* tooltip) {
  Theme& t = theme();
  ImDrawList* dl = ImGui::GetWindowDrawList();
  ImFont* font = fBody();
  const std::string shown = elideText(text, maxWidth, font);
  const ImVec2 textSizePx = measure(font, shown);
  const ImVec2 p = ImGui::GetCursorScreenPos();
  bool hovered = false;
  bool clicked = false;
  if (!gBlocked) {
    ImGui::PushID(id);
    ImGui::SetCursorScreenPos(p);
    ImGui::InvisibleButton("##link", ImVec2(std::max(textSizePx.x, sp(12.0f)),
                                            std::max(textSizePx.y, sp(12.0f))));
    hovered = ImGui::IsItemHovered();
    clicked = ImGui::IsItemClicked();
    ImGui::PopID();
    if (hovered && tooltip) ImGui::SetTooltip("%s", tooltip);
  }
  drawText(dl, font, p, hovered ? t.colors.accentHover : t.colors.accent, shown);
  if (hovered) {
    dl->AddLine(ImVec2(p.x, p.y + textSizePx.y + sp(1.0f)),
                ImVec2(p.x + measure(font, shown).x, p.y + textSizePx.y + sp(1.0f)),
                withAlpha(t.colors.accentHover, 0.7f), 1.0f);
  }
  ImGui::SetCursorScreenPos(ImVec2(p.x + textSizePx.x, p.y));
  return clicked;
}

bool selectCard(const char* id, const char* title, const char* tagline,
                const char* description, bool selected, float height,
                const char* glyph) {
  Theme& t = theme();
  const Palette& c = t.colors;
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const float width = layoutWidth();
  const ImVec2 p = ImGui::GetCursorScreenPos();
  const Rect rect{p, ImVec2(p.x + width, p.y + height)};
  const float rounding = sp(22.0f);

  bool hovered = false;
  bool clicked = false;
  float anim = 0.0f;
  if (!gBlocked) {
    ImGui::PushID(id);
    ImGui::SetCursorScreenPos(p);
    ImGui::InvisibleButton("##select", ImVec2(width, height));
    hovered = ImGui::IsItemHovered();
    clicked = ImGui::IsItemClicked();
    anim = animate(ImGui::GetID("##select"), hovered ? 1.0f : 0.0f, 16.0f);
    ImGui::PopID();
  }

  // 卡片本体
  paint::shadow(dl, rect.min, rect.max, rounding, selected ? 1.0f : 0.75f,
                selected ? sp(34.0f) : sp(22.0f));
  dl->AddRectFilled(rect.min, rect.max,
                    selected ? c.glassStrong : c.glass, rounding);
  dl->AddRectFilled(ImVec2(rect.min.x + 1.0f, rect.min.y + 1.0f),
                    ImVec2(rect.max.x - 1.0f, rect.min.y + height * 0.55f),
                    withAlpha(IM_COL32(255, 255, 255, 255),
                              selected ? 0.42f : 0.28f + 0.14f * anim),
                    rounding, ImDrawFlags_RoundCornersTop);
  if (selected) {
    paint::glow(dl, rect.min, rect.max, rounding, withAlpha(c.accent, 0.55f),
                sp(10.0f));
    dl->AddRect(rect.min, rect.max, withAlpha(c.accent, 0.95f), rounding, 0,
                sp(1.6f));
    dl->AddRect(ImVec2(rect.min.x + sp(1.6f), rect.min.y + sp(1.6f)),
                ImVec2(rect.max.x - sp(1.6f), rect.max.y - sp(1.6f)),
                withAlpha(IM_COL32(255, 255, 255, 255), 0.55f), rounding, 0, 1.0f);
  } else {
    const float hoverBorder = 0.28f + 0.42f * anim;
    dl->AddRect(rect.min, rect.max,
                withAlpha(IM_COL32(255, 255, 255, 255), 0.85f), rounding, 0, 1.0f);
    if (anim > 0.01f) {
      dl->AddRect(rect.min, rect.max, withAlpha(c.accent, hoverBorder), rounding, 0,
                  sp(1.4f));
    }
  }
  paint::highlight(dl, rect.min, rect.max, rounding, 0.55f);

  // 图标
  Icon icon = Icon::Sparkle;
  if (glyph) {
    const std::string g = glyph;
    if (g == "local")
      icon = Icon::House;
    else if (g == "server")
      icon = Icon::Server;
    else if (g == "cloud")
      icon = Icon::Globe;
    else if (g == "check")
      icon = Icon::Check;
    else if (g == "shield")
      icon = Icon::Shield;
  }
  const ImVec2 badgeCenter(rect.min.x + sp(58.0f), rect.min.y + sp(62.0f));
  const ImU32 from = selected ? c.accentHover : IM_COL32(120, 150, 210, 255);
  const ImU32 to = selected ? c.accentDeep : IM_COL32(70, 100, 165, 255);
  iconBadge(dl, badgeCenter, sp(26.0f), icon, from, to, 0.52f);

  // 标题与 tagline
  const float textX = badgeCenter.x + sp(42.0f);
  drawText(dl, fTitle(), ImVec2(textX, rect.min.y + sp(38.0f)),
           selected ? c.text : c.text, title ? title : "");
  drawText(dl, fSmall(), ImVec2(textX, rect.min.y + sp(70.0f)), c.textSecondary,
           tagline ? tagline : "");

  // 右下角单选指示
  const ImVec2 radio(rect.max.x - sp(34.0f), rect.min.y + sp(38.0f));
  if (selected) {
    dl->AddCircleFilled(radio, sp(11.0f), c.accent, 32);
    paint::checkmark(dl, radio, sp(11.0f), c.textInverse, sp(2.2f));
  } else {
    dl->AddCircle(radio, sp(11.0f), withAlpha(c.textTertiary, 0.55f), 32,
                  sp(1.4f));
    if (anim > 0.02f) {
      dl->AddCircle(radio, sp(11.0f), withAlpha(c.accent, 0.55f * anim), 32,
                    sp(1.4f));
    }
  }

  // 要点列表
  float y = rect.min.y + sp(112.0f);
  const std::vector<std::string> features =
      splitLines(description ? description : "");
  for (const std::string& feature : features) {
    if (feature.empty()) continue;
    const ImVec2 dot(rect.min.x + sp(40.0f), y + sp(8.0f));
    dl->AddCircleFilled(dot, sp(6.5f),
                        withAlpha(selected ? c.accent : c.textTertiary, 0.18f),
                        20);
    dl->AddLine(ImVec2(dot.x - sp(3.0f), dot.y + sp(0.2f)),
                ImVec2(dot.x - sp(0.6f), dot.y + sp(2.8f)),
                selected ? c.accent : c.textSecondary, sp(1.5f));
    dl->AddLine(ImVec2(dot.x - sp(0.6f), dot.y + sp(2.8f)),
                ImVec2(dot.x + sp(3.2f), dot.y - sp(2.6f)),
                selected ? c.accent : c.textSecondary, sp(1.5f));
    drawText(dl, fBody(), ImVec2(rect.min.x + sp(56.0f), y), c.textSecondary,
             feature);
    y += lineHeight(fBody()) + sp(6.0f);
  }

  ImGui::SetCursorScreenPos(ImVec2(p.x + width, p.y));
  return clicked;
}

// ---------------------------------------------------------------- 控件
bool toggle(const char* id, bool* value, bool disabled) {
  Theme& t = theme();
  const Palette& c = t.colors;
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const float h = sp(28.0f);
  const float w = sp(48.0f);
  const ImVec2 p = ImGui::GetCursorScreenPos();
  bool changed = false;
  float anim = 0.0f;
  bool hovered = false;
  ImGui::PushID(id);
  const ImGuiID aid = ImGui::GetID("##toggle");
  if (!disabled && !gBlocked) {
    ImGui::SetCursorScreenPos(p);
    ImGui::InvisibleButton("##toggle", ImVec2(w, h));
    hovered = ImGui::IsItemHovered();
    if (ImGui::IsItemClicked()) {
      *value = !*value;
      changed = true;
    }
  }
  ImGui::PopID();
  anim = animate(aid, (value && *value) ? 1.0f : 0.0f, 18.0f);
  const ImU32 off = mixColor(IM_COL32(150, 158, 172, 190),
                             IM_COL32(120, 130, 148, 210), 0.0f);
  ImU32 track = mixColor(disabled ? IM_COL32(180, 186, 196, 130) : off, c.accent,
                         anim);
  if (hovered && !disabled) track = lighten(track, 0.08f);
  dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), track, h * 0.5f);
  if (anim > 0.01f && !disabled) {
    for (int i = 2; i >= 1; --i) {
      const float off2 = sp(1.5f) * static_cast<float>(i);
      dl->AddRectFilled(ImVec2(p.x - off2, p.y + sp(1.0f)),
                        ImVec2(p.x + w + off2, p.y + h + off2),
                        withAlpha(c.accent, 0.05f), h * 0.5f + off2);
    }
  }
  const float knobR = h * 0.5f - sp(3.0f);
  const float knobX = p.x + sp(3.0f) + knobR +
                      (w - sp(6.0f) - knobR * 2.0f) * anim;
  const ImVec2 knobCenter(knobX, p.y + h * 0.5f);
  dl->AddCircleFilled(ImVec2(knobCenter.x, knobCenter.y + sp(1.0f)), knobR,
                      withAlpha(c.shadowTint, 0.30f), 24);
  dl->AddCircleFilled(knobCenter, knobR,
                      disabled ? IM_COL32(245, 245, 248, 200)
                               : IM_COL32(255, 255, 255, 252),
                      24);
  ImGui::SetCursorScreenPos(ImVec2(p.x + w, p.y));
  return changed;
}

bool segmented(const char* id, const char* const* labels, int count, int* index) {
  Theme& t = theme();
  const Palette& c = t.colors;
  ImDrawList* dl = ImGui::GetWindowDrawList();
  if (!labels || count <= 0 || !index) return false;
  const float h = sp(36.0f);
  const float width = layoutWidth();
  const ImVec2 p = ImGui::GetCursorScreenPos();
  const Rect rect{p, ImVec2(p.x + width, p.y + h)};
  bool changed = false;
  ImGui::PushID(id);
  const ImGuiID aid = ImGui::GetID("##seg");
  const float anim = animate(aid, static_cast<float>(*index), 16.0f);
  const float pad = sp(3.0f);
  const float segW = (width - pad * 2.0f) / static_cast<float>(count);
  bool hoveredItems[16] = {false};
  const int itemCount = std::min(count, 16);
  for (int i = 0; i < itemCount; ++i) {
    if (gBlocked) break;
    ImGui::PushID(i);
    ImGui::SetCursorScreenPos(
        ImVec2(p.x + pad + segW * static_cast<float>(i), p.y + pad));
    ImGui::InvisibleButton("##segitem", ImVec2(segW, h - pad * 2.0f));
    hoveredItems[i] = ImGui::IsItemHovered();
    if (ImGui::IsItemClicked() && *index != i) {
      *index = i;
      changed = true;
    }
    ImGui::PopID();
  }
  // 轨道
  dl->AddRectFilled(rect.min, rect.max,
                    withAlpha(IM_COL32(110, 128, 158, 255), 0.10f), h * 0.5f);
  dl->AddRect(rect.min, rect.max,
              withAlpha(IM_COL32(255, 255, 255, 255), 0.62f), h * 0.5f, 0,
              sp(0.9f));
  // 滑块
  const float thumbX = p.x + pad + segW * anim;
  const Rect thumb{ImVec2(thumbX, p.y + pad),
                   ImVec2(thumbX + segW, p.y + h - pad)};
  dl->AddRectFilled(ImVec2(thumb.min.x + sp(1.0f), thumb.min.y + sp(1.5f)),
                    ImVec2(thumb.max.x - sp(1.0f), thumb.max.y + sp(1.5f)),
                    withAlpha(IM_COL32(30, 50, 90, 255), 0.13f),
                    (h - pad * 2.0f) * 0.5f);
  dl->AddRectFilled(thumb.min, thumb.max, IM_COL32(255, 255, 255, 250),
                    (h - pad * 2.0f) * 0.5f);
  paint::highlight(dl, thumb.min, thumb.max, h * 0.5f, 0.9f);
  for (int i = 0; i < count; ++i) {
    const Rect seg{ImVec2(p.x + pad + segW * static_cast<float>(i), p.y + pad),
                   ImVec2(p.x + pad + segW * static_cast<float>(i + 1),
                          p.y + h - pad)};
    const bool isSelected = (i == *index);
    drawTextCenter(dl, isSelected ? fMedium() : fBody(), seg,
                   isSelected ? c.text
                              : (i < itemCount && hoveredItems[i] ? c.text
                                                                  : c.textSecondary),
                   labels[i] ? labels[i] : "");
  }
  ImGui::PopID();
  ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + h));
  return changed;
}

bool checkbox(const char* id, bool* value, const char* label) {
  Theme& t = theme();
  const Palette& c = t.colors;
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const float box = sp(20.0f);
  const float h = sp(26.0f);
  ImFont* font = fBody();
  const float labelW = measure(font, label ? label : "").x;
  const ImVec2 p = ImGui::GetCursorScreenPos();
  const float totalW = box + (labelW > 0.0f ? sp(10.0f) + labelW : 0.0f);
  bool changed = false;
  float anim = 0.0f;
  bool hovered = false;
  ImGui::PushID(id);
  if (!gBlocked) {
    ImGui::SetCursorScreenPos(p);
    ImGui::InvisibleButton("##check", ImVec2(totalW, h));
    hovered = ImGui::IsItemHovered();
    if (ImGui::IsItemClicked()) {
      *value = !*value;
      changed = true;
    }
  }
  ImGui::PopID();
  anim = animate(ImGui::GetID("##check"), (value && *value) ? 1.0f : 0.0f, 20.0f);
  const Rect boxRect{ImVec2(p.x, p.y + (h - box) * 0.5f),
                     ImVec2(p.x + box, p.y + (h - box) * 0.5f + box)};
  if (anim > 0.01f) {
    dl->AddRectFilled(boxRect.min, boxRect.max, mixColor(c.glass, c.accent, anim),
                      sp(6.0f));
    const ImU32 border = mixColor(withAlpha(c.textTertiary, 0.55f), c.accent, anim);
    dl->AddRect(boxRect.min, boxRect.max, border, sp(6.0f), 0, sp(1.2f));
    if (anim > 0.35f) {
      paint::checkmark(dl, boxRect.center(), box * 0.85f, c.textInverse,
                       sp(2.0f));
    }
  } else {
    dl->AddRectFilled(boxRect.min, boxRect.max,
                      withAlpha(IM_COL32(255, 255, 255, 255), 0.7f), sp(6.0f));
    dl->AddRect(boxRect.min, boxRect.max,
                hovered ? withAlpha(c.accent, 0.55f)
                        : withAlpha(c.textTertiary, 0.55f),
                sp(6.0f), 0, sp(1.2f));
  }
  if (labelW > 0.0f) {
    drawText(dl, font, ImVec2(p.x + box + sp(10.0f), p.y + (h - textSize(font)) * 0.5f),
             c.textSecondary, label);
  }
  ImGui::SetCursorScreenPos(ImVec2(p.x + totalW, p.y));
  return changed;
}

// ---------------------------------------------------------------- 展示
void progressBar(float fraction, const ImVec2& size) {
  Theme& t = theme();
  const Palette& c = t.colors;
  ImDrawList* dl = ImGui::GetWindowDrawList();
  fraction = std::max(0.0f, std::min(1.0f, fraction));
  const float w = size.x > 1.0f ? size.x : layoutWidth();
  const float h = size.y > 1.0f ? size.y : sp(10.0f);
  const ImVec2 p = ImGui::GetCursorScreenPos();
  const Rect track{p, ImVec2(p.x + w, p.y + h)};
  const float rounding = h * 0.5f;
  dl->AddRectFilled(track.min, track.max,
                    withAlpha(IM_COL32(110, 128, 158, 255), 0.16f), rounding);
  dl->AddRect(track.min, track.max,
              withAlpha(IM_COL32(255, 255, 255, 255), 0.55f), rounding, 0,
              sp(0.9f));
  if (fraction > 0.0f) {
    const float fillW = std::max(h, w * fraction);
    const Rect fill{track.min, ImVec2(track.min.x + fillW, track.max.y)};
    dl->AddRectFilled(fill.min, fill.max, c.accentDeep, rounding);
    dl->AddRectFilled(ImVec2(fill.min.x, fill.min.y),
                      ImVec2(fill.max.x, fill.min.y + h * 0.5f),
                      withAlpha(lighten(c.accent, 0.28f), 0.85f), rounding,
                      ImDrawFlags_RoundCornersTop);
    paint::highlight(dl, fill.min, fill.max, rounding, 0.65f);
    // 末端柔光
    dl->AddCircleFilled(ImVec2(fill.max.x - rounding, fill.center().y), h * 0.5f,
                        withAlpha(IM_COL32(255, 255, 255, 255), 0.25f), 16);
  }
  ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + h));
}

void statusDot(ImU32 color, float radius) {
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const ImVec2 p = ImGui::GetCursorScreenPos();
  const ImVec2 center(p.x + radius, p.y + radius);
  dl->AddCircleFilled(center, radius * 2.1f, withAlpha(color, 0.14f), 20);
  dl->AddCircleFilled(center, radius, color, 20);
  ImGui::SetCursorScreenPos(ImVec2(p.x + radius * 2.0f + sp(8.0f), p.y));
}

void badge(const char* text, ImU32 background, ImU32 foreground) {
  ImDrawList* dl = ImGui::GetWindowDrawList();
  ImFont* font = fSmall();
  const std::string label = text ? text : "";
  const ImVec2 textPx = measure(font, label);
  const float h = textPx.y + sp(10.0f);
  const float w = textPx.x + sp(20.0f);
  const ImVec2 p = ImGui::GetCursorScreenPos();
  const Rect rect{p, ImVec2(p.x + w, p.y + h)};
  dl->AddRectFilled(rect.min, rect.max, background, h * 0.5f);
  dl->AddRect(rect.min, rect.max,
              withAlpha(IM_COL32(255, 255, 255, 255), 0.45f), h * 0.5f, 0,
              sp(0.8f));
  drawTextCenter(dl, font, rect, foreground, label);
  ImGui::SetCursorScreenPos(ImVec2(p.x + w + sp(8.0f), p.y));
}

void sectionTitle(const char* text, float topGap) {
  if (topGap > 0.0f) advanceY(sp(topGap));
  Theme& t = theme();
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const ImVec2 p = ImGui::GetCursorScreenPos();
  drawText(dl, fMedium(), p, t.colors.text, text ? text : "");
  ImGui::SetCursorScreenPos(
      ImVec2(p.x, p.y + textSize(fMedium()) + sp(10.0f)));
}

void bodyText(const char* text, ImU32 color, bool wrap) {
  Theme& t = theme();
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const ImVec2 p = ImGui::GetCursorScreenPos();
  const ImU32 col = color ? color : t.colors.textSecondary;
  const float wrapWidth = wrap ? layoutWidth() : 100000.0f;
  drawTextWrapped(dl, fBody(), p, col, text ? text : "", wrapWidth);
  const float h = measureWrappedHeight(fBody(), text ? text : "", wrapWidth);
  ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + h));
}

void hint(const char* text) {
  Theme& t = theme();
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const ImVec2 p = ImGui::GetCursorScreenPos();
  drawText(dl, fSmall(), p, t.colors.textTertiary, text ? text : "");
  ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + textSize(fSmall()) + sp(6.0f)));
}

void divider(float topGap, float bottomGap) {
  if (topGap > 0.0f) advanceY(sp(topGap));
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const ImVec2 p = ImGui::GetCursorScreenPos();
  const float w = layoutWidth();
  paint::hairline(dl, p.x, p.x + w, p.y, 0.85f);
  ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + sp(1.0f) + sp(bottomGap)));
}

void spacer(float height) { advanceY(sp(height)); }

void labelValue(const char* label, const std::string& value,
                const char* monoPrefix) {
  Theme& t = theme();
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const ImVec2 p = ImGui::GetCursorScreenPos();
  const float labelW = sp(92.0f);
  drawText(dl, fSmall(), ImVec2(p.x, p.y + sp(2.0f)), t.colors.textTertiary,
           label ? label : "");
  std::string shown = value;
  if (monoPrefix && monoPrefix[0]) shown = std::string(monoPrefix) + value;
  const float avail = std::max(sp(40.0f), layoutWidth() - labelW);
  drawText(dl, fBody(), ImVec2(p.x + labelW, p.y), t.colors.text,
           elideText(shown, avail, fBody()));
  ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + std::max(textSize(fBody()), sp(20.0f)) + sp(10.0f)));
}

// ---------------------------------------------------------------- 输入
bool textField(const char* id, std::string* value, const char* hintText,
               float width, bool numeric) {
  Theme& t = theme();
  const Palette& c = t.colors;
  ImDrawList* dl = ImGui::GetWindowDrawList();
  ImFont* font = fBody();
  const float h = sp(40.0f);
  const float w = width > 1.0f ? width : layoutWidth();
  const ImVec2 p = ImGui::GetCursorScreenPos();
  const Rect rect{p, ImVec2(p.x + w, p.y + h)};
  const bool blocked = gBlocked;
  bool changed = false;

  ImGui::PushID(id);
  const ImGuiID fid = ImGui::GetID("##tf");
  FieldVisual& vis = gFields[fid];
  const float rounding = sp(10.0f);
  const ImU32 bg = blocked ? withAlpha(IM_COL32(255, 255, 255, 255), 0.35f)
                           : withAlpha(IM_COL32(255, 255, 255, 255),
                                       vis.focus ? 0.92f : (vis.hover ? 0.82f : 0.66f));
  dl->AddRectFilled(rect.min, rect.max, bg, rounding);
  if (vis.focus && !blocked) {
    paint::glow(dl, rect.min, rect.max, rounding, withAlpha(c.accent, 0.35f),
                sp(5.0f));
  }
  dl->AddRect(rect.min, rect.max,
              vis.focus && !blocked
                  ? withAlpha(c.accent, 0.85f)
                  : (vis.hover && !blocked ? withAlpha(c.accent, 0.35f)
                                           : withAlpha(c.textTertiary, 0.30f)),
              rounding, 0, vis.focus && !blocked ? sp(1.6f) : sp(1.0f));

  if (!blocked) {
    char buffer[1024];
    std::snprintf(buffer, sizeof(buffer), "%s", value ? value->c_str() : "");
    const float padX = sp(13.0f);
    const float padY = (h - textSize(font)) * 0.5f;
    ImGui::PushFont(font);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(padX, padY));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, rounding);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Text,
                          ImGui::ColorConvertU32ToFloat4(c.text));
    ImGui::PushStyleColor(ImGuiCol_TextDisabled,
                          ImGui::ColorConvertU32ToFloat4(c.textTertiary));
    ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + (h - textSize(font)) * 0.5f - padY));
    ImGui::SetNextItemWidth(w);
    ImGuiInputTextFlags flags = 0;
    if (numeric) flags |= ImGuiInputTextFlags_CharsDecimal;
    changed = ImGui::InputTextWithHint("##tf", hintText ? hintText : "", buffer,
                                       sizeof(buffer), flags);
    vis.hover = ImGui::IsItemHovered();
    vis.focus = ImGui::IsItemActive();
    ImGui::PopStyleColor(6);
    ImGui::PopStyleVar(2);
    ImGui::PopFont();
    if (changed && value) *value = buffer;
  } else {
    const std::string shown =
        (value && !value->empty()) ? elideText(*value, w - sp(26.0f), font)
                                   : std::string(hintText ? hintText : "");
    drawText(dl, font, ImVec2(p.x + sp(13.0f), p.y + (h - textSize(font)) * 0.5f),
             (value && !value->empty()) ? c.text : c.textTertiary, shown);
    vis.hover = false;
    vis.focus = false;
  }
  ImGui::PopID();
  ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + h));
  return changed;
}

// ---------------------------------------------------------------- 文件选择
std::string pickFile(const char* title, const std::vector<std::string>& extensions) {
  std::string filter = "所有文件 (*.*)|*.*";
  if (!extensions.empty()) {
    std::string patterns;
    std::string label;
    for (size_t i = 0; i < extensions.size(); ++i) {
      std::string ext = extensions[i];
      if (!ext.empty() && ext.front() != '.') ext = "." + ext;
      if (i) {
        patterns += ";";
        label += " / ";
      }
      patterns += "*" + ext;
      label += "*" + ext;
    }
    filter = label + "|" + patterns + "|所有文件 (*.*)|*.*";
  }
  const std::string script =
      "[Console]::OutputEncoding = New-Object System.Text.UTF8Encoding $false\n"
      "Add-Type -AssemblyName System.Windows.Forms | Out-Null\n"
      "$f = New-Object System.Windows.Forms.Form\n"
      "$f.TopMost = $true; $f.ShowInTaskbar = $false; "
      "$f.FormBorderStyle = 'None'; $f.StartPosition = 'CenterScreen'; "
      "$f.Size = New-Object System.Drawing.Size(1,1); $f.Opacity = 0.01\n"
      "$f.Show()\n"
      "$d = New-Object System.Windows.Forms.OpenFileDialog\n" +
      std::string("$d.Title = ") + quoteSingle(title ? title : "选择文件") + "\n" +
      std::string("$d.Filter = ") + quoteSingle(filter) + "\n" +
      "$d.CheckFileExists = $true\n"
      "if ($d.ShowDialog($f) -eq [System.Windows.Forms.DialogResult]::OK) { "
      "[Console]::Out.Write($d.FileName) }\n"
      "$f.Close()\n";
  const std::string command = powerShellCommand(script);
  if (command.empty()) {
    // 非 Windows 平台：尝试 zenity / kdialog / osascript。
#if defined(__APPLE__)
    std::string osa = "osascript -e 'POSIX path of (choose file with prompt \"" +
                      std::string(title ? title : "选择文件") + "\")' 2>/dev/null";
    std::string out = trimDialogResult(runCapture(osa));
    return out;
#else
    std::string cmd = "zenity --file-selection --title='" +
                      std::string(title ? title : "选择文件") + "'";
    for (const std::string& ext : extensions) {
      std::string e = ext;
      if (!e.empty() && e.front() == '.') e.erase(0, 1);
      cmd += " --file-filter='*." + e + "'";
    }
    cmd += " 2>/dev/null";
    std::string out = trimDialogResult(runCapture(cmd));
    if (!out.empty()) return out;
    return {};
#endif
  }
  return trimDialogResult(runCapture(command));
}

std::string pickFolder(const std::string& initialDir) {
  std::string script =
      "[Console]::OutputEncoding = New-Object System.Text.UTF8Encoding $false\n"
      "Add-Type -AssemblyName System.Windows.Forms | Out-Null\n"
      "$f = New-Object System.Windows.Forms.Form\n"
      "$f.TopMost = $true; $f.ShowInTaskbar = $false; "
      "$f.FormBorderStyle = 'None'; $f.StartPosition = 'CenterScreen'; "
      "$f.Size = New-Object System.Drawing.Size(1,1); $f.Opacity = 0.01\n"
      "$f.Show()\n"
      "$d = New-Object System.Windows.Forms.FolderBrowserDialog\n"
      "$d.Description = '选择安装目录'\n"
      "$d.ShowNewFolderButton = $true\n";
  if (!initialDir.empty()) {
    script += std::string("if (Test-Path -LiteralPath ") +
              quoteSingle(initialDir) + ") { $d.SelectedPath = " +
              quoteSingle(initialDir) + " }\n";
  }
  script +=
      "if ($d.ShowDialog($f) -eq [System.Windows.Forms.DialogResult]::OK) { "
      "[Console]::Out.Write($d.SelectedPath) }\n"
      "$f.Close()\n";
  const std::string command = powerShellCommand(script);
  if (command.empty()) {
#if defined(__APPLE__)
    std::string out = trimDialogResult(
        runCapture("osascript -e 'POSIX path of (choose folder with prompt "
                   "\"选择安装目录\")' 2>/dev/null"));
    return out;
#else
    std::string out = trimDialogResult(
        runCapture("zenity --file-selection --directory --title='选择安装目录' "
                   "2>/dev/null"));
    if (!out.empty()) return out;
    return {};
#endif
  }
  return trimDialogResult(runCapture(command));
}

// ---------------------------------------------------------------- 横幅 / 其它
bool banner(const char* id, const std::string& text, bool error) {
  Theme& t = theme();
  const Palette& c = t.colors;
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const float w = layoutWidth();
  const float padX = sp(18.0f);
  const float iconSize = sp(22.0f);
  const ImVec2 p = ImGui::GetCursorScreenPos();
  const float textWidth = w - padX * 2.0f - iconSize - sp(12.0f) - sp(28.0f);
  const float textH = measureWrappedHeight(fBody(), text, textWidth);
  const float h = std::max(sp(52.0f), textH + sp(26.0f));
  const Rect rect{p, ImVec2(p.x + w, p.y + h)};
  const ImU32 tint = error ? c.danger : c.success;
  dl->AddRectFilled(rect.min, rect.max, withAlpha(tint, 0.10f), sp(14.0f));
  dl->AddRect(rect.min, rect.max, withAlpha(tint, 0.32f), sp(14.0f), 0,
              sp(1.1f));
  const ImVec2 iconCenter(rect.min.x + padX + iconSize * 0.5f, rect.center().y);
  dl->AddCircleFilled(iconCenter, iconSize * 0.5f, withAlpha(tint, 0.18f), 24);
  paintIcon(dl, iconCenter, iconSize * 0.62f,
            error ? Icon::Warning : Icon::Check, tint, 1.5f);
  drawTextWrapped(dl, fBody(),
                  ImVec2(rect.min.x + padX + iconSize + sp(12.0f),
                         rect.center().y - textH * 0.5f),
                  c.text, text, textWidth);
  bool closed = false;
  if (!gBlocked) {
    ImGui::PushID(id);
    ImGui::SetCursorScreenPos(
        ImVec2(rect.max.x - sp(34.0f), rect.center().y - sp(11.0f)));
    ImGui::InvisibleButton("##close", ImVec2(sp(22.0f), sp(22.0f)));
    const bool hovered = ImGui::IsItemHovered();
    if (hovered)
      dl->AddCircleFilled(ImVec2(rect.max.x - sp(23.0f), rect.center().y),
                          sp(11.0f), withAlpha(c.textTertiary, 0.18f), 20);
    paintIcon(dl, ImVec2(rect.max.x - sp(23.0f), rect.center().y), sp(11.0f),
              Icon::Cross, c.textSecondary, 1.3f);
    closed = ImGui::IsItemClicked();
    ImGui::PopID();
  }
  ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + h));
  return closed;
}

TitleBarAction titleBar(const std::string& title, const std::string& subtitle) {
  Theme& t = theme();
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const ImVec2 p = ImGui::GetCursorScreenPos();
  drawText(dl, fMedium(), p, t.colors.text, title);
  drawText(dl, fSmall(), ImVec2(p.x, p.y + textSize(fMedium()) + sp(2.0f)),
           t.colors.textSecondary, subtitle);
  return TitleBarAction::None;
}

void spinner(float radius, float thickness, float phase) {
  Theme& t = theme();
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const ImVec2 p = ImGui::GetCursorScreenPos();
  const ImVec2 center(p.x + radius, p.y + radius);
  dl->AddCircle(center, radius, withAlpha(t.colors.accent, 0.15f), 32, thickness);
  const float base = phase * 2.0f * kPi;
  for (int k = 2; k >= 0; --k) {
    const float a0 = base + static_cast<float>(k) * 0.55f;
    dl->PathArcTo(center, radius, a0, a0 + 0.62f, 16);
    dl->PathStroke(withAlpha(t.colors.accent,
                             0.25f + 0.22f * static_cast<float>(2 - k)),
                   0, thickness);
  }
  ImGui::SetCursorScreenPos(ImVec2(p.x + radius * 2.0f + sp(8.0f), p.y));
}

int confirmDialog(const char* id, const char* title, const std::string& message,
                  const char* confirmLabel, const char* cancelLabel, bool danger,
                  bool* toggleValue, const char* toggleLabel) {
  Theme& t = theme();
  const Palette& c = t.colors;
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const ImVec2 display = ImGui::GetIO().DisplaySize;
  modalScrim(0.30f);

  const float w = sp(468.0f);
  const float pad = sp(28.0f);
  const float titleH = textSize(fTitle()) + sp(10.0f);
  const float msgH = measureWrappedHeight(fBody(), message, w - pad * 2.0f);
  const float toggleH = toggleValue ? sp(36.0f) : 0.0f;
  const float buttonsH = sp(40.0f);
  const float h = pad + titleH + sp(8.0f) + msgH + sp(18.0f) + toggleH +
                  sp(22.0f) + buttonsH + pad;
  const ImVec2 p(display.x * 0.5f - w * 0.5f, display.y * 0.5f - h * 0.5f);
  const Rect rect{p, ImVec2(p.x + w, p.y + h)};
  paint::glassPanel(dl, rect.min, rect.max, sp(22.0f), true);

  ImGui::PushID(id);
  drawText(dl, fTitle(), ImVec2(rect.min.x + pad, rect.min.y + pad), c.text,
           title ? title : "");
  drawTextWrapped(dl, fBody(),
                  ImVec2(rect.min.x + pad, rect.min.y + pad + titleH + sp(8.0f)),
                  c.textSecondary, message, w - pad * 2.0f);
  if (toggleValue) {
    ImGui::SetCursorScreenPos(
        ImVec2(rect.min.x + pad,
               rect.min.y + pad + titleH + sp(8.0f) + msgH + sp(18.0f)));
    checkbox("##confirm-toggle", toggleValue, toggleLabel ? toggleLabel : "");
  }
  const float buttonY = rect.max.y - pad - buttonsH;
  const float cancelW = measure(fMedium(), cancelLabel ? cancelLabel : "").x + sp(40.0f);
  const float confirmW = measure(fMedium(), confirmLabel ? confirmLabel : "").x + sp(40.0f);
  const float gap = sp(12.0f);
  bool confirmed = false;
  bool cancelled = false;
  ImGui::SetCursorScreenPos(
      ImVec2(rect.max.x - pad - confirmW, buttonY + sp(2.0f)));
  if (danger) {
    confirmed = buttonImpl("##confirm", confirmLabel, Icon::None,
                           ImVec2(confirmW, sp(40.0f)), Tone::DangerSolid, false);
  } else {
    confirmed = buttonImpl("##confirm", confirmLabel, Icon::None,
                           ImVec2(confirmW, sp(40.0f)), Tone::Primary, false);
  }
  ImGui::SetCursorScreenPos(
      ImVec2(rect.max.x - pad - confirmW - gap - cancelW, buttonY + sp(2.0f)));
  cancelled = buttonImpl("##cancel", cancelLabel, Icon::None,
                         ImVec2(cancelW, sp(40.0f)), Tone::Secondary, false);
  ImGui::PopID();
  if (confirmed) return 1;
  if (cancelled) return 2;
  return 0;
}

}  // namespace mes::ui
