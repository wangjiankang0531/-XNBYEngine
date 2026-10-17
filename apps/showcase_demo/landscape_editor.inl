// Windows editor shell around the same Vulkan landscape renderer.
namespace {
  std::wstring editorWide(const std::string& text) {
    int n = MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0);
    std::wstring out(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), out.data(), n);
    return out;
  }
  std::string editorUtf8(const std::wstring& text) {
    int n = WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0, nullptr, nullptr);
    std::string out(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), out.data(), n, nullptr, nullptr);
    return out;
  }
} // namespace
bool ShowcaseInteractiveApp::initEditorWindow() {
  hinstance_ = GetModuleHandleW(nullptr);
  POINT cursor{};
  GetCursorPos(&cursor);
  MONITORINFO monitor{sizeof(monitor)};
  GetMonitorInfoW(MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST), &monitor);
  const RECT work = monitor.rcWork;
  editorSurfaceBrush_ = CreateSolidBrush(RGB(243, 246, 250));
  editorInputBrush_ = CreateSolidBrush(RGB(255, 255, 255));
  editorChromeBrush_ = CreateSolidBrush(RGB(20, 36, 57));
  WNDCLASSW frame{};
  frame.lpfnWndProc = editorFrameProc;
  frame.hInstance = hinstance_;
  frame.lpszClassName = L"XNBYLandscapeFrame";
  frame.hCursor = LoadCursor(nullptr, IDC_ARROW);
  frame.hbrBackground = editorChromeBrush_;
  RegisterClassW(&frame);
  editorFrame_ = CreateWindowExW(0,
                                 frame.lpszClassName,
                                 L"信能本源 · 驻马店场景编辑器",
                                 WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                                 work.left,
                                 work.top,
                                 work.right - work.left,
                                 work.bottom - work.top,
                                 nullptr,
                                 nullptr,
                                 hinstance_,
                                 this);
  if (!editorFrame_)
    return false;
  editorDpi_ = int(GetDpiForWindow(editorFrame_));
  editorFont_ = CreateFontW(-MulDiv(13, editorDpi_, 96),
                            0,
                            0,
                            0,
                            FW_NORMAL,
                            FALSE,
                            FALSE,
                            FALSE,
                            DEFAULT_CHARSET,
                            OUT_DEFAULT_PRECIS,
                            CLIP_DEFAULT_PRECIS,
                            CLEARTYPE_QUALITY,
                            DEFAULT_PITCH,
                            L"Microsoft YaHei UI");
  editorHeadingFont_ = CreateFontW(-MulDiv(16, editorDpi_, 96),
                                   0,
                                   0,
                                   0,
                                   FW_SEMIBOLD,
                                   FALSE,
                                   FALSE,
                                   FALSE,
                                   DEFAULT_CHARSET,
                                   OUT_DEFAULT_PRECIS,
                                   CLIP_DEFAULT_PRECIS,
                                   CLEARTYPE_QUALITY,
                                   DEFAULT_PITCH,
                                   L"Microsoft YaHei UI");
  for (auto item : {std::pair{3200, L"隐藏编辑面板  F9"}, std::pair{3201, L"停靠左侧"}, std::pair{3202, L"停靠右侧"}}) {
    HWND control = CreateWindowExW(0,
                                   L"BUTTON",
                                   item.second,
                                   WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                                   0,
                                   0,
                                   1,
                                   1,
                                   editorFrame_,
                                   reinterpret_cast<HMENU>(INT_PTR(item.first)),
                                   hinstance_,
                                   nullptr);
    SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(editorFont_), TRUE);
    editorControls_[item.first] = control;
  }
  WNDCLASSW render{};
  render.lpfnWndProc = windowProc;
  render.hInstance = hinstance_;
  render.lpszClassName = L"XNBYLandscapeViewport";
  render.hCursor = LoadCursor(nullptr, IDC_ARROW);
  RegisterClassW(&render);
  hwnd_ = CreateWindowExW(
      0, render.lpszClassName, L"三维地景", WS_CHILD | WS_VISIBLE, 0, 0, 1, 1, editorFrame_, nullptr, hinstance_, this);
  ShowWindow(editorFrame_, SW_SHOW);
  ShowWindow(editorFrame_, SW_MAXIMIZE);
  editorLayout();
  return hwnd_ != nullptr;
}

