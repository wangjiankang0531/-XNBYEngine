# GitHub regenerated the v3.0.0 tag archive in 2026 without changing the
# extracted source files.  The upstream vcpkg registry still records the old
# archive hash, so keep the normal integrity check with the current official
# archive hash instead of disabling verification.
vcpkg_from_github(
  OUT_SOURCE_PATH SOURCE_PATH
  REPO syoyo/tinygltf
  REF "v${VERSION}"
  SHA512 b9a689f25284206969f31ba382abd06cd4ed69ee6a118af3484aaa17b68aebefddc97a4e722e3bde8fa39b737677cb09cbd3d3e6f2a573ac4ba1fb718478b79a
  HEAD_REF master
)

vcpkg_replace_string("${SOURCE_PATH}/tiny_gltf.h" "#include \"json.hpp\"" "#include <nlohmann/json.hpp>")
file(
  INSTALL
    "${SOURCE_PATH}/tiny_gltf.h"
    "${SOURCE_PATH}/tiny_gltf_v3.h"
    "${SOURCE_PATH}/tinygltf_json.h"
  DESTINATION "${CURRENT_PACKAGES_DIR}/include"
)
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
