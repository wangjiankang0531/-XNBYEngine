# Shader compilation configuration
# This file manages GLSL shader compilation to SPIR-V

find_program(GLSLC glslc HINTS $ENV{VULKAN_SDK}/bin /opt/homebrew/bin /usr/local/bin)

set(SHADER_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)
set(SHADER_BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR}/spirv)

file(MAKE_DIRECTORY ${SHADER_BINARY_DIR})

# Collect all shader files
file(GLOB_RECURSE SHADER_SOURCES
  "${SHADER_SOURCE_DIR}/*.vert"
  "${SHADER_SOURCE_DIR}/*.frag"
  "${SHADER_SOURCE_DIR}/*.comp"
  "${SHADER_SOURCE_DIR}/*.geom"
  "${SHADER_SOURCE_DIR}/*.tesc"
  "${SHADER_SOURCE_DIR}/*.tese"
)

set(SHADER_OUTPUTS)
if(GLSLC AND SHADER_SOURCES)
  foreach(SHADER ${SHADER_SOURCES})
    get_filename_component(SHADER_NAME ${SHADER} NAME)
    set(SHADER_OUTPUT "${SHADER_BINARY_DIR}/${SHADER_NAME}.spv")

    add_custom_command(
      OUTPUT ${SHADER_OUTPUT}
      COMMAND ${GLSLC} -O ${SHADER} -o ${SHADER_OUTPUT}
      MAIN_DEPENDENCY ${SHADER}
      COMMENT "Compiling shader: ${SHADER_NAME}"
      VERBATIM
    )

    list(APPEND SHADER_OUTPUTS ${SHADER_OUTPUT})
  endforeach()
endif()

add_custom_target(ht_shaders ALL DEPENDS ${SHADER_OUTPUTS})

# Create a library for shader resources
add_library(ht_shader_resources INTERFACE)
target_include_directories(ht_shader_resources INTERFACE ${SHADER_BINARY_DIR})

message(STATUS "ht_shaders configured")
