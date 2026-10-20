# Team motion modules integration — 2026-10-20

Source: jeromeyhq/htengine master da9898f3054f898924dc1637771eb2947e2d5131, reviewed against 9914aa62bd78a627d3727f88b1a7142d63273252 (15 commits, 59 changed paths).

Imported `pose_smoother.hpp`, `follow_camera.hpp`, `geo_motion.hpp` and their two renderer test files. These provide bounded automatic pose queues, fixed-duration interpolation, shortest-angle rotation, terrain-aware ellipsoid interpolation, chase/fixed camera following and orbit/zoom limits.

Compatibility changes: geo_motion explicitly includes the existing coordinate/enu_frame.hpp; the follow-camera test constructs the equivalent T(ECEF) * ENU * Rz matrix locally, because this branch does not contain the upstream geo_frame module. Production math behavior is unchanged. Repository clang-format was applied; the installed formatter reports two pre-existing unsupported configuration keys.

Validation: independent C++20/MSVC Release build in tests/motion; 30 tests passed (17 PoseSmoother, 5 GeoMotion, 8 FollowCamera). Includes 120-second simulated 100 Hz input/30 fps output queue bound, burst/backlog recovery, 50 km ellipsoid height, damping and shortest-angle checks. No Vulkan device or existing production libraries required.

Configure the standalone test project with `cmake -S tests/motion -B build-motion` and the environment's glm/GTest package paths, then build and run CTest. The two test sources are also registered in the normal ht_tests target; the complete engine suite was not rerun in this increment.

This is a reviewed source-module integration, not a full merge of all 59 upstream paths. Cloud-server/SDK wiring, remote protocols, screenshots and upstream operational scripts have not been imported. Runtime integration into the Zhumadian editor/web scene and project-specific visual acceptance remain pending. Existing asset/BIM/analysis work and prior held executables are preserved. No running editor, public server or released library was replaced.
