#include "Box.h"
#include "Core.h"

uint32_t BoxInstance::sm_ModelIndex = ~0;

Box::Box() : Model("Assets/Models/Box.obj") {}


BoxInstance::BoxInstance(const glm::vec3& centre, const glm::vec3& size, const glm::vec3& rotateRadians, const Material& material)
: Instance {
   sm_ModelIndex,
   glm::transpose(
      glm::scale(
         glm::rotate(
            glm::rotate(
               glm::rotate(
                  glm::translate(glm::identity<glm::mat4x4>(), centre),
                  rotateRadians.x,
                  {1.0f, 0.0f, 0.0f}
               ),
               -rotateRadians.y,  // y axis flipped for vulkan
               {0.0f, 1.0f, 0.0f}
            ),
            rotateRadians.z,
            {0.0f, 0.0f, 1.0f}
         ),
         size
      )
   ),
   material
}
{
   ASSERT(sm_ModelIndex != ~0, "ERROR: Cube model instance has not been set.  You must set the cube model index (via SetModelIndex()) before instantiating a cube.");
}


void BoxInstance::SetModelIndex(uint32_t modelIndex) {
   sm_ModelIndex = modelIndex;
}