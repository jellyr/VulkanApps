#pragma once

#include "Instance.h"

class Sphere : public Instance {
public:
   Sphere(glm::vec3 centre, float radius, Material material);

public:
   static void SetModelIndex(uint32_t modelIndex);

private:
   static uint32_t sm_modelIndex;
};
