# mesh_decimator — offline glTF → decimated .glb tool (UV/normal-preserving
# quadric simplification via meshoptimizer). Used to turn Poly Haven photoscan
# trees (1–17 M tris) into instancing-friendly runtime assets for the park
# vegetation system. Not linked into the engine; pure tool.
find_package(meshoptimizer CONFIG REQUIRED)
find_path(TINYGLTF_INCLUDE_DIRS "tiny_gltf.h")
find_package(Stb REQUIRED)

add_executable(mesh_decimator main.cpp)
set_target_properties(mesh_decimator PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_include_directories(mesh_decimator PRIVATE ${TINYGLTF_INCLUDE_DIRS} ${Stb_INCLUDE_DIR})
target_link_libraries(mesh_decimator PRIVATE meshoptimizer::meshoptimizer)
