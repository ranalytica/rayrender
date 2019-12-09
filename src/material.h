#ifndef MATERIALH
#define MATERIALH 

#include "ray.h"
#include "hitable.h"
#include "onbh.h"
#include "pdf.h"
#include "rng.h"
#include "mathinline.h"
#include "microfacetdist.h"

#include <chrono>
#include <thread>

struct hit_record;

vec3 reflect(const vec3& v, const vec3& n) {
  return(v - 2*dot(v,n) * n);
}

Float schlick(Float cosine, Float ref_idx) {
  Float r0 = (1 - ref_idx) / (1 + ref_idx);
  r0 = r0 * r0;
  return(r0 + (1-r0) * pow((1-cosine),5));
}

Float schlick_reflection(Float cosine, Float r0) {
  return(r0 + (1-r0) * pow((1-cosine),5));
}

bool refract(const vec3& v, const vec3& n, Float ni_over_nt, vec3& refracted) {
  vec3 uv = unit_vector(v);
  Float dt = dot(uv, n);
  Float discriminant = 1.0 - ni_over_nt * ni_over_nt * (1 - dt * dt);
  if(discriminant > 0) {
    refracted = ni_over_nt * (uv - n * dt) - n * sqrt(discriminant);
    return(true);
  } else {
    return(false);
  }
}

struct scatter_record {
  ray specular_ray;
  bool is_specular;
  vec3 attenuation;
  pdf *pdf_ptr = nullptr;
  ~scatter_record() { if(pdf_ptr) delete pdf_ptr; }
};

class material {
  public:
    virtual bool scatter(const ray& r_in, const hit_record& hrec, scatter_record& srec, random_gen& rng) {
      return(false);
    };
    virtual vec3 f(const ray& r_in, const hit_record& rec, const ray& scattered) const {
      return(vec3(0,0,0));
    }
    virtual vec3 emitted(const ray& r_in, const hit_record& rec, Float u, Float v, const vec3& p) const {
      return(vec3(0,0,0));
    }
};

class lambertian : public material {
  public: 
    lambertian(texture *a) : albedo(a) {}
    vec3 f(const ray& r_in, const hit_record& rec, const ray& scattered) const {
      //unit_vector(scattered.direction()) == wo
      //r_in.direction() == wi
      Float cosine = dot(rec.normal, unit_vector(scattered.direction()));
      if(cosine < 0) {
        cosine = 0;
      }
      return(albedo->value(rec.u, rec.v, rec.p) * cosine * M_1_PI);
    }
    bool scatter(const ray& r_in, const hit_record& hrec, scatter_record& srec, random_gen& rng) {
      srec.is_specular = false;
      srec.attenuation = albedo->value(hrec.u, hrec.v, hrec.p);
      srec.pdf_ptr = new cosine_pdf(hrec.normal);
      return(true);
    }
    
    texture *albedo;
};

class metal : public material {
  public:
    metal(const vec3& a, Float f) : albedo(a) { if (f < 1) fuzz = f; else fuzz = 1;}
    virtual bool scatter(const ray& r_in, const hit_record& hrec, scatter_record& srec, random_gen& rng) {
      vec3 reflected = reflect(unit_vector(r_in.direction()),hrec.normal);
      srec.specular_ray = ray(hrec.p, reflected + fuzz * rng.random_in_unit_sphere(), r_in.time());
      srec.attenuation = albedo;
      srec.is_specular = true;
      srec.pdf_ptr = 0;
      return(true);
    }
    vec3 albedo;
    Float fuzz;
};
// 
class dielectric : public material {
  public:
    dielectric(const vec3& a, Float ri, random_gen& rng) : ref_idx(ri), albedo(a), rng(rng) {};
    virtual bool scatter(const ray& r_in, const hit_record& hrec, scatter_record& srec, random_gen& rng) {
      srec.is_specular = true;
      vec3 outward_normal;
      vec3 reflected = reflect(r_in.direction(), hrec.normal);
      Float ni_over_nt;
      srec.attenuation = albedo;
      vec3 refracted;
      Float reflect_prob;
      Float cosine;
      if(dot(r_in.direction(), hrec.normal) > 0) {
        outward_normal = -hrec.normal;
        ni_over_nt = ref_idx;
        cosine = ref_idx * dot(r_in.direction(), hrec.normal) / r_in.direction().length();
      } else {
        outward_normal = hrec.normal;
        ni_over_nt = 1.0 / ref_idx;
        cosine = -dot(r_in.direction(), hrec.normal) / r_in.direction().length();
      }
      if(refract(r_in.direction(), outward_normal, ni_over_nt, refracted)) {
        reflect_prob = schlick(cosine, ref_idx);
      } else {
        reflect_prob = 1.0;
      }
      if(rng.unif_rand() < reflect_prob) {
        srec.specular_ray = ray(hrec.p, reflected, r_in.time());
      }  else {
        srec.specular_ray = ray(hrec.p, refracted, r_in.time());
      }
      return(true);
    }
    Float ref_idx;
    vec3 albedo;
    random_gen rng;
};

