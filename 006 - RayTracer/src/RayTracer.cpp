#include "RayTracer.h"

#include "Bindings.glsl"
#include "Core.h"

using uint = uint32_t;
#include "Constants.glsl"
#include "Box.h"
#include "GeometryInstance.h"
#include "Offset.h"
#include "Rectangle2D.h"
#include "Sphere.h"

using mat4 = glm::mat4;
using uint = uint32_t;
using vec3 = glm::vec3;
#include "UniformBufferObject.glsl"

#include "Utility.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>

#include <random>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#define M_PI 3.14159265358979323846f

std::unique_ptr<Vulkan::Application> CreateApplication(int argc, const char* argv[]) {
   return std::make_unique<RayTracer>(argc, argv);
}


inline float RandomFloat() {
   static std::uniform_real_distribution<float> distribution(0.0, 1.0);
   static std::mt19937 generator;
   static std::function<float()> rand_generator = std::bind(distribution, generator);
   return rand_generator();
}


inline float RandomFloat(float min, float max) {
   return min + (max - min) * RandomFloat();
}


RayTracer::RayTracer(int argc, const char* argv[])
: Vulkan::Application {
   { "Ray Tracer", VK_MAKE_VERSION(1,0,0) },
#ifdef _DEBUG
   /*enableValidation=*/true
#else
   /*enableValidation=*/false
#endif
}
{
   Init();
}


RayTracer::~RayTracer() {
   DestroyDescriptorSets();
   DestroyDescriptorPool();
   DestroyPipeline();
   DestroyPipelineLayout();
   DestroyDescriptorSetLayout();
   DestroyUniformBuffers();
   DestroyStorageImages();
   DestroyAccelerationStructures();
   DestroyTextureResources();
   DestroyMaterialBuffer();
   DestroyAABBBuffer();
   DestroyOffsetBuffer();
   DestroyIndexBuffer();
   DestroyVertexBuffer();
}


std::vector<const char*> RayTracer::GetRequiredInstanceExtensions() {
   return {VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME};
}


std::vector<const char*> RayTracer::GetRequiredDeviceExtensions() {
   return {VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME, VK_NV_RAY_TRACING_EXTENSION_NAME, VK_KHR_MAINTENANCE3_EXTENSION_NAME, VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME};
}


vk::PhysicalDeviceFeatures RayTracer::GetRequiredPhysicalDeviceFeatures(vk::PhysicalDeviceFeatures availableFeatures) {
   vk::PhysicalDeviceFeatures features;
   if (availableFeatures.samplerAnisotropy) {
      features.setSamplerAnisotropy(true);
   } else {
      ASSERT(false, "Device does not support sampler anisotrophy")
   }
   return features;
}


void* RayTracer::GetRequiredPhysicalDeviceFeaturesEXT() {
   static vk::PhysicalDeviceDescriptorIndexingFeatures indexingFeatures;
   indexingFeatures.runtimeDescriptorArray = 1;
   return &indexingFeatures;
}


void RayTracer::Init() {
   Vulkan::Application::Init();

   auto properties = m_PhysicalDevice.getProperties2<vk::PhysicalDeviceProperties2, vk::PhysicalDeviceRayTracingPropertiesNV>();
   m_RayTracingProperties = properties.get<vk::PhysicalDeviceRayTracingPropertiesNV>();

   // Check requested push constant size against hardware limit
   // Specs require 128 bytes, so if the device complies our push constant buffer should always fit into memory
   if (properties.get<vk::PhysicalDeviceProperties2>().properties.limits.maxPushConstantsSize < sizeof(Constants)) {
      throw std::runtime_error("(Push)Constants too large");
   }

   CreateScene();
   CreateVertexBuffer();
   CreateIndexBuffer();
   CreateOffsetBuffer();
   CreateAABBBuffer();
   CreateMaterialBuffer();
   CreateTextureResources();
   CreateAccelerationStructures();
   CreateStorageImages();
   CreateUniformBuffers();
   CreateDescriptorSetLayout();
   CreatePipelineLayout();
   CreatePipeline();
   CreateDescriptorPool();
   CreateDescriptorSets();
   RecordCommandBuffers();
}


void RayTracer::CreateScene() {

   Model::SetDefaultShaderHitGroupIndex(eTrianglesHitGroup - eFirstHitGroup);
   Sphere::SetDefaultShaderHitGroupIndex(eSphereHitGroup - eFirstHitGroup);
   Box::SetDefaultShaderHitGroupIndex(eBoxHitGroup - eFirstHitGroup);

   m_Scene.AddTextureResource("Earth", "Assets/Textures/earthmap.jpg");

   SphereInstance::SetModelIndex(m_Scene.AddModel(std::make_unique<Sphere>()));
   BoxInstance::SetModelIndex(m_Scene.AddModel(std::make_unique<Box>(false)));
   ProceduralBoxInstance::SetModelIndex(m_Scene.AddModel(std::make_unique<Box>(true)));
   Rectangle2DInstance::SetModelIndex(m_Scene.AddModel(std::make_unique<Rectangle2D>()));

   //CreateSceneFurnaceTest();
   //CreateSceneNormalsTest();
   //CreateSceneSimple();
   //CreateSceneRayTracingInOneWeekend();
   //CreateSceneRayTracingTheNextWeekTexturesAndLight();
   //CreateSceneCornellBoxWithBoxes();
   //CreateSceneCornellBoxWithSmokeBoxes();
   //CreateSceneCornellBoxWithEarth();
   //CreateSceneRayTracingTheNextWeekFinal();
   CreateSceneWineGlass();

}


void RayTracer::CreateSceneFurnaceTest() {
   m_Eye = {8.0f, 2.0f, 2.0f};
   m_Direction = {-2.0f, -0.25f, -0.25};
   m_Up = {0.0f, 1.0f, 0.0f};

   m_Scene.SetHorizonColor({1.0f, 1.0f, 1.0f});
   m_Scene.SetZenithColor({1.0f, 1.0f, 1.0f});

   Material grey = Lambertian(FlatColor({0.5f, 0.5f, 0.5f}));
   Material metal = Metallic(FlatColor({0.5f, 0.5f, 0.5f}), 0.0);

   // Do not accumulate frames in the furnace test.
   // The point is to test 1 iteration of the random sampling, not accumulated result.
   m_Scene.SetAccumulateFrames(false);

   enum class Test {
      lambertian,
      metal
   };

   Test test = Test::lambertian;

   switch (test) {
      case Test::lambertian:
         // If lambertian material is working properly, then the rendered result
         // should be a uniform grey filled circle.
         // The color of the circle should be RGB(180,180,180)   (=sqrt(0.5) from gamma correction, times 255 for conversion to RGB)
         m_Scene.AddInstance(std::make_unique<SphereInstance>(glm::vec3 {0.0f, 1.0f, 2.0f}, 1.0f, grey));
         break;

      case Test::metal:
         // If metallic material is working properly, then the rendered result
         // should be a uniform grey filled circle.
         // Because:  metal material is a perfect reflector (real metals aren't),
         // and metal tints reflected light with its color (not so "glossy" non-metals)
         m_Scene.AddInstance(std::make_unique<SphereInstance>(glm::vec3 {0.0f, 1.0f, -2.0f}, 1.0f, metal));
         break;
   }

}


void RayTracer::CreateSceneNormalsTest() {
   m_Eye = {0.0f, 0.0f, 6.0f};
   m_Direction = {0.0f, 0.0f, -1.0};
   m_Up = {0.0f, 1.0f, 0.0f};

   uint wineGlass = m_Scene.AddModel(std::make_unique<Model>("Assets/Models/WineGlass.obj"));

   Material normals = Lambertian(Normals());

   // Objects shaded according to their normals.
   // Faces should be coloured consistently with the sphere.
   // (i.e. a face should be coloured the same as the point on the sphere that faces in same direction)

   m_Scene.AddInstance(std::make_unique<SphereInstance>(
      glm::vec3 {0.0f, 0.0f, 0.0f},
      1.0f,
      normals
   ));


   m_Scene.AddInstance(std::make_unique<BoxInstance>(
      glm::vec3 {2.0f, 0.0f, 0.0f},
      glm::vec3 {1.0f},
      glm::vec3 {glm::radians(20.0f), glm::radians(45.0f), glm::radians(0.0f)},
      normals
   ));
 

   glm::vec3 glassCentre = {-2.0f, 0.0f, 0.0f};
   glm::vec3 glassSize = {1.0f, 1.0f, 1.0f};
   glm::mat3x4 transform = glm::transpose(glm::scale(glm::translate(glm::identity<glm::mat4x4>(), glassCentre), glassSize));
   m_Scene.AddInstance(std::make_unique<Instance>(wineGlass, transform, normals));
}


void RayTracer::CreateSceneSimple() {
   m_Eye = {8.0f, 2.0f, 2.0f};
   m_Direction = {-2.0f, -0.25f, -0.5};
   m_Up = {0.0f, 1.0f, 0.0f};

   m_Scene.SetHorizonColor({0.2f, 0.2f, 0.2f});
   m_Scene.SetZenithColor({0.02f, 0.02f, 0.02f});

   Material blue = Lambertian(FlatColor({0.2f, 0.2f, 1.0f}));
   Material white = Lambertian(FlatColor({1.0f, 1.0f, 1.0f}));
   Material hardPlastic = Phong(FlatColor({0.2f, 0.2f, 1.0f}), 0.1f, 0.1f);
   Material chromium = Metallic(FlatColor({0.549f, 0.556f, 0.554f}), 0.0);
   Material light = Light(FlatColor({170.0f, 170.0f, 170.0f}), 0.0);

   m_Scene.AddInstance(std::make_unique<Rectangle2DInstance>(
      glm::vec3 {0.0f, 0.0f, 0.0f},
      glm::vec2 {1000.0f, 1000.0f},
      glm::vec3 {glm::radians(-90.0f), glm::radians(0.0f), glm::radians(0.0f)},
      blue
   ));

   m_Scene.AddInstance(std::make_unique<SphereInstance>(
      glm::vec3 {0.0f, 1.0f, 0.0f}   /*centre*/,
      1.0f                            /*radius*/,
      hardPlastic
   ));

   m_Scene.AddInstance(std::make_unique<SphereInstance>(
      glm::vec3(0.0f, 20.0f, 20.0f),
      1.0f,
      light
   ));
}

