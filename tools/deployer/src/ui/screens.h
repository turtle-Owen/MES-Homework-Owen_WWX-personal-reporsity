// 四个页面的绘制入口（使用 app.h 的 AppState）。
#pragma once

namespace mes { struct AppState; }

namespace mes::ui {

void drawBackground();
void drawTopBar(mes::AppState& state);
void drawWelcome(mes::AppState& state);
void drawConfigure(mes::AppState& state);
void drawInstalling(mes::AppState& state);
void drawResult(mes::AppState& state);
void drawAbout(mes::AppState& state, bool asModal);
void drawFooter(mes::AppState& state);
void drawToast(mes::AppState& state);

}  // namespace mes::ui
