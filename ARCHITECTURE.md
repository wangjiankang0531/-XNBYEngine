# 仓库结构（ARCHITECTURE）

本仓库同时容纳**引擎**、**编辑器**和几个**独立产品/服务**。它们各自有独立的构建，
容易混淆，这份文档是权威的"谁是谁"。改动结构前先读这里。

## 一、引擎（顶层 CMake 构建 → `build/`）

根 `CMakeLists.txt` 用 `add_subdirectory(...)` 构建的、组成 HongTu 引擎的静态库模块：

| 目录 | 库 | 职责 |
|---|---|---|
| `core/` | `ht_core` | 日志、内存、数学、类型、profiler |
| `rhi/` | `ht_rhi` | Vulkan 抽象（设备/缓冲/纹理/管线/命令） |
| `renderer/` | `ht_renderer` | 延迟/前向、PBR、阴影、后期、GlobeRenderer |
| `scene/` | `ht_scene` | ECS、场景图、八叉树 |
| `asset/` | `ht_asset` | glTF/FBX/纹理/着色器加载 |
| `window/` | `ht_window` | SDL2 窗口 + 输入 |
| `coordinate/` | `ht_coordinate` | 大地/ECEF/RTC 数学 |
| `measure/` | `ht_measure` | 测量（直线/高度/面积/点坐标） |
| `gis/` | `ht_gis` | 瓦片 provider（天地图/Esri/OSM/本地）、DEM |
| `cloud/` | `ht_cloud` | **体积云渲染**（天上的云）— 见下「cloud 三义」 |
| `shaders/` | `ht_shaders` | GLSL→SPIR-V 编译 |

依赖严格向上：`core ← rhi ← renderer ← scene`。`window/coordinate/gis/asset/cloud` 为旁支。

`apps/` 下是各 demo 与服务端：`earth_demo`、`park_demo`、`geo_pbr_demo`、`cloud_demo`、
`cloud_render_server` 等,每个一个 `main.cpp` + `CMakeLists.txt`。(应急演练已不再有自有
渲染二进制 —— 它经 cloud-manager 用通用 `cloud_render_server` 渲染,见下方 drill 条目。)

## 二、独立子项目（各自独立构建，不在顶层 CMake 内）

| 目录 | 是什么 | 构建 |
|---|---|---|
| `hongtu-editor/` | **当前编辑器**（ImGui+SDL2，离屏 Vulkan 渲染）。经 `../` 相对路径把引擎当静态库消费 | `hongtu-editor/build/`（自带 `_engine/` 重编一份引擎库） |
| `hongtu-cloud-manager/` | **云渲染管理平台**（web + agent + master，远程/推流渲染实例编排） | 自带 |
| `drill/` | 应急演练系统(两个子项目同一目录):`drill/drill-session-service/`(会话服务 + Java 推演 `DrillSim`,Java/Maven)+ `drill/drill-web/`(前端,Vite)。**渲染经 cloud-manager**(前端调 `/api/play` 领通用 `cloud_render_server` 实例),**无自有渲染二进制、对引擎/cloud-manager 零代码耦合,可整体移出独立仓库** | 各自 `pom.xml` / `package.json` |

> ⚠️ 改引擎模块后**两个构建都要重编**：顶层 `build/` 只更新 demo，编辑器在
> `hongtu-editor/build/_engine/` 里有自己重编的一份。

## 三、"cloud" 三义（命名重载，别混）

"cloud" 在本仓库指**两类完全不同**的东西：

1. **体积云渲染**（天上的云，引擎特性）
   - `cloud/`（`ht_cloud` 模块）、`apps/cloud_demo`
2. **云端/远程推流渲染**（把渲染放服务器、WebRTC 推到浏览器）
   - `apps/cloud_render_server`（推流渲染服务端）
   - `hongtu-cloud-manager/`（这些实例的管理/编排平台）

> 命名暂未统一（避免改动 `hongtu-cloud-manager` 的协议/文档）。如需彻底消歧，
> 可把第 2 类重命名为 `render-stream` / `remote-render`，但属较大改动。

## 四、资源 / 工具 / 文档目录

| 目录 | 内容 |
|---|---|
| `assets/` | 运行时/示例资产(模型、HDR、角色 glb 等)。`assets/park/` 为可重新生成的园区资产,已 gitignore;`assets/characters/` 为漫游骨骼人物(CesiumMan) |
| `texture/` | 纹理与图标(编辑器图标、地面贴图等) |
| `cmake/` | CMake 辅助模块 |
| `scripts/` | 辅助脚本 |
| `tools/` | 离线工具(如 `mesh_decimator`,顶层 CMake 构建) |
| `test_assets/` | 测试用的生成资产(gitignore) |
| `docs/` | 文档。`docs/project/` 放产品 PRD / 任务清单 / 交付报告等;`docs/superpowers/` 放设计/计划稿 |
| `data/` | 场景/剧本数据:`data/scenes/*.json`、`data/scenarios/*.json` 跟踪;运行时 `*.db`、`projects/` 已 gitignore |

## 五、根目录文件(只应有这些)

`README.md`(引擎介绍)· `ARCHITECTURE.md`(本文件)· `CLAUDE.md`(给 Claude 的项目规则)·
`AGENTS.md` · `CMakeLists.txt`(引擎顶层构建)· `vcpkg.json` · `.gitignore`。

> 其余历史松散文件已整理:旧报告/PRD/任务清单移入 `docs/project/`;demo 跑出来的
> 截图(`disaster_*.png` 等)、`a.out`、`*.log` 是产物,已删除 + gitignore,会随
> demo 重新生成,不要提交到根目录。

## 六、生成物 / 运行时（不是源码，应被 .gitignore）

`build/`、`hongtu-editor/build/`、`vcpkg_installed/`、`data/*.db`、`data/projects/`、
`logs/`、`tile_cache/`、`*/node_modules/`、`*/dist/`、`*/.vite/`、`*/target/`、
根目录 demo 截图 `*.png` 等为构建/运行产物，不属于"架构"。

## 七、归档

`archive_legacy_engine_editor/`、`archive_legacy_apps_editor/` 为早期 Qt6 编辑器，
已归档、不构建，仅留作参考。

> 清理记录：原 `editor/`（早期 `ht_editor` 死桩，从未进构建）与 `engine/`
> （core/renderer/... 的旧"参考副本"，grep 串味元凶）已删除。编辑器只有
> `hongtu-editor/`，引擎模块只有顶层那些目录。