void RayTracer::CreateSceneRayTracingInOneWeekend() {
   m_Eye = {8.0f, 3.0f, 2.0f};
   m_Direction = {-2.0f, -0.5f, -0.5};
   m_Up = {0.0f, 1.0f, 0.0f};

   m_Scene.SetHorizonColor({0.75f, 0.85f, 1.0f});
   m_Scene.SetZenithColor({0.5f, 0.7f, 1.0f});

   m_Scene.AddInstance(std::make_unique<Rectangle2DInstance>(
      glm::vec3 {0.0f, 0.0f, 0.0f}                                         /*origin*/,
      glm::vec2 {1000.0f, 1000.0f}                                              /*size*/,
      glm::vec3 {glm::radians(-90.0f), glm::radians(0.0f), glm::radians(0.0f)}  /*rotation*/,
      Lambertian(                                                               /*material*/
         FlatColor({0.5f, 0.5f, 0.5f})                                             /*texture*/
      )
   ));

   // small random spheres
   for (int a = -11; a < 11; a++) {
      for (int b = -11; b < 11; b++) {
         float chooseMaterial = RandomFloat();
         vec3 centre(a + 0.9f * RandomFloat(), 1.2f, b + 0.9f * RandomFloat());
         if (
            (glm::length(centre - glm::vec3 {-4.0f, 0.2f, 0.0f}) > 0.9f) &&
            (glm::length(centre - glm::vec3 {0.0f, 0.2f, 0.0f}) > 0.9f) &&
            (glm::length(centre - glm::vec3 {4.0f, 0.2f, 0.0f}) > 0.9f)
         ) {
            Material material;
            if (chooseMaterial < 0.8) {
               // diffuse
               material = Lambertian(
                  FlatColor({RandomFloat() * RandomFloat(),RandomFloat() * RandomFloat(), RandomFloat() * RandomFloat()})
               );
            } else if (chooseMaterial < 0.95) {
               // metal
               material = Metallic(
                  FlatColor({0.5 * (RandomFloat(1.0f, 2.0f)), 0.5 * (RandomFloat(1.0f, 2.0f)), 0.5 * (RandomFloat(1.0f, 2.0f))}),
                  0.5f * RandomFloat()
               );
            } else {
               material = Dielectric(FlatColor({1.0f, 1.0f, 1.0f}), 1.5f);
            }
            m_Scene.AddInstance(std::make_unique<SphereInstance>(centre, 0.2f, material));
         }
      }
   }

   // the three main spheres...
   m_Scene.AddInstance(std::make_unique<SphereInstance>(
      glm::vec3 {0.0f, 1.0f, 0.0f}    /*centre*/,
      1.0f                            /*radius*/,
      Dielectric(                     /*material*/
         FlatColor(                      /*transmittance*/
            {1.0f, 1.0f, 1.0f}
         ),
         1.5f                         /*refractive index*/
      )
   ));

   m_Scene.AddInstance(std::make_unique<SphereInstance>(
      glm::vec3 {-4.0f, 1.00f, 0.0f}     /*centre*/,
      1.0f                              /*radius*/,
      Lambertian(                       /*material*/
         FlatColor({0.4f, 0.2f, 0.1f})     /*diffuse*/
      )
   ));

   m_Scene.AddInstance(std::make_unique<SphereInstance>(
      glm::vec3 {4.0f, 1.0f, 0.0f}       /*centre*/,
      1.0f                               /*radius*/,
      Metallic(                          /*material*/
         FlatColor({0.7f, 0.6f, 0.5f})      /*specular*/,
         0.01f                              /*roughness*/
      )
   ));
}


void RayTracer::CreateSceneRayTracingTheNextWeekTexturesAndLight() {
   m_Eye = {8.0f, 3.0f, 2.0f};
   m_Direction = {-2.0f, -0.5f, -0.5};
   m_Up = {0.0f, 1.0f, 0.0f};

   //m_Scene.SetHorizonColor({0.005f, 0.0f, 0.05f});
   //m_Scene.SetZenithColor({0.0f, 0.0f, 0.0f});
   m_Scene.SetHorizonColor({0.75f, 0.85f, 1.0f});
   m_Scene.SetZenithColor({0.5f, 0.7f, 1.0f});


   // note: Shifted everything up by 1 unit in the y direction, so that the background plane is not at y=0
   //       (checkerboard texture does not work well across large axis-aligned faces where sin(value) = 0)
   m_Scene.AddInstance(std::make_unique<Rectangle2DInstance>(
      glm::vec3 {0.0f, 1.0f, 0.0f}                                         /*origin*/,
      glm::vec2 {1000.0f, 1000.0f}                                              /*size*/,
      glm::vec3 {glm::radians(-90.0f), glm::radians(0.0f), glm::radians(0.0f)}  /*rotation*/,
      Lambertian(                                                               /*material*/
         CheckerBoard({0.2f, 0.3f, 0.1f}, {0.9, 0.9, 0.9}, 10.0f)                  /*texture*/
      )
   ));

   // small random spheres
   for (int a = -11; a < 11; a++) {
      for (int b = -11; b < 11; b++) {
         const float chooseMaterial = RandomFloat();
         const float chooseTexture = RandomFloat();
         const vec3 centre(a + 0.9f * RandomFloat(), 1.2f, b + 0.9f * RandomFloat());
         if (
            (glm::length(centre - glm::vec3 {-4.0f, 1.2f, 0.0f}) > 0.9f) &&
            (glm::length(centre - glm::vec3 {0.0f, 1.2f, 0.0f}) > 0.9f) &&
            (glm::length(centre - glm::vec3 {4.0f, 1.2f, 0.0f}) > 0.9f)
         ) {
            Material material;
            if (chooseMaterial < 0.8) {
               // diffuse
               if (chooseTexture < 0.33) {
                  // flatcolor
                  material = Lambertian(
                     FlatColor({RandomFloat() * RandomFloat(),RandomFloat() * RandomFloat(), RandomFloat() * RandomFloat()})
                  );
               } else if (chooseTexture < 0.67) {
                  // simplex
                  material = Lambertian(
                     Simplex3D(
                        {RandomFloat() * RandomFloat(), RandomFloat() * RandomFloat(), RandomFloat() * RandomFloat()},
                        10 * RandomFloat(),
                        RandomFloat()
                     )
                  );
               } else {
                  material = Lambertian(
                     Turbulence(
                        {RandomFloat() * RandomFloat(), RandomFloat() * RandomFloat(), RandomFloat() * RandomFloat()},
                        10 * RandomFloat(),
                        RandomFloat(),
                        static_cast<int>(10 * RandomFloat())
                     )
                  );
               }
            } else if (chooseMaterial < 0.95) {
               // metal
               material = Metallic(
                  FlatColor({0.5 * (1 + RandomFloat()), 0.5 * (1 + RandomFloat()), 0.5 * (1 + RandomFloat())}),
                  0.5f * RandomFloat()
               );
            } else {
               material = Light(FlatColor({10.0f, 10.0f, 10.0f}), 0.0f);
            }
            m_Scene.AddInstance(std::make_unique<SphereInstance>(centre, 0.2f, material));
         }
      }
   }

   // the three main spheres...
   m_Scene.AddInstance(std::make_unique<SphereInstance>(
      glm::vec3 {0.0f, 2.0f, 0.0f}                /*centre*/,
      1.0f                                        /*radius*/,
      Light(FlatColor({20.0f, 20.0f, 20.0f}), 0.0f) /*material*/
   ));
   m_Scene.AddInstance(std::make_unique<SphereInstance>(
      glm::vec3 {-4.0f, 2.0f, 0.0f}    /*centre*/,
      1.0f                             /*radius*/,
      Metallic(                           /*material*/
         FlatColor(                       /*texture*/
            {0.4f, 0.2f, 0.1f}               /*specular*/
         ),
         0.0f                                /*roughness*/
      )
   ));
   m_Scene.AddInstance(std::make_unique<SphereInstance>(
      glm::vec3 {4.0f, 2.0f, 0.0f}      /*centre*/,
      1.0f                              /*radius*/,
      Lambertian(                          /*material*/
         FlatColor({0.2f, 0.2f, 0.7f})     /*diffuse*/
      )
   ));
   m_Scene.AddInstance(std::make_unique<SphereInstance>(
      glm::vec3 {4.0f, 2.0f, 0.0f}      /*centre*/,
      1.001f                            /*radius*/,
      Dielectric(                          /*material*/
         FlatColor({1.0f, 1.0f, 1.0f})     /*transmittance*/,
         1.5f
      )
   ));
}


void RayTracer::CreateCornellBox(const glm::vec3 size, const float brightness) {
   m_Scene.SetHorizonColor({0.0f, 0.0f, 0.0f});
   m_Scene.SetZenithColor({0.f, 0.0f, 0.0f});

   m_Eye = {0.0f, 0.0f, 800.0f};
   m_Direction = {0.0f, 0.0f, -150.0f};

   const Material red = Lambertian(FlatColor({0.65f, 0.05f, 0.05f}));
   const Material green = Lambertian(FlatColor({0.12f, 0.45f, 0.15f}));
   const Material white = Lambertian(FlatColor({0.73f, 0.73f, 0.73f}));
   const Material light = Light(FlatColor({brightness, brightness, brightness}), 1.0f);

   const glm::vec3 halfSize = size / 2.0f;
   const glm::vec2 lightSize = {130.0f, 105.0f};

   const glm::vec3 clockwiseY90 = {glm::radians(0.0f), glm::radians(90.0f), glm::radians(0.0f)};
   const glm::vec3 counterClockwiseY90 = {glm::radians(0.0f), glm::radians(-90.0f), glm::radians(0.0f)};

   const glm::vec3 clockwiseX90 = {glm::radians(90.0f), glm::radians(0.0f), glm::radians(0.0f)};
   const glm::vec3 counterClockwiseX90 = {glm::radians(-90.0f), glm::radians(0.0f), glm::radians(0.0f)};

   m_Scene.AddInstance(std::make_unique<Rectangle2DInstance>(
      glm::vec3{-halfSize.x, 0.0f, -halfSize.z},
      glm::vec2{size.x, size.y},
      counterClockwiseY90,
      green
   ));

   m_Scene.AddInstance(std::make_unique<Rectangle2DInstance>(
      glm::vec3{halfSize.x, 0.0f, -halfSize.z},
      glm::vec2{size.x, size.y},
      clockwiseY90,
      red
   ));

    m_Scene.AddInstance(std::make_unique<Rectangle2DInstance>(
       glm::vec3{0.0f, halfSize.y, -halfSize.z},
       glm::vec2{size.x, size.y},
       clockwiseX90,
       white
    ));
 
    m_Scene.AddInstance(std::make_unique<Rectangle2DInstance>(
       glm::vec3{0.0f, -halfSize.y, -halfSize.z},
       glm::vec2{size.x, size.y},
       counterClockwiseX90,
       white
    ));
 
    m_Scene.AddInstance(std::make_unique<Rectangle2DInstance>(
       glm::vec3{0.0f, 0.0f, -size.z},
       glm::vec2{size.x, size.y},
       glm::vec3{0.0f},
       white
    ));
 
    m_Scene.AddInstance(std::make_unique<Rectangle2DInstance>(
       glm::vec3{0.0f, halfSize.y - 0.1f, -halfSize.z},
       lightSize,
       clockwiseX90,
       light
    ));
}


