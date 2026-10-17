#include <gtest/gtest.h>
#include <hongtu/core/memory/allocator.hpp>

using namespace hongtu::core;

class MallocAllocatorTest : public ::testing::Test {
 protected:
  MallocAllocator allocator;
};

TEST_F(MallocAllocatorTest, AllocateAndDeallocate) {
  void* ptr = allocator.allocate(64);
  EXPECT_NE(ptr, nullptr);
  allocator.deallocate(ptr, 64);
}

TEST_F(MallocAllocatorTest, AllocateAligned) {
  void* ptr = allocator.allocate(64, 32);
  EXPECT_NE(ptr, nullptr);
  EXPECT_EQ(reinterpret_cast<uintptr_t>(ptr) % 32, 0);
  allocator.deallocate(ptr, 64);
}

TEST_F(MallocAllocatorTest, Reallocate) {
  void* ptr = allocator.allocate(64);
  void* new_ptr = allocator.reallocate(ptr, 64, 128);
  EXPECT_NE(new_ptr, nullptr);
  allocator.deallocate(new_ptr, 128);
}

class LinearAllocatorTest : public ::testing::Test {
 protected:
  LinearAllocator allocator{1024};
};

TEST_F(LinearAllocatorTest, AllocateSequential) {
  void* ptr1 = allocator.allocate(64);
  void* ptr2 = allocator.allocate(64);
  EXPECT_NE(ptr1, nullptr);
  EXPECT_NE(ptr2, nullptr);
  EXPECT_LT(ptr1, ptr2);
}

TEST_F(LinearAllocatorTest, Reset) {
  allocator.allocate(64);
  allocator.reset();
  EXPECT_EQ(allocator.get_allocated_size(), 0);
}

TEST_F(LinearAllocatorTest, AllocateAligned) {
  void* ptr = allocator.allocate(32, 16);
  EXPECT_EQ(reinterpret_cast<uintptr_t>(ptr) % 16, 0);
}

class StackAllocatorTest : public ::testing::Test {
 protected:
  StackAllocator allocator{1024};
};

TEST_F(StackAllocatorTest, LIFO) {
  void* ptr1 = allocator.allocate(64);
  void* ptr2 = allocator.allocate(64);
  allocator.deallocate(ptr2, 64);
  EXPECT_EQ(allocator.get_allocated_size(), 64);
}

TEST_F(StackAllocatorTest, Reset) {
  allocator.allocate(64);
  allocator.reset();
  EXPECT_EQ(allocator.get_allocated_size(), 0);
}
