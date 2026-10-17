#include <hongtu/core/memory/allocator.hpp>

#include <cstdlib>
#include <stdexcept>

namespace hongtu::core {

  // ============================================================================
  // MallocAllocator implementation
  // ============================================================================

  void* MallocAllocator::allocate(usize size, usize alignment) {
#ifdef _WIN32
    void* ptr = _aligned_malloc(size, alignment);
    if (ptr) {
      allocated_size_ += size;
    }
    return ptr;
#else
    if (alignment <= alignof(std::max_align_t)) {
      void* ptr = std::malloc(size);
      if (ptr) {
        allocated_size_ += size;
      }
      return ptr;
    }

    // Aligned allocation for larger alignments
    void* ptr = nullptr;
    if (posix_memalign(&ptr, alignment, size) != 0) {
      ptr = nullptr;
    }
    if (ptr) {
      allocated_size_ += size;
    }
    return ptr;
#endif
  }

  void MallocAllocator::deallocate(void* ptr, usize size) {
    if (ptr) {
#ifdef _WIN32
      _aligned_free(ptr);
#else
      std::free(ptr);
#endif
      if (size > 0) {
        allocated_size_ -= size;
      }
    }
  }

  void* MallocAllocator::reallocate(void* ptr, usize old_size, usize new_size, usize alignment) {
#ifdef _WIN32
    void* new_ptr = _aligned_realloc(ptr, new_size, alignment);
    if (new_ptr) {
      if (new_size >= old_size) {
        allocated_size_ += new_size - old_size;
      } else {
        allocated_size_ -= old_size - new_size;
      }
    }
    return new_ptr;
#else
    if (alignment <= alignof(std::max_align_t)) {
      void* new_ptr = std::realloc(ptr, new_size);
      if (new_ptr) {
        allocated_size_ += (new_size - old_size);
      }
      return new_ptr;
    }

    // For aligned reallocation, allocate new block and copy
    void* new_ptr = allocate(new_size, alignment);
    if (new_ptr && ptr) {
      std::memcpy(new_ptr, ptr, old_size < new_size ? old_size : new_size);
      deallocate(ptr, old_size);
    }
    return new_ptr;
#endif
  }

  // ============================================================================
  // LinearAllocator implementation
  // ============================================================================

  LinearAllocator::LinearAllocator(usize capacity) : capacity_(capacity), current_offset_(0) {
    buffer_ = static_cast<u8*>(std::malloc(capacity));
    if (!buffer_) {
      throw std::bad_alloc();
    }
  }

  LinearAllocator::~LinearAllocator() {
    if (buffer_) {
      std::free(buffer_);
      buffer_ = nullptr;
    }
  }

  void* LinearAllocator::allocate(usize size, usize alignment) {
    usize aligned_offset = align_forward(reinterpret_cast<usize>(buffer_ + current_offset_), alignment);
    usize offset = aligned_offset - reinterpret_cast<usize>(buffer_);

    if (offset + size > capacity_) {
      throw std::bad_alloc();
    }

    u8* ptr = buffer_ + offset;
    current_offset_ = offset + size;
    return ptr;
  }

  void LinearAllocator::deallocate(void* ptr, usize size) {
    // Linear allocator does not support deallocation
    (void)ptr;
    (void)size;
  }

  void* LinearAllocator::reallocate(void* ptr, usize old_size, usize new_size, usize alignment) {
    // For linear allocator, only works if reallocating the most recent allocation
    void* new_ptr = allocate(new_size, alignment);
    if (new_ptr && ptr) {
      std::memcpy(new_ptr, ptr, old_size < new_size ? old_size : new_size);
    }
    return new_ptr;
  }

  void LinearAllocator::reset() {
    current_offset_ = 0;
  }

  // ============================================================================
  // PoolAllocator implementation
  // ============================================================================

  PoolAllocator::PoolAllocator(usize element_size, usize element_count)
      : element_size_(element_size), element_count_(element_count), available_count_(element_count) {
    buffer_ = static_cast<u8*>(std::malloc(element_size * element_count));
    if (!buffer_) {
      throw std::bad_alloc();
    }

    // Initialize free list
    free_list_ = reinterpret_cast<Node*>(buffer_);
    Node* current = free_list_;
    for (usize i = 0; i < element_count - 1; ++i) {
      current->next = reinterpret_cast<Node*>(buffer_ + (i + 1) * element_size);
      current = current->next;
    }
    current->next = nullptr;
  }

  PoolAllocator::~PoolAllocator() {
    if (buffer_) {
      std::free(buffer_);
      buffer_ = nullptr;
    }
  }

  void* PoolAllocator::allocate(usize size, usize alignment) {
    (void)alignment;
    if (size != element_size_ || available_count_ == 0) {
      throw std::bad_alloc();
    }

    Node* node = free_list_;
    free_list_ = free_list_->next;
    --available_count_;
    return node;
  }

  void PoolAllocator::deallocate(void* ptr, usize size) {
    (void)size;
    if (!ptr) {
      return;
    }

    Node* node = reinterpret_cast<Node*>(ptr);
    node->next = free_list_;
    free_list_ = node;
    ++available_count_;
  }

  void* PoolAllocator::reallocate(void* ptr, usize old_size, usize new_size, usize alignment) {
    (void)old_size;
    (void)alignment;
    if (new_size != element_size_) {
      throw std::bad_alloc();
    }
    return ptr;
  }

  // ============================================================================
  // StackAllocator implementation
  // ============================================================================

  StackAllocator::StackAllocator(usize capacity) : capacity_(capacity), current_offset_(0) {
    buffer_ = static_cast<u8*>(std::malloc(capacity));
    if (!buffer_) {
      throw std::bad_alloc();
    }
  }

  StackAllocator::~StackAllocator() {
    if (buffer_) {
      std::free(buffer_);
      buffer_ = nullptr;
    }
  }

  void* StackAllocator::allocate(usize size, usize alignment) {
    usize aligned_offset = align_forward(reinterpret_cast<usize>(buffer_ + current_offset_), alignment);
    usize offset = aligned_offset - reinterpret_cast<usize>(buffer_);

    if (offset + size > capacity_) {
      throw std::bad_alloc();
    }

    u8* ptr = buffer_ + offset;
    current_offset_ = offset + size;
    return ptr;
  }

  void StackAllocator::deallocate(void* ptr, usize size) {
    if (ptr && size > 0 && reinterpret_cast<u8*>(ptr) + size == buffer_ + current_offset_) {
      current_offset_ -= size;
    }
  }

  void* StackAllocator::reallocate(void* ptr, usize old_size, usize new_size, usize alignment) {
    void* new_ptr = allocate(new_size, alignment);
    if (new_ptr && ptr) {
      std::memcpy(new_ptr, ptr, old_size < new_size ? old_size : new_size);
    }
    return new_ptr;
  }

  void StackAllocator::reset() {
    current_offset_ = 0;
  }

  // ============================================================================
  // AllocatorManager implementation
  // ============================================================================

  AllocatorManager& AllocatorManager::instance() {
    static AllocatorManager instance;
    return instance;
  }

  AllocatorManager::AllocatorManager() {
    default_allocator_ = std::make_unique<MallocAllocator>();
  }

  void AllocatorManager::set_default_allocator(Unique<Allocator> allocator) {
    default_allocator_ = std::move(allocator);
  }

  Allocator* AllocatorManager::get_default_allocator() {
    return default_allocator_.get();
  }

} // namespace hongtu::core
