// Compact point/line/region property pages. Their lists map to document indices.
namespace {
  LRESULT CALLBACK landscapeEditProc(HWND control, UINT message, WPARAM w, LPARAM l, UINT_PTR id, DWORD_PTR) {
    if (message == WM_KEYDOWN && w == 'A' && (GetKeyState(VK_CONTROL) & 0x8000)) {
      SendMessageW(control, EM_SETSEL, 0, -1);
      return 0;
    }
    if (message == WM_NCDESTROY)
      RemoveWindowSubclass(control, landscapeEditProc, id);
    return DefSubclassProc(control, message, w, l);
  }
} // namespace
bool ShowcaseInteractiveApp::initEditorPanel() {
  INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_STANDARD_CLASSES | ICC_TAB_CLASSES};
  InitCommonControlsEx(&icc);
  WNDCLASSW wc{};
  wc.lpfnWndProc = controlPanelProc;
  wc.hInstance = hinstance_;
  wc.lpszClassName = L"XNBYLandscapePanel";
  wc.hbrBackground = editorSurfaceBrush_;
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  RegisterClassW(&wc);
  controlsHwnd_ = CreateWindowExW(0,
                                  wc.lpszClassName,
                                  L"标注和图层",
                                  WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_CLIPCHILDREN,
                                  0,
                                  0,
                                  420,
                                  windowHeight_,
                                  editorFrame_,
                                  nullptr,
                                  hinstance_,
                                  this);
  auto add = [&](int id, const wchar_t* kind, const wchar_t* name, int x, int y, int w, int h, DWORD flags = 0) {
    auto control = CreateWindowExW(
        std::wstring(kind) == L"EDIT" ? WS_EX_CLIENTEDGE : 0,
        kind,
        name,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP |
            (std::wstring(kind) == L"BUTTON" && (flags & BS_TYPEMASK) == BS_PUSHBUTTON ? BS_OWNERDRAW : flags),
        x,
        y + 48,
        w,
        h,
        controlsHwnd_,
        reinterpret_cast<HMENU>(INT_PTR(id)),
        hinstance_,
        nullptr);
    SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(id == 3100 ? editorHeadingFont_ : editorFont_), TRUE);
    editorControlRects_[id] = {x, y + 48, x + w, y + 48 + h};
    editorControls_[id] = control;
    if (std::wstring(kind) == L"EDIT")
      SetWindowSubclass(control, landscapeEditProc, 1, 0);
    return control;
  };
  auto button = [&](int id, const wchar_t* name, int x, int y, int w = 92) {
    add(id, L"BUTTON", name, x, y, w, 27, BS_PUSHBUTTON);
  };
  auto edit = [&](int id, const wchar_t* name, int x, int y, int w) {
    add(id, L"EDIT", name, x, y, w, 24, ES_AUTOHSCROLL);
  };
  auto label = [&](int id, const wchar_t* name, int x, int y, int w = 100) {
    add(id, L"STATIC", name, x, y, w, 22);
  };
  auto combo = [&](int id, int x, int y, int w, std::initializer_list<const wchar_t*> names) {
    auto c = add(id, L"COMBOBOX", L"", x, y, w, 180, CBS_DROPDOWNLIST);
    for (auto n : names)
      SendMessageW(c, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(n));
    SendMessageW(c, CB_SETCURSEL, 0, 0);
  };
  label(3100, L"信能本源  /  场景编辑", 14, 12, 390);
  button(3001, L"打开", 14, 37);
  button(3002, L"保存", 114, 37);
  button(3003, L"另存为", 214, 37);
  button(3004, L"新建", 314, 37);
  button(3005, L"总览", 14, 70);
  button(3006, L"斜视", 114, 70);
  button(3007, L"近景", 214, 70);
  button(3008, L"定位标注", 314, 70);
  label(3101, L"图层", 14, 104, 90);
  add(3020, L"LISTBOX", L"", 14, 126, 392, 60, LBS_NOTIFY | WS_BORDER | WS_VSCROLL);
  button(3021, L"新图层", 14, 192);
  button(3022, L"显隐", 114, 192);
  button(3023, L"锁定", 214, 192);
  button(3024, L"删除层", 314, 192);
  edit(3025, L"默认图层", 14, 225, 292);
  button(3026, L"改名", 314, 223);
  auto tabs = add(3102, WC_TABCONTROLW, L"", 14, 260, 392, 29, TCS_FIXEDWIDTH);
  SendMessageW(tabs, TCM_SETITEMSIZE, 0, MAKELPARAM(125, 26));
  for (auto name : {L"点标绘", L"线标绘", L"面标绘"}) {
    TCITEMW item{};
    item.mask = TCIF_TEXT;
    item.pszText = const_cast<wchar_t*>(name);
    SendMessageW(tabs, TCM_INSERTITEMW, TabCtrl_GetItemCount(tabs), reinterpret_cast<LPARAM>(&item));
  }
  add(3040, L"LISTBOX", L"", 14, 297, 392, 57, LBS_NOTIFY | WS_BORDER | WS_VSCROLL);
  button(3041, L"删除标注", 14, 360, 112);
  button(3042, L"移到图层", 132, 360, 112);
  combo(3043, 250, 361, 156, {});
  label(3103, L"标注名称", 14, 397, 75);
  edit(3050, L"新标注", 94, 395, 312);
  combo(3051, 14, 430, 178, {L"定位图标", L"圆形", L"方形", L"星形", L"警告", L"自定义 PNG"});
  button(3052, L"选择 PNG", 199, 429, 100);
  edit(3053, L"32", 309, 430, 97);
  combo(3054, 14, 430, 178, {L"实线", L"虚线", L"点线"});
  label(3110, L"线宽 px", 199, 432, 94);
  edit(3055, L"4", 309, 430, 97);
  button(3032, L"颜色", 14, 462, 72);
  edit(3056, L"#22D3EE", 94, 463, 130);
  label(3111, L"透明度", 233, 466, 69);
  edit(3057, L"1", 309, 463, 97);
  button(3063, L"填色", 14, 496, 72);
  edit(3058, L"#22D3EE", 94, 497, 130);
  label(3112, L"填充透明度", 233, 500, 76);
  edit(3059, L"0.25", 309, 497, 97);
  add(3070, L"BUTTON", L"显示文字标牌", 14, 535, 140, 24, BS_AUTOCHECKBOX);
  edit(3071, L"", 158, 535, 248);
  label(3113, L"字号 px", 14, 569, 62);
  edit(3072, L"16", 80, 567, 55);
  edit(3073, L"#FFFFFF", 143, 567, 106);
  label(3114, L"偏移 X/Y", 256, 569, 62);
  edit(3075, L"12", 318, 567, 41);
  edit(3076, L"-16", 365, 567, 41);
  // Label opacity follows the feature opacity; it remains independent in saved files.
  edit(3074, L"1", 0, 0, 1);
  ShowWindow(editorControls_[3074], SW_HIDE);
  label(3115, L"顶点列表：序号 / 东向 X / 北向 Y / 高程 Z（米）", 14, 602, 392);
  add(3080, L"LISTBOX", L"", 14, 625, 392, 68, LBS_NOTIFY | WS_BORDER | WS_VSCROLL);
  label(3116, L"X / Y / Z", 14, 703, 76);
  edit(3081, L"0", 94, 700, 98);
  edit(3082, L"0", 201, 700, 98);
  edit(3083, L"0", 308, 700, 98);
  button(3084, L"更新顶点", 14, 731);
  button(3085, L"插入顶点", 114, 731);
  button(3086, L"删除顶点", 214, 731);
  button(3087, L"定位顶点", 314, 731);
  button(3011, L"新建点", 14, 771);
  button(3061, L"完成绘制", 114, 771);
  button(3062, L"取消绘制", 214, 771);
  button(3010, L"漫游", 314, 771);
  button(3030, L"撤销", 14, 805);
  button(3031, L"重做", 114, 805);
  add(3090,
      L"STATIC",
      L"样式有效即生效；绘制左键落点，Enter 完成。\n右键环视、滚轮推进；XYZ 使用驻马店局部米制坐标。",
      214,
      805,
      192,
      44);
  add(3091, L"STATIC", L"", 14, 853, 392, 65);
  for (auto item : {std::pair{3192, L"左"}, std::pair{3193, L"右"}, std::pair{3191, L"收起"}}) {
    const int x = item.first == 3192 ? 234 : item.first == 3193 ? 280 : 326;
    auto control = CreateWindowExW(0,
                                   L"BUTTON",
                                   item.second,
                                   WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                                   x,
                                   10,
                                   item.first == 3191 ? 66 : 40,
                                   28,
                                   controlsHwnd_,
                                   reinterpret_cast<HMENU>(INT_PTR(item.first)),
                                   hinstance_,
                                   nullptr);
    SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(editorFont_), TRUE);
    editorControls_[item.first] = control;
  }
  refreshEditor();
  editorSelectFeature();
  editorLayout();
  return controlsHwnd_ != nullptr;
}
void ShowcaseInteractiveApp::refreshEditor() {
  if (!controlsHwnd_)
    return;
  editorRefreshing_ = true;
  auto& layers = editorDocument_["layers"];
  editorLayer_ = std::min(editorLayer_, layers.size() - 1);
  auto target = SendMessageW(editorControls_[3043], CB_GETCURSEL, 0, 0);
  SendMessageW(editorControls_[3020], LB_RESETCONTENT, 0, 0);
  SendMessageW(editorControls_[3043], CB_RESETCONTENT, 0, 0);
  for (const auto& layer : layers) {
    auto name = editorWide(layer["name"]);
    auto row = (layer["visible"].get<bool>() ? L"● " : L"○ ") + name + (layer["locked"].get<bool>() ? L" [锁定]" : L"");
    SendMessageW(editorControls_[3020], LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(row.c_str()));
    SendMessageW(editorControls_[3043], CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name.c_str()));
  }
  SendMessageW(editorControls_[3020], LB_SETCURSEL, editorLayer_, 0);
  SendMessageW(
      editorControls_[3043], CB_SETCURSEL, target >= 0 && target < int(layers.size()) ? target : editorLayer_, 0);
  SetWindowTextW(editorControls_[3025], editorWide(layers[editorLayer_]["name"]).c_str());
  const char* types[] = {"point", "line", "region"};
  auto& features = layers[editorLayer_]["features"];
  if (editorFeature_ >= int(features.size()))
    editorFeature_ = -1;
  if (editorFeature_ >= 0 && features[editorFeature_]["type"] != types[editorTabIndex_])
    editorFeature_ = -1;
  editorFeatureRows_.clear();
  SendMessageW(editorControls_[3040], LB_RESETCONTENT, 0, 0);
  for (size_t k = 0; k < features.size(); ++k)
    if (features[k]["type"] == types[editorTabIndex_]) {
      auto name = editorWide(features[k]["name"]);
      editorFeatureRows_.push_back(int(k));
      SendMessageW(editorControls_[3040], LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name.c_str()));
    }
  int selected = -1;
  for (size_t k = 0; k < editorFeatureRows_.size(); ++k)
    if (editorFeatureRows_[k] == editorFeature_)
      selected = int(k);
  SendMessageW(editorControls_[3040], LB_SETCURSEL, selected, 0);
  for (int id : {3051, 3052, 3053})
    ShowWindow(editorControls_[id], editorTabIndex_ == 0 ? SW_SHOW : SW_HIDE);
  for (int id : {3054, 3055, 3110})
    ShowWindow(editorControls_[id], editorTabIndex_ != 0 ? SW_SHOW : SW_HIDE);
  for (int id : {3063, 3058, 3059, 3112})
    ShowWindow(editorControls_[id], editorTabIndex_ == 2 ? SW_SHOW : SW_HIDE);
  SetWindowTextW(editorControls_[3011],
                 editorTabIndex_ == 0   ? L"新建点"
                 : editorTabIndex_ == 1 ? L"新建线"
                                        : L"新建面");
  const auto count = editorDraft_["positions"].size();
  const size_t minimum = editorTabIndex_ == 0 ? 1 : editorTabIndex_ == 1 ? 2 : 3;
  EnableWindow(editorControls_[3061], count >= minimum);
  EnableWindow(editorControls_[3062], editorMode_ != "navigate" || count > 0);
  std::wstring mode = editorMode_ == "point"    ? L"绘制点：点击地表即完成"
                      : editorMode_ == "line"   ? L"绘制线：至少 2 点，Enter 完成"
                      : editorMode_ == "region" ? L"绘制面：至少 3 点，点击首点或 Enter 闭合"
                                                : L"漫游：先选择页签，再点击新建点 / 线 / 面";
  std::wstring status = mode + L"\n顶点数：" + std::to_wstring(count) + L"  " + editorFile_.filename().wstring() +
                        (editorDirty_ ? L"（未保存）" : L"（已保存）");
  SetWindowTextW(editorControls_[3091], status.c_str());
  editorRefreshing_ = false;
  editorVertices();
}
void ShowcaseInteractiveApp::editorSelectFeature() {
  editorRefreshing_ = true;
  Json f = editorFeature_ >= 0 ? editorDocument_["layers"][editorLayer_]["features"][editorFeature_] : editorDraft_;
  auto s = f.value("style", hongtu::landscape::defaultStyle()),
       label = f.value("label", hongtu::landscape::defaultLabel());
  editorDraft_["style"] = s;
  if (editorFeature_ < 0) {
    label["visible"] = true;
    editorDraft_["label"] = label;
  }
  SetWindowTextW(editorControls_[3050],
                 editorWide(f.value("name",
                                    editorTabIndex_ == 0   ? "新点"
                                    : editorTabIndex_ == 1 ? "新线"
                                                           : "新区域"))
                     .c_str());
  for (auto [id, key] : std::initializer_list<std::pair<int, const char*>>{
           {3053, "sizePx"}, {3055, "widthPx"}, {3057, "opacity"}, {3059, "fillOpacity"}})
    SetWindowTextW(editorControls_[id], editorWide(s[key].dump()).c_str());
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
  SendMessageW(editorControls_[3070], BM_SETCHECK, label["visible"].get<bool>() ? BST_CHECKED : BST_UNCHECKED, 0);
  SetWindowTextW(editorControls_[3071], editorWide(label["text"]).c_str());
  SetWindowTextW(editorControls_[3073], editorWide(label["color"]).c_str());
  for (auto [id, key] : std::initializer_list<std::pair<int, const char*>>{{3072, "fontPx"}, {3074, "opacity"}})
    SetWindowTextW(editorControls_[id], editorWide(label[key].dump()).c_str());
  SetWindowTextW(editorControls_[3075], editorWide(label["offset"][0].dump()).c_str());
  SetWindowTextW(editorControls_[3076], editorWide(label["offset"][1].dump()).c_str());
  editorVertex_ = -1;
  editorLiveCheckpoint_ = false;
  editorRefreshing_ = false;
  editorVertices();
}
void ShowcaseInteractiveApp::editorTab() {
  if (editorStylePending_) {
    KillTimer(controlsHwnd_, 3180);
    editorStylePending_ = false;
    editorLiveStyle();
  }
  int next = TabCtrl_GetCurSel(editorControls_[3102]);
  if (next < 0 || next == editorTabIndex_)
    return;
  if (!editorDraft_["positions"].empty() &&
      MessageBoxW(editorFrame_, L"切换页签会取消尚未完成的绘制，继续？", L"正在绘制", MB_YESNO | MB_ICONQUESTION) !=
          IDYES) {
    TabCtrl_SetCurSel(editorControls_[3102], editorTabIndex_);
    return;
  }
  editorTabIndex_ = next;
  editorFeature_ = -1;
  editorVertex_ = -1;
  editorMode_ = "navigate";
  const char* types[] = {"point", "line", "region"};
  editorDraft_ = {{"type", types[next]}, {"positions", Json::array()}, {"style", hongtu::landscape::defaultStyle()}};
  refreshEditor();
  editorSelectFeature();
}
void ShowcaseInteractiveApp::editorVertices() {
  editorRefreshing_ = true;
  SendMessageW(editorControls_[3080], LB_RESETCONTENT, 0, 0);
  const auto& p = editorFeature_ >= 0 ? editorDocument_["layers"][editorLayer_]["features"][editorFeature_]["positions"]
                                      : editorDraft_["positions"];
  for (size_t k = 0; k < p.size(); ++k) {
    wchar_t row[180]{};
    swprintf_s(row,
               L"%03u   %.3f   %.3f   %.3f",
               unsigned(k + 1),
               p[k][0].get<double>(),
               p[k][1].get<double>(),
               p[k][2].get<double>());
    SendMessageW(editorControls_[3080], LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(row));
  }
  if (editorVertex_ >= int(p.size()))
    editorVertex_ = -1;
  SendMessageW(editorControls_[3080], LB_SETCURSEL, editorVertex_, 0);
  if (editorVertex_ >= 0)
    for (int k = 0; k < 3; ++k)
      SetWindowTextW(editorControls_[3081 + k], editorWide(p[editorVertex_][k].dump()).c_str());
  editorRefreshing_ = false;
}
Json ShowcaseInteractiveApp::editorLabel() {
  auto text = [&](int id) {
    wchar_t b[1024]{};
    GetWindowTextW(editorControls_[id], b, 1024);
    return editorUtf8(b);
  };
  auto number = [&](int id) {
    auto t = text(id);
    size_t consumed = 0;
    double n = std::stod(t, &consumed);
    if (consumed != t.size())
      throw std::runtime_error("标牌数值格式不正确");
    return n;
  };
  Json label = hongtu::landscape::defaultLabel();
  label["visible"] = SendMessageW(editorControls_[3070], BM_GETCHECK, 0, 0) == BST_CHECKED;
  label["text"] = text(3071);
  label["fontPx"] = number(3072);
  label["color"] = text(3073);
  label["opacity"] = number(3057);
  label["offset"] = {number(3075), number(3076)};
  hongtu::landscape::validateLabel(label);
  return label;
}
void ShowcaseInteractiveApp::editorLiveStyle() {
  if (editorRefreshing_)
    return;
  try {
    Json style = editorStyle(), label = editorLabel();
    wchar_t b[1024]{};
    GetWindowTextW(editorControls_[3050], b, 1024);
    const auto name = editorUtf8(b);
    if (editorFeature_ >= 0) {
      if (editorDocument_["layers"][editorLayer_]["locked"]) {
        SetWindowTextW(editorControls_[3091], L"图层已锁定，请先解锁后编辑。");
        return;
      }
      auto next = editorDocument_;
      auto& f = next["layers"][editorLayer_]["features"][editorFeature_];
      f["style"] = style;
      f["label"] = label;
      f["name"] = name;
      hongtu::landscape::validate(next);
      if (next != editorDocument_) {
        if (!editorLiveCheckpoint_)
          editorCheckpoint();
        editorLiveCheckpoint_ = true;
        editorDocument_ = std::move(next);
      }
    } else {
      auto test = hongtu::landscape::emptyDocument();
      test["layers"][0]["features"].push_back({{"id", "style-check"},
                                               {"name", name},
                                               {"type", "point"},
                                               {"positions", {{0, 0, 0}}},
                                               {"style", style},
                                               {"label", label}});
      hongtu::landscape::validate(test);
    }
    editorDraft_["style"] = style;
    editorDraft_["label"] = label;
    editorDraft_["name"] = name;
    SetWindowTextW(editorControls_[3091],
                   editorFeature_ >= 0
                       ? (editorDirty_ ? L"样式和文字标牌已生效（未保存）。" : L"样式和文字标牌已生效（已保存）。")
                       : L"绘制样式已生效；落点时采用当前设置。");
    // Refresh only the name row. Do not disturb the active text edit/caret or focus.
    if (editorFeature_ >= 0) {
      int row = int(SendMessageW(editorControls_[3040], LB_GETCURSEL, 0, 0));
      if (row >= 0) {
        auto text = editorWide(name);
        SendMessageW(editorControls_[3040], LB_DELETESTRING, row, 0);
        SendMessageW(editorControls_[3040], LB_INSERTSTRING, row, reinterpret_cast<LPARAM>(text.c_str()));
        SendMessageW(editorControls_[3040], LB_SETCURSEL, row, 0);
      }
    }
  } catch (const std::exception& e) {
    SetWindowTextW(editorControls_[3091], (L"输入尚未有效，保留上次有效样式：" + editorWide(e.what())).c_str());
  }
}
