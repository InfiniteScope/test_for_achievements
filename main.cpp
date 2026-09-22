// FoodExplorer - 程序入口
// 布局：顶部栏 + 左侧导航栏 + 地图区域 + 右侧详情面板 + 底部状态栏
// 交互：拖拽地图、滚轮缩放、点击标记或列表卡片选中餐厅

#include "raylib.h"
#include "raygui.h"
#include "ui/layout.h"
#include "ui/theme.h"
#include "ui/map_view.h"
#include "ui/font_manager.h"
#include "ui/widgets.h"
#include "ui/chrome.h"
#include "ui/detail_panel.h"
#include "models/restaurant.h"
#include "services/data_service.h"
#include "services/favorites_service.h"
#include "services/history_service.h"
#include "ui/win_api.h"
#include <cstdio>
#include <cmath>
#include <vector>
#include <string>

int main() {
#ifdef _WIN32
    WinApiSetDpiAware();
#endif
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_WINDOW_UNDECORATED);
    InitWindow(1400, 900, "FoodExplorer - 舟山美食智能推荐系统");
    SetWindowMinSize(layout::MIN_W, layout::MIN_H);
    SetTargetFPS(60);

    // 设置窗口/任务栏图标（从 PNG 加载）
    Image icon = LoadImage("resources/app_icon.png");
    if (icon.data != nullptr) {
        SetWindowIcon(icon);
        UnloadImage(icon);
    }

    // 加载数据服务
    if (!services::GetData().LoadAll()) {
        TraceLog(LOG_ERROR, "加载餐厅数据失败");
    }
    const std::vector<Restaurant>& restaurants = services::GetData().Restaurants();
    TraceLog(LOG_INFO, "已加载 %d 家餐厅", (int)restaurants.size());

    services::GetFavorites().Load(restaurants);
    services::GetHistory().Load();

    // 加载中文字体（必须在 InitWindow 之后）
    fontmgr::Load(32);
    fontmgr::SetScale(1.0f); // 初始字体大小 100%
    
    // 设置 raygui 使用我们的中文字体
    GuiSetFont(fontmgr::Get());

    // 初始化外框（导航栏状态）
    chrome::Init();
    chrome::SetStatus("就绪");
    detail_panel::Init();

    // 加载用户主题偏好
    theme::Load();

    // 地图视图大小适配左侧导航栏和右侧面板之间的区域
    MapView map(layout::MapW(), layout::MapH());
    map.SetOrigin(layout::MapX(), layout::MapY());
    map.SetCenter(30.0100, 122.1050);
    map.SetZoom(11.0);

    int hoveredId  = -1;
    int selectedId = -1;
    NavPage lastPage = chrome::CurrentPage();

    // 压入初始历史状态
    services::GetHistory().Push({lastPage, selectedId, ""});

    while (!WindowShouldClose()) {
        // 处理窗口缩放
        map.Resize(layout::MapW(), layout::MapH());
        map.SetOrigin(layout::MapX(), layout::MapY());

        // 更新
        map.Update();
        chrome::Update();

        // 自定义标题栏按钮
        chrome::TitleBarAction tbAction = chrome::UpdateTitleBar();
        void* hwnd = GetWindowHandle();
        switch (tbAction) {
            case chrome::TitleBarAction::Minimize:
                WinApiMinimizeWindow(hwnd);
                break;
            case chrome::TitleBarAction::Maximize:
                WinApiMaximizeWindow(hwnd);
                break;
            case chrome::TitleBarAction::Close:
                WinApiCloseWindow(hwnd);
                break;
            default:
                break;
        }

        // 窗口边缘调整大小（无边框窗口需要手动处理）
        bool resizingPanel = false;
#ifdef _WIN32
        {
            int border = 5;
            int sw = GetScreenWidth();
            int sh = GetScreenHeight();
            Vector2 m = GetMousePosition();
            bool onLeft = m.x < border;
            bool onRight = m.x > sw - border;
            bool onTop = m.y < border;
            bool onBottom = m.y > sh - border;

            // 标题栏区域不触发边缘调整
            bool onTitleBar = (m.y < layout::TITLE_BAR_H);

            int ht = 0;
            MouseCursor cursor = MOUSE_CURSOR_DEFAULT;
            if (!onTitleBar) {
                if (onLeft && onTop) { ht = HTTOPLEFT; cursor = MOUSE_CURSOR_RESIZE_NWSE; }
                else if (onRight && onTop) { ht = HTTOPRIGHT; cursor = MOUSE_CURSOR_RESIZE_NESW; }
                else if (onLeft && onBottom) { ht = HTBOTTOMLEFT; cursor = MOUSE_CURSOR_RESIZE_NESW; }
                else if (onRight && onBottom) { ht = HTBOTTOMRIGHT; cursor = MOUSE_CURSOR_RESIZE_NWSE; }
                else if (onLeft) { ht = HTLEFT; cursor = MOUSE_CURSOR_RESIZE_EW; }
                else if (onRight) { ht = HTRIGHT; cursor = MOUSE_CURSOR_RESIZE_EW; }
                else if (onBottom) { ht = HTBOTTOM; cursor = MOUSE_CURSOR_RESIZE_NS; }
            }

            resizingPanel = layout::UpdateRightPanelResize();
            if (ht != 0) {
                SetMouseCursor(cursor);
                if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                    WinApiResizeWindow(hwnd, ht);
                }
            } else if (layout::IsHoveringRightPanelEdge() || resizingPanel) {
                SetMouseCursor(MOUSE_CURSOR_RESIZE_EW);
            } else {
                SetMouseCursor(MOUSE_CURSOR_DEFAULT);
            }
        }
