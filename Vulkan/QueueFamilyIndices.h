#pragma once

#include <optional>

namespace Vulkan {

struct QueueFamilyIndices {
   std::optional<uint32_t> GraphicsFamily;
   std::optional<uint32_t> PresentFamily;

   bool IsComplete() {
      return GraphicsFamily.has_value() && PresentFamily.has_value();
   }
};

}
