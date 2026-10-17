add_executable(earth_demo main.cpp)


target_link_libraries(earth_demo
  PRIVATE
    ht_renderer ht_scene ht_rhi ht_core ht_asset ht_coordinate ht_gis
    SDL2::SDL2 glm::glm nlohmann_json::nlohmann_json
)

target_compile_definitions(earth_demo PRIVATE HT_ENGINE_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
if(WIN32)
  target_compile_definitions(earth_demo PRIVATE SDL_MAIN_HANDLED)
endif()

set_target_properties(earth_demo PROPERTIES
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON
)

if(NOT MSVC)
  target_compile_options(earth_demo PRIVATE -Wall -Wextra -Wpedantic -Wno-unused-parameter)
endif()

# Make engine-root textures available beside the executable so paths such as
# "texture/sky/..." resolve when launching from the build output directory.
# Creating directory symlinks requires elevated Windows privileges, therefore
# Windows uses a post-build copy while Unix-like hosts keep the cheap symlink.
if(WIN32)
  add_custom_command(TARGET earth_demo POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/texture"
            "$<TARGET_FILE_DIR:earth_demo>/texture"
    COMMENT "Copying Earth Demo texture assets"
  )
elseif(NOT EXISTS "${CMAKE_CURRENT_BINARY_DIR}/texture")
  file(CREATE_LINK "${CMAKE_SOURCE_DIR}/texture"
                   "${CMAKE_CURRENT_BINARY_DIR}/texture"
                   SYMBOLIC)
endif()

message(STATUS "earth_demo configured")
