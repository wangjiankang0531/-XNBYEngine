#pragma once

#include <hongtu/core/common.hpp>
#include <hongtu/core/memory/allocator.hpp>

namespace hongtu::core {

class MemoryPool {
 public:
  MemoryPool(usize block_size, usize initial_blocks = 10);
  ~MemoryPool();

  void* allocate();
  void deallocate(void* ptr);

  usize block_size() const { return block_size_; }
  usize available_blocks() const { return available_blocks_; }

 private:
  struct Block {
    Block* next;
  };

  usize block_size_;
  usize available_blocks_;
  Block* free_list_;
  Vector<u8*> allocated_buffers_;
};

} // namespace hongtu::core