// Regression probe uses the editor's own commands and checks the live Vulkan target.
// It never writes user layers or sends input to other programs.
void ShowcaseInteractiveApp::editorShellProbeStep(int frame) {
  if (frame % 8)
    return;
  static Json records = Json::array();
  static const Json originalDocument = editorDocument_;
  static const Json originalDraft = editorDraft_;
  const size_t stage = records.size();
  RECT client{}, viewport{}, panel{}, window{};
  GetClientRect(editorFrame_, &client);
  GetWindowRect(hwnd_, &viewport);
  MapWindowPoints(nullptr, editorFrame_, reinterpret_cast<POINT*>(&viewport), 2);
  GetWindowRect(controlsHwnd_, &panel);
  MapWindowPoints(nullptr, editorFrame_, reinterpret_cast<POINT*>(&panel), 2);
  GetWindowRect(editorFrame_, &window);
  MONITORINFO monitor{sizeof(monitor)};
  GetMonitorInfoW(MonitorFromWindow(editorFrame_, MONITOR_DEFAULTTONEAREST), &monitor);
  const auto extent = device_->getSwapchainExtent();
  if (extent.width != UINT(viewport.right - viewport.left) || extent.height != UINT(viewport.bottom - viewport.top) ||
      framebuffers_.size() != device_->getSwapchainImageCount() || editorDocument_ != originalDocument ||
      editorDraft_ != originalDraft || bool(IsWindowVisible(controlsHwnd_)) != editorPanelVisible_)
    throw std::runtime_error("Editor shell target/document regression");
  if (stage == 0 && !IsZoomed(editorFrame_))
    throw std::runtime_error("Editor did not start maximized");
  if ((!editorPanelVisible_ && (viewport.left != 0 || viewport.right != client.right)) ||
      (editorPanelVisible_ && editorDockRight_ && (viewport.left != 0 || panel.right != client.right)) ||
      (editorPanelVisible_ && !editorDockRight_ && (panel.left != 0 || viewport.left != panel.right)))
    throw std::runtime_error("Editor dock coverage regression");
  device_->waitIdle();
  std::vector<uint8_t> pixels;
  uint32_t width = 0, height = 0;
  if (!device_->readCapturedFrame(pixels, width, height) || width != extent.width || height != extent.height)
    throw std::runtime_error("Editor resized frame capture failed");
  size_t bright = 0;
  for (size_t i = 0; i + 3 < pixels.size(); i += 4)
    bright += int(pixels[i]) + int(pixels[i + 1]) + int(pixels[i + 2]) > 80;
  if (bright < size_t(width) * height / 2)
    throw std::runtime_error("Editor resized scene became blank");
  const char* names[] = {"startup_maximized",
                         "panel_hidden",
                         "panel_left",
                         "panel_right",
                         "window_1366x768",
                         "window_1024x640",
                         "panel_scrolled",
                         "restored_maximized"};
  const auto screenshot = fs::path(cli_.stats).parent_path() / (std::string(names[stage]) + ".png");
  stbi_write_png(screenshot.string().c_str(), int(width), int(height), 4, pixels.data(), int(width * 4));
  records.push_back(
      {{"state", names[stage]},
       {"client", {client.right, client.bottom}},
       {"viewport", {viewport.left, viewport.top, viewport.right, viewport.bottom}},
       {"swapchain", {width, height}},
       {"panel_visible", editorPanelVisible_},
       {"dock_right", editorDockRight_},
       {"scroll", editorScroll_},
       {"dpi", editorDpi_},
       {"maximized", bool(IsZoomed(editorFrame_))},
       {"work_area", {monitor.rcWork.left, monitor.rcWork.top, monitor.rcWork.right, monitor.rcWork.bottom}},
       {"bright_pixels", bright}});
  if (stage == 0)
    editorShellCommand(3200);
  else if (stage == 1)
    editorShellCommand(3201);
  else if (stage == 2)
    editorShellCommand(3202);
  else if (stage == 3 || stage == 4) {
    ShowWindow(editorFrame_, SW_RESTORE);
    const int w = stage == 3 ? 1366 : 1024, h = stage == 3 ? 768 : 640;
    SetWindowPos(editorFrame_, nullptr, monitor.rcWork.left + 20, monitor.rcWork.top + 20, w, h, SWP_NOZORDER);
  } else if (stage == 5) {
    editorScroll_ = 10000;
    editorPanelLayout();
  } else if (stage == 6) {
    editorScroll_ = 0;
    editorShellCommand(3201);
    ShowWindow(editorFrame_, SW_MAXIMIZE);
  } else {
    std::ofstream output(cli_.stats);
    output << Json{
        {"passed", true},
        {"document_unchanged", true},
        {"draft_unchanged", true},
        {"states", records},
        {"input_injection",
         false}}.dump(2);
    running_ = false;
  }
}

void ShowcaseInteractiveApp::editorLayout() {
  if (!editorFrame_ || IsIconic(editorFrame_))
    return;
  RECT r{};
  GetClientRect(editorFrame_, &r);
  const auto px = [&](int v) {
    return MulDiv(v, editorDpi_, 96);
  };
  const int chrome = px(52), panel = editorPanelVisible_ ? px(438) : 0;
  const int width = std::max(1L, r.right - panel), height = std::max(1L, r.bottom - chrome);
  if (windowWidth_ != width || windowHeight_ != height)
    editorResizePending_ = true;
  windowWidth_ = width;
  windowHeight_ = height;
  if (hwnd_)
    MoveWindow(hwnd_, editorPanelVisible_ && !editorDockRight_ ? panel : 0, chrome, width, height, TRUE);
  if (controlsHwnd_) {
    ShowWindow(controlsHwnd_, editorPanelVisible_ ? SW_SHOW : SW_HIDE);
    MoveWindow(controlsHwnd_, editorDockRight_ ? r.right - panel : 0, chrome, px(438), height, TRUE);
    editorPanelLayout();
  }
  int x = r.right - px(362);
  for (int id : {3200, 3201, 3202}) {
    const int w = id == 3200 ? 158 : 88;
    if (editorControls_.contains(id))
      MoveWindow(editorControls_[id], x, px(10), px(w), px(32), TRUE);
    x += px(w + 6);
  }
  InvalidateRect(editorFrame_, nullptr, FALSE);
}

