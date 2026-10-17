#pragma once

#include <hongtu/core/common.hpp>
#include <memory>

namespace hongtu::core {

// ============================================================================
// Memory alignment utilities
// ============================================================================

constexpr usize align_forward(usize ptr, usize alignment) {
  return (ptr + (alignment - 1)) & ~(alignment - 1);
}

constexpr usize align_offset(usize ptr, usize alignment) {
  return align_forward(ptr, alignment) - ptr;
}

// ============================================================================
// Allocator interface
// ============================================================================

class Allocator {
 public:
  virtual ~Allocator() = default;

  virtual void* allocate(usize size, usize alignment = alignof(std::max_align_t)) = 0;
  virtual void deallocate(void* ptr, usize size = 0) = 0;
  virtual void* reallocate(void* ptr, usize old_size, usize new_size,
                           usize alignment = alignof(std::max_align_t)) = 0;

  // Helper function to get total allocated size
  virtual usize get_allocated_size() const = 0;
};

// ============================================================================
// Malloc-based allocator (default)
// ============================================================================

class MallocAllocator : public Allocator {
 public:
  void* allocate(usize size, usize alignment = alignof(std::max_align_t)) override;
  void deallocate(void* ptr, usize size = 0) override;
  void* reallocate(void* ptr, usize old_size, usize new_size,
                   usize alignment = alignof(std::max_align_t)) override;
  usize get_allocated_size() const override { return allocated_size_; }

 private:
  usize allocated_size_{0};
};

// ============================================================================
// Linear allocator (fast, no deallocation)
// ============================================================================

class LinearAllocator : public Allocator {
 public:
  explicit LinearAllocator(usize capacity);
  ~LinearAllocator() override;

  void* allocate(usize size, usize alignment = alignof(std::max_align_t)) override;
  void deallocate(void* ptr, usize size = 0) override;
  void* reallocate(void* ptr, usize old_size, usize new_size,
                   usize alignment = alignof(std::max_align_t)) override;
  usize get_allocated_size() const override { return current_offset_; }

  void reset();

 private:
  u8* buffer_;
  usize capacity_;
  usize current_offset_;
};

// ============================================================================
// Pool allocator for fixed-size allocations
// ============================================================================

class PoolAllocator : public Allocator {
 public:
  PoolAllocator(usize element_size, usize element_count);
  ~PoolAllocator() override;

  void* allocate(usize size, usize alignment = alignof(std::max_align_t)) override;
  void deallocate(void* ptr, usize size = 0) override;
  void* reallocate(void* ptr, usize old_size, usize new_size,
                   usize alignment = alignof(std::max_align_t)) override;
  usize get_allocated_size() const override { return element_count_ * element_size_; }

 private:
  struct Node {
    Node* next;
  };

  u8* buffer_;
  Node* free_list_;
  usize element_size_;
  usize element_count_;
  usize available_count_;
};

// ============================================================================
// Stack allocator
// ============================================================================

class StackAllocator : public Allocator {
 public:
  explicit StackAllocator(usize capacity);
  ~StackAllocator() override;

  void* allocate(usize size, usize alignment = alignof(std::max_align_t)) override;
  void deallocate(void* ptr, usize size = 0) override;
  void* reallocate(void* ptr, usize old_size, usize new_size,
                   usize alignment = alignof(std::max_align_t)) override;
  usize get_allocated_size() const override { return current_offset_; }

  void reset();

 private:
  u8* buffer_;
  usize capacity_;
  usize current_offset_;
};

// ============================================================================
// Global allocator management
// ============================================================================

class AllocatorManager {
 public:
  static AllocatorManager& instance();

  void set_default_allocator(Unique<Allocator> allocator);
  Allocator* get_default_allocator();

 private:
  AllocatorManager();
  Unique<Allocator> default_allocator_;
};

// ============================================================================
// Helper functions for allocation with type safety
// ============================================================================

template<typename T, typename... Args>
T* allocate_object(Allocator* allocator, Args&&... args) {
  if (!allocator) {
    allocator = AllocatorManager::instance().get_default_allocator();
  }
  void* ptr = allocator->allocate(sizeof(T), alignof(T));
  return new (ptr) T(std::forward<Args>(args)...);
}

template<typename T>
void deallocate_object(Allocator* allocator, T* ptr) {
  if (!allocator) {
    allocator = AllocatorManager::instance().get_default_allocator();
  }
  if (ptr) {
    ptr->~T();
    allocator->deallocate(ptr, sizeof(T));
  }
}

} // namespace hongtu::core
