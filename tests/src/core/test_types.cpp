#include <gtest/gtest.h>
#include <hongtu/core/types/types.hpp>

using namespace hongtu;
using namespace hongtu::core;

TEST(ResultTest, OkValue) {
  Result<int, String> result = Result<int, String>::ok(42);
  EXPECT_TRUE(result.is_ok());
  EXPECT_FALSE(result.is_error());
  EXPECT_EQ(result.unwrap(), 42);
}

TEST(ResultTest, ErrorValue) {
  Result<int, String> result = Result<int, String>::error("Error message");
  EXPECT_FALSE(result.is_ok());
  EXPECT_TRUE(result.is_error());
  EXPECT_EQ(result.unwrap_error(), "Error message");
}

TEST(ResultTest, UnwrapOr) {
  Result<int, String> ok_result = Result<int, String>::ok(42);
  Result<int, String> err_result = Result<int, String>::error("Error");

  EXPECT_EQ(ok_result.unwrap_or(10), 42);
  EXPECT_EQ(err_result.unwrap_or(10), 10);
}

TEST(HandleTest, Validity) {
  Handle<int> invalid;
  EXPECT_FALSE(invalid.is_valid());

  Handle<int> valid(123);
  EXPECT_TRUE(valid.is_valid());
  EXPECT_EQ(valid.get(), 123);
}

TEST(HandleTest, Comparison) {
  Handle<int> h1(100);
  Handle<int> h2(100);
  Handle<int> h3(200);

  EXPECT_EQ(h1, h2);
  EXPECT_NE(h1, h3);
  EXPECT_LT(h1, h3);
}

TEST(BitOperationsTest, HasFlag) {
  u32 value = 0b1101u;
  EXPECT_TRUE(has_flag(value, u32{0b0001}));
  EXPECT_TRUE(has_flag(value, u32{0b1000}));
  EXPECT_FALSE(has_flag(value, u32{0b0010}));
}

TEST(BitOperationsTest, SetFlag) {
  u32 value = 0b0000u;
  value = set_flag(value, u32{0b0001});
  EXPECT_EQ(value, 0b0001u);
}

TEST(BitOperationsTest, ClearFlag) {
  u32 value = 0b1111u;
  value = clear_flag(value, u32{0b0001});
  EXPECT_EQ(value, 0b1110u);
}

TEST(BitOperationsTest, ToggleFlag) {
  u32 value = 0b1010u;
  value = toggle_flag(value, u32{0b0101});
  EXPECT_EQ(value, 0b1111u);
}
