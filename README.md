# 信能本源引擎 XNBYEngine

持续开发的模块化 C++20/Vulkan 三维引擎，长期渲染效果与效率对标 UE。
保留 HongTu 命名空间及 ht_ 模块名以维持现有代码兼容。当前能力不等于 UE 目标已完成。

引擎模块：core、rhi、renderer、scene、asset、window、coordinate、measure、gis、cloud。
apps/ 为引擎能力验证程序；驻马店生产应用单独位于同级 Application 仓库。
开启 HT_ENGINE_LIBRARIES_ONLY 可以只构建引擎库，供 Application 通过 add_subdirectory 使用。
cmake -S . -B build -DHT_BUILD_TESTS=ON 配置，cmake --build build --config Release 构建，
ctest --test-dir build -C Release --output-on-failure 测试。依赖按 vcpkg.json 安装。
新工作目录的权威长期记录为 ../Document/Engine/project/htengine_goal_state.md。
资产和文档在同级 Assets/Document，Git 仅同步源码及可再生配置。历史材料在 Backup。


## 源码检出目录

当前远端仓库为 wangjiankang0531/-XNBYEngine 与 -ZhumadianRiskControl。
请分别检出到 XNBYEngine 与 Application，使应用的同级引擎路径保持一致：

```text
git clone https://github.com/wangjiankang0531/-XNBYEngine.git XNBYEngine
git clone https://github.com/wangjiankang0531/-ZhumadianRiskControl.git Application
```

Git 仅保存源码。实际 GIS、模型、DLG、业务台账、完整部署记录与运行时位于本地同级 Assets/Document/Application/Packages。
引擎能力验证程序与驻马店应用分开维护；应用当前可体验版本为 EXE 0.9.2 / 网页 0.4。
