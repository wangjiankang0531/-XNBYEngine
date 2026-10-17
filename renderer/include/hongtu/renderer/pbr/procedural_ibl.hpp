#pragma once

#include <hongtu/rhi/RHIDevice.h>
#include <hongtu/rhi/RHIResources.h>
#include <glm/glm.hpp>

#include <string>
#include <vector>

namespace hongtu::renderer {

using namespace HongTu::RHI;

/// Reusable procedural / HDR image-based-lighting baker + cube owner.
///
/// Lifted verbatim from the editor's inline IBL infrastructure
/// (`hongtu-editor/src/viewport/viewport_renderer.cpp` +
/// `src/environment/hdr_loader.cpp`) so BOTH the editor and the cloud-side
/// `ForwardSceneRenderer` share the SAME warm procedural IBL — diffuse
/// irradiance cube + prefiltered (box-mip) specular cube — instead of the
/// flat analytic env fallback.
///
/// Owns:
///   * The 2-cube descriptor set layout compatible with
///     `PbrForwardPipeline::initializeWithIbl` / `VegetationPipeline` set=2
///     (binding 0 = diffuse irradiance cube, binding 1 = specular mip cube).
///   * The cubemap textures / views + cube sampler.
///   * The IBL descriptor set bound each draw.
///
/// Usage:
///   1. `ibl.buildLayout(device);`  (once, BEFORE building the PBR pipeline so
///      its layout can reference set=2 — getDescriptorLayout() feeds
///      `initializeWithIbl`).
///   2. `ibl.buildProcedural(device, /*preset=*/1, /*rotation_rad=*/0.0f);`
///      bakes + uploads + (re)allocates the descriptor set.
///   3. Bind `ibl.getDescriptorSet()` at set=2 for PBR mesh + vegetation draws.
class ProceduralIbl {
 public:
  /// Built-in procedural HDR presets (match the editor's EnvironmentPanel).
  enum class Preset {
    Sunset,    ///< warm orange horizon, dark blue zenith
    BlueSky,   ///< clear daytime — bright blue zenith, brown ground (default)
    Studio,    ///< neutral studio softbox, slight gradient
    Overcast,  ///< gray flat sky
  };

  // ---- CPU bake primitives (public so callers can pre-bake offline) --------

  /// 6 cube face buffers, each face_size×face_size float32 RGBA.
  struct CubeFaces {
    int                             face_size = 0;
    std::vector<std::vector<float>> faces;  // [6][face_size*face_size*4]
  };

  /// Fill `out_pixels` (size = w*h*3 floats) with the chosen preset
  /// (256×128 equirect, row 0 = zenith).
  static void generateProceduralHdr(Preset preset, std::vector<float>& out_pixels,
                                    int& out_w, int& out_h);
  /// CPU equirect → cubemap (bilinear, optional Y-axis rotation).
  static CubeFaces buildCubemapFromEquirect(const float* eq_pixels, int eq_w, int eq_h,
                                            int face_size = 256, float rotation_rad = 0.0f);
  /// CPU Lambert cosine-weighted diffuse irradiance cube.
  static CubeFaces buildIrradianceFromCube(const CubeFaces& env_cube, int out_size = 32,
                                           int sample_count = 256);
  /// Box-filter mip chain (cheap GGX-prefilter stand-in for specular).
  static std::vector<CubeFaces> buildCubeMipChain(const CubeFaces& base, int target_mips = 6);

  // ---- Lifecycle -----------------------------------------------------------

  /// Build sampler + 2-cube descriptor set layout. Idempotent — safe to call
  /// again (no-op once built). MUST run before the PBR pipeline build.
  bool buildLayout(RHIDevice* device);

  /// Bake a built-in procedural preset → upload cubes → (re)build descriptor
  /// set. Builds the layout first if needed. `preset_index` clamps to [0,3].
  bool buildProcedural(RHIDevice* device, int preset_index = 1, float rotation_rad = 0.0f);

  /// Load an equirect .hdr file → bake → upload. Returns false if the file
  /// can't be opened / decoded.
  bool buildFromHdrFile(RHIDevice* device, const std::string& abs_path,
                        float rotation_rad = 0.0f);

  // ---- Accessors -----------------------------------------------------------

  RHIDescriptorSetLayoutPtr getDescriptorLayout() const { return desc_layout_; }
  RHIDescriptorSetPtr       getDescriptorSet()    const { return desc_set_; }
  RHITextureViewPtr         getDiffuseView()      const { return diffuse_cube_view_; }
  RHITextureViewPtr         getSpecView()         const { return spec_cube_view_; }
  bool                      ready()               const { return desc_set_ != nullptr; }

 private:
  /// Upload baked CPU cube data into GPU cubes + (re)build the descriptor set.
  bool uploadCubes(const std::vector<std::vector<std::vector<float>>>& spec_mips,
                   int spec_face_size,
                   const std::vector<std::vector<float>>& irradiance_faces,
                   int irr_face_size);

  RHIDevice*                device_ = nullptr;
  RHITexturePtr             diffuse_cube_tex_;
  RHITextureViewPtr         diffuse_cube_view_;
  RHITexturePtr             spec_cube_tex_;
  RHITextureViewPtr         spec_cube_view_;
  RHISamplerPtr             sampler_;
  RHIDescriptorSetLayoutPtr desc_layout_;
  RHIDescriptorSetPtr       desc_set_;
  uint32_t                  spec_face_size_ = 0;
  uint32_t                  spec_mip_count_ = 0;
};

}  // namespace hongtu::renderer
