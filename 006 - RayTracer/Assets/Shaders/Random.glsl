#extension GL_EXT_control_flow_attributes : require

// Generates a seed for a random number generator from 2 inputs plus a backoff
// https://github.com/nvpro-samples/optix_prime_baking/blob/master/random.h
// https://en.wikipedia.org/wiki/Tiny_Encryption_Algorithm
uint InitRandomSeed(uint val0, uint val1) {
   uint v0 = val0, v1 = val1, s0 = 0;
   
   [[unroll]] 
   for (uint n = 0; n < 16; n++) {
      s0 += 0x9e3779b9;
      v0 += ((v1 << 4) + 0xa341316c) ^ (v1 + s0) ^ ((v1 >> 5) + 0xc8013ea4);
      v1 += ((v0 << 4) + 0xad90777d) ^ (v0 + s0) ^ ((v0 >> 5) + 0x7e95761e);
   }

   return v0;
}


uint RandomInt(inout uint seed) {
   // LCG values from Numerical Recipes
   return (seed = 1664525 * seed + 1013904223);
}


float RandomFloat(inout uint seed) {
   // Float version using bitmask from Numerical Recipes
   const uint one = 0x3f800000;
   const uint msk = 0x007fffff;
   return uintBitsToFloat(one | (msk & (RandomInt(seed) >> 9))) - 1.0;
}


float RandomFloat(float minValue, float maxValue, inout uint seed) {
   return minValue + (maxValue - minValue) * RandomFloat(seed);
}


vec2 RandomInUnitDisk(inout uint seed) {
   vec2 p;
   do {
      p = 2.0 * vec2(RandomFloat(seed), RandomFloat(seed)) - 1.0;
   } while (dot(p, p) > 1.0f);
   return p;
}


vec3 RandomInUnitSphere(inout uint seed) {
   vec3 p;
   do {
      p = 2.0 * vec3(RandomFloat(seed), RandomFloat(seed), RandomFloat(seed)) - 1.0;
   } while (dot(p, p) > 1.0);
   return p;
}


vec3 RandomUnitVector(inout uint seed) {
   const float a = RandomFloat(0.0, 2.0 * 3.1415926535897932384626433832795, seed);
   const float z = RandomFloat(-1.0, 1.0, seed);
   const float r = sqrt(1.0 - z * z);
   return vec3(r * cos(a), r * sin(a), z);
}


mat3x3 GetOrthoNormalBasis(const vec3 normal) {
   vec3 helper = vec3(1.0, 0.0, 0.0);
   if (abs(normal.x) > 0.99f) {
      helper = vec3(0.0, 0.0, 1.0);
   }
   vec3 tangent = normalize(cross(normal, helper));
   vec3 binormal = normalize(cross(normal, tangent));
   return mat3x3(tangent, binormal, normal);
}


// alpha = 0 -> uniform sampling
// alpha = 1 -> cosine sampling
// alpha > 1 -> phong sampling
vec3 RandomOnUnitHemisphere(const vec3 normal, const float alpha, inout uint seed) {
   const float cosTheta = pow(RandomFloat(0.0, 1.0, seed), 1.0 / (alpha + 1.0));
   const float sinTheta = sqrt(1.0 - cosTheta * cosTheta);
   const float phi = RandomFloat(0.0, 2.0 * 3.1415926535897932384626433832795, seed);
   return GetOrthoNormalBasis(normal) * vec3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
}
