// Diagnostic only: report the x86 feature state visible to native and Wine-hosted processes.
// This never loads, patches, or redistributes any Pluto payload.

#include <cstdint>
#include <cstdio>
#include <cstdlib>

struct CpuidResult {
  uint32_t eax;
  uint32_t ebx;
  uint32_t ecx;
  uint32_t edx;
};

static CpuidResult cpuid(uint32_t leaf, uint32_t subleaf = 0) {
  CpuidResult result;
  __asm__ volatile("cpuid"
                   : "=a"(result.eax), "=b"(result.ebx), "=c"(result.ecx), "=d"(result.edx)
                   : "a"(leaf), "c"(subleaf));
  return result;
}

static void feature(const char *name, int supported) {
  std::printf("builtin %-6s %s\n", name, supported ? "yes" : "no");
}

int main() {
  const char *advertise = std::getenv("ROSETTA_ADVERTISE_AVX");
  std::printf("ROSETTA_ADVERTISE_AVX=%s\n", advertise ? advertise : "<unset>");

  const CpuidResult leaf0 = cpuid(0);
  const CpuidResult leaf1 = cpuid(1);
  std::printf("cpuid.0 max=%u ebx=%08x ecx=%08x edx=%08x\n", leaf0.eax, leaf0.ebx, leaf0.ecx,
              leaf0.edx);
  std::printf("cpuid.1 eax=%08x ebx=%08x ecx=%08x edx=%08x\n", leaf1.eax, leaf1.ebx, leaf1.ecx,
              leaf1.edx);
  std::printf("raw osxsave=%s avx=%s fma=%s f16c=%s\n", (leaf1.ecx & (1u << 27)) ? "yes" : "no",
              (leaf1.ecx & (1u << 28)) ? "yes" : "no", (leaf1.ecx & (1u << 12)) ? "yes" : "no",
              (leaf1.ecx & (1u << 29)) ? "yes" : "no");

  if (leaf0.eax >= 7) {
    const CpuidResult leaf7 = cpuid(7);
    std::printf("cpuid.7.0 eax=%08x ebx=%08x ecx=%08x edx=%08x\n", leaf7.eax, leaf7.ebx, leaf7.ecx,
                leaf7.edx);
    std::printf("raw avx2=%s bmi1=%s bmi2=%s\n", (leaf7.ebx & (1u << 5)) ? "yes" : "no",
                (leaf7.ebx & (1u << 3)) ? "yes" : "no", (leaf7.ebx & (1u << 8)) ? "yes" : "no");
  } else {
    std::printf("cpuid.7 unavailable\n");
  }

  if (leaf1.ecx & (1u << 27)) {
    uint32_t eax = 0;
    uint32_t edx = 0;
    __asm__ volatile("xgetbv" : "=a"(eax), "=d"(edx) : "c"(0));
    std::printf("xcr0=%08x%08x\n", edx, eax);
  } else {
    std::printf("xcr0 unavailable without OSXSAVE\n");
  }

  __builtin_cpu_init();
  feature("avx", __builtin_cpu_supports("avx"));
  feature("avx2", __builtin_cpu_supports("avx2"));
  feature("fma", __builtin_cpu_supports("fma"));
  feature("f16c", __builtin_cpu_supports("f16c"));
  feature("bmi", __builtin_cpu_supports("bmi"));
  feature("bmi2", __builtin_cpu_supports("bmi2"));
  return 0;
}
