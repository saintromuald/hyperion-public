// SHA-256 support and the pinned digest for the one accepted official Pluto DLL.
// Pathname hashing is consistency evidence only; pre-execution verification uses
// the held, write/delete-denied file handle in pluto-bridge.cpp.
#ifndef PLUTO_BRIDGE_OFFICIAL_DLL_H
#define PLUTO_BRIDGE_OFFICIAL_DLL_H

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace pluto_bridge {

inline const char *OfficialPlutoSha256() {
  return "603fea2f77a4b652f29b832c2977cf5bc9d83bbcb927febd04499220020ce3bc";
}

class Sha256 {
 public:
  Sha256()
      : state_{0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
               0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u},
        total_(0), used_(0) {}

  void Update(const unsigned char *data, std::size_t size) {
    total_ += size;
    while (size != 0) {
      const std::size_t take = size < sizeof buffer_ - used_ ? size : sizeof buffer_ - used_;
      std::memcpy(buffer_ + used_, data, take);
      used_ += take;
      data += take;
      size -= take;
      if (used_ == sizeof buffer_) {
        Transform(buffer_);
        used_ = 0;
      }
    }
  }

  void Finish(char hex[65]) {
    const std::uint64_t bits = static_cast<std::uint64_t>(total_) * 8;
    buffer_[used_++] = 0x80;
    if (used_ > 56) {
      std::memset(buffer_ + used_, 0, 64 - used_);
      Transform(buffer_);
      used_ = 0;
    }
    std::memset(buffer_ + used_, 0, 56 - used_);
    for (unsigned i = 0; i < 8; ++i)
      buffer_[63 - i] = static_cast<unsigned char>(bits >> (i * 8));
    Transform(buffer_);
    static const char digits[] = "0123456789abcdef";
    for (unsigned i = 0; i < 8; ++i)
      for (unsigned j = 0; j < 4; ++j) {
        const unsigned char byte = static_cast<unsigned char>(state_[i] >> (24 - j * 8));
        hex[(i * 4 + j) * 2] = digits[byte >> 4];
        hex[(i * 4 + j) * 2 + 1] = digits[byte & 15];
      }
    hex[64] = '\0';
  }

 private:
  static std::uint32_t Rotate(std::uint32_t value, unsigned bits) {
    return (value >> bits) | (value << (32 - bits));
  }

  void Transform(const unsigned char block[64]) {
    static const std::uint32_t constants[64] = {
        0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u,
        0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
        0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u, 0xe49b69c1u, 0xefbe4786u,
        0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
        0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u,
        0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
        0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u, 0xa2bfe8a1u, 0xa81a664bu,
        0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
        0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au,
        0x5b9cca4fu, 0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
        0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};
    std::uint32_t words[64];
    for (unsigned i = 0; i < 16; ++i)
      words[i] = (static_cast<std::uint32_t>(block[i * 4]) << 24) |
                 (static_cast<std::uint32_t>(block[i * 4 + 1]) << 16) |
                 (static_cast<std::uint32_t>(block[i * 4 + 2]) << 8) |
                 static_cast<std::uint32_t>(block[i * 4 + 3]);
    for (unsigned i = 16; i < 64; ++i) {
      const std::uint32_t s0 = Rotate(words[i - 15], 7) ^ Rotate(words[i - 15], 18) ^
                               (words[i - 15] >> 3);
      const std::uint32_t s1 = Rotate(words[i - 2], 17) ^ Rotate(words[i - 2], 19) ^
                               (words[i - 2] >> 10);
      words[i] = words[i - 16] + s0 + words[i - 7] + s1;
    }
    std::uint32_t a = state_[0], b = state_[1], c = state_[2], d = state_[3];
    std::uint32_t e = state_[4], f = state_[5], g = state_[6], h = state_[7];
    for (unsigned i = 0; i < 64; ++i) {
      const std::uint32_t sum1 = Rotate(e, 6) ^ Rotate(e, 11) ^ Rotate(e, 25);
      const std::uint32_t choose = (e & f) ^ (~e & g);
      const std::uint32_t temp1 = h + sum1 + choose + constants[i] + words[i];
      const std::uint32_t sum0 = Rotate(a, 2) ^ Rotate(a, 13) ^ Rotate(a, 22);
      const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
      const std::uint32_t temp2 = sum0 + majority;
      h = g; g = f; f = e; e = d + temp1;
      d = c; c = b; b = a; a = temp1 + temp2;
    }
    state_[0] += a; state_[1] += b; state_[2] += c; state_[3] += d;
    state_[4] += e; state_[5] += f; state_[6] += g; state_[7] += h;
  }

  std::uint32_t state_[8];
  std::size_t total_;
  std::size_t used_;
  unsigned char buffer_[64];
};

inline bool HashFileSha256(const char *path, char hex[65]) {
  FILE *file = std::fopen(path, "rb");
  if (file == NULL)
    return false;
  Sha256 hash;
  unsigned char block[65536];
  bool ok = true;
  for (;;) {
    const std::size_t got = std::fread(block, 1, sizeof block, file);
    if (got != 0)
      hash.Update(block, got);
    if (got != sizeof block) {
      ok = std::ferror(file) == 0;
      break;
    }
  }
  ok = std::fclose(file) == 0 && ok;
  if (ok)
    hash.Finish(hex);
  return ok;
}

}  // namespace pluto_bridge

#endif  // PLUTO_BRIDGE_OFFICIAL_DLL_H
