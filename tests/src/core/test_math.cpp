#include <gtest/gtest.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

using namespace glm;

TEST(MathTest, VectorOperations) {
  vec3 v1(1.0f, 2.0f, 3.0f);
  vec3 v2(4.0f, 5.0f, 6.0f);

  vec3 sum = v1 + v2;
  EXPECT_FLOAT_EQ(sum.x, 5.0f);
  EXPECT_FLOAT_EQ(sum.y, 7.0f);
  EXPECT_FLOAT_EQ(sum.z, 9.0f);

  float dot_product = dot(v1, v2);
  EXPECT_FLOAT_EQ(dot_product, 32.0f);

  vec3 cross_product = cross(v1, v2);
  EXPECT_FLOAT_EQ(cross_product.x, -3.0f);
  EXPECT_FLOAT_EQ(cross_product.y, 6.0f);
  EXPECT_FLOAT_EQ(cross_product.z, -3.0f);
}

TEST(MathTest, MatrixOperations) {
  mat4 identity = mat4(1.0f);
  vec4 v(1.0f, 2.0f, 3.0f, 1.0f);

  vec4 result = identity * v;
  EXPECT_FLOAT_EQ(result.x, 1.0f);
  EXPECT_FLOAT_EQ(result.y, 2.0f);
  EXPECT_FLOAT_EQ(result.z, 3.0f);
  EXPECT_FLOAT_EQ(result.w, 1.0f);
}

TEST(MathTest, Transformations) {
  mat4 translation = translate(mat4(1.0f), vec3(1.0f, 2.0f, 3.0f));
  vec4 v(0.0f, 0.0f, 0.0f, 1.0f);

  vec4 result = translation * v;
  EXPECT_FLOAT_EQ(result.x, 1.0f);
  EXPECT_FLOAT_EQ(result.y, 2.0f);
  EXPECT_FLOAT_EQ(result.z, 3.0f);
}

TEST(MathTest, VectorNormalization) {
  vec3 v(3.0f, 4.0f, 0.0f);
  vec3 normalized = normalize(v);

  EXPECT_FLOAT_EQ(length(normalized), 1.0f);
  EXPECT_FLOAT_EQ(normalized.x, 0.6f);
  EXPECT_FLOAT_EQ(normalized.y, 0.8f);
}
