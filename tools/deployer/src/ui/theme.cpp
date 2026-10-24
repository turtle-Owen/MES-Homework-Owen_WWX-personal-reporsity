// Apple 风格主题：调色板、字号层级、系统中文字体与轻毛玻璃绘制原语。
#include "ui/theme.h"

#include "platform/platform.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace mes::ui {
namespace {

Theme gTheme;

inline ImU32 rgba(int r, int g, int b, int a) {
  return IM_COL32(r, g, b, a);
}
inline ImU32 rgba(int r, int g, int b) { return IM_COL32(r, g, b, 255); }

void buildPalette(Palette& c) {
  c.backgroundTop = rgba(247, 249, 252);
  c.backgroundBottom = rgba(226, 234, 245);
  c.glass = rgba(255, 255, 255, 150);
  c.glassStrong = rgba(255, 255, 255, 218);
  c.glassBorder = rgba(255, 255, 255, 216);
  c.glassShadow = rgba(36, 56, 96, 26);
  c.text = rgba(29, 29, 31);
  c.textSecondary = rgba(108, 108, 114);
  c.textTertiary = rgba(146, 146, 152);
  c.accent = rgba(10, 132, 255);
  c.accentHover = rgba(64, 156, 255);
  c.accentSoft = rgba(10, 132, 255, 30);
  c.success = rgba(48, 209, 88);
  c.warning = rgba(255, 159, 10);
  c.danger = rgba(255, 69, 58);
  c.separator = rgba(60, 60, 67, 26);
  c.windowClose = rgba(255, 95, 87);
  c.windowMinimize = rgba(254, 188, 46);
  c.textInverse = rgba(255, 255, 255);
  c.successSoft = rgba(48, 209, 88, 30);
  c.warningSoft = rgba(255, 159, 10, 34);
  c.dangerSoft = rgba(255, 69, 58, 30);
  c.accentDeep = rgba(6, 109, 224);
  c.accentLight = rgba(66, 160, 255);
  c.glassTint = rgba(232, 240, 252, 120);
  c.scrollbar = rgba(60, 60, 67, 56);
  c.shadowTint = rgba(35, 55, 95, 40);
}

void applyMetrics(float scale) {
  Theme& t = gTheme;
  t.scale = scale;
  t.topBarHeight = 52.0f * scale;
  t.footerHeight = 34.0f * scale;
  t.margin = 40.0f * scale;
  t.radiusCard = 20.0f * scale;
  t.radiusControl = 12.0f * scale;
  t.gap = 16.0f * scale;
  t.rowHeight = 40.0f * scale;
}

// ------------------------------------------------------------------ 字体探测
struct FontPick {
  std::string path;
  int index = 0;
  bool bold = false;
};

bool fileReadable(const std::string& path) {
  if (path.empty()) return false;
  FILE* f = std::fopen(path.c_str(), "rb");
  if (!f) return false;
  std::fclose(f);
  return true;
}

// 读取 TTC 头，确认 FontNo 在集合范围内（stb_truetype 对越界索引会静默失败）。
bool ttcIndexValid(const std::string& path, int index) {
  FILE* f = std::fopen(path.c_str(), "rb");
  if (!f) return false;
  unsigned char head[12] = {0};
  const size_t got = std::fread(head, 1, sizeof(head), f);
  std::fclose(f);
  if (got < 12) return true;  // 普通 ttf：交给 ImGui 处理
  if (head[0] == 't' && head[1] == 't' && head[2] == 'c' && head[3] == 'f') {
    const unsigned int numFonts = (static_cast<unsigned int>(head[8]) << 24) |
                                  (static_cast<unsigned int>(head[9]) << 16) |
                                  (static_cast<unsigned int>(head[10]) << 8) |
                                  static_cast<unsigned int>(head[11]);
    return index >= 0 && static_cast<unsigned int>(index) < numFonts;
  }
  return index == 0;
}

bool looksBold(const std::string& path) {
  std::string lower;
  lower.reserve(path.size());
  for (char ch : path) lower.push_back(static_cast<char>(ch >= 'A' && ch <= 'Z' ? ch + 32 : ch));
  static const char* kHints[] = {"bd.", "bold", "semibold", "dengb", "heiti",
                                 "medium", "black"};
  for (const char* hint : kHints) {
    if (lower.find(hint) != std::string::npos) return true;
  }
  return false;
}

void appendPick(std::vector<FontPick>& out, const std::string& path, int index,
                bool bold) {
  for (const FontPick& existing : out) {
    if (existing.path == path && existing.index == index) return;
  }
  FontPick pick;
  pick.path = path;
  pick.index = index;
  pick.bold = bold;
  out.push_back(pick);
}

bool looksCjk(const std::string& path) {
  std::string lower;
  lower.reserve(path.size());
  for (char ch : path) lower.push_back(static_cast<char>(ch >= 'A' && ch <= 'Z' ? ch + 32 : ch));
  static const char* kHints[] = {"msyh",  "simhei",  "simsun", "deng",
                                 "pingfang", "noto", "wqy",    "heiti",
                                 "hiragino", "cjk",  "yahei",  "han",
                                 "unifont", "arial unicode"};
  for (const char* hint : kHints) {
    if (lower.find(hint) != std::string::npos) return true;
  }
  return false;
}

// platform 的候选 + 内置兜底（保证在平台层尚未提供字体时也不出现方框字）。
std::vector<FontPick> fontPickList() {
  std::vector<FontPick> picks;
  for (const platform::FontCandidate& c : platform::fontCandidates()) {
    if (c.path.empty()) continue;
    appendPick(picks, c.path, c.index, looksBold(c.path));
  }
#if defined(_WIN32)
  appendPick(picks, "C:/Windows/Fonts/msyh.ttc", 0, false);
  appendPick(picks, "C:/Windows/Fonts/msyhbd.ttc", 0, true);
  appendPick(picks, "C:/Windows/Fonts/simhei.ttf", 0, true);
  appendPick(picks, "C:/Windows/Fonts/Deng.ttf", 0, false);
  appendPick(picks, "C:/Windows/Fonts/simsun.ttc", 0, false);
#elif defined(__APPLE__)
  appendPick(picks, "/System/Library/Fonts/PingFang.ttc", 0, false);
  appendPick(picks, "/System/Library/Fonts/PingFang.ttc", 1, false);
  appendPick(picks, "/System/Library/Fonts/PingFang.ttc", 2, false);
  appendPick(picks, "/System/Library/Fonts/STHeiti Medium.ttc", 0, true);
  appendPick(picks, "/System/Library/Fonts/Hiragino Sans GB.ttc", 0, false);
  appendPick(picks, "/Library/Fonts/Arial Unicode.ttf", 0, false);
#else
  appendPick(picks, "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc", 0, false);
  appendPick(picks, "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc", 2, false);
  appendPick(picks, "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc", 0, false);
  appendPick(picks, "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc", 2, false);
  appendPick(picks, "/usr/share/fonts/truetype/wqy/wqy-microhei.ttc", 0, false);
  appendPick(picks, "/usr/share/fonts/wqy-microhei/wqy-microhei.ttc", 0, false);
  appendPick(picks, "/usr/share/fonts/truetype/wqy/wqy-zenhei.ttc", 0, false);
  appendPick(picks, "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 0, false);
#endif
  // CJK 字体优先（platform 候选顺序不保证时也不会先拿到纯拉丁字体）。
  std::stable_sort(picks.begin(), picks.end(),
                   [](const FontPick& a, const FontPick& b) {
                     return looksCjk(a.path) && !looksCjk(b.path);
                   });
  return picks;
}

ImFont* loadFontFromPick(const FontPick& pick, float pixelSize,
                         const ImWchar* ranges) {
  if (!fileReadable(pick.path) || !ttcIndexValid(pick.path, pick.index)) return nullptr;
  ImFontConfig cfg;
  cfg.FontNo = pick.index;
  cfg.OversampleH = 2;
  cfg.OversampleV = 1;
  cfg.PixelSnapH = false;
  cfg.RasterizerMultiply = 1.06f;
  return ImGui::GetIO().Fonts->AddFontFromFileTTF(pick.path.c_str(), pixelSize,
                                                 &cfg, ranges);
}

}  // namespace

Theme& theme() { return gTheme; }

float uiScale() { return gTheme.scale > 0.01f ? gTheme.scale : 1.0f; }

float textSize(const ImFont* font) {
  const ImFont* f = font ? font : gTheme.fontBody;
  if (!f) return 15.0f;
  const float logical = f->FontSize * ImGui::GetIO().FontGlobalScale;
  return logical > 1.0f ? logical : 1.0f;
}

ImU32 withAlpha(ImU32 color, float alpha) {
  if (alpha <= 0.0f) return color & 0x00FFFFFFu;
  if (alpha >= 1.0f) return color | 0xFF000000u;
  const ImU32 a = static_cast<ImU32>(alpha * 255.0f + 0.5f);
  return (color & 0x00FFFFFFu) | (a << IM_COL32_A_SHIFT);
}

namespace {
inline int channel(ImU32 color, int shift) {
  return static_cast<int>((color >> shift) & 0xFFu);
}
}  // namespace

ImU32 mixColor(ImU32 a, ImU32 b, float t) {
  t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
  const int r = static_cast<int>(channel(a, IM_COL32_R_SHIFT) * (1 - t) +
                                 channel(b, IM_COL32_R_SHIFT) * t + 0.5f);
  const int g = static_cast<int>(channel(a, IM_COL32_G_SHIFT) * (1 - t) +
                                 channel(b, IM_COL32_G_SHIFT) * t + 0.5f);
  const int bl = static_cast<int>(channel(a, IM_COL32_B_SHIFT) * (1 - t) +
                                  channel(b, IM_COL32_B_SHIFT) * t + 0.5f);
  const int al = static_cast<int>(channel(a, IM_COL32_A_SHIFT) * (1 - t) +
                                  channel(b, IM_COL32_A_SHIFT) * t + 0.5f);
  return IM_COL32(r, g, bl, al);
}

ImU32 lighten(ImU32 color, float amount) {
  return mixColor(color, IM_COL32(255, 255, 255, 255), amount);
}

ImU32 darken(ImU32 color, float amount) {
  return mixColor(color, IM_COL32(0, 0, 0, 255), amount);
}

ImVec2 measureText(const ImFont* font, const char* text) {
  const ImFont* f = font ? font : gTheme.fontBody;
  if (!f || !text) return ImVec2(0, 0);
  const ImVec2 raw = const_cast<ImFont*>(f)->CalcTextSizeA(f->FontSize, FLT_MAX, 0.0f, text);
  const float globalScale = ImGui::GetIO().FontGlobalScale;
  return ImVec2(raw.x * globalScale, raw.y * globalScale);
}

float lineHeight(const ImFont* font) {
  return textSize(font) * 1.42f;
}

void syncFontScale() {
  ImGuiIO& io = ImGui::GetIO();
  const float framebufferScale =
      io.DisplayFramebufferScale.x > 0.1f ? io.DisplayFramebufferScale.x : 1.0f;
  const float target = 1.0f / framebufferScale;
  if (std::fabs(io.FontGlobalScale - target) > 0.002f) {
    io.FontGlobalScale = target;
  }
}

void drawLabel(ImDrawList* drawList, ImFont* font, const ImVec2& pos,
               ImU32 color, const char* text) {
  if (!drawList || !text || !text[0]) return;
  drawList->AddText(font, textSize(font), pos, color, text);
}

void drawLabelCenter(ImDrawList* drawList, ImFont* font, const ImVec2& min,
                     const ImVec2& max, ImU32 color, const char* text) {
  if (!drawList || !text || !text[0]) return;
  const ImVec2 size = measureText(font, text);
  drawList->AddText(font, textSize(font),
                    ImVec2(min.x + (max.x - min.x - size.x) * 0.5f,
                           min.y + (max.y - min.y - size.y) * 0.5f),
                    color, text);
}

void drawLabelWrapped(ImDrawList* drawList, ImFont* font, const ImVec2& pos,
                      ImU32 color, const char* text, float wrapWidth) {
  if (!drawList || !text || !text[0]) return;
  drawList->AddText(font, textSize(font), pos, color, text, nullptr, wrapWidth);
}

float measureWrappedHeight(ImFont* font, const char* text, float wrapWidth) {
  if (!font || !text || !text[0]) return 0.0f;
  const float globalScale = ImGui::GetIO().FontGlobalScale;
  const float wrapRaster =
      globalScale > 0.0001f ? wrapWidth / globalScale : wrapWidth;
  const ImVec2 size =
      font->CalcTextSizeA(font->FontSize, FLT_MAX, wrapRaster, text);
  return size.y * (globalScale > 0.0001f ? globalScale : 1.0f);
}

const ImWchar* cjkGlyphRanges() {
  static ImVector<ImWchar> ranges;
  if (!ranges.empty()) return ranges.Data;
  ImFontGlyphRangesBuilder builder;
  builder.AddRanges(
      ImGui::GetIO().Fonts->GetGlyphRangesChineseSimplifiedCommon());
  // 部署器源码里会出现的全部汉字（UI + core + platform），避免常用字表缺字。
  static const char kCorpus[] = u8"–—（），、。：；？“”「」×≤≥→°·…安按案暗把白百败班板版半包薄饱保报贝备背被本崩比必毕闭避边编变便标表别柄并玻补不布步部才材裁参残仓藏操侧测策层叉查产尝常场超车彻撤称成承程持尺充抽出初除础储处触传串窗创纯词磁此次从篡存寸错打大代带待单但淡弹当导到道地的登等低抵底递第典点电调掉叠丁顶定动都兜逗独读度端短段断队对多额而尔二发法反返范方防访放非费分份风否肤服浮符幅辅父负附复赋覆该改盖概感杠高告格隔个给根跟工公功供共勾构够固关观官管惯光归规轨滚果裹过还含函汉好号耗合何和河核横后候呼忽糊互户护滑化划画话坏欢环缓换恢徽回会绘活火或获击机基及级极即集辑几计记技际继加夹架间监兼检剪简见件建健渐键将降交焦角脚较阶接节结捷截解界仅尽进近经精景径净静境镜旧就局矩句拒具据绝均卡开看康考拷靠棵可空控抠口扣库跨块快宽况框溃扩括拉来赖拦栏览老了类累冷离璃里理力立例连联链量列临麟零令浏留流六漏录路轮论逻络律虑略码卖满忙毛冒没每密幂免面秒敏名明命模末默目幕拿脑内能念钮努诺偶排牌盘判配批皮匹片拼品平评凭屏破普铺期其麒启起契器迁签前浅强墙且切轻清情擎请求区驱取去权全泉缺确然染让绕认任仍日容柔如入塞三色杀删上尚稍少设身深审生声省剩失师十时识实矢使始示式试视是适释收手首受授书殊输属署术束树竖数双顺瞬说丝四素算随孙损缩所索锁它台态探套特提题体替天添填条跳贴听停通同统头投透图推退拖外完万网往望危围尾为未位喂文稳问握无五勿务物误雾吸析息晰系细下先闲显险现限线相详享响向项象像消销小校效些协斜携写泄卸心新信行形型性修须需许序绪续旋选渲学循压言沿颜演验样要钥也业页一依移已以亿义议异译驿意因阴银引隐应迎营影用优由有右于余与语预域遇元园员原圆源约越晕允运再在载暂造则责择增栅摘展占栈章长障找照罩遮折这帧真诊整正证之支知执直值止只址指至志制质致置中终钟重骤逐主助住注专转装状追缀准桌资子自字总走足阻组最左作坐座做";
  builder.AddText(kCorpus);
  // Latin-1 补充（× · ° 等）与符号区。
  static const ImWchar kExtraRanges[] = {0x00A0, 0x00FF, 0x2010, 0x2027,
                                         0x2030, 0x203A, 0x2190, 0x2199,
                                         0x2212, 0x2212, 0x25A0, 0x25CF,
                                         0x2605, 0x2606, 0x2713, 0x2717, 0};
  builder.AddRanges(kExtraRanges);
  builder.BuildRanges(&ranges);
  return ranges.Data;
}
void loadFonts(float dpiScale) {
  Theme& t = theme();
  const float scale =
      (dpiScale > 0.5f && dpiScale < 3.0f) ? dpiScale : 1.0f;
  applyMetrics(scale);
  buildPalette(t.colors);

  ImGuiIO& io = ImGui::GetIO();
  io.Fonts->Clear();
  io.FontGlobalScale = 1.0f;

  // 字体光栅化尺寸：按 DPI 放大以保证清晰度，但限制上限避免 CJK 图集过大。
  const float rasterScale = std::min(scale, 1.6f);
  const ImWchar* ranges = cjkGlyphRanges();
  const auto px = [rasterScale](float logical) { return logical * rasterScale; };

  const std::vector<FontPick> picks = fontPickList();
  FontPick regular;
  FontPick bold;
  bool hasRegular = false;
  bool hasBold = false;
  for (const FontPick& pick : picks) {
    if (!fileReadable(pick.path) || !ttcIndexValid(pick.path, pick.index)) continue;
    if (!hasRegular) {
      regular = pick;
      hasRegular = true;
      t.cjkFontPath = pick.path;
    } else if (!hasBold && pick.bold) {
      bold = pick;
      hasBold = true;
    }
    if (hasRegular && hasBold) break;
  }
  if (!hasRegular) {
    io.FontGlobalScale = 1.0f;
    t.fontBody = io.Fonts->AddFontDefault();
    t.fontSmall = t.fontBody;
    t.fontMedium = t.fontBody;
    t.fontTitle = t.fontBody;
    t.fontHero = t.fontBody;
    t.fontIcon = t.fontBody;
    t.cjkFontPath.clear();
    io.FontDefault = t.fontBody;
    return;
  }

  t.fontSmall = loadFontFromPick(regular, px(12.5f), ranges);
  t.fontBody = loadFontFromPick(regular, px(15.0f), ranges);
  t.fontMedium = loadFontFromPick(regular, px(16.5f), ranges);
  const FontPick titlePick = hasBold ? bold : regular;
  t.fontTitle = loadFontFromPick(titlePick, px(22.0f), ranges);
  t.fontHero = loadFontFromPick(titlePick, px(31.0f), ranges);
  t.fontIcon = loadFontFromPick(titlePick, px(15.0f), ranges);
  if (hasBold) t.boldFontPath = bold.path;
  if (!t.fontSmall) t.fontSmall = t.fontBody;
  if (!t.fontMedium) t.fontMedium = t.fontBody;
  if (!t.fontTitle) t.fontTitle = t.fontBody;
  if (!t.fontHero) t.fontHero = t.fontBody;
  if (!t.fontIcon) t.fontIcon = t.fontBody;
  if (!t.fontBody) {
    t.fontBody = io.Fonts->AddFontDefault();
    t.fontSmall = t.fontSmall ? t.fontSmall : t.fontBody;
    t.fontMedium = t.fontMedium ? t.fontMedium : t.fontBody;
    t.fontTitle = t.fontTitle ? t.fontTitle : t.fontBody;
    t.fontHero = t.fontHero ? t.fontHero : t.fontBody;
    t.fontIcon = t.fontIcon ? t.fontIcon : t.fontBody;
  }
  io.FontDefault = t.fontBody;
}

void applyStyle() {
  Theme& t = theme();
  applyMetrics(t.scale);
  if (gTheme.colors.backgroundTop == 0) buildPalette(t.colors);

  ImGuiStyle& style = ImGui::GetStyle();
  style.WindowPadding = ImVec2(0, 0);
  style.FramePadding = ImVec2(12, 8);
  style.ItemSpacing = ImVec2(8, 8);
  style.ItemInnerSpacing = ImVec2(8, 6);
  style.ScrollbarSize = 9.0f * t.scale;
  style.GrabMinSize = 10.0f * t.scale;
  style.WindowRounding = 0.0f;
  style.ChildRounding = 14.0f * t.scale;
  style.FrameRounding = 12.0f * t.scale;
  style.PopupRounding = 14.0f * t.scale;
  style.ScrollbarRounding = 9.0f * t.scale;
  style.GrabRounding = 8.0f * t.scale;
  style.TabRounding = 8.0f * t.scale;
  style.WindowBorderSize = 0.0f;
  style.ChildBorderSize = 0.0f;
  style.FrameBorderSize = 0.0f;
  style.PopupBorderSize = 0.0f;
  style.AntiAliasedLines = true;
  style.AntiAliasedLinesUseTex = true;
  style.AntiAliasedFill = true;
  style.CircleTessellationMaxError = 0.25f;
  style.WindowMenuButtonPosition = ImGuiDir_None;

  const ImVec4 transparent(0.0f, 0.0f, 0.0f, 0.0f);
  ImVec4* c = style.Colors;
  c[ImGuiCol_Text] = ImGui::ColorConvertU32ToFloat4(t.colors.text);
  c[ImGuiCol_TextDisabled] = ImGui::ColorConvertU32ToFloat4(t.colors.textTertiary);
  c[ImGuiCol_WindowBg] = transparent;
  c[ImGuiCol_ChildBg] = transparent;
  c[ImGuiCol_PopupBg] = transparent;
  c[ImGuiCol_MenuBarBg] = transparent;
  c[ImGuiCol_Border] = transparent;
  c[ImGuiCol_BorderShadow] = transparent;
  c[ImGuiCol_FrameBg] = transparent;
  c[ImGuiCol_FrameBgHovered] = transparent;
  c[ImGuiCol_FrameBgActive] = transparent;
  c[ImGuiCol_TitleBg] = transparent;
  c[ImGuiCol_TitleBgActive] = transparent;
  c[ImGuiCol_TitleBgCollapsed] = transparent;
  c[ImGuiCol_ScrollbarBg] = transparent;
  c[ImGuiCol_ScrollbarGrab] =
      ImGui::ColorConvertU32ToFloat4(withAlpha(t.colors.scrollbar, 0.55f));
  c[ImGuiCol_ScrollbarGrabHovered] =
      ImGui::ColorConvertU32ToFloat4(withAlpha(t.colors.scrollbar, 0.85f));
  c[ImGuiCol_ScrollbarGrabActive] = ImGui::ColorConvertU32ToFloat4(t.colors.scrollbar);
  c[ImGuiCol_CheckMark] = ImGui::ColorConvertU32ToFloat4(t.colors.accent);
  c[ImGuiCol_SliderGrab] = ImGui::ColorConvertU32ToFloat4(t.colors.accent);
  c[ImGuiCol_SliderGrabActive] = ImGui::ColorConvertU32ToFloat4(t.colors.accentHover);
  c[ImGuiCol_Button] = transparent;
  c[ImGuiCol_ButtonHovered] = transparent;
  c[ImGuiCol_ButtonActive] = transparent;
  c[ImGuiCol_Header] = ImGui::ColorConvertU32ToFloat4(t.colors.accentSoft);
  c[ImGuiCol_HeaderHovered] = ImGui::ColorConvertU32ToFloat4(t.colors.accentSoft);
  c[ImGuiCol_HeaderActive] = ImGui::ColorConvertU32ToFloat4(t.colors.accentSoft);
  c[ImGuiCol_Separator] = ImGui::ColorConvertU32ToFloat4(t.colors.separator);
  c[ImGuiCol_SeparatorHovered] = ImGui::ColorConvertU32ToFloat4(t.colors.accentSoft);
  c[ImGuiCol_SeparatorActive] = ImGui::ColorConvertU32ToFloat4(t.colors.accentSoft);
  c[ImGuiCol_ResizeGrip] = transparent;
  c[ImGuiCol_ResizeGripHovered] = transparent;
  c[ImGuiCol_ResizeGripActive] = transparent;
  c[ImGuiCol_Tab] = transparent;
  c[ImGuiCol_TabHovered] = transparent;
  c[ImGuiCol_TabSelected] = transparent;
  c[ImGuiCol_PlotLines] = ImGui::ColorConvertU32ToFloat4(t.colors.accent);
  c[ImGuiCol_PlotHistogram] = ImGui::ColorConvertU32ToFloat4(t.colors.accent);
  c[ImGuiCol_TextSelectedBg] =
      ImGui::ColorConvertU32ToFloat4(withAlpha(t.colors.accent, 0.30f));
  c[ImGuiCol_NavCursor] = ImGui::ColorConvertU32ToFloat4(t.colors.accent);
  c[ImGuiCol_DragDropTarget] = ImGui::ColorConvertU32ToFloat4(t.colors.accent);
}

namespace paint {

void shadow(ImDrawList* dl, const ImVec2& min, const ImVec2& max,
            float rounding, float alpha, float spread) {
  if (!dl || alpha <= 0.0f) return;
  const Theme& t = gTheme;
  const ImU32 tint = t.colors.shadowTint;
  const int steps = 6;
  for (int i = steps; i >= 1; --i) {
    const float k = static_cast<float>(i) / static_cast<float>(steps);
    const float offset = spread * k * 0.55f + 1.0f * t.scale;
    const float layerAlpha =
        alpha * 0.075f * (1.0f - k) * (1.0f - k) * 4.0f;
    dl->AddRectFilled(ImVec2(min.x - offset, min.y - offset * 0.55f + 2.0f * t.scale),
                      ImVec2(max.x + offset, max.y + offset + 2.0f * t.scale),
                      withAlpha(tint, layerAlpha), rounding + offset);
  }
}

void verticalGradient(ImDrawList* dl, const ImVec2& min, const ImVec2& max,
                      ImU32 top, ImU32 bottom) {
  if (!dl) return;
  dl->AddRectFilledMultiColor(min, max, top, top, bottom, bottom);
}

void border(ImDrawList* dl, const ImVec2& min, const ImVec2& max,
            float rounding, ImU32 color) {
  if (!dl) return;
  dl->AddRect(min, max, color, rounding, 0, 1.0f);
}

void highlight(ImDrawList* dl, const ImVec2& min, const ImVec2& max,
               float rounding, float alpha) {
  if (!dl) return;
  const float y = min.y + 0.75f;
  dl->AddLine(ImVec2(min.x + rounding * 0.7f, y),
              ImVec2(max.x - rounding * 0.7f, y),
              withAlpha(IM_COL32(255, 255, 255, 255), alpha), 1.0f);
}

void glassPanel(ImDrawList* dl, const ImVec2& min, const ImVec2& max,
                float rounding, bool strong) {
  if (!dl) return;
  const Theme& t = gTheme;
  const Palette& p = t.colors;
  shadow(dl, min, max, rounding, strong ? 0.95f : 0.75f,
         (strong ? 30.0f : 22.0f) * t.scale);
  dl->AddRectFilled(min, max, strong ? p.glassStrong : p.glass, rounding);
  // 顶部丝光：白色 → 透明 的竖向渐变。
  const ImVec2 a(min.x + 1.0f, min.y + 1.0f);
  const ImVec2 b(max.x - 1.0f, max.y - 1.0f);
  const ImU32 top = withAlpha(IM_COL32(255, 255, 255, 255), strong ? 0.55f : 0.40f);
  const ImU32 bottom = withAlpha(IM_COL32(255, 255, 255, 255), 0.02f);
  verticalGradient(dl, a, ImVec2(b.x, a.y + (b.y - a.y) * 0.62f), top, bottom);
  // 底部冷色薄雾，营造"背景透出"的玻璃感。
  const ImU32 mistTop = withAlpha(p.glassTint, 0.0f);
  const ImU32 mistBottom = withAlpha(p.glassTint, strong ? 0.55f : 0.40f);
  verticalGradient(dl, ImVec2(a.x, a.y + (b.y - a.y) * 0.55f), b, mistTop,
                   mistBottom);
  border(dl, min, max, rounding, withAlpha(p.glassBorder, strong ? 0.95f : 0.8f));
  highlight(dl, min, max, rounding, strong ? 0.9f : 0.65f);
}

void spotlight(ImDrawList* dl, const ImVec2& center, float radius, ImU32 color) {
  if (!dl || radius <= 1.0f) return;
  const float baseA = static_cast<float>((color >> IM_COL32_A_SHIFT) & 0xFF) / 255.0f;
  if (baseA <= 0.001f) return;
  const int r = static_cast<int>((color >> IM_COL32_R_SHIFT) & 0xFF);
  const int g = static_cast<int>((color >> IM_COL32_G_SHIFT) & 0xFF);
  const int b = static_cast<int>((color >> IM_COL32_B_SHIFT) & 0xFF);
  const int steps = 14;
  for (int i = 0; i < steps; ++i) {
    const float k = static_cast<float>(i) / static_cast<float>(steps);
    const float ringRadius = radius * (1.0f - k * 0.88f);
    const float alpha = baseA * (2.0f / steps) * (1.0f - k) * 0.9f;
    dl->AddCircleFilled(center, ringRadius, IM_COL32(r, g, b,
                                                     static_cast<int>(alpha * 255.0f + 0.5f)),
                        48);
  }
}

void glow(ImDrawList* dl, const ImVec2& min, const ImVec2& max, float rounding,
          ImU32 color, float spread) {
  if (!dl) return;
  const float a = static_cast<float>((color >> IM_COL32_A_SHIFT) & 0xFF) / 255.0f;
  if (a <= 0.01f) return;
  const int layers = 4;
  for (int i = layers; i >= 1; --i) {
    const float k = static_cast<float>(i) / static_cast<float>(layers);
    const float off = spread * k;
    dl->AddRect(ImVec2(min.x - off, min.y - off), ImVec2(max.x + off, max.y + off),
                withAlpha(color, a * 0.30f / static_cast<float>(layers)),
                rounding + off, 0, 2.0f * k + 1.0f);
  }
}

void hairline(ImDrawList* dl, float x0, float x1, float y, float alpha) {
  if (!dl) return;
  dl->AddLine(ImVec2(x0, y), ImVec2(x1, y),
              withAlpha(gTheme.colors.separator, alpha), 1.0f);
}

void checkmark(ImDrawList* dl, const ImVec2& center, float size, ImU32 color,
               float thickness) {
  if (!dl) return;
  const ImVec2 pts[3] = {
      ImVec2(center.x - size * 0.34f, center.y + size * 0.02f),
      ImVec2(center.x - size * 0.08f, center.y + size * 0.28f),
      ImVec2(center.x + size * 0.36f, center.y - size * 0.30f),
  };
  dl->PathLineTo(pts[0]);
  dl->PathLineTo(pts[1]);
  dl->PathLineTo(pts[2]);
  dl->PathStroke(color, 0, thickness);
  const float half = thickness * 0.5f;
  dl->AddCircleFilled(pts[0], half, color, 6);
  dl->AddCircleFilled(pts[1], half * 1.05f, color, 6);
  dl->AddCircleFilled(pts[2], half, color, 6);
}

void cross(ImDrawList* dl, const ImVec2& center, float size, ImU32 color,
           float thickness) {
  if (!dl) return;
  const float d = size * 0.32f;
  dl->AddLine(ImVec2(center.x - d, center.y - d), ImVec2(center.x + d, center.y + d),
              color, thickness);
  dl->AddLine(ImVec2(center.x + d, center.y - d), ImVec2(center.x - d, center.y + d),
              color, thickness);
  const float half = thickness * 0.5f;
  dl->AddCircleFilled(ImVec2(center.x - d, center.y - d), half, color, 6);
  dl->AddCircleFilled(ImVec2(center.x + d, center.y - d), half, color, 6);
  dl->AddCircleFilled(ImVec2(center.x + d, center.y + d), half, color, 6);
  dl->AddCircleFilled(ImVec2(center.x - d, center.y + d), half, color, 6);
}

}  // namespace paint
}  // namespace mes::ui