class diffuse_light : public material {
public:
  diffuse_light(texture *a) : emit(a) {}
  virtual bool scatter(const ray& r_in, const hit_record& rec, scatter_record& srec, random_gen& rng) {
    return(false);
  }
  virtual vec3 emitted(const ray& r_in, const hit_record& rec, Float u, Float v, const vec3& p) const {
    if(dot(rec.normal, r_in.direction()) < 0.0) {
      return(emit->value(u,v,p));
    } else {
      return(vec3(0,0,0));
    }
  }
  texture *emit;
};

class isotropic : public material {
public:
  isotropic(texture *a) : albedo(a) {}
  virtual bool scatter(const ray& r_in, const hit_record& rec, scatter_record& srec, random_gen& rng) {
    srec.is_specular = true;
    srec.specular_ray = ray(rec.p, rng.random_in_unit_sphere());
    srec.attenuation = albedo->value(rec.u,rec.v,rec.p);
    return(true);
  }
  vec3 f(const ray& r_in, const hit_record& rec, const ray& scattered) const {
    return(albedo->value(rec.u,rec.v,rec.p) * 0.25 * M_1_PI);
  }
  texture *albedo;
};

class orennayar : public material {
public:
  orennayar(texture *a, Float sigma) : albedo(a) {
    Float sigma2 = sigma*sigma;
    A = 1.0f - (sigma2 / (2.0f * (sigma2 + 0.33f)));
    B = 0.45f * sigma2 / (sigma2 + 0.09f);
  }
  bool scatter(const ray& r_in, const hit_record& hrec, scatter_record& srec, random_gen& rng) {
    srec.is_specular = false;
    srec.attenuation = albedo->value(hrec.u, hrec.v, hrec.p);
    srec.pdf_ptr = new cosine_pdf(hrec.normal);
    return(true);
  }
  //Equivalent to f() function, minus Spectrum
  vec3 f(const ray& r_in, const hit_record& rec, const ray& scattered) const {
    vec3 wi = unit_vector(r_in.direction());
    vec3 wo = unit_vector(scattered.direction());
    
    Float cosine = dot(rec.normal, wo);
    if(cosine < 0) {
      cosine = 0;
    }
    
    Float sinThetaI = SinTheta(wi);
    Float sinThetaO = SinTheta(wo);
    Float maxCos = 0;
    if(sinThetaI > 1e-4 && sinThetaO > 1e-4) {
      Float sinPhiI = SinPhi(wi);
      Float cosPhiI = CosPhi(wi);
      Float sinPhiO = SinPhi(wo);
      Float cosPhiO = CosPhi(wo);
      Float dCos = cosPhiI * cosPhiO + sinPhiI * sinPhiO;
      maxCos = std::max((Float)0, dCos);
    }
    Float sinAlpha, tanBeta;
    if(AbsCosTheta(wi) > AbsCosTheta(wo)) {
      sinAlpha = sinThetaO;
      tanBeta = sinThetaI / AbsCosTheta(wi);
    } else {
      sinAlpha = sinThetaI;
      tanBeta = sinThetaO / AbsCosTheta(wo);
    }
    return(albedo->value(rec.u, rec.v, rec.p) * (A + B * maxCos * sinAlpha * tanBeta ) * M_1_PI * cosine);
  }
  Float A, B;
  texture *albedo;
};