void RayTracer::CreateSceneCornellBoxWithBoxes() {
   const glm::vec3 size = {555.0f, 555.0f, 555.0f};
   const glm::vec3 halfSize = size / 2.0f;

   const Material white = Lambertian(FlatColor({0.73, 0.73, 0.73}));

   CreateCornellBox(size, 15.0f);

   const glm::vec3 box1Size = {165.0f, 330.0f, 165.0f};
   const glm::vec3 box1Centre = glm::vec3 {-halfSize.x * 0.30f, -(size.y - box1Size.y) * 0.5f, -halfSize.z * 1.25};
   const glm::vec3 box1Rotation = {glm::radians(0.0f), glm::radians(-15.0f), glm::radians(0.0f)};
   m_Scene.AddInstance(std::make_unique<BoxInstance>(box1Centre, box1Size, box1Rotation, white));

   const glm::vec3 box2Size = {165.0f, 165.0f, 165.0f};
   const glm::vec3 box2Centre = glm::vec3 {+halfSize.x * 0.35f, -(size.y - box2Size.y) * 0.5f, -halfSize.z * 0.65};
   const glm::vec3 box2Rotation = {glm::radians(0.0f), glm::radians(18.0f), glm::radians(0.0f)};
   m_Scene.AddInstance(std::make_unique<BoxInstance>(box2Centre, box2Size, box2Rotation, white));

}


void RayTracer::CreateSceneCornellBoxWithSmokeBoxes() {
   const glm::vec3 size = {555.0f, 555.0f, 555.0f};
   const glm::vec3 halfSize = size / 2.0f;

   const Material smoke = Smoke(FlatColor({0.0f, 0.0f, 0.0f}), 0.01f);
   const Material fog = Smoke(FlatColor({1.0f, 1.0f, 1.0f}), 0.01f);

   CreateCornellBox(size, 15.0f);

   const glm::vec3 box1Size = {165.0f, 330.0f, 165.0f};
   const glm::vec3 box1Centre = glm::vec3 {-halfSize.x * 0.30f, (-(size.y - box1Size.y) * 0.5f), -halfSize.z * 1.25};
   const glm::vec3 box1Rotation = {glm::radians(0.0f), glm::radians(-15.0f), glm::radians(0.0f)};
   m_Scene.AddInstance(std::make_unique<ProceduralBoxInstance>(box1Centre, box1Size, box1Rotation, smoke));

   const glm::vec3 box2Size = {165.0f, 165.0f, 165.0f};
   const glm::vec3 box2Centre = glm::vec3 {+halfSize.x * 0.35f, (-(size.y - box2Size.y) * 0.5f), -halfSize.z * 0.65};
   const glm::vec3 box2Rotation = {glm::radians(0.0f), glm::radians(18.0f), glm::radians(0.0f)};
   m_Scene.AddInstance(std::make_unique<ProceduralBoxInstance>(box2Centre, box2Size, box2Rotation, fog));
}


void RayTracer::CreateSceneCornellBoxWithEarth() {
   const glm::vec3 size = {555.0f, 555.0f, 555.0f};
   const glm::vec3 halfSize = size / 2.0f;

   CreateCornellBox(size, 15.0f);

   const float earthSize = 165.0f;
   const glm::vec3 earthCentre = glm::vec3 {+halfSize.x * 0.35f, (-(size.y - earthSize) * 0.5f), -halfSize.z * 0.65};
   m_Scene.AddInstance(std::make_unique<SphereInstance>(earthCentre, earthSize / 2.0f, Lambertian(Texture{m_Scene.GetTextureId("Earth")})));
}


void RayTracer::CreateSceneRayTracingTheNextWeekFinal() {
   m_Scene.SetHorizonColor({0.0f, 0.0f, 0.0f});
   m_Scene.SetZenithColor({0.f, 0.0f, 0.0f});

   m_Eye = {-200.0f, 0.0f, 600.0f};
   m_Direction = {50.0f, 1.0f, -140.0f};

   const Material green = Lambertian(FlatColor({0.48f, 0.83f, 0.53f}));
   const Material light = Light(FlatColor({7000.0f, 7000.0f, 7000.0f}), 27.0f);
   const Material white = Lambertian(FlatColor({0.73f, 0.73f, 0.73f}));
   const Material black = Lambertian(FlatColor({0.0f, 0.0f, 0.0f}));

   const int boxesPerSize = 20;
   const float boxSize = 100.0f;
   for (int i = 0; i < boxesPerSize; ++i) {
      for (int j = 0; j < boxesPerSize; ++j) {
         const glm::vec3 centre = {1278.0f - ((i + 0.5f) * boxSize), -278.0f, 1000.0f - ((j + 0.5f) * boxSize)};
         const glm::vec3 size = {boxSize, RandomFloat(1.0f, 101.0f), boxSize};
         m_Scene.AddInstance(std::make_unique<BoxInstance>(centre, size, glm::vec3 {}, green));
      }
   }

   // moving sphere. Not done.

   // glass sphere
   m_Scene.AddInstance(std::make_unique<SphereInstance>(glm::vec3{18.0f, -128.0f, -45.0f}, 50.0f, Dielectric(FlatColor({1.0f, 1.0f, 1.0f}), 1.5f)));

   // metal sphere
   m_Scene.AddInstance(std::make_unique<SphereInstance>(glm::vec3{278.0f, -128.0f, -145.0f}, 50.0f, Metallic(FlatColor({0.8f, 0.8f, 0.9f}), 1.0f)));

   // glass ball filled with blue smoke
   m_Scene.AddInstance(std::make_unique<SphereInstance>(glm::vec3{-82.0f, -128.0f, -145.0f}, 70.0f, Dielectric(FlatColor({1.0f, 1.0f, 1.0f}), 1.5f)));
   m_Scene.AddInstance(std::make_unique<SphereInstance>(glm::vec3{-82.0f, -128.0f, -145.0f}, 69.99f, Smoke(FlatColor({0.2f, 0.4f, 0.9f}), 0.2f)));

   // polystyrene cube
   glm::mat4x4 transform = glm::rotate(glm::translate(glm::identity<glm::mat4x4>(), {213.0f, -8.0f, -560.0f}), glm::radians(15.0f), {0.0f, 1.0f, 0.0f});
   for (int i = 0; i < 1000; ++i) {
      const glm::vec4 centre = {RandomFloat(0.0f, 165.0f), RandomFloat(0.0f, 165.0f), RandomFloat(0.0f, 165.0f), 1.0f};
      const glm::vec4 centreTransformed = transform * centre;
      m_Scene.AddInstance(std::make_unique<SphereInstance>(centreTransformed, 10.0f, white));
   }

   // marble ball
   m_Scene.AddInstance(std::make_unique<SphereInstance>(glm::vec3{58.0f, 2.0f, -300.0f}, 80.0f, Lambertian(Marble({1.0f, 1.0f, 1.0f}, 0.01f, 0.5f, 7))));

   // earth textured sphere
   m_Scene.AddInstance(std::make_unique<SphereInstance>(glm::vec3{-122.0f, -78.0f, -400.0f}, 100.0f, Lambertian(Texture{m_Scene.GetTextureId("Earth")})));


   // ceiling
   //m_Scene.AddInstance(std::make_unique<Rectangle2DInstance>(glm::vec3{5.0f - 1000.0f - 150.f, 276.0f, -278.0f}, glm::vec2{2000.0f, 4132.5f}, glm::vec3{glm::radians(90.0f), glm::radians(0.0f), glm::radians(0.0f)}, black));
   //m_Scene.AddInstance(std::make_unique<Rectangle2DInstance>(glm::vec3{5.0f + 1000.0f + 150.0f, 276.0f, -278.0f}, glm::vec2{2000.0f, 4132.5f}, glm::vec3{glm::radians(90.0f), glm::radians(0.0f), glm::radians(0.0f)}, black));
   //m_Scene.AddInstance(std::make_unique<Rectangle2DInstance>(glm::vec3{5.0f, 276.0f, -278.0f + 132.5 + 1000.0f}, glm::vec2{300.0f, 2000.0f}, glm::vec3{glm::radians(90.0f), glm::radians(0.0f), glm::radians(0.0f)}, black));
   //m_Scene.AddInstance(std::make_unique<Rectangle2DInstance>(glm::vec3{5.0f, 276.0f, -278.0 - 132.5 - 1000.f}, glm::vec2{300.0f, 2000.0f}, glm::vec3{glm::radians(90.0f), glm::radians(0.0f), glm::radians(0.0f)}, black));

   // mist under ceiling
   //m_Scene.AddInstance(std::make_unique<ProceduralBoxInstance>(glm::vec3{0.0f, 0.0f, 0.0f}, glm::vec3{2000.0f, 554.0f, 2000.0f}, glm::vec3{}, Smoke(FlatColor({1.0f, 1.0f, 1.0f}), 0.0001f)));

   // mist covering whole scene
   m_Scene.AddInstance(std::make_unique<SphereInstance>(glm::vec3{0.0f, 0.0f, 0.0f}, 2000.0f, Smoke(FlatColor({1.0f, 1.0f, 1.0f}), 0.0001f)));

   // The light
   m_Scene.AddInstance(std::make_unique<Rectangle2DInstance>(glm::vec3{5.0f, 276.0f, -279.5f}, glm::vec2{30.0f, 26.0f}, glm::vec3{glm::radians(90.0f), glm::radians(0.0f), glm::radians(0.0f)}, light));
}