#else
        resizingPanel = layout::UpdateRightPanelResize();
        if (layout::IsHoveringRightPanelEdge() || resizingPanel) {
            SetMouseCursor(MOUSE_CURSOR_RESIZE_EW);
        } else {
            SetMouseCursor(MOUSE_CURSOR_DEFAULT);
        }
#endif

        // 检测地图标记悬停（仅限光标在地图区域内时）
        Vector2 mouse = GetMousePosition();
        hoveredId = -1;
        bool mouseOnMap = (mouse.x >= layout::MapX() && mouse.x < layout::MapX() + layout::MapW()
                        && mouse.y >= layout::MapY() && mouse.y < layout::MapY() + layout::MapH());
        if (mouseOnMap) {
            for (const auto &r : restaurants) {
                Vector2 pos = map.LatLngToScreen(r.lat, r.lng);
                float dx = mouse.x - pos.x;
                float dy = mouse.y - pos.y;
                if (dx * dx + dy * dy < 16 * 16) {
                    hoveredId = r.id;
                    break;
                }
            }
        }

        // 点击地图标记选中门店，或点击空白处设置"我的位置"
        if (mouseOnMap && IsMouseButtonReleased(MOUSE_BUTTON_LEFT) && !map.DidJustDrag()) {
            if (hoveredId >= 0) {
                selectedId = hoveredId;
                chrome::SetStatus("已选中门店");
            } else {
                // 将用户位置设为点击的地图点
                double lat, lng;
                map.ScreenToLatLng(mouse, lat, lng);
                map.SetUserLocation(lat, lng);
                chrome::SetStatus("已设置我的位置");
            }
        }

        // 详情面板交互（列表点击、滚动、返回按钮、撤销）
        detail_panel::PanelAction action = detail_panel::Update(restaurants, selectedId, hoveredId);
        if (action.requestUndo) {
            auto state = services::GetHistory().Undo();
            chrome::SetPage(state.page);
            selectedId = state.selectedId;
            chrome::SetStatus("撤销上一步");
        } else if (action.selectedId == -2) {
            selectedId = -1;
            detail_panel::Reset();
            chrome::SetStatus("返回列表");
        } else if (action.selectedId >= 0) {
            selectedId = action.selectedId;
            chrome::SetStatus("已选中门店");
        }

        // 页面或选择变化时压入历史状态
        NavPage curPage = chrome::CurrentPage();
        if (curPage != lastPage || (selectedId >= 0 && selectedId != services::GetHistory().Current().selectedId)) {
            services::GetHistory().Push({curPage, selectedId, ""});
            lastPage = curPage;
        }

        // 绘制
        BeginDrawing();
        ClearBackground(theme::Current().bg_dark);

        // 地图区域背景（海洋色底图，避免缺失瓦片时显示深色 UI 颜色）
        DrawRectangle(layout::MapX(), layout::MapY(), layout::MapW(), layout::MapH(), theme::Current().map_base);

        // 瓦片
        map.Draw();

        // 细微暗色调叠加层，使高德瓦片与 UI 主题融合
        DrawRectangle(layout::MapX(), layout::MapY(), layout::MapW(), layout::MapH(), theme::Current().map_overlay);

        // 餐厅标记 - 图钉绘制（底层）
        BeginScissorMode(layout::MapX(), layout::MapY(), layout::MapW(), layout::MapH());
        for (const auto &r : restaurants) {
            Vector2 pos = map.LatLngToScreen(r.lat, r.lng);
            if (pos.x < layout::MapX() - 40 || pos.x > layout::MapX() + layout::MapW() + 40
             || pos.y < layout::MapY() - 50 || pos.y > layout::MapY() + layout::MapH() + 20)
                continue;

            bool isSel = (r.id == selectedId);
            bool isHov = (r.id == hoveredId);

            if (isHov || isSel) {
                DrawCircleV(pos, 18.0f, theme::Current().accent_glow);
            }

            float radius = isSel ? 12.0f : (isHov ? 11.0f : 8.5f);
            Color fill = isSel ? theme::Current().marker_sel : (isHov ? theme::Current().marker_hov : theme::Current().marker);
            widgets::DrawMapPin(pos.x, pos.y, radius, fill, theme::Current().bg_dark, theme::Current().bg_dark);
        }
        EndScissorMode();

        // 用户位置标记（中层：图钉之上，店名之下）
        if (map.HasUserLocation()) {
            double uLat, uLng;
            map.GetUserLocation(uLat, uLng);
            Vector2 up = map.LatLngToScreen(uLat, uLng);
            if (up.x >= layout::MapX() - 20 && up.x <= layout::MapX() + layout::MapW() + 20
             && up.y >= layout::MapY() - 20 && up.y <= layout::MapY() + layout::MapH() + 20) {
                float pulse = 14.0f + 2.0f * sinf((float)GetTime() * 3.0f);
                DrawCircleV(up, pulse, theme::Current().accent_glow);
                DrawCircleV(up, 7.0f, theme::Current().user_pos);
                DrawCircleLines((int)up.x, (int)up.y, 11, theme::Current().text_primary);
                widgets::DrawStr("我的位置", (int)up.x + 14, (int)up.y - 8, 12, theme::Current().text_secondary);
            }
        }

        // 当前路线折线叠加层（路线页面或导航覆盖层打开时显示）
        if (chrome::CurrentPage() == NavPage::Route || detail_panel::IsRouteOverlayActive()) {
            const auto& polyline = detail_panel::CurrentRoutePolyline();
            if (polyline.size() >= 2) {
                map.DrawRoute(polyline, theme::Current().accent, 4.0f);
            }
        }

        // 店名（最顶层，只显示悬停/选中的）
        BeginScissorMode(layout::MapX(), layout::MapY(), layout::MapW(), layout::MapH());
        for (const auto &r : restaurants) {
            Vector2 pos = map.LatLngToScreen(r.lat, r.lng);
            if (pos.x < layout::MapX() - 40 || pos.x > layout::MapX() + layout::MapW() + 40
             || pos.y < layout::MapY() - 50 || pos.y > layout::MapY() + layout::MapH() + 20)
                continue;

            bool isSel = (r.id == selectedId);
            bool isHov = (r.id == hoveredId);
            if (!isHov && !isSel) continue;

            float radius = isSel ? 12.0f : 11.0f;
            const char* name = r.name.c_str();
            int tw = widgets::TextWidth(name, 13) + 12;
            Rectangle lbl = {pos.x - tw / 2.0f, pos.y - radius - 28, (float)tw, 18};
            DrawRectangleRounded(lbl, 0.3f, 4, theme::Current().bg_overlay);
            widgets::DrawStrCenter(name, (int)lbl.x, (int)lbl.y, (int)lbl.width, (int)lbl.height,
                                   13, theme::Current().text_primary);
        }
        EndScissorMode();

        // 地图区域边框
        DrawRectangleLines(layout::MapX(), layout::MapY(), layout::MapW(), layout::MapH(), theme::Current().border);

        // 缩放指示器（地图区域左下角）
        {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "缩放 %.1f", map.GetZoom());
            Rectangle bg = {(float)layout::MapX() + 10, (float)(layout::MapY() + layout::MapH() - 30), 90, 22};
            DrawRectangleRounded(bg, 0.3f, 4, theme::Current().bg_overlay);
            widgets::DrawStr(buf, (int)bg.x + 8, (int)bg.y + 4, 12, theme::Current().text_secondary);
        }

        // 外框（标题栏、顶部、左侧、底部）
        chrome::DrawTitleBar();
        chrome::DrawTop();
        chrome::DrawLeft();

        const char* hint = (selectedId >= 0)
            ? "点击其他标记或列表项切换门店  |  点击空白处设置我的位置  |  滚轮缩放  |  拖拽平移  |  Ctrl+Z 撤销"
            : "点击地图标记查看门店，点击空白处设置我的位置  |  滚轮缩放  |  拖拽平移  |  Ctrl+Z 撤销";
        chrome::DrawBottom(hint);

        // 右侧详情面板
        detail_panel::Draw(restaurants, selectedId, hoveredId, map);

        // 右侧面板拖拽调整大小手柄（最顶层绘制）
        {
            int edgeX = layout::RightX();
            Color handleCol = layout::IsHoveringRightPanelEdge() || resizingPanel
                ? theme::Current().accent
                : theme::Current().border;
            DrawLineEx({(float)edgeX, (float)layout::RightY()},
                       {(float)edgeX, (float)(layout::RightY() + layout::RightH())},
                       layout::IsHoveringRightPanelEdge() || resizingPanel ? 3.0f : 1.0f, handleCol);
        }

        EndDrawing();
    }

    services::GetFavorites().Save();
    services::GetHistory().Save();
    theme::Save();

    detail_panel::Cleanup();
    fontmgr::Unload();
    CloseWindow();
    return 0;
}

