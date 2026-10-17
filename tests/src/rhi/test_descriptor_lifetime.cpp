#include <gtest/gtest.h>
#include <hongtu/rhi/RHIDevice.h>

using namespace HongTu::RHI;

namespace {
  class DescriptorLifetimeTest : public ::testing::Test {
  protected:
    RHIFactory* factory = nullptr;
    RHIDevice* device = nullptr;
    RHIBufferPtr buffer;
    RHIDescriptorSetLayoutPtr layout;
    DescriptorSetDesc description;

    void SetUp() override {
      factory = Backend::createFactory(Backend::Type::Vulkan);
      ASSERT_NE(factory, nullptr);
      device = factory->createDevice(false);
      ASSERT_NE(device, nullptr) << "This actual Vulkan resource gate requires a GPU device";
      BufferDesc data;
      data.size = 16;
      data.usage = static_cast<BufferUsageFlags>(BufferUsageFlagBits::Uniform);
      data.memoryProperties = static_cast<MemoryPropertyFlags>(MemoryPropertyFlagBits::HostVisible);
      buffer = device->createBuffer(data);
      ASSERT_NE(buffer, nullptr);
      DescriptorSetLayoutDesc definition;
      definition.bindings.push_back(
          {0, 1, static_cast<ShaderStageFlags>(ShaderStageFlagBits::Vertex), DescriptorType::UniformBuffer});
      layout = device->createDescriptorSetLayout(definition);
      ASSERT_NE(layout, nullptr);
      DescriptorBinding binding;
      binding.type = DescriptorType::UniformBuffer;
      binding.buffer = buffer;
      description.bindings.push_back(binding);
      ASSERT_TRUE(device->getDescriptorAllocationInfo().available);
    }

    void TearDown() override {
      description.bindings.clear();
      buffer.reset();
      layout.reset();
      if (device)
        factory->destroyDevice(device);
      if (factory)
        Backend::destroyFactory(factory);
    }
  };
} // namespace

TEST_F(DescriptorLifetimeTest, ReleasedSetsReuseActualOverflowPools) {
  uint32_t warmPools = 0;
  constexpr size_t count = 3000;
  for (int cycle = 0; cycle < 4; ++cycle) {
    std::vector<RHIDescriptorSetPtr> sets;
    for (size_t i = 0; i < count; ++i) {
      auto set = device->createDescriptorSet(layout, description);
      ASSERT_NE(set, nullptr) << cycle << ':' << i;
      sets.push_back(std::move(set));
    }
    const auto full = device->getDescriptorAllocationInfo();
    EXPECT_EQ(full.liveSets, count);
    if (cycle == 0)
      warmPools = full.poolCount;
    EXPECT_EQ(full.poolCount, warmPools);
    sets.clear(); // No submissions reference these test-only allocations.
    const auto empty = device->getDescriptorAllocationInfo();
    EXPECT_EQ(empty.liveSets, 0u);
    EXPECT_EQ(empty.allocatedSets, empty.freedSets);
    EXPECT_EQ(empty.allocatedSets, count * (cycle + 1));
    EXPECT_EQ(empty.freeFailures, 0u);
  }
  EXPECT_GT(warmPools, 1u) << "Must exercise real overflow-pool release as well as the primary pool";
}

TEST_F(DescriptorLifetimeTest, RejectedBindingsReleaseTheirAllocation) {
  auto invalid = description;
  invalid.bindings[0].buffer.reset();
  EXPECT_EQ(device->createDescriptorSet(layout, invalid), nullptr);
  const auto info = device->getDescriptorAllocationInfo();
  EXPECT_EQ(info.allocatedSets, 1u);
  EXPECT_EQ(info.freedSets, 1u);
  EXPECT_EQ(info.liveSets, 0u);
  EXPECT_EQ(info.freeFailures, 0u);
  EXPECT_EQ(device->createDescriptorSet(nullptr, description), nullptr);
}

TEST_F(DescriptorLifetimeTest, SharedOwnerReleasesOnceAndCanOutliveDevice) {
  auto set = device->createDescriptorSet(layout, description);
  ASSERT_NE(set, nullptr);
  auto copy = set;
  set.reset();
  EXPECT_EQ(device->getDescriptorAllocationInfo().liveSets, 1u);
  copy.reset();
  EXPECT_EQ(device->getDescriptorAllocationInfo().liveSets, 0u);
  auto late = device->createDescriptorSet(layout, description);
  ASSERT_NE(late, nullptr);
  description.bindings.clear();
  buffer.reset();
  layout.reset();
  factory->destroyDevice(device);
  device = nullptr;
  EXPECT_NO_THROW(late.reset()); // Pool teardown already freed the native set.
}