void RayTracer::CreateSceneWineGlass() {

   const glm::vec3 size = {555.0f, 555.0f, 555.0f};

   m_Scene.SetHorizonColor({0.0f, 0.0f, 0.0f});
   m_Scene.SetZenithColor({0.0f, 0.0f, 0.0f});

   uint wineGlass = m_Scene.AddModel(std::make_unique<Model>("Assets/Models/WineGlass.obj"));

   CreateCornellBox(size, 50.0f);

   Material glass = Dielectric(FlatColor({1.0f, 1.0f, 1.0f}), 1.5f);
   Material chromium = Metallic(FlatColor({0.549f, 0.556f, 0.554f}), 0.0);

   const glm::vec2 rectSize = {165.0f, 330.0f};
   const glm::vec3 rectCentre = glm::vec3 {-120.0f, -(size.y - rectSize.y) * 0.5f, -250.0f};
   const glm::vec3 rectRotation = {glm::radians(0.0f), glm::radians(-39.5f), glm::radians(0.0f)};
   m_Scene.AddInstance(std::make_unique<Rectangle2DInstance>(rectCentre, rectSize, rectRotation, chromium));

   glm::vec3 glassCentre = {130.0f, -278.0f, -170.0f};
   glm::vec3 glassSize = {200.0f, 200.0f, 200.0f};

   glm::mat3x4 transform = glm::transpose(glm::scale(glm::translate(glm::identity<glm::mat4x4>(), glassCentre), glassSize));
   m_Scene.AddInstance(std::make_unique<Instance>(wineGlass, transform, glass));

}