void ShowcaseInteractiveApp::editorPanelLayout() {
  if (!controlsHwnd_)
    return;
  RECT client{};
  GetClientRect(controlsHwnd_, &client);
  const auto px = [&](int v) {
    return MulDiv(v, editorDpi_, 96);
  };
  const int available = std::max(1L, client.bottom - px(48));
  const int content = px(932);
  editorScroll_ = std::clamp(editorScroll_, 0, std::max(0, content - available));
  SCROLLINFO info{sizeof(info), SIF_RANGE | SIF_PAGE | SIF_POS};
  info.nMin = 0;
  info.nMax = content - 1;
  info.nPage = UINT(available);
  info.nPos = editorScroll_;
  SetScrollInfo(controlsHwnd_, SB_VERT, &info, TRUE);
  // Clip scrolling property controls below the persistent panel grip.
  for (const auto& [id, base] : editorControlRects_) {
    HWND control = editorControls_[id];
    const int top = px(base.top) - editorScroll_, h = px(base.bottom - base.top);
    MoveWindow(control, px(base.left), top, px(base.right - base.left), h, TRUE);
    HRGN region = CreateRectRgn(0, std::max(0, px(48) - top), px(base.right - base.left), h);
    if (!SetWindowRgn(control, region, TRUE))
      DeleteObject(region);
  }
  for (int id : {3192, 3193, 3191}) {
    const int x = id == 3192 ? 234 : id == 3193 ? 280 : 326;
    MoveWindow(editorControls_[id], px(x), px(10), px(id == 3191 ? 66 : 40), px(28), TRUE);
    SetWindowPos(editorControls_[id], HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
  }
  InvalidateRect(controlsHwnd_, nullptr, TRUE);
}

void ShowcaseInteractiveApp::editorShellCommand(int id) {
  if (id == 3200 || id == 3191)
    editorPanelVisible_ = !editorPanelVisible_;
  else if (id == 3201 || id == 3192) {
    editorDockRight_ = false;
    editorPanelVisible_ = true;
  } else if (id == 3202 || id == 3193) {
    editorDockRight_ = true;
    editorPanelVisible_ = true;
  } else
    return;
  SetWindowTextW(editorControls_[3200], editorPanelVisible_ ? L"隐藏编辑面板  F9" : L"显示编辑面板  F9");
  // Finish typing/focus transitions before moving the view. Draft/document state is preserved.
  editorLayout();
}

bool ShowcaseInteractiveApp::editorResizeViewport() {
  if (!editorResizePending_)
    return true;
  device_->waitIdle();
  commandBuffer_->reset();
  framebuffers_.clear();
  depthView_.reset();
  depthTexture_.reset();
  const auto format = device_->getSwapchainFormat();
  if (!device_->resizeSwapchain({uint32_t(windowWidth_), uint32_t(windowHeight_)}) ||
      device_->getSwapchainFormat() != format || !createDepth() || !createFramebuffers()) {
    MessageBoxW(editorFrame_, L"三维视口尺寸更新失败，请重新启动编辑器。", L"信能本源", MB_OK | MB_ICONERROR);
    running_ = false;
    return false;
  }
  const auto extent = device_->getSwapchainExtent();
  windowWidth_ = int(extent.width);
  windowHeight_ = int(extent.height);
  // Presentation semaphores are renewed with the swapchain; the frame fence stays signalled.
  imageAvailable_ = device_->createSemaphore();
  renderFinished_ = device_->createSemaphore();
  editorResizePending_ = false;
  return imageAvailable_ && renderFinished_;
}

bool ShowcaseInteractiveApp::editorThemeMessage(HWND hwnd, UINT message, WPARAM w, LPARAM l, LRESULT& result) {
  const auto px = [&](int v) {
    return MulDiv(v, editorDpi_, 96);
  };
  result = 0;
  if (message == WM_COMMAND && LOWORD(w) >= 3191 && LOWORD(w) <= 3202) {
    editorShellCommand(LOWORD(w));
    return true;
  }
  if (message == WM_CTLCOLORSTATIC || message == WM_CTLCOLOREDIT || message == WM_CTLCOLORLISTBOX) {
    HDC dc = reinterpret_cast<HDC>(w);
    const bool input = message != WM_CTLCOLORSTATIC;
    SetTextColor(dc, RGB(35, 53, 74));
    SetBkColor(dc, input ? RGB(255, 255, 255) : RGB(243, 246, 250));
    result = reinterpret_cast<LRESULT>(input ? editorInputBrush_ : editorSurfaceBrush_);
    return true;
  }
  if (message == WM_DRAWITEM) {
    auto item = reinterpret_cast<DRAWITEMSTRUCT*>(l);
    if (item->CtlType != ODT_BUTTON)
      return false;
    const bool primary = item->CtlID == 3002 || item->CtlID == 3011 || item->CtlID == 3061;
    const bool chrome = item->CtlID >= 3191;
    const bool disabled = item->itemState & ODS_DISABLED, pressed = item->itemState & ODS_SELECTED;
    COLORREF fill = disabled  ? RGB(224, 229, 235)
                    : primary ? (pressed ? RGB(9, 113, 134) : RGB(15, 143, 163))
                    : chrome  ? RGB(38, 60, 84)
                              : (pressed ? RGB(218, 230, 241) : RGB(255, 255, 255));
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, chrome ? RGB(69, 91, 113) : RGB(204, 217, 229));
    auto oldBrush = SelectObject(item->hDC, brush), oldPen = SelectObject(item->hDC, pen);
    RECT r = item->rcItem;
    FillRect(item->hDC, &r, chrome ? editorChromeBrush_ : editorSurfaceBrush_);
    RoundRect(item->hDC, r.left, r.top, r.right - 1, r.bottom - 1, px(8), px(8));
    wchar_t text[128]{};
    GetWindowTextW(item->hwndItem, text, 128);
    SetBkMode(item->hDC, TRANSPARENT);
    SetTextColor(item->hDC, disabled ? RGB(133, 148, 163) : primary || chrome ? RGB(246, 251, 255) : RGB(35, 53, 74));
    auto font = SelectObject(item->hDC, editorFont_);
    DrawTextW(item->hDC, text, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    if (item->itemState & ODS_FOCUS) {
      InflateRect(&r, -px(3), -px(3));
      DrawFocusRect(item->hDC, &r);
    }
    SelectObject(item->hDC, font);
    SelectObject(item->hDC, oldBrush);
    SelectObject(item->hDC, oldPen);
    DeleteObject(brush);
    DeleteObject(pen);
    return true;
  }
  if (hwnd == controlsHwnd_) {
    if (message == WM_VSCROLL || message == WM_MOUSEWHEEL) {
      if (message == WM_MOUSEWHEEL)
        editorScroll_ -= GET_WHEEL_DELTA_WPARAM(w) * px(60) / WHEEL_DELTA;
      else {
        SCROLLINFO info{sizeof(info), SIF_ALL};
        GetScrollInfo(hwnd, SB_VERT, &info);
        switch (LOWORD(w)) {
          case SB_LINEUP:
            editorScroll_ -= px(32);
            break;
          case SB_LINEDOWN:
            editorScroll_ += px(32);
            break;
          case SB_PAGEUP:
            editorScroll_ -= int(info.nPage);
            break;
          case SB_PAGEDOWN:
            editorScroll_ += int(info.nPage);
            break;
          case SB_THUMBTRACK:
          case SB_THUMBPOSITION:
            editorScroll_ = info.nTrackPos;
            break;
          case SB_TOP:
            editorScroll_ = 0;
            break;
          case SB_BOTTOM:
            editorScroll_ = info.nMax;
            break;
        }
      }
      editorPanelLayout();
      return true;
    }
    if (message == WM_LBUTTONDOWN && int(short(HIWORD(l))) < px(48)) {
      editorPanelDragging_ = true;
      SetCapture(hwnd);
      return true;
    }
    if (message == WM_LBUTTONUP && editorPanelDragging_) {
      editorPanelDragging_ = false;
      ReleaseCapture();
      POINT cursor{};
      GetCursorPos(&cursor);
      ScreenToClient(editorFrame_, &cursor);
      RECT frame{};
      GetClientRect(editorFrame_, &frame);
      if (cursor.x < px(160))
        editorShellCommand(3201);
      else if (cursor.x > frame.right - px(160))
        editorShellCommand(3202);
      return true;
    }
    if (message == WM_CAPTURECHANGED)
      editorPanelDragging_ = false;
  }
  if (message == WM_PAINT) {
    PAINTSTRUCT paint{};
    HDC dc = BeginPaint(hwnd, &paint);
    RECT client{};
    GetClientRect(hwnd, &client);
    const bool frame = hwnd == editorFrame_;
    FillRect(dc, &client, frame ? editorChromeBrush_ : editorSurfaceBrush_);
    RECT header{0, 0, client.right, px(frame ? 52 : 48)};
    FillRect(dc, &header, editorChromeBrush_);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, frame ? RGB(226, 243, 251) : RGB(166, 194, 217));
    auto font = SelectObject(dc, frame ? editorHeadingFont_ : editorFont_);
    header.left = px(16);
    header.right = frame ? std::max<LONG>(px(16), client.right - px(374)) : px(228);
    DrawTextW(dc,
              frame ? L"XNBY  /  驻马店风险防控" : L"编辑面板 · 拖动吸附边缘",
              -1,
              &header,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    SelectObject(dc, font);
    EndPaint(hwnd, &paint);
    return true;
  }
  return false;
}

LRESULT CALLBACK ShowcaseInteractiveApp::editorFrameProc(HWND hwnd, UINT message, WPARAM w, LPARAM l) {
  auto app = reinterpret_cast<ShowcaseInteractiveApp*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (message == WM_NCCREATE) {
    auto c = reinterpret_cast<CREATESTRUCTW*>(l);
    app = static_cast<ShowcaseInteractiveApp*>(c->lpCreateParams);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
  }
  if (app) {
    LRESULT themed = 0;
    if (app->editorThemeMessage(hwnd, message, w, l, themed))
      return themed;
    if (message == WM_SIZE) {
      if (w != SIZE_MINIMIZED)
        app->editorLayout();
      return 0;
    }
    if (message == WM_GETMINMAXINFO) {
      auto size = reinterpret_cast<MINMAXINFO*>(l);
      size->ptMinTrackSize = {MulDiv(800, app->editorDpi_, 96), MulDiv(480, app->editorDpi_, 96)};
      return 0;
    }
    if (message == WM_DPICHANGED) {
      app->editorDpi_ = HIWORD(w);
      const auto suggested = reinterpret_cast<RECT*>(l);
      auto font = app->editorFont_, heading = app->editorHeadingFont_;
      app->editorFont_ = CreateFontW(-MulDiv(13, app->editorDpi_, 96),
                                     0,
                                     0,
                                     0,
                                     FW_NORMAL,
                                     FALSE,
                                     FALSE,
                                     FALSE,
                                     DEFAULT_CHARSET,
                                     0,
                                     0,
                                     CLEARTYPE_QUALITY,
                                     0,
                                     L"Microsoft YaHei UI");
      app->editorHeadingFont_ = CreateFontW(-MulDiv(16, app->editorDpi_, 96),
                                            0,
                                            0,
                                            0,
                                            FW_SEMIBOLD,
                                            FALSE,
                                            FALSE,
                                            FALSE,
                                            DEFAULT_CHARSET,
                                            0,
                                            0,
                                            CLEARTYPE_QUALITY,
                                            0,
                                            L"Microsoft YaHei UI");
      for (const auto& [id, control] : app->editorControls_)
        SendMessageW(control,
                     WM_SETFONT,
                     reinterpret_cast<WPARAM>(id == 3100 ? app->editorHeadingFont_ : app->editorFont_),
                     TRUE);
      if (font)
        DeleteObject(font);
      if (heading)
        DeleteObject(heading);
      SetWindowPos(hwnd,
                   nullptr,
                   suggested->left,
                   suggested->top,
                   suggested->right - suggested->left,
                   suggested->bottom - suggested->top,
                   SWP_NOZORDER | SWP_NOACTIVATE);
      app->editorLayout();
      return 0;
    }
    if (message == WM_CLOSE) {
      if (app->editorClose()) {
        app->running_ = false;
        DestroyWindow(hwnd);
      }
      return 0;
    }
    if (message == WM_DESTROY) {
      app->editorFrame_ = nullptr;
      app->running_ = false;
      PostQuitMessage(0);
      return 0;
    }
  }
  return DefWindowProcW(hwnd, message, w, l);
}

#include "landscape_editor_panel.inl"
void ShowcaseInteractiveApp::editorCheckpoint() {
  editorUndo_.push_back(editorDocument_);
  if (editorUndo_.size() > 100)
    editorUndo_.erase(editorUndo_.begin());
  editorRedo_.clear();
  editorDirty_ = true;
}
Json ShowcaseInteractiveApp::editorStyle() {
  auto text = [&](int id) {
    wchar_t buffer[1024]{};
    GetWindowTextW(editorControls_[id], buffer, 1024);
    return editorUtf8(buffer);
  };
  Json s = hongtu::landscape::defaultStyle();
  const char* names[] = {"pin", "circle", "square", "star", "warning", "custom"};
  int index = int(SendMessageW(editorControls_[3051], CB_GETCURSEL, 0, 0));
  s["icon"] = names[std::clamp(index, 0, 5)];
  if (index == 5) {
    if (editorDraft_["style"].contains("iconData"))
      s["iconData"] = editorDraft_["style"]["iconData"];
    else if (editorFeature_ >= 0)
      s["iconData"] = editorDocument_["layers"][editorLayer_]["features"][editorFeature_]["style"].at("iconData");
    else
      throw std::runtime_error("请先选择自定义 PNG");
  }
  const char* patterns[] = {"solid", "dash", "dot"};
  s["pattern"] = patterns[std::clamp(int(SendMessageW(editorControls_[3054], CB_GETCURSEL, 0, 0)), 0, 2)];
  for (auto [id, key] : std::initializer_list<std::pair<int, const char*>>{
           {3053, "sizePx"}, {3055, "widthPx"}, {3057, "opacity"}, {3059, "fillOpacity"}}) {
    auto t = text(id);
    size_t consumed = 0;
    double value = std::stod(t, &consumed);
    if (consumed != t.size())
      throw std::runtime_error("数值格式不正确");
    s[key] = value;
  }
  s["color"] = text(3056);
  s["fillColor"] = text(3058);
  return s;
}
bool ShowcaseInteractiveApp::editorClose() {
  if (editorStylePending_) {
    KillTimer(controlsHwnd_, 3180);
    editorStylePending_ = false;
    editorLiveStyle();
  }
  if (!editorDirty_)
    return true;
  int choice =
      MessageBoxW(editorFrame_, L"保存当前图层文件后退出？", L"未保存的更改", MB_YESNOCANCEL | MB_ICONQUESTION);
  if (choice == IDCANCEL)
    return false;
  if (choice == IDNO)
    return true;
  editorCommand(3002);
  return !editorDirty_;
}
void ShowcaseInteractiveApp::editorFinish() {
  try {
    if (editorDraft_["positions"].empty())
      return;
    const size_t minimum = editorTabIndex_ == 0 ? 1 : editorTabIndex_ == 1 ? 2 : 3;
    if (editorDraft_["positions"].size() < minimum) {
      SetWindowTextW(
          editorControls_[3091],
          editorTabIndex_ == 1 ? L"线至少需要两个不同的顶点，请继续落点。" : L"面至少需要三个不同的顶点，请继续落点。");
      return;
    }
    auto candidate = editorDocument_;
    auto feature = editorDraft_;
    feature["style"] = editorStyle();
    feature["label"] = editorLabel();
    wchar_t name[1024]{};
    GetWindowTextW(editorControls_[3050], name, 1024);
    feature["name"] = editorUtf8(name);
    feature["id"] = "feature-" + std::to_string(GetTickCount64()) + "-" + std::to_string(frameCount_);
    if (candidate["layers"][editorLayer_]["locked"])
      throw std::runtime_error("当前图层已锁定");
    candidate["layers"][editorLayer_]["features"].push_back(feature);
    hongtu::landscape::validate(candidate);
    editorCheckpoint();
    editorDocument_ = std::move(candidate);
    editorFeature_ = int(editorDocument_["layers"][editorLayer_]["features"].size()) - 1;
    editorDraft_["positions"] = Json::array();
    editorMode_ = "navigate";
    refreshEditor();
    editorSelectFeature();
  } catch (const std::exception& e) {
    MessageBoxW(editorFrame_, editorWide(e.what()).c_str(), L"无法完成标注", MB_OK | MB_ICONWARNING);
  }
}
void ShowcaseInteractiveApp::editorCommand(int id) {
  if (editorRefreshing_)
    return;
  if (id == 3043)
    return;
  if (editorStylePending_) {
    KillTimer(controlsHwnd_, 3180);
    editorStylePending_ = false;
    editorLiveStyle();
  }
  auto text = [&](int control) {
    wchar_t b[1024]{};
    GetWindowTextW(editorControls_[control], b, 1024);
    return editorUtf8(b);
  };
  auto pathDialog = [&](bool saving, const wchar_t* filter) -> fs::path {
    wchar_t file[32768]{};
    if (saving)
      wcsncpy_s(file, editorFile_.c_str(), _TRUNCATE);
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = editorFrame_;
    ofn.lpstrFile = file;
    ofn.nMaxFile = 32768;
    ofn.lpstrFilter = filter;
    ofn.lpstrDefExt = saving ? L"json" : nullptr;
    ofn.Flags = OFN_NOCHANGEDIR | OFN_EXPLORER | (saving ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
    return (saving ? GetSaveFileNameW(&ofn) : GetOpenFileNameW(&ofn)) ? fs::path(file) : fs::path();
  };
  try {
    auto& layers = editorDocument_["layers"];
    auto& layer = layers[editorLayer_];
    auto& features = layer["features"];
    if (id == 3001 || id == 3004) {
      if (editorDirty_ && !editorClose())
        return;
      if (id == 3001) {
        auto path = pathDialog(false, L"XNBY 图层 (*.json)\0*.json\0\0");
        if (path.empty())
          return;
        auto next = hongtu::landscape::load(path);
        if (next["layers"].empty())
          throw std::runtime_error("编辑项目至少应有一个图层");
        editorDocument_ = std::move(next);
        editorFile_ = path;
      } else {
        editorDocument_ = hongtu::landscape::emptyDocument();
        editorFile_ = resolveProjectPath("projects") / L"未命名图层.json";
      }
      editorDirty_ = id == 3004;
      editorUndo_.clear();
      editorRedo_.clear();
      editorLayer_ = 0;
      editorFeature_ = -1;
      editorDraft_["positions"] = Json::array();
      editorMode_ = "navigate";
    } else if (id == 3002 || id == 3003) {
      auto path = editorFile_;
      if (id == 3003 || path.empty())
        path = pathDialog(true, L"XNBY 图层 (*.json)\0*.json\0\0");
      if (path.empty())
        return;
      if (!path.parent_path().empty())
        fs::create_directories(path.parent_path());
      hongtu::landscape::save(path, editorDocument_);
      editorFile_ = path;
      editorDirty_ = false;
    } else if (id >= 3005 && id <= 3007) {
      cli_.zhumadianView = id == 3005 ? "overview" : id == 3006 ? "oblique" : "close";
      settings_ = applyCliOverrides(cli_, settingsForPreset(cli_.preset, sceneConfig_));
      applyCameraPreset();
    } else if (id == 3008 && editorFeature_ >= 0) {
      auto p = hongtu::landscape::positions(features[editorFeature_]);
      glm::vec3 center(0);
      for (auto v : p)
        center += v;
      center /= float(p.size());
      camPos_ = center + glm::vec3(0, -250, 220);
      camYaw_ = 0;
      camPitch_ = -.72f;
    } else if (id >= 3010 && id <= 3013) {
      editorMode_ = id == 3010 ? "navigate" : editorTabIndex_ == 0 ? "point" : editorTabIndex_ == 1 ? "line" : "region";
      editorDraft_["type"] = editorMode_ == "navigate" ? "line" : editorMode_;
      editorDraft_["positions"] = Json::array();
      editorFeature_ = -1;
      editorDraft_.erase("name");
      editorSelectFeature();
    } else if (id == 3020) {
      int selection = int(SendMessageW(editorControls_[3020], LB_GETCURSEL, 0, 0));
      if (selection >= 0) {
        editorLayer_ = selection;
        editorFeature_ = -1;
        editorDraft_["positions"] = Json::array();
      }
    } else if (id == 3021) {
      editorCheckpoint();
      layers.push_back({{"id", "layer-" + std::to_string(GetTickCount64())},
                        {"name", "新图层"},
                        {"visible", true},
                        {"locked", false},
                        {"features", Json::array()}});
      editorLayer_ = layers.size() - 1;
      editorFeature_ = -1;
    } else if (id == 3022) {
      editorCheckpoint();
      layer["visible"] = !layer["visible"].get<bool>();
    } else if (id == 3023) {
      editorCheckpoint();
      layer["locked"] = !layer["locked"].get<bool>();
    } else if (id == 3024) {
      if (layers.size() == 1)
        throw std::runtime_error("请保留至少一个图层");
      if (layer["locked"])
        throw std::runtime_error("图层已锁定");
      editorCheckpoint();
      layers.erase(editorLayer_);
      editorLayer_ = 0;
      editorFeature_ = -1;
    } else if (id == 3026) {
      if (layer["locked"])
        throw std::runtime_error("图层已锁定");
      editorCheckpoint();
      layer["name"] = text(3025);
    } else if (id == 3040) {
      int row = int(SendMessageW(editorControls_[3040], LB_GETCURSEL, 0, 0));
      editorFeature_ = row >= 0 && row < int(editorFeatureRows_.size()) ? editorFeatureRows_[row] : -1;
      editorMode_ = "navigate";
      editorDraft_["positions"] = Json::array();
      editorSelectFeature();
      if (editorFeature_ >= 0) {
        auto& f = features[editorFeature_];
        auto& s = f["style"];
        editorDraft_["style"] = s;
        SetWindowTextW(editorControls_[3050], editorWide(f["name"]).c_str());
        for (auto [control, key] : std::initializer_list<std::pair<int, const char*>>{
                 {3053, "sizePx"}, {3055, "widthPx"}, {3057, "opacity"}, {3059, "fillOpacity"}})
          SetWindowTextW(editorControls_[control], editorWide(s[key].dump()).c_str());
        SetWindowTextW(editorControls_[3056], editorWide(s["color"]).c_str());
        SetWindowTextW(editorControls_[3058], editorWide(s["fillColor"]).c_str());
        std::vector<std::string> icons = {"pin", "circle", "square", "star", "warning", "custom"},
                                 patterns = {"solid", "dash", "dot"};
        SendMessageW(editorControls_[3051],
                     CB_SETCURSEL,
                     std::find(icons.begin(), icons.end(), s["icon"].get<std::string>()) - icons.begin(),
                     0);
        SendMessageW(editorControls_[3054],
                     CB_SETCURSEL,
                     std::find(patterns.begin(), patterns.end(), s["pattern"].get<std::string>()) - patterns.begin(),
                     0);
      }
    } else if (id == 3041 && editorFeature_ >= 0) {
      if (layer["locked"])
        throw std::runtime_error("图层已锁定");
      editorCheckpoint();
      features.erase(editorFeature_);
      editorFeature_ = -1;
    } else if (id == 3042 && editorFeature_ >= 0) {
      int target = int(SendMessageW(editorControls_[3043], CB_GETCURSEL, 0, 0));
      if (target < 0 || target == int(editorLayer_))
        return;
      if (layer["locked"] || layers[target]["locked"])
        throw std::runtime_error("源图层或目标图层已锁定");
      auto f = features[editorFeature_];
      editorCheckpoint();
      features.erase(editorFeature_);
      layers[target]["features"].push_back(f);
      editorLayer_ = target;
      editorFeature_ = int(layers[target]["features"].size()) - 1;
    } else if (id == 3052) {
      auto file = pathDialog(false, L"PNG 图标 (*.png)\0*.png\0\0");
      if (file.empty())
        return;
      if (fs::file_size(file) > 4 * 1024 * 1024)
        throw std::runtime_error("PNG 图标最多 4 MiB");
      std::ifstream in(file, std::ios::binary);
      std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), {});
      int w = 0, h = 0, c = 0;
      if (!stbi_info_from_memory(data.data(), int(data.size()), &w, &h, &c) || w > 2048 || h > 2048)
        throw std::runtime_error("图标应为有效 PNG，尺寸不超过 2048×2048");
      editorDraft_["style"]["iconData"] = "data:image/png;base64," + hongtu::landscape::encode64(data);
      SendMessageW(editorControls_[3051], CB_SETCURSEL, 5, 0);
      editorLiveCheckpoint_ = false;
      editorLiveStyle();
    } else if (id == 3060) {
      auto style = editorStyle();
      editorDraft_["style"] = style;
      if (editorFeature_ >= 0) {
        if (layer["locked"])
          throw std::runtime_error("图层已锁定");
        auto next = editorDocument_;
        next["layers"][editorLayer_]["features"][editorFeature_]["style"] = style;
        next["layers"][editorLayer_]["features"][editorFeature_]["name"] = text(3050);
        hongtu::landscape::validate(next);
        editorCheckpoint();
        editorDocument_ = std::move(next);
      }
    } else if (id == 3061) {
      editorFinish();
      return;
    } else if (id == 3062) {
      editorDraft_["positions"] = Json::array();
      editorMode_ = "navigate";
    } else if (id == 3030 && !editorUndo_.empty()) {
      editorRedo_.push_back(editorDocument_);
      editorDocument_ = editorUndo_.back();
      editorUndo_.pop_back();
      editorDirty_ = true;
      editorFeature_ = -1;
      editorLayer_ = 0;
    } else if (id == 3031 && !editorRedo_.empty()) {
      editorUndo_.push_back(editorDocument_);
      editorDocument_ = editorRedo_.back();
      editorRedo_.pop_back();
      editorDirty_ = true;
      editorFeature_ = -1;
      editorLayer_ = 0;
    } else if (id == 3080) {
      editorVertex_ = int(SendMessageW(editorControls_[3080], LB_GETCURSEL, 0, 0));
      editorVertices();
      return;
    } else if (id >= 3084 && id <= 3087) {
      if (layer["locked"])
        throw std::runtime_error("图层已锁定");
      if (editorVertex_ < 0 && id != 3085)
        throw std::runtime_error("请先在顶点列表中选择顶点");
      auto vertex = Json::array();
      for (int k = 0; k < 3; ++k) {
        auto t = text(3081 + k);
        size_t consumed = 0;
        double v = std::stod(t, &consumed);
        if (consumed != t.size() || !std::isfinite(v) || std::abs(v) > 100000)
          throw std::runtime_error("XYZ 须为局部米制范围内的有效数值");
        vertex.push_back(v);
      }
      if (id == 3087) {
        glm::vec3 p(vertex[0].get<float>(), vertex[1].get<float>(), vertex[2].get<float>());
        camPos_ = p + glm::vec3(0, -100, 90);
        camYaw_ = 0;
        camPitch_ = -.72f;
      } else {
        auto next = editorDocument_;
        auto draft = editorDraft_;
        auto& p = editorFeature_ >= 0 ? next["layers"][editorLayer_]["features"][editorFeature_]["positions"]
                                      : draft["positions"];
        int selected = editorVertex_;
        if (id == 3084)
          p.at(selected) = vertex;
        else if (id == 3085) {
          if (editorTabIndex_ == 0 && !p.empty())
            throw std::runtime_error("点标绘仅支持一个坐标点");
          selected = editorVertex_ >= 0 ? editorVertex_ + 1 : int(p.size());
          p.insert(p.begin() + selected, vertex);
        } else {
          p.erase(selected);
          selected = std::min(selected, int(p.size()) - 1);
        }
        if (editorFeature_ >= 0) {
          hongtu::landscape::validate(next);
          editorCheckpoint();
          editorDocument_ = std::move(next);
        } else {
          if (p.size() > 1000)
            throw std::runtime_error("最多支持 1000 个顶点");
          editorDraft_ = std::move(draft);
        }
        editorVertex_ = selected;
      }
    } else if (id == 3032 || id == 3063) {
      static COLORREF custom[16]{};
      CHOOSECOLORW cc{};
      cc.lStructSize = sizeof(cc);
      cc.hwndOwner = editorFrame_;
      cc.lpCustColors = custom;
      cc.Flags = CC_FULLOPEN | CC_RGBINIT;
      int field = id == 3032 ? 3056 : 3058;
      auto c = hongtu::landscape::Overlay::color(text(field), 1);
      cc.rgbResult = RGB(int(c.r * 255), int(c.g * 255), int(c.b * 255));
      if (ChooseColorW(&cc)) {
        char b[8];
        snprintf(b, 8, "#%02X%02X%02X", GetRValue(cc.rgbResult), GetGValue(cc.rgbResult), GetBValue(cc.rgbResult));
        SetWindowTextW(editorControls_[field], editorWide(b).c_str());
        editorLiveCheckpoint_ = false;
        editorLiveStyle();
      }
    }
    refreshEditor();
    if (id == 3001 || id == 3004 || id == 3020 || id == 3021 || id == 3024 || id == 3041 || id == 3042 || id == 3030 ||
        id == 3031)
      editorSelectFeature();
    SetFocus(hwnd_);
  } catch (const std::exception& e) {
    MessageBoxW(editorFrame_, editorWide(e.what()).c_str(), L"编辑操作未完成", MB_OK | MB_ICONWARNING);
  }
}
glm::mat4 ShowcaseInteractiveApp::editorViewProjection() const {
  auto dir =
      glm::vec3(std::sin(camYaw_) * std::cos(camPitch_), std::cos(camYaw_) * std::cos(camPitch_), std::sin(camPitch_));
  auto clip = cameraClipPlanesFor(cli_, camPos_);
  auto proj = glm::perspective(
      glm::radians(settings_.fovDeg), float(windowWidth_) / windowHeight_, clip.nearMeters, clip.farMeters);
  proj[1][1] *= -1;
  return proj * glm::lookAt(camPos_, camPos_ + dir, glm::vec3(0, 0, 1));
}
std::optional<glm::vec3> ShowcaseInteractiveApp::editorPick(glm::vec2 pixel) const {
  const auto ray = hongtu::renderer::screenToWorldRay(
      pixel.x, windowHeight_ - pixel.y, windowWidth_, windowHeight_, glm::inverse(glm::dmat4(editorViewProjection())));
  if (auto hit = nativePager_.pickRay(ray))
    return glm::vec3(hit->hit.point);
  return {};
}
void ShowcaseInteractiveApp::editorClick(glm::vec2 pixel) {
  if (editorDocument_["layers"][editorLayer_]["locked"]) {
    MessageBoxW(editorFrame_, L"当前图层已锁定", L"无法绘制", MB_OK);
    return;
  }
  const auto& draftPoints = editorDraft_["positions"];
  // A closing click is an action, never a duplicate polygon vertex. A second
  // click at the final line vertex also completes a line without a zero edge.
  if ((editorMode_ == "region" && draftPoints.size() >= 3) || (editorMode_ == "line" && draftPoints.size() >= 2)) {
    const auto& anchor = editorMode_ == "region" ? draftPoints.front() : draftPoints.back();
    auto screen =
        hongtu::landscape::Overlay::project({anchor[0].get<float>(), anchor[1].get<float>(), anchor[2].get<float>()},
                                            editorViewProjection(),
                                            windowWidth_,
                                            windowHeight_);
    if (screen && glm::distance(*screen, pixel) < 8.f) {
      editorFinish();
      return;
    }
  }
  auto p = editorPick(pixel);
  if (!p) {
    SetWindowTextW(editorControls_[3091], L"该位置未命中地景，请在地表或建筑表面落点。");
    return;
  }
  try {
    editorDraft_["style"] = editorStyle();
  } catch (const std::exception& e) {
    MessageBoxW(editorFrame_, editorWide(e.what()).c_str(), L"样式无效", MB_OK | MB_ICONWARNING);
    return;
  }
  editorDraft_["type"] = editorMode_;
  if (!draftPoints.empty()) {
    const auto& last = draftPoints.back();
    if (glm::distance(glm::vec3(last[0].get<float>(), last[1].get<float>(), last[2].get<float>()),
                      *p + glm::vec3(0, 0, .3f)) < .001f)
      return;
  }
  editorDraft_["positions"].push_back({p->x, p->y, p->z + .3f});
  if (editorMode_ == "point")
    editorFinish();
  else
    refreshEditor();
}