// class glossy : public material {
// public:
//   glossy(texture *a, vec3 gloss, MicrofacetDistribution *distribution) :
//   albedo(a), glossycolor(gloss), distribution(distribution) {}
//   vec3 f(const ray& r_in, const hit_record& rec, const ray& scattered) const {
//     vec3 wi = unit_vector(r_in.direction());
//     vec3 wo = unit_vector(scattered.direction());
//     vec3 color = albedo->value(rec.u, rec.v, rec.p);
//     auto pow5 = [](Float v) { return (v * v) * (v * v) * v; };
//     
//     vec3 diffuse = (28.0f/(23.0f*M_PI)) * color *
//       (vec3(1.0,1.0,1.0) - glossycolor) *
//       (1 - pow5(1.0f - 0.5f * AbsCosTheta(wi))) *
//       (1 - pow5(1.0f - 0.5f * AbsCosTheta(wo)));
//     vec3 wh = wi + wo;
//     if (wh.x() == 0 && wh.y() == 0 && wh.z() == 0) {
//       return(vec3(0,0,0));
//     }
//     wh.make_unit_vector();
//     vec3 specular = distribution->D(wh) / (4 * std::fabs(dot(wi, wh)) *
//         std::max(AbsCosTheta(wi), AbsCosTheta(wo))) *
//         schlick_fresnel(dot(wi, wh), color);
//     return(diffuse + specular);
//   }
//   bool scatter(const ray& r_in, const hit_record& hrec, scatter_record& srec, random_gen& rng) {
//     Float rand = rng.unif_rand();
//     if (rand < .5) {
//       rand *= 2;
//       srec.is_specular = false;
//       srec.attenuation = albedo->value(hrec.u, hrec.v, hrec.p);
//       srec.pdf_ptr = new cosine_pdf(hrec.normal);
//       return(true);
//     } else {
//       rand = 2 * (rand - 0.5f);
//       vec3 wh = distribution->scatter(wo, rng);
//       *wi = Reflect(wo, wh);
//       if (!SameHemisphere(wo, *wi)) return vec3(0.f);
//       
//     }
//     *pdf = Pdf(wo, *wi);
//     return f(wo, *wi);
//     srec.is_specular = false;
//     srec.attenuation = albedo->value(hrec.u, hrec.v, hrec.p);
//     srec.pdf_ptr = new cosine_pdf(hrec.normal);
//     return(true);
//   }
// 
//   texture *albedo;
//   vec3 glossycolor;
//   MicrofacetDistribution *distribution;
// };

class MicrofacetReflection : public material {
public:
  MicrofacetReflection(texture* a, MicrofacetDistribution *distribution, Float ref_idx)
    : albedo(a), distribution(distribution), ri(ref_idx) {}
  
  bool scatter(const ray& r_in, const hit_record& hrec, scatter_record& srec, random_gen& rng) {
    srec.is_specular = false;
    srec.attenuation = albedo->value(hrec.u, hrec.v, hrec.p);
    if(distribution->GetType()) {
      srec.pdf_ptr = new micro_trow_pdf(hrec.normal, r_in.direction(), distribution);
    } else {
      srec.pdf_ptr = new micro_beck_pdf(hrec.normal, distribution->GetAlpha(), r_in.direction());
    }
    return(true);
  }
  //wh = surface normal

  vec3 f(const ray& r_in, const hit_record& rec, const ray& scattered) const {
    vec3 wi = unit_vector(r_in.direction());
    vec3 wo = unit_vector(scattered.direction());
    
    Float cosThetaO = AbsCosTheta(wo);
    Float cosThetaI = AbsCosTheta(wi);
    vec3 wh = wi + wo;
    if (cosThetaI == 0 || cosThetaO == 0) {
      return(vec3(0,0,0));
    }
    if (wh.x() == 0 && wh.y() == 0 && wh.z() == 0) {
      return(vec3(0,0,0));
    }
    wh = unit_vector(wh);
    Float cosine = dot(wh, wo);
    if(cosine < 0) {
      cosine = 0;
    }
    Float F = schlick_reflection(dot(wi, wh), ri);
    Float G = distribution->G(wo,wi);
    Float D = distribution->D(wh);
    return(albedo->value(rec.u, rec.v, rec.p) * F * G * D  / (4 * cosThetaI * cosThetaO) * cosine);
  }
private:
  texture *albedo;
  MicrofacetDistribution *distribution;
  Float ri;
};

#endif