void RayTracer::CreateVertexBuffer() {
   std::vector<Vertex> vertices;
   size_t vertexCount = 0;
   for (const auto& model : m_Scene.GetModels()) {
      vertexCount += model->GetVertices().size();
   }
   vertices.reserve(vertexCount);

   // for each model in scene, pack its vertices into vertex buffer
   for (const auto& model : m_Scene.GetModels()) {
      vertices.insert(vertices.end(), model->GetVertices().begin(), model->GetVertices().end());
   }

   vk::DeviceSize size = vertices.size() * sizeof(Vertex);
   Vulkan::Buffer stagingBuffer(m_Device, m_PhysicalDevice, size, vk::BufferUsageFlagBits::eTransferSrc, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
   stagingBuffer.CopyFromHost(0, size, vertices.data());

   m_VertexBuffer = std::make_unique<Vulkan::Buffer>(m_Device, m_PhysicalDevice, size, vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eStorageBuffer, vk::MemoryPropertyFlagBits::eDeviceLocal);
   CopyBuffer(stagingBuffer.m_Buffer, m_VertexBuffer->m_Buffer, 0, 0, size);
}


void RayTracer::DestroyVertexBuffer() {
   m_VertexBuffer.reset(nullptr);
}


void RayTracer::CreateIndexBuffer() {
   std::vector<uint32_t> indices;
   size_t indexCount = 0;
   for (const auto& model : m_Scene.GetModels()) {
      indexCount += model->GetIndices().size();
   }
   indices.reserve(indexCount);

   // for each model in scene, pack its indices into index buffer
   for (const auto& model : m_Scene.GetModels()) {
      indices.insert(indices.end(), model->GetIndices().begin(), model->GetIndices().end());
   }

   uint32_t count = static_cast<uint32_t>(indices.size());
   vk::DeviceSize size = count * sizeof(uint32_t);

   Vulkan::Buffer stagingBuffer = {
      m_Device,
      m_PhysicalDevice,
      size,
      vk::BufferUsageFlagBits::eTransferSrc,
      vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
   };
   stagingBuffer.CopyFromHost(0, size, indices.data());

   m_IndexBuffer = std::make_unique<Vulkan::IndexBuffer>(m_Device, m_PhysicalDevice, size, count, vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eStorageBuffer, vk::MemoryPropertyFlagBits::eDeviceLocal);
   CopyBuffer(stagingBuffer.m_Buffer, m_IndexBuffer->m_Buffer, 0, 0, size);
}


void RayTracer::CreateOffsetBuffer() {
   std::vector<Offset> modelOffsets;
   std::vector<Offset> instanceOffsets;
   modelOffsets.reserve(m_Scene.GetModels().size());
   uint32_t vertexOffset = 0;
   uint32_t indexOffset = 0;
   for (const auto& model : m_Scene.GetModels()) {
      modelOffsets.push_back({vertexOffset, indexOffset});
      vertexOffset += static_cast<uint32_t>(model->GetVertices().size());
      indexOffset += static_cast<uint32_t>(model->GetIndices().size());
   }

   instanceOffsets.reserve(m_Scene.GetInstances().size());
   for (const auto& instance : m_Scene.GetInstances()) {
      instanceOffsets.push_back(modelOffsets[instance->GetModelIndex()]);
   };

   vk::DeviceSize size = instanceOffsets.size() * sizeof(Offset);

   Vulkan::Buffer stagingBuffer(m_Device, m_PhysicalDevice, size, vk::BufferUsageFlagBits::eTransferSrc, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
   stagingBuffer.CopyFromHost(0, size, instanceOffsets.data());

   m_OffsetBuffer = std::make_unique<Vulkan::Buffer>(m_Device, m_PhysicalDevice, size, vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eStorageBuffer, vk::MemoryPropertyFlagBits::eDeviceLocal);
   CopyBuffer(stagingBuffer.m_Buffer, m_OffsetBuffer->m_Buffer, 0, 0, size);
}


void RayTracer::DestroyOffsetBuffer() {
   m_OffsetBuffer.reset(nullptr);
}


void RayTracer::CreateAABBBuffer() {
   std::vector<std::array<glm::vec3, 2>> aabbs;
   aabbs.reserve(m_Scene.GetModels().size());
   for (const auto& model : m_Scene.GetModels()) {
      if (model->IsProcedural()) {
         aabbs.emplace_back(model->GetBoundingBox());
      }
   }

   vk::DeviceSize size = aabbs.size() * sizeof(std::array<glm::vec3, 2>);

   if (size > 0) {
      Vulkan::Buffer stagingBuffer(m_Device, m_PhysicalDevice, size, vk::BufferUsageFlagBits::eTransferSrc, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
      stagingBuffer.CopyFromHost(0, size, aabbs.data());

      m_AABBBuffer = std::make_unique<Vulkan::Buffer>(m_Device, m_PhysicalDevice, size, vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eStorageBuffer, vk::MemoryPropertyFlagBits::eDeviceLocal);
      CopyBuffer(stagingBuffer.m_Buffer, m_AABBBuffer->m_Buffer, 0, 0, size);
   }
}


void RayTracer::DestroyAABBBuffer() {
   m_AABBBuffer.reset(nullptr);
}


void RayTracer::CreateMaterialBuffer() {
   std::vector<Material> materials;
   materials.reserve(m_Scene.GetInstances().size());
   for (const auto& instance : m_Scene.GetInstances()) {
      materials.emplace_back(instance->GetMaterial());
   };

   vk::DeviceSize size = materials.size() * sizeof(Material);

   Vulkan::Buffer stagingBuffer(m_Device, m_PhysicalDevice, size, vk::BufferUsageFlagBits::eTransferSrc, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
   stagingBuffer.CopyFromHost(0, size, materials.data());

   m_MaterialBuffer = std::make_unique<Vulkan::Buffer>(m_Device, m_PhysicalDevice, size, vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eStorageBuffer, vk::MemoryPropertyFlagBits::eDeviceLocal);
   CopyBuffer(stagingBuffer.m_Buffer, m_MaterialBuffer->m_Buffer, 0, 0, size);
}


void RayTracer::DestroyMaterialBuffer() {
   m_MaterialBuffer.reset(nullptr);
}


void RayTracer::CreateTextureResources() {
   for (const auto& textureFileName : m_Scene.GetTextureFileNames()) {
      int texWidth;
      int texHeight;
      int texChannels;

      stbi_uc* pixels = stbi_load(textureFileName.c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
      vk::DeviceSize size = static_cast<vk::DeviceSize>(texWidth) * static_cast<vk::DeviceSize>(texHeight) * 4;
      uint32_t mipLevels = 1; // static_cast<uint32_t>(std::floor(std::log2(std::max(texWidth, texHeight)))) + 1;

      if (!pixels) {
         ASSERT(false, "ERROR: failed to load texture '{}'", textureFileName);
      }

      Vulkan::Buffer stagingBuffer(m_Device, m_PhysicalDevice, size, vk::BufferUsageFlagBits::eTransferSrc, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
      stagingBuffer.CopyFromHost(0, size, pixels);

      stbi_image_free(pixels);

      auto texture = std::make_unique<Vulkan::Image>(
         m_Device,
         m_PhysicalDevice,
         texWidth,
         texHeight,
         mipLevels,
         vk::SampleCountFlagBits::e1,
         vk::Format::eR8G8B8A8Unorm,
         vk::ImageTiling::eOptimal,
         vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
         vk::MemoryPropertyFlagBits::eDeviceLocal
      );
      TransitionImageLayout(texture->m_Image, vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal, mipLevels);
      CopyBufferToImage(stagingBuffer.m_Buffer, texture->m_Image, texWidth, texHeight);
      GenerateMIPMaps(texture->m_Image, vk::Format::eR8G8B8A8Unorm, texWidth, texHeight, mipLevels);

      texture->CreateImageView(vk::Format::eR8G8B8A8Unorm, vk::ImageAspectFlagBits::eColor, mipLevels);

      m_Textures.emplace_back(std::move(texture));
   }

   vk::SamplerCreateInfo ci = {
      {}                                  /*flags*/,
      vk::Filter::eLinear                 /*magFilter*/,
      vk::Filter::eLinear                 /*minFilter*/,
      vk::SamplerMipmapMode::eLinear      /*mipmapMode*/,
      vk::SamplerAddressMode::eRepeat     /*addressModeU*/,
      vk::SamplerAddressMode::eRepeat     /*addressModeV*/,
      vk::SamplerAddressMode::eRepeat     /*addressModeW*/,
      0.0f                                /*mipLodBias*/,
      true                                /*anisotropyEnable*/,
      16                                  /*maxAnisotropy*/,
      false                               /*compareEnable*/,
      vk::CompareOp::eAlways              /*compareOp*/,
      0.0f                                /*minLod*/,
      static_cast<float>(1/*mipLevels*/)       /*maxLod*/,
      vk::BorderColor::eFloatOpaqueBlack  /*borderColor*/,
      false                               /*unnormalizedCoordinates*/
   };
   m_TextureSampler = m_Device.createSampler(ci);
}


void RayTracer::DestroyTextureResources() {
   if (m_Device && m_TextureSampler) {
      m_Device.destroy(m_TextureSampler);
      m_TextureSampler = nullptr;
   }
   for (auto& texture : m_Textures) {
      texture.reset(nullptr);
   }
   m_Textures.clear();
}


void RayTracer::CreateAccelerationStructures() {
   vk::DeviceSize vertexOffset = 0;
   vk::DeviceSize indexOffset = 0;
   vk::DeviceSize aabbOffset = 0;
   std::vector<std::vector<vk::GeometryNV>> geometryGroups;

   geometryGroups.reserve(m_Scene.GetModels().size());
   for (const auto& model : m_Scene.GetModels()) {
      std::vector<vk::GeometryNV> geometries;
      geometries.reserve(1);
      geometries.emplace_back(
         model->IsProcedural() ?
         vk::GeometryNV {
            vk::GeometryTypeNV::eAabbs                               /*geometryType*/,
            vk::GeometryDataNV {
               vk::GeometryTrianglesNV {}                                   /*triangles*/,
               vk::GeometryAABBNV {
                  m_AABBBuffer->m_Buffer                                       /*aabbData*/,
                  1                                                            /*numAABBs*/,
                  2 * sizeof(glm::vec3)                                        /*stride*/,
                  aabbOffset                                                   /*offset*/
               }                                                            /*aabbs*/
            }                                                            /*geometry*/,
            vk::GeometryFlagBitsNV::eOpaque                              /*flags*/
         }
         :
         vk::GeometryNV {
            vk::GeometryTypeNV::eTriangles                               /*geometryType*/,
            vk::GeometryDataNV {
               vk::GeometryTrianglesNV {
                  m_VertexBuffer->m_Buffer                                     /*vertexData*/,
                  vertexOffset                                                 /*vertexOffset*/,
                  static_cast<uint32_t>(model->GetVertices().size())               /*vertexCount*/,
                  static_cast<vk::DeviceSize>(sizeof(Vertex))                  /*vertexStride*/,
                  vk::Format::eR32G32B32Sfloat                                 /*vertexFormat*/,
                  m_IndexBuffer->m_Buffer                                      /*indexData*/,
                  indexOffset                                                  /*indexOffset*/,
                  static_cast<uint32_t>(model->GetIndices().size())                /*indexCount*/,
                  vk::IndexType::eUint32                                       /*indexType*/,
                  nullptr                                                      /*transformData*/,
                  0                                                            /*transformOffset*/
               }                                                            /*triangles*/
            }                                                            /*geometry*/,
            vk::GeometryFlagBitsNV::eOpaque                              /*flags*/
         }
      );
      geometryGroups.emplace_back(std::move(geometries));
      vertexOffset += model->GetVertices().size() * sizeof(Vertex);
      indexOffset += model->GetIndices().size() * sizeof(uint32_t);
      if (model->IsProcedural()) {
         aabbOffset += 2 * sizeof(glm::vec3);
      }
   }

   CreateBottomLevelAccelerationStructures(geometryGroups);

   uint32_t i = 0;
   std::vector<Vulkan::GeometryInstance> geometryInstances;

   geometryInstances.reserve(m_Scene.GetInstances().size());
   for (const auto& instance : m_Scene.GetInstances()) {
      ASSERT(m_BLAS.at(instance->GetModelIndex()).m_Handle, "ERROR: BLAS handle is null.  Have you forgotten to allocate and bind memory?");
      geometryInstances.emplace_back(
         instance->GetTransform(),
         i++                                                                           /*instance index*/,
         0xff                                                                          /*visibility mask*/,
         m_Scene.GetModels().at(instance->GetModelIndex())->GetShaderHitGroupIndex()   /*hit group index*/,
         static_cast<uint32_t>(vk::GeometryInstanceFlagBitsNV::eTriangleCullDisable)   /*instance flags*/,
         m_BLAS.at(instance->GetModelIndex()).m_Handle                                 /*acceleration structure handle*/
      );
   };

   // Each geometry instance instantiates all of the geometries that are in the BLAS that the instance refers to.
   // If you want to instantiate geometries independently of each other, then they need to be in different BLASs
   // Apparently, a general rule is that the fewer BLASs the better. 
   CreateTopLevelAccelerationStructure(geometryInstances);

   BuildAccelerationStructures(geometryInstances);
}


void RayTracer::DestroyAccelerationStructures() {
   DestroyTopLevelAccelerationStructure();
   DestroyBottomLevelAccelerationStructures();
}


void RayTracer::DestroyIndexBuffer() {
   m_IndexBuffer.reset(nullptr);
}


void RayTracer::CreateStorageImages() {
   m_OutputImage = std::make_unique<Vulkan::Image>(m_Device, m_PhysicalDevice, m_Extent.width, m_Extent.height, 1, vk::SampleCountFlagBits::e1, m_Format, vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eStorage, vk::MemoryPropertyFlagBits::eDeviceLocal);
   m_OutputImage->CreateImageView(m_Format, vk::ImageAspectFlagBits::eColor, 1);
   TransitionImageLayout(m_OutputImage->m_Image, vk::ImageLayout::eUndefined, vk::ImageLayout::eGeneral, 1);

   m_AccumumlationImage = std::make_unique<Vulkan::Image>(m_Device, m_PhysicalDevice, m_Extent.width, m_Extent.height, 1, vk::SampleCountFlagBits::e1, vk::Format::eR32G32B32A32Sfloat, vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eStorage, vk::MemoryPropertyFlagBits::eDeviceLocal);
   m_AccumumlationImage->CreateImageView(vk::Format::eR32G32B32A32Sfloat, vk::ImageAspectFlagBits::eColor, 1);
   TransitionImageLayout(m_AccumumlationImage->m_Image, vk::ImageLayout::eUndefined, vk::ImageLayout::eGeneral, 1);
}


void RayTracer::DestroyStorageImages() {
   m_AccumumlationImage.reset(nullptr);
   m_OutputImage.reset(nullptr);
}


void RayTracer::CreateUniformBuffers() {
   vk::DeviceSize size = sizeof(UniformBufferObject);

   // We are potentially queuing up more than one command buffer for rendering at once.
   // (see m_Settings.MaxFramesInFlight).
   // This means that each command buffer needs its own uniform buffer.
   // (if they all shared one uniform buffer, then we might start updating the uniform buffer
   // when a previously queued command buffer hasn't yet finished rendering).
   //
   // If we had instead made sure that only one command buffer is ever rendering at once,
   // then we could get away with just one uniform buffer.

   //
   // TODO: experiment: We are creating the uniform buffers as hostVisible and hostCoherent
   //                   Would it be better to have them as device-local  (and copy after each update)?
   m_UniformBuffers.reserve(m_CommandBuffers.size());
   for (const auto& commandBuffer : m_CommandBuffers) {
      m_UniformBuffers.emplace_back(m_Device, m_PhysicalDevice, size, vk::BufferUsageFlagBits::eUniformBuffer, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
   }
}


void RayTracer::DestroyUniformBuffers() {
   m_UniformBuffers.clear();
}


void RayTracer::CreateDescriptorSetLayout() {
   // Setup layout of descriptors used in this example
   // Basically connects the different shader stages to descriptors for binding uniform buffers, image samplers, etc.
   // So every shader binding should map to one descriptor set layout binding

   vk::DescriptorSetLayoutBinding accelerationStructureLB = {
      BINDING_TLAS                                                                 /*binding*/,
      vk::DescriptorType::eAccelerationStructureNV                                 /*descriptorType*/,
      1                                                                            /*descriptorCount*/,
      vk::ShaderStageFlagBits::eRaygenNV | vk::ShaderStageFlagBits::eClosestHitNV  /*stageFlags*/,
      nullptr                                                                      /*pImmutableSamplers*/
   };

   vk::DescriptorSetLayoutBinding accumulationImageLB = {
      BINDING_ACCUMULATIONIMAGE             /*binding*/,
      vk::DescriptorType::eStorageImage     /*descriptorType*/,
      1                                     /*descriptorCount*/,
      vk::ShaderStageFlagBits::eRaygenNV    /*stageFlags*/,
      nullptr                               /*pImmutableSamplers*/
   };

   vk::DescriptorSetLayoutBinding outputImageLB = {
      BINDING_OUTPUTIMAGE                   /*binding*/,
      vk::DescriptorType::eStorageImage     /*descriptorType*/,
      1                                     /*descriptorCount*/,
      vk::ShaderStageFlagBits::eRaygenNV    /*stageFlags*/,
      nullptr                               /*pImmutableSamplers*/
   };

   vk::DescriptorSetLayoutBinding uniformBufferLB = {
      BINDING_UNIFORMBUFFER                                                                                           /*binding*/,
      vk::DescriptorType::eUniformBuffer                                                                              /*descriptorType*/,
      1                                                                                                               /*descriptorCount*/,
      vk::ShaderStageFlagBits::eRaygenNV | vk::ShaderStageFlagBits::eIntersectionNV | vk::ShaderStageFlagBits::eClosestHitNV | vk::ShaderStageFlagBits::eMissNV  /*stageFlags*/,
      nullptr                                                                                                         /*pImmutableSamplers*/
   };

   vk::DescriptorSetLayoutBinding vertexBufferLB = {
      BINDING_VERTEXBUFFER                      /*binding*/,
      vk::DescriptorType::eStorageBuffer        /*descriptorType*/,
      1                                         /*descriptorCount*/,
      vk::ShaderStageFlagBits::eClosestHitNV    /*stageFlags*/,
      nullptr                                   /*pImmutableSamplers*/
   };
   
   vk::DescriptorSetLayoutBinding indexBufferLB = {
      BINDING_INDEXBUFFER                       /*binding*/,
      vk::DescriptorType::eStorageBuffer        /*descriptorType*/,
      1                                         /*descriptorCount*/,
      vk::ShaderStageFlagBits::eClosestHitNV   /*stageFlags*/,
      nullptr                                   /*pImmutableSamplers*/
   };

   vk::DescriptorSetLayoutBinding offsetBufferLB = {
      BINDING_OFFSETBUFFER                      /*binding*/,
      vk::DescriptorType::eStorageBuffer        /*descriptorType*/,
      1                                         /*descriptorCount*/,
      vk::ShaderStageFlagBits::eClosestHitNV    /*stageFlags*/,
      nullptr                                   /*pImmutableSamplers*/
   };

   vk::DescriptorSetLayoutBinding materialBufferLB = {
      BINDING_MATERIALBUFFER                    /*binding*/,
      vk::DescriptorType::eStorageBuffer        /*descriptorType*/,
      1                                         /*descriptorCount*/,
      vk::ShaderStageFlagBits::eIntersectionNV | vk::ShaderStageFlagBits::eClosestHitNV  /*stageFlags*/,
      nullptr                                   /*pImmutableSamplers*/
   };

   vk::DescriptorSetLayoutBinding textureSamplerLB = {
      BINDING_TEXTURESAMPLERS                     /*binding*/,
      vk::DescriptorType::eCombinedImageSampler   /*descriptorType*/,
      static_cast<uint32_t>(m_Textures.size())    /*descriptorCount*/,
      vk::ShaderStageFlagBits::eClosestHitNV      /*stageFlags*/,
      nullptr                                     /*pImmutableSamplers*/
   };

   std::array<vk::DescriptorSetLayoutBinding, BINDING_NUMBINDINGS> layoutBindings = {
      accelerationStructureLB,
      accumulationImageLB,
      outputImageLB,
      uniformBufferLB,
      vertexBufferLB,
      indexBufferLB,
      offsetBufferLB,
      materialBufferLB,
      textureSamplerLB
   };

   m_DescriptorSetLayout = m_Device.createDescriptorSetLayout({
      {}                                           /*flags*/,
      static_cast<uint32_t>(layoutBindings.size()) /*bindingCount*/,
      layoutBindings.data()                        /*pBindings*/
   });
}


void RayTracer::DestroyDescriptorSetLayout() {
   if (m_Device && m_DescriptorSetLayout) {
      m_Device.destroy(m_DescriptorSetLayout);
      m_DescriptorSetLayout = nullptr;
   }
}


void RayTracer::CreatePipelineLayout() {
   // Create the pipeline layout that is used to generate the rendering pipelines that are based on the descriptor set layout
   // In a more complex scenario you would have different pipeline layouts for different descriptor set layouts that could be reused

   vk::PushConstantRange pushConstantRange = {
      vk::ShaderStageFlagBits::eRaygenNV | vk::ShaderStageFlagBits::eMissNV    /*stageFlags*/,
      0                                                                        /*offset*/,
      static_cast<uint32_t>(sizeof(Constants))                                 /*size*/
   };

   m_PipelineLayout = m_Device.createPipelineLayout({
      {}                       /*flags*/,
      1                        /*setLayoutCount*/,
      &m_DescriptorSetLayout   /*pSetLayouts*/,
      1                        /*pushConstantRangeCount*/,
      &pushConstantRange       /*pPushConstantRanges*/
   });
}


void RayTracer::DestroyPipelineLayout() {
   if (m_Device && m_PipelineLayout) {
      m_Device.destroy(m_PipelineLayout);
      m_PipelineLayout = nullptr;
   }
}

void RayTracer::CreatePipeline() {
   // Create the graphics pipeline used in this example
   // Vulkan uses the concept of rendering pipelines to encapsulate fixed states, replacing OpenGL's complex state machine
   // A pipeline is then stored and hashed on the GPU making pipeline changes very fast
   // Note: There are still a few dynamic states that are not directly part of the pipeline (but the info that they are used is)

   enum {
      eRayGen,
      eMiss,
      eTrianglesClosestHit,
      eSphereIntersection,
      eSphereClosestHit,
      eBoxIntersection,
      eBoxClosestHit,

      eNumShaders
   };

   std::array<vk::PipelineShaderStageCreateInfo, eNumShaders> shaderStages;

   shaderStages[eRayGen] = vk::PipelineShaderStageCreateInfo {
      {}                                                                           /*flags*/,
      vk::ShaderStageFlagBits::eRaygenNV                                           /*stage*/,
      CreateShaderModule(Vulkan::ReadFile("Assets/Shaders/RayTrace.rgen.spv"))     /*module*/,
      "main"                                                                       /*name*/,
      nullptr                                                                      /*pSpecializationInfo*/
   };

   shaderStages[eMiss] = vk::PipelineShaderStageCreateInfo {
      {}                                                                           /*flags*/,
      vk::ShaderStageFlagBits::eMissNV                                             /*stage*/,
      CreateShaderModule(Vulkan::ReadFile("Assets/Shaders/RayTrace.rmiss.spv"))    /*module*/,
      "main"                                                                       /*name*/,
      nullptr                                                                      /*pSpecializationInfo*/
   };

   shaderStages[eTrianglesClosestHit] = vk::PipelineShaderStageCreateInfo {
      {}                                                                           /*flags*/,
      vk::ShaderStageFlagBits::eClosestHitNV                                       /*stage*/,
      CreateShaderModule(Vulkan::ReadFile("Assets/Shaders/Triangles.rchit.spv"))   /*module*/,
      "main"                                                                       /*name*/,
      nullptr                                                                      /*pSpecializationInfo*/
   };

   shaderStages[eSphereIntersection] = vk::PipelineShaderStageCreateInfo {
      {}                                                                           /*flags*/,
      vk::ShaderStageFlagBits::eIntersectionNV                                     /*stage*/,
      CreateShaderModule(Vulkan::ReadFile("Assets/Shaders/Sphere.rint.spv"))       /*module*/,
      "main"                                                                       /*name*/,
      nullptr                                                                      /*pSpecializationInfo*/
   };

   shaderStages[eSphereClosestHit] = vk::PipelineShaderStageCreateInfo {
      {}                                                                           /*flags*/,
      vk::ShaderStageFlagBits::eClosestHitNV                                       /*stage*/,
      CreateShaderModule(Vulkan::ReadFile("Assets/Shaders/Sphere.rchit.spv"))      /*module*/,
      "main"                                                                       /*name*/,
      nullptr                                                                      /*pSpecializationInfo*/
   };

   shaderStages[eBoxIntersection] = vk::PipelineShaderStageCreateInfo {
      {}                                                                           /*flags*/,
      vk::ShaderStageFlagBits::eIntersectionNV                                     /*stage*/,
      CreateShaderModule(Vulkan::ReadFile("Assets/Shaders/Box.rint.spv"))          /*module*/,
      "main"                                                                       /*name*/,
      nullptr                                                                      /*pSpecializationInfo*/
   };

   shaderStages[eBoxClosestHit] = vk::PipelineShaderStageCreateInfo {
      {}                                                                           /*flags*/,
      vk::ShaderStageFlagBits::eClosestHitNV                                       /*stage*/,
      CreateShaderModule(Vulkan::ReadFile("Assets/Shaders/Box.rchit.spv"))         /*module*/,
      "main"                                                                       /*name*/,
      nullptr                                                                      /*pSpecializationInfo*/
   };


   std::array<vk::RayTracingShaderGroupCreateInfoNV, eNumShaderGroups> groups;

   groups[eRayGenGroup] = vk::RayTracingShaderGroupCreateInfoNV {
      vk::RayTracingShaderGroupTypeNV::eGeneral /*type*/,
      eRayGen                                   /*generalShader*/,
      VK_SHADER_UNUSED_NV                       /*closestHitShader*/,
      VK_SHADER_UNUSED_NV                       /*anyHitShader*/,
      VK_SHADER_UNUSED_NV                       /*intersectionShader*/
   };

   groups[eMissGroup] = vk::RayTracingShaderGroupCreateInfoNV {
      vk::RayTracingShaderGroupTypeNV::eGeneral /*type*/,
      eMiss                                     /*generalShader*/,
      VK_SHADER_UNUSED_NV                       /*closestHitShader*/,
      VK_SHADER_UNUSED_NV                       /*anyHitShader*/,
      VK_SHADER_UNUSED_NV                       /*intersectionShader*/
   };

   groups[eTrianglesHitGroup] = vk::RayTracingShaderGroupCreateInfoNV {
      vk::RayTracingShaderGroupTypeNV::eTrianglesHitGroup /*type*/,
      VK_SHADER_UNUSED_NV                       /*generalShader*/,
      eTrianglesClosestHit                      /*closestHitShader*/,
      VK_SHADER_UNUSED_NV                       /*anyHitShader*/,
      VK_SHADER_UNUSED_NV                       /*intersectionShader*/
   };

   groups[eSphereHitGroup] = vk::RayTracingShaderGroupCreateInfoNV {
      vk::RayTracingShaderGroupTypeNV::eProceduralHitGroup /*type*/,
      VK_SHADER_UNUSED_NV                       /*generalShader*/,
      eSphereClosestHit                         /*closestHitShader*/,
      VK_SHADER_UNUSED_NV                       /*anyHitShader*/,
      eSphereIntersection                       /*intersectionShader*/
   };

   groups[eBoxHitGroup] = vk::RayTracingShaderGroupCreateInfoNV {
      vk::RayTracingShaderGroupTypeNV::eProceduralHitGroup /*type*/,
      VK_SHADER_UNUSED_NV                       /*generalShader*/,
      eBoxClosestHit                            /*closestHitShader*/,
      VK_SHADER_UNUSED_NV                       /*anyHitShader*/,
      eBoxIntersection                          /*intersectionShader*/
   };

   vk::RayTracingPipelineCreateInfoNV pipelineCI = {
      {}                                         /*flags*/,
      static_cast<uint32_t>(shaderStages.size()) /*stageCount*/,
      shaderStages.data()                        /*pStages*/,
      static_cast<uint32_t>(groups.size())       /*groupCount*/,
      groups.data()                              /*pGroups*/,
      1                                          /*maxRecursionDepth*/,
      m_PipelineLayout                           /*layout*/,
      nullptr                                    /*basePipelineHandle*/,
      0                                          /*basePipelineIndex*/
   };

   // .value works around bug in Vulkan.hpp (refer https://github.com/KhronosGroup/Vulkan-Hpp/issues/659)
   m_Pipeline = m_Device.createRayTracingPipelineNV(m_PipelineCache, pipelineCI).value;

   // Create buffer for the shader binding table.
   // Note that regardless of the shaderGroupHandleSize, the entries in the shader binding table must be aligned on multiples of m_RayTracingProperties.shaderGroupBaseAlignment
   uint32_t shaderBindingTableEntrySize = (m_RayTracingProperties.shaderGroupHandleSize + m_RayTracingProperties.shaderGroupBaseAlignment - 1) & ~(m_RayTracingProperties.shaderGroupBaseAlignment - 1);

   const vk::DeviceSize tableSize = static_cast<vk::DeviceSize>(shaderBindingTableEntrySize) * eNumShaderGroups;
   const vk::DeviceSize handlesSize = static_cast<vk::DeviceSize>(m_RayTracingProperties.shaderGroupHandleSize) * eNumShaderGroups;

   uint32_t tableStride = shaderBindingTableEntrySize;
   uint32_t handleStride = m_RayTracingProperties.shaderGroupHandleSize;

   // First, get all the handles from the device,  and then copy them to the shader binding table with the correct alignment
   std::vector<uint8_t> shaderHandleStorage;
   shaderHandleStorage.resize(handlesSize);
   m_Device.getRayTracingShaderGroupHandlesNV<uint8_t>(m_Pipeline, 0, eNumShaderGroups, shaderHandleStorage);

   std::vector<uint8_t> shaderBindingTable;
   shaderBindingTable.resize(tableSize);

   for (uint32_t i = 0; i < eNumShaderGroups; ++i) {
      std::memcpy(shaderBindingTable.data() + (i * tableStride), shaderHandleStorage.data() + (i * handleStride), handleStride);
   }

   m_ShaderBindingTable = std::make_unique<Vulkan::Buffer>(m_Device, m_PhysicalDevice, tableSize, vk::BufferUsageFlagBits::eRayTracingNV, vk::MemoryPropertyFlagBits::eHostVisible);
   m_ShaderBindingTable->CopyFromHost(0, tableSize, shaderBindingTable.data());

   // Shader modules are no longer needed once the graphics pipeline has been created
   for (auto& shaderStage : shaderStages) {
      DestroyShaderModule(shaderStage.module);
   }
}


void RayTracer::DestroyPipeline() {
   m_ShaderBindingTable.reset(nullptr);
   if (m_Device && m_Pipeline) {
      m_Device.destroy(m_Pipeline);
   }
}


void RayTracer::CreateDescriptorPool() {
   std::array<vk::DescriptorPoolSize, 5> typeCounts = {
      vk::DescriptorPoolSize {
         vk::DescriptorType::eAccelerationStructureNV,
         static_cast<uint32_t>(m_SwapChainFrameBuffers.size())
      },
      vk::DescriptorPoolSize {
         vk::DescriptorType::eStorageImage,
         static_cast<uint32_t>(2 * m_SwapChainFrameBuffers.size())
      },
      vk::DescriptorPoolSize {
         vk::DescriptorType::eUniformBuffer,
         static_cast<uint32_t>(m_SwapChainFrameBuffers.size())
      },
      vk::DescriptorPoolSize {
         vk::DescriptorType::eStorageBuffer,
         static_cast<uint32_t>(4 * m_SwapChainFrameBuffers.size()) // 4 storage buffers:  Vertex, Index, Offset, Material
      },
      vk::DescriptorPoolSize {
         vk::DescriptorType::eCombinedImageSampler,
         static_cast<uint32_t>(m_Textures.size() * m_SwapChainFrameBuffers.size())
      }
   };

   vk::DescriptorPoolCreateInfo descriptorPoolCI = {
      vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet       /*flags*/,
      static_cast<uint32_t>(54 * m_SwapChainFrameBuffers.size()) /*maxSets*/,
      static_cast<uint32_t>(typeCounts.size())                   /*poolSizeCount*/,
      typeCounts.data()                                          /*pPoolSizes*/
   };
   m_DescriptorPool = m_Device.createDescriptorPool(descriptorPoolCI);
}


void RayTracer::DestroyDescriptorPool() {
   if (m_Device && m_DescriptorPool) {
      m_Device.destroy(m_DescriptorPool);
   }
}


void RayTracer::CreateDescriptorSets() {

   std::vector layouts(m_SwapChainFrameBuffers.size(), m_DescriptorSetLayout);
   vk::DescriptorSetAllocateInfo allocInfo = {
      m_DescriptorPool,
      static_cast<uint32_t>(layouts.size()),
      layouts.data()
   };
   m_DescriptorSets = m_Device.allocateDescriptorSets(allocInfo);

   // Update the descriptor set determining the shader binding points
   // For every binding point used in a shader there needs to be one
   // descriptor set matching that binding point

   for (uint32_t i = 0; i < m_SwapChainFrameBuffers.size(); ++i) {
      vk::WriteDescriptorSetAccelerationStructureNV descriptorAccelerationStructureInfo = {
         1                               /*accelerationStructureCount*/,
         &m_TLAS.m_AccelerationStructure /*pAccelerationStructures*/
      };

      vk::WriteDescriptorSet accelerationStructureWrite = {
         m_DescriptorSets[i]                          /*dstSet*/,
         BINDING_TLAS                                 /*dstBinding*/,
         0                                            /*dstArrayElement*/,
         1                                            /*descriptorCount*/,
         vk::DescriptorType::eAccelerationStructureNV /*descriptorType*/,
         nullptr                                      /*pImageInfo*/,
         nullptr                                      /*pBufferInfo*/,
         nullptr                                      /*pTexelBufferView*/
      };
      accelerationStructureWrite.pNext = &descriptorAccelerationStructureInfo;

      vk::DescriptorImageInfo accumulationImageDescriptor = {
         nullptr                             /*sampler*/,
         m_AccumumlationImage->m_ImageView   /*imageView*/,
         vk::ImageLayout::eGeneral           /*imageLayout*/
      };
      vk::WriteDescriptorSet accumulationImageWrite = {
         m_DescriptorSets[i]                          /*dstSet*/,
         BINDING_ACCUMULATIONIMAGE                    /*dstBinding*/,
         0                                            /*dstArrayElement*/,
         1                                            /*descriptorCount*/,
         vk::DescriptorType::eStorageImage            /*descriptorType*/,
         &accumulationImageDescriptor                 /*pImageInfo*/,
         nullptr                                      /*pBufferInfo*/,
         nullptr                                      /*pTexelBufferView*/
      };

      vk::DescriptorImageInfo outputImageDescriptor = {
         nullptr                       /*sampler*/,
         m_OutputImage->m_ImageView   /*imageView*/,
         vk::ImageLayout::eGeneral     /*imageLayout*/
      };
      vk::WriteDescriptorSet outputImageWrite = {
         m_DescriptorSets[i]                          /*dstSet*/,
         BINDING_OUTPUTIMAGE                          /*dstBinding*/,
         0                                            /*dstArrayElement*/,
         1                                            /*descriptorCount*/,
         vk::DescriptorType::eStorageImage            /*descriptorType*/,
         &outputImageDescriptor                       /*pImageInfo*/,
         nullptr                                      /*pBufferInfo*/,
         nullptr                                      /*pTexelBufferView*/
      };

      vk::WriteDescriptorSet uniformBufferWrite = {
         m_DescriptorSets[i]                          /*dstSet*/,
         BINDING_UNIFORMBUFFER                        /*dstBinding*/,
         0                                            /*dstArrayElement*/,
         1                                            /*descriptorCount*/,
         vk::DescriptorType::eUniformBuffer           /*descriptorType*/,
         nullptr                                      /*pImageInfo*/,
         &m_UniformBuffers[i].m_Descriptor            /*pBufferInfo*/,
         nullptr                                      /*pTexelBufferView*/
      };

      vk::DescriptorBufferInfo vertexBufferDescriptor = {
         m_VertexBuffer->m_Buffer /*buffer*/,
         0                        /*offset*/,
         VK_WHOLE_SIZE            /*range*/
      };
      vk::WriteDescriptorSet vertexBufferWrite = {
         m_DescriptorSets[i]                          /*dstSet*/,
         BINDING_VERTEXBUFFER                         /*dstBinding*/,
         0                                            /*dstArrayElement*/,
         1                                            /*descriptorCount*/,
         vk::DescriptorType::eStorageBuffer           /*descriptorType*/,
         nullptr                                      /*pImageInfo*/,
         &vertexBufferDescriptor                      /*pBufferInfo*/,
         nullptr                                      /*pTexelBufferView*/
      };

      vk::DescriptorBufferInfo indexBufferDescriptor = {
         m_IndexBuffer->m_Buffer  /*buffer*/,
         0                        /*offset*/,
         VK_WHOLE_SIZE            /*range*/
      };
      vk::WriteDescriptorSet indexBufferWrite = {
         m_DescriptorSets[i]                          /*dstSet*/,
         BINDING_INDEXBUFFER                          /*dstBinding*/,
         0                                            /*dstArrayElement*/,
         1                                            /*descriptorCount*/,
         vk::DescriptorType::eStorageBuffer           /*descriptorType*/,
         nullptr                                      /*pImageInfo*/,
         &indexBufferDescriptor                       /*pBufferInfo*/,
         nullptr                                      /*pTexelBufferView*/
      };

      vk::DescriptorBufferInfo offsetBufferDescriptor = {
         m_OffsetBuffer->m_Buffer    /*buffer*/,
         0                           /*offset*/,
         VK_WHOLE_SIZE               /*range*/
      };
      vk::WriteDescriptorSet offsetBufferWrite = {
         m_DescriptorSets[i]                          /*dstSet*/,
         BINDING_OFFSETBUFFER                         /*dstBinding*/,
         0                                            /*dstArrayElement*/,
         1                                            /*descriptorCount*/,
         vk::DescriptorType::eStorageBuffer           /*descriptorType*/,
         nullptr                                      /*pImageInfo*/,
         &offsetBufferDescriptor                      /*pBufferInfo*/,
         nullptr                                      /*pTexelBufferView*/
      };

      vk::DescriptorBufferInfo materialBufferDescriptor = {
         m_MaterialBuffer->m_Buffer  /*buffer*/,
         0                           /*offset*/,
         VK_WHOLE_SIZE               /*range*/
      };
      vk::WriteDescriptorSet materialBufferWrite = {
         m_DescriptorSets[i]                          /*dstSet*/,
         BINDING_MATERIALBUFFER                       /*dstBinding*/,
         0                                            /*dstArrayElement*/,
         1                                            /*descriptorCount*/,
         vk::DescriptorType::eStorageBuffer           /*descriptorType*/,
         nullptr                                      /*pImageInfo*/,
         &materialBufferDescriptor                    /*pBufferInfo*/,
         nullptr                                      /*pTexelBufferView*/
      };

      std::vector<vk::DescriptorImageInfo> textureImageDescriptors;
      textureImageDescriptors.reserve(m_Textures.size());
      for(const auto& texture : m_Textures) {
         textureImageDescriptors.emplace_back(
            m_TextureSampler                          /*sampler  (can use same sampler for multiple textures)*/,
            texture->m_ImageView                      /*imageView*/,
            vk::ImageLayout::eShaderReadOnlyOptimal   /*imageLayout*/
         );
      }

      vk::WriteDescriptorSet textureSamplersWrite = {
         m_DescriptorSets[i]                          /*dstSet*/,
         BINDING_TEXTURESAMPLERS                      /*dstBinding*/,
         0                                            /*dstArrayElement*/,
         static_cast<uint32_t>(m_Textures.size())     /*descriptorCount*/,
         vk::DescriptorType::eCombinedImageSampler    /*descriptorType*/,
         textureImageDescriptors.data()               /*pImageInfo*/,
         nullptr                                      /*pBufferInfo*/,
         nullptr                                      /*pTexelBufferView*/
      };

      std::array<vk::WriteDescriptorSet, BINDING_NUMBINDINGS> writeDescriptorSets = {
         accelerationStructureWrite,
         accumulationImageWrite,
         outputImageWrite,
         uniformBufferWrite,
         vertexBufferWrite,
         indexBufferWrite,
         offsetBufferWrite,
         materialBufferWrite,
         textureSamplersWrite
      };

      m_Device.updateDescriptorSets(writeDescriptorSets, nullptr);
   }
}


void RayTracer::DestroyDescriptorSets() {
   if (m_Device) {
      m_Device.freeDescriptorSets(m_DescriptorPool, m_DescriptorSets);
      m_DescriptorSets.clear();
   }
}


void RayTracer::RecordCommandBuffers() {
   // Record the command buffers that are submitted to the graphics queue at each render.
   // We record one commend buffer per frame buffer (this allows us to pre-record the command
   // buffers, as we can bind the command buffer to its frame buffer here
   // (as opposed to having just one command buffer that gets built and then bound to appropriate frame buffer
   // at render time)
   vk::CommandBufferBeginInfo commandBufferBI = {
      {}      /*flags*/,
      nullptr /*pInheritanceInfo*/
   };

   vk::ImageSubresourceRange subresourceRange = {
      vk::ImageAspectFlagBits::eColor   /*aspectMask*/,
      0                                 /*baseMipLevel*/,
      1                                 /*levelCount*/,
      0                                 /*baseArrayLayer*/,
      1                                 /*layerCount*/
   };

   // TODO: You probably don't actually want these things to be "constants"
   //       all of them are things that you might want to be able to change
   //       (without re-recording the entire command buffer)
   //       Could just shove them into the uniform buffer object instead.
   Constants constants = {
      3                           /*min ray bounces*/,
      64                          /*max ray bounces*/,
      0.0                         /*lens aperture            DISABLED IN RAYGEN SHADER*/,
      800.0                       /*lens focal length        DISABLED IN RAYGEN SHADER*/
   };

   for (uint32_t i = 0; i < m_CommandBuffers.size(); ++i) {
      vk::CommandBuffer& commandBuffer = m_CommandBuffers[i];
      commandBuffer.begin(commandBufferBI);
      commandBuffer.pushConstants<Constants>(m_PipelineLayout, vk::ShaderStageFlagBits::eRaygenNV | vk::ShaderStageFlagBits::eMissNV, 0, constants);
      commandBuffer.bindPipeline(vk::PipelineBindPoint::eRayTracingNV, m_Pipeline);
      commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eRayTracingNV, m_PipelineLayout, 0, m_DescriptorSets[i], nullptr);  // (i)th command buffer is bound to the (i)th descriptor set

      uint32_t shaderBindingTableEntrySize = (m_RayTracingProperties.shaderGroupHandleSize + m_RayTracingProperties.shaderGroupBaseAlignment - 1) & ~(m_RayTracingProperties.shaderGroupBaseAlignment - 1);

      commandBuffer.traceRaysNV(
         m_ShaderBindingTable->m_Buffer, static_cast<vk::DeviceSize>(shaderBindingTableEntrySize) * eRayGenGroup,
         m_ShaderBindingTable->m_Buffer, static_cast<vk::DeviceSize>(shaderBindingTableEntrySize) * eMissGroup, shaderBindingTableEntrySize,
         m_ShaderBindingTable->m_Buffer, static_cast<vk::DeviceSize>(shaderBindingTableEntrySize) * eFirstHitGroup, shaderBindingTableEntrySize,
         nullptr, 0, 0,
         m_Extent.width, m_Extent.height, 1
      );

      vk::ImageMemoryBarrier barrier = {
         {}                                    /*srcAccessMask*/,
         vk::AccessFlagBits::eTransferWrite    /*dstAccessMask*/,
         vk::ImageLayout::eUndefined           /*oldLayout*/,
         vk::ImageLayout::eTransferDstOptimal  /*newLayout*/,
         VK_QUEUE_FAMILY_IGNORED               /*srcQueueFamilyIndex*/,
         VK_QUEUE_FAMILY_IGNORED               /*dstQueueFamilyIndex*/,
         m_SwapChainImages[i].m_Image          /*image*/,
         subresourceRange
      };
      commandBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eAllCommands, vk::PipelineStageFlagBits::eAllCommands, {}, nullptr, nullptr, barrier);

      barrier = {
         {}                                    /*srcAccessMask*/,
         vk::AccessFlagBits::eTransferRead     /*dstAccessMask*/,
         vk::ImageLayout::eGeneral             /*oldLayout*/,
         vk::ImageLayout::eTransferSrcOptimal  /*newLayout*/,
         VK_QUEUE_FAMILY_IGNORED               /*srcQueueFamilyIndex*/,
         VK_QUEUE_FAMILY_IGNORED               /*dstQueueFamilyIndex*/,
         m_OutputImage->m_Image               /*image*/,
         subresourceRange
      };
      commandBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eAllCommands, vk::PipelineStageFlagBits::eAllCommands, {}, nullptr, nullptr, barrier);

      vk::ImageCopy copyRegion = {
         {vk::ImageAspectFlagBits::eColor, 0, 0, 1 } /*srcSubresource*/,
         {0, 0, 0}                                   /*srcOffset*/,
         {vk::ImageAspectFlagBits::eColor, 0, 0, 1 } /*dstSubresource*/,
         {0, 0, 0}                                   /*dstOffset*/,
         {m_Extent.width, m_Extent.height, 1}        /*extent*/
      };
      commandBuffer.copyImage(m_OutputImage->m_Image, vk::ImageLayout::eTransferSrcOptimal, m_SwapChainImages[i].m_Image, vk::ImageLayout::eTransferDstOptimal, copyRegion);

      barrier = {
         vk::AccessFlagBits::eTransferWrite    /*srcAccessMask*/,
         {}                                    /*dstAccessMask*/,
         vk::ImageLayout::eTransferDstOptimal  /*oldLayout*/,
         vk::ImageLayout::ePresentSrcKHR       /*newLayout*/,
         VK_QUEUE_FAMILY_IGNORED               /*srcQueueFamilyIndex*/,
         VK_QUEUE_FAMILY_IGNORED               /*dstQueueFamilyIndex*/,
         m_SwapChainImages[i].m_Image          /*image*/,
         subresourceRange
      };
      commandBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eAllCommands, vk::PipelineStageFlagBits::eAllCommands, {}, nullptr, nullptr, barrier);

      barrier = {
         vk::AccessFlagBits::eTransferRead     /*srcAccessMask*/,
         {}                                    /*dstAccessMask*/,
         vk::ImageLayout::eTransferSrcOptimal  /*oldLayout*/,
         vk::ImageLayout::eGeneral             /*newLayout*/,
         VK_QUEUE_FAMILY_IGNORED               /*srcQueueFamilyIndex*/,
         VK_QUEUE_FAMILY_IGNORED               /*dstQueueFamilyIndex*/,
         m_OutputImage->m_Image               /*image*/,
         subresourceRange
      };
      commandBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eAllCommands, vk::PipelineStageFlagBits::eAllCommands, {}, nullptr, nullptr, barrier);

      commandBuffer.end();
   }
}


void RayTracer::Update(double deltaTime) {
   __super::Update(deltaTime);

   if (
      (glfwGetKey(m_Window, GLFW_KEY_W) == GLFW_PRESS) ||
      (glfwGetKey(m_Window, GLFW_KEY_A) == GLFW_PRESS) ||
      (glfwGetKey(m_Window, GLFW_KEY_S) == GLFW_PRESS) ||
      (glfwGetKey(m_Window, GLFW_KEY_D) == GLFW_PRESS) ||
      (glfwGetKey(m_Window, GLFW_KEY_R) == GLFW_PRESS) ||
      (glfwGetKey(m_Window, GLFW_KEY_F) == GLFW_PRESS) ||
      m_LeftMouseDown
   ) {
      m_AccumulatedImageCount = 0;
   }
   if (!m_Scene.GetAccumulateFrames()) {
      m_AccumulatedImageCount = 0;
   }
   ++m_AccumulatedImageCount;
}


void RayTracer::RenderFrame() {

   glm::mat4 projection = glm::perspective(m_FoVRadians, static_cast<float>(m_Extent.width) / static_cast<float>(m_Extent.height), 0.01f, 100.0f);
   // flip y axis for vulkan
   projection[1][1] *= -1;
   glm::mat4 modelView = glm::lookAt(m_Eye, m_Eye + glm::normalize(m_Direction), m_Up);

   UniformBufferObject ubo = {
      glm::inverse(modelView),
      glm::inverse(projection),
      glm::vec4{m_Scene.GetHorizonColor(), 0.0f},
      glm::vec4{m_Scene.GetZenithColor(), 0.0f},
      m_AccumulatedImageCount
   };

   // All the rendering instructions are in pre-recorded command buffer (which gets submitted to the GPU in EndFrame()).  All we have to do here is update the uniform buffer.
   BeginFrame();
   m_UniformBuffers[m_CurrentImage].CopyFromHost(0, sizeof(UniformBufferObject), &ubo);
   EndFrame();
}


void RayTracer::OnWindowResized() {
   __super::OnWindowResized();
   DestroyDescriptorSets();
   CreateStorageImages();
   CreateDescriptorSets();
   RecordCommandBuffers();
   m_AccumulatedImageCount = 0;
}


