#pragma once

// Shared water-shading GLSL, prepended to every shader that renders water:
// the base-globe + streamed-tile fragment shaders (global ocean, M1/M2/M2.5)
// and the WaterSurfaceRenderer fragment shader (local water bodies, M3).
// Functions only — no layout declarations — so it composes with each shader's
// own uniforms. See docs/superpowers/specs/2026-06-09-geo-water-design.md.

namespace hongtu::renderer {

inline constexpr const char* kWaterGlsl = R"glsl(
    // Cheap value noise (hash-based) for the glint sparkle. World-locked so the
    // sparkle doesn't swim with the camera; animated via time offsets.
    float ht_hash21(vec2 p) {
      p = fract(p * vec2(123.34, 456.21));
      p += dot(p, p + 45.32);
      return fract(p.x * p.y);
    }
    float ht_vnoise(vec2 p) {
      vec2 i = floor(p), f = fract(p);
      f = f * f * (3.0 - 2.0 * f);
      float a = ht_hash21(i),            b = ht_hash21(i + vec2(1.0, 0.0));
      float c = ht_hash21(i + vec2(0.0, 1.0)), d = ht_hash21(i + vec2(1.0, 1.0));
      return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
    }
    // Classify an imagery texel as water in [0,1]. Sea reads cyan/blue-dominant
    // at mid luminance; deliberately conservative — better to miss water than to
    // glint on a dark mountain shadow.
    //
    // The smoothstep bands are deliberately WIDE: imagery color shifts (turbid
    // estuary vs open sea, mosaic seams between satellite passes) hit this
    // classifier directly, and narrow bands turn those shifts into hard
    // water/no-water boundary lines on the sea. Wide bands make the mask —
    // and therefore the Fresnel/glint — fade across the shift instead.
    float waterMask(vec3 c) {
      float luma = dot(c, vec3(0.299, 0.587, 0.114));
      float blueness = (c.b + c.g) * 0.5 - c.r;          // >0 for sea
      float m = smoothstep(0.0, 0.22, blueness);
      m *= smoothstep(0.015, 0.10, luma) * (1.0 - smoothstep(0.70, 0.95, luma));
      m *= smoothstep(-0.10, 0.05, c.b - c.g);           // reject green-dominant land
      return clamp(m, 0.0, 1.0);
    }
    // Procedural sky color along a reflected ray. Brightens toward the horizon
    // and warms toward the reflected sun. Used by the water Fresnel reflection.
    vec3 ht_skyReflect(vec3 dir, vec3 up, vec3 sunDir) {
      float el = clamp(dot(dir, up), 0.0, 1.0);          // 1 = zenith, 0 = horizon
      vec3 zenith  = vec3(0.16, 0.33, 0.60);
      vec3 horizon = vec3(0.62, 0.76, 0.90);
      vec3 col = mix(horizon, zenith, el);
      float s = max(dot(dir, sunDir), 0.0);
      col += vec3(1.0, 0.86, 0.62) * pow(s, 8.0) * 0.5;  // warmth near reflected sun
      return col;
    }
    // Shade a water albedo: Fresnel sky reflection (M2) + sun glint (M1).
    // `up` = geodetic surface normal; `viewDir`/`sunDir` point away from surface.
    //
    //   * FRESNEL — grazing views (toward the horizon) reflect the sky and turn
    //               silvery-blue; near-nadir views see through to the albedo.
    //               Schlick, F0 ~ 0.02 for water.
    //   * GLINT   — a smooth specular BAND locates the sun reflection; world-
    //               locked value noise gives the sparkle TEXTURE inside it.
    //               Two concerns kept separate so neither forms a lattice.
    // No-op off the lit side or where mask ~ 0.
    vec3 applyWaterGlint(vec3 albedo, vec3 up, vec3 worldPos, vec3 viewDir,
                         vec3 sunDir, float time, float mask) {
      if (mask <= 0.001) return albedo;
      float daylit = smoothstep(-0.05, 0.18, dot(up, sunDir));
      if (daylit <= 0.0) return albedo;
      vec3 color = albedo;

      // Fresnel sky reflection. Cap kept moderate (0.55): the tint scales
      // with the color-derived mask, so a strong cap turns every mask
      // gradient (imagery color shifts, mosaic seams) into a visible
      // water-boundary line at grazing angles.
      float ndv = max(dot(up, viewDir), 0.0);
      float F = 0.02 + 0.98 * pow(1.0 - ndv, 5.0);
      vec3 sky = ht_skyReflect(reflect(-viewDir, up), up, sunDir);
      color = mix(color, sky * 0.92, min(F, 0.55) * mask * daylit);

      // Sun glint on top of the reflection.
      vec3 H = normalize(viewDir + sunDir);
      float band = pow(max(dot(up, H), 0.0), 40.0);
      if (band > 0.001) {
        vec3 ref = abs(up.z) < 0.99 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
        vec3 t = normalize(cross(up, ref));
        vec3 b = cross(up, t);
        vec2 q = vec2(dot(worldPos, t), dot(worldPos, b)) * 0.004;  // ~250 m cells
        float n1 = ht_vnoise(q        + vec2(time * 0.50, time * 0.17));
        float n2 = ht_vnoise(q * 2.3  - vec2(time * 0.31, time * 0.43));
        float sparkle = smoothstep(0.55, 1.0, n1 * 0.6 + n2 * 0.5);
        float glint = band * (0.22 + 1.7 * sparkle);  // faint sheen + sparkle peaks
        color += vec3(1.0, 0.97, 0.9) * glint * mask * daylit;
      }
      return color;
    }
)glsl";

}  // namespace hongtu::renderer
