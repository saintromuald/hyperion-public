// HXPM: optional, versioned static Hyperion pathing metadata for Pluto.
// This codec has no Windows, BWAPI, game, or inference dependency.
#ifndef PLUTO_BRIDGE_HXPM_H
#define PLUTO_BRIDGE_HXPM_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <string>
#include <utility>
#include <vector>

namespace pluto_hxpm {

constexpr uint32_t kMagic = 0x4D505848;  // "HXPM" in little-endian bytes
constexpr uint16_t kVersion = 1;
constexpr uint16_t kHeaderBytes = 32;
constexpr uint16_t kRegionBytes = 34;
constexpr size_t kMaxMapSide = 256;
constexpr size_t kMaxStarts = 8;
constexpr size_t kMaxRegions = 5000;       // BWAPI GameData::regions
constexpr size_t kMaxSplits = 25000;       // BW::SAI_Paths::splitTiles
constexpr size_t kMaxNeighborsPerRegion = 255;
constexpr size_t kMaxNeighborIds = kMaxRegions * kMaxNeighborsPerRegion;
constexpr size_t kMaxPayloadBytes =
    kMaxStarts * 4 + kMaxMapSide * kMaxMapSide * 2 + kMaxSplits * 6 +
    kMaxRegions * kRegionBytes + kMaxNeighborIds * 2;
constexpr size_t kMaxRecordBytes = kHeaderBytes + kMaxPayloadBytes;

struct Start {
  uint16_t x;
  uint16_t y;
};

struct Split {
  uint16_t minitile_mask;
  uint16_t region1;
  uint16_t region2;
};

struct Region {
  uint16_t access_flags = 0;
  uint16_t group = 0;
  uint16_t tile_count = 0;
  uint8_t path_count = 0;
  uint8_t neighbor_count = 0;
  uint8_t defence_priority = 0;
  uint8_t neighbor_property = 0;
  int32_t center_x = 0;  // pixels * 256
  int32_t center_y = 0;  // pixels * 256
  int32_t left = 0;
  int32_t top = 0;
  int32_t right = 0;
  int32_t bottom = 0;
  std::vector<uint16_t> neighbors;
};

struct Metadata {
  uint16_t map_width = 0;
  uint16_t map_height = 0;
  std::vector<Start> starts;
  std::vector<uint16_t> map_tile_region_id;  // row-major [y * map_width + x]
  std::vector<Split> split_tiles;
  std::vector<Region> regions;
};

enum class ParseResult {
  kOk,
  kTruncated,
  kInvalid,
};

enum class StreamMetadataState {
  kWaiting,
  kComplete,
  kLegacyIncomplete,
};

// Enforces one static record, before frames, for the one match in this process.
// It also prevents a later frame with different dimensions from using stale data.
class StreamState {
 public:
  bool AcceptMetadata(uint16_t map_width, uint16_t map_height) {
    if (state_ != StreamMetadataState::kWaiting || saw_frame_)
      return false;
    map_width_ = map_width;
    map_height_ = map_height;
    state_ = StreamMetadataState::kComplete;
    return true;
  }

  bool AcceptFrame(uint16_t map_width, uint16_t map_height) {
    if (saw_frame_ && (map_width != frame_width_ || map_height != frame_height_))
      return false;
    if (state_ == StreamMetadataState::kComplete &&
        (map_width != map_width_ || map_height != map_height_))
      return false;
    if (state_ == StreamMetadataState::kWaiting)
      state_ = StreamMetadataState::kLegacyIncomplete;
    if (!saw_frame_) {
      frame_width_ = map_width;
      frame_height_ = map_height;
      saw_frame_ = true;
    }
    return true;
  }

  StreamMetadataState state() const { return state_; }
  bool saw_frame() const { return saw_frame_; }

 private:
  StreamMetadataState state_ = StreamMetadataState::kWaiting;
  bool saw_frame_ = false;
  uint16_t map_width_ = 0;
  uint16_t map_height_ = 0;
  uint16_t frame_width_ = 0;
  uint16_t frame_height_ = 0;
};

inline uint16_t Read16(const uint8_t *p) {
  return static_cast<uint16_t>(p[0] | (static_cast<uint16_t>(p[1]) << 8));
}

inline uint32_t Read32(const uint8_t *p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

inline void Append16(std::vector<uint8_t> &out, uint16_t v) {
  out.push_back(static_cast<uint8_t>(v));
  out.push_back(static_cast<uint8_t>(v >> 8));
}

inline void Append32(std::vector<uint8_t> &out, uint32_t v) {
  out.push_back(static_cast<uint8_t>(v));
  out.push_back(static_cast<uint8_t>(v >> 8));
  out.push_back(static_cast<uint8_t>(v >> 16));
  out.push_back(static_cast<uint8_t>(v >> 24));
}

inline void Append32(std::vector<uint8_t> &out, int32_t v) {
  Append32(out, static_cast<uint32_t>(v));
}

inline bool Fail(std::string *error, const char *message) {
  if (error != nullptr)
    *error = message;
  return false;
}

inline size_t NeighborCount(const Metadata &metadata) {
  size_t total = 0;
  for (const Region &region : metadata.regions)
    total += region.neighbors.size();
  return total;
}

inline bool PayloadBytes(const Metadata &metadata, size_t *payload) {
  const uint64_t value = static_cast<uint64_t>(metadata.starts.size()) * 4 +
                         static_cast<uint64_t>(metadata.map_tile_region_id.size()) * 2 +
                         static_cast<uint64_t>(metadata.split_tiles.size()) * 6 +
                         static_cast<uint64_t>(metadata.regions.size()) * kRegionBytes +
                         static_cast<uint64_t>(NeighborCount(metadata)) * 2;
  if (value > kMaxPayloadBytes)
    return false;
  *payload = static_cast<size_t>(value);
  return true;
}

inline bool Validate(const Metadata &metadata, std::string *error = nullptr) {
  if (metadata.map_width == 0 || metadata.map_height == 0 ||
      metadata.map_width > kMaxMapSide || metadata.map_height > kMaxMapSide)
    return Fail(error, "invalid HXPM map dimensions");
  if (metadata.starts.size() > kMaxStarts)
    return Fail(error, "too many HXPM start locations");
  if (metadata.regions.size() > kMaxRegions)
    return Fail(error, "too many HXPM regions");
  if (metadata.split_tiles.size() > kMaxSplits)
    return Fail(error, "too many HXPM split tiles");
  const size_t tile_count = static_cast<size_t>(metadata.map_width) * metadata.map_height;
  if (metadata.map_tile_region_id.size() != tile_count)
    return Fail(error, "HXPM tile array length does not match its map");
  for (const Start &start : metadata.starts)
    if (start.x >= metadata.map_width || start.y >= metadata.map_height)
      return Fail(error, "HXPM start location is outside the map");
  for (uint16_t id : metadata.map_tile_region_id) {
    if (id >= 0x2000) {
      if (static_cast<size_t>(id - 0x2000) >= metadata.split_tiles.size())
        return Fail(error, "HXPM tile references an invalid split record");
    } else if (id >= metadata.regions.size()) {
      return Fail(error, "HXPM tile references an invalid region");
    }
  }
  for (const Split &split : metadata.split_tiles)
    if (split.region1 >= metadata.regions.size() || split.region2 >= metadata.regions.size())
      return Fail(error, "HXPM split tile references an invalid region");
  size_t total_neighbors = 0;
  for (const Region &region : metadata.regions) {
    if (region.path_count > region.neighbor_count)
      return Fail(error, "HXPM path count exceeds neighbor count");
    if (region.neighbors.size() != region.neighbor_count)
      return Fail(error, "HXPM neighbor array length does not match its count");
    total_neighbors += region.neighbors.size();
    if (total_neighbors > kMaxNeighborIds)
      return Fail(error, "too many HXPM neighbor ids");
    for (uint16_t neighbor : region.neighbors)
      if (neighbor >= metadata.regions.size())
        return Fail(error, "HXPM neighbor id is outside the region table");
  }
  size_t payload = 0;
  if (!PayloadBytes(metadata, &payload))
    return Fail(error, "HXPM payload is oversized");
  (void)payload;
  return true;
}

inline bool Encode(const Metadata &metadata, std::vector<uint8_t> &out,
                   std::string *error = nullptr) {
  if (!Validate(metadata, error))
    return false;
  size_t payload = 0;
  if (!PayloadBytes(metadata, &payload))
    return Fail(error, "HXPM payload is oversized");
  const size_t neighbor_count = NeighborCount(metadata);
  std::vector<uint8_t> encoded;
  encoded.reserve(kHeaderBytes + payload);
  Append32(encoded, kMagic);
  Append16(encoded, kVersion);
  Append16(encoded, kHeaderBytes);
  Append16(encoded, metadata.map_width);
  Append16(encoded, metadata.map_height);
  Append16(encoded, static_cast<uint16_t>(metadata.regions.size()));
  Append16(encoded, static_cast<uint16_t>(metadata.starts.size()));
  Append32(encoded, static_cast<uint32_t>(metadata.split_tiles.size()));
  Append32(encoded, static_cast<uint32_t>(metadata.map_tile_region_id.size()));
  Append32(encoded, static_cast<uint32_t>(neighbor_count));
  Append32(encoded, static_cast<uint32_t>(payload));
  for (const Start &start : metadata.starts) {
    Append16(encoded, start.x);
    Append16(encoded, start.y);
  }
  for (uint16_t id : metadata.map_tile_region_id)
    Append16(encoded, id);
  for (const Split &split : metadata.split_tiles) {
    Append16(encoded, split.minitile_mask);
    Append16(encoded, split.region1);
    Append16(encoded, split.region2);
  }
  for (const Region &region : metadata.regions) {
    Append16(encoded, region.access_flags);
    Append16(encoded, region.group);
    Append16(encoded, region.tile_count);
    encoded.push_back(region.path_count);
    encoded.push_back(region.neighbor_count);
    encoded.push_back(region.defence_priority);
    encoded.push_back(region.neighbor_property);
    Append32(encoded, region.center_x);
    Append32(encoded, region.center_y);
    Append32(encoded, region.left);
    Append32(encoded, region.top);
    Append32(encoded, region.right);
    Append32(encoded, region.bottom);
    for (uint16_t neighbor : region.neighbors)
      Append16(encoded, neighbor);
  }
  if (encoded.size() != kHeaderBytes + payload)
    return Fail(error, "internal HXPM encoded length mismatch");
  out.swap(encoded);
  return true;
}

inline ParseResult Parse(const std::vector<uint8_t> &bytes, Metadata &out,
                         std::string *error = nullptr) {
  if (bytes.size() < kHeaderBytes) {
    Fail(error, "truncated HXPM fixed header");
    return ParseResult::kTruncated;
  }
  if (Read32(bytes.data()) != kMagic) {
    Fail(error, "bad HXPM magic");
    return ParseResult::kInvalid;
  }
  if (Read16(bytes.data() + 4) != kVersion || Read16(bytes.data() + 6) != kHeaderBytes) {
    Fail(error, "unsupported HXPM version or header size");
    return ParseResult::kInvalid;
  }
  const size_t map_width = Read16(bytes.data() + 8);
  const size_t map_height = Read16(bytes.data() + 10);
  const size_t region_count = Read16(bytes.data() + 12);
  const size_t start_count = Read16(bytes.data() + 14);
  const size_t split_count = Read32(bytes.data() + 16);
  const size_t tile_count = Read32(bytes.data() + 20);
  const size_t neighbor_count = Read32(bytes.data() + 24);
  const size_t payload = Read32(bytes.data() + 28);
  if (map_width == 0 || map_height == 0 || map_width > kMaxMapSide ||
      map_height > kMaxMapSide || region_count > kMaxRegions ||
      start_count > kMaxStarts || split_count > kMaxSplits ||
      neighbor_count > kMaxNeighborIds || payload > kMaxPayloadBytes ||
      tile_count != map_width * map_height) {
    Fail(error, "invalid or oversized HXPM header counts");
    return ParseResult::kInvalid;
  }
  const uint64_t expected64 = static_cast<uint64_t>(start_count) * 4 + tile_count * 2 +
                              static_cast<uint64_t>(split_count) * 6 +
                              region_count * kRegionBytes + neighbor_count * 2;
  if (expected64 > kMaxPayloadBytes || expected64 != payload) {
    Fail(error, "HXPM payload length does not match its header counts");
    return ParseResult::kInvalid;
  }
  const size_t expected = static_cast<size_t>(expected64);
  if (bytes.size() < kHeaderBytes + expected) {
    Fail(error, "truncated HXPM payload");
    return ParseResult::kTruncated;
  }
  if (bytes.size() != kHeaderBytes + expected) {
    Fail(error, "trailing bytes after HXPM record");
    return ParseResult::kInvalid;
  }

  Metadata parsed;
  parsed.map_width = static_cast<uint16_t>(map_width);
  parsed.map_height = static_cast<uint16_t>(map_height);
  parsed.starts.resize(start_count);
  parsed.map_tile_region_id.resize(tile_count);
  parsed.split_tiles.resize(split_count);
  parsed.regions.resize(region_count);
  size_t cursor = kHeaderBytes;
  for (Start &start : parsed.starts) {
    start.x = Read16(bytes.data() + cursor);
    start.y = Read16(bytes.data() + cursor + 2);
    cursor += 4;
  }
  for (uint16_t &id : parsed.map_tile_region_id) {
    id = Read16(bytes.data() + cursor);
    cursor += 2;
  }
  for (Split &split : parsed.split_tiles) {
    split.minitile_mask = Read16(bytes.data() + cursor);
    split.region1 = Read16(bytes.data() + cursor + 2);
    split.region2 = Read16(bytes.data() + cursor + 4);
    cursor += 6;
  }
  size_t decoded_neighbors = 0;
  for (Region &region : parsed.regions) {
    region.access_flags = Read16(bytes.data() + cursor);
    region.group = Read16(bytes.data() + cursor + 2);
    region.tile_count = Read16(bytes.data() + cursor + 4);
    region.path_count = bytes[cursor + 6];
    region.neighbor_count = bytes[cursor + 7];
    region.defence_priority = bytes[cursor + 8];
    region.neighbor_property = bytes[cursor + 9];
    region.center_x = static_cast<int32_t>(Read32(bytes.data() + cursor + 10));
    region.center_y = static_cast<int32_t>(Read32(bytes.data() + cursor + 14));
    region.left = static_cast<int32_t>(Read32(bytes.data() + cursor + 18));
    region.top = static_cast<int32_t>(Read32(bytes.data() + cursor + 22));
    region.right = static_cast<int32_t>(Read32(bytes.data() + cursor + 26));
    region.bottom = static_cast<int32_t>(Read32(bytes.data() + cursor + 30));
    cursor += kRegionBytes;
    decoded_neighbors += region.neighbor_count;
    if (decoded_neighbors > neighbor_count) {
      Fail(error, "HXPM region neighbor counts exceed the header total");
      return ParseResult::kInvalid;
    }
    region.neighbors.resize(region.neighbor_count);
    for (uint16_t &neighbor : region.neighbors) {
      neighbor = Read16(bytes.data() + cursor);
      cursor += 2;
    }
  }
  if (decoded_neighbors != neighbor_count || cursor != bytes.size()) {
    Fail(error, "HXPM region arrays do not consume the declared payload");
    return ParseResult::kInvalid;
  }
  if (!Validate(parsed, error))
    return ParseResult::kInvalid;
  out = std::move(parsed);
  return ParseResult::kOk;
}

inline bool WordRangeContains(const void *base, size_t words, const void *pointer,
                              size_t count) {
  if (base == nullptr || pointer == nullptr || words > SIZE_MAX / 2)
    return false;
  const uintptr_t first = reinterpret_cast<uintptr_t>(base);
  const uintptr_t at = reinterpret_cast<uintptr_t>(pointer);
  const uintptr_t bytes = words * 2;
  if (at < first || at - first > bytes || ((at - first) & 1) != 0)
    return false;
  const size_t offset = static_cast<size_t>(at - first) / 2;
  return offset <= words && count <= words - offset;
}

// Extract and encode an actual ep_glob_t. The template keeps this shared header
// independent of engine headers; the producer and standalone tests instantiate it
// with the real type from sai_PathCreate.h. No source structure is modified.
template <typename EpGlob>
bool Serialize(const EpGlob &glob, unsigned map_width, unsigned map_height,
               const Start *starts, size_t start_count, int stride, size_t map_cells,
               size_t split_capacity, size_t pool_words, std::vector<uint8_t> &out,
               std::string *error = nullptr) {
  if (map_width == 0 || map_height == 0 || map_width > kMaxMapSide ||
      map_height > kMaxMapSide || start_count > kMaxStarts ||
      (start_count != 0 && starts == nullptr))
    return Fail(error, "invalid HXPM map or start metadata");
  if (stride < static_cast<int>(map_width) || stride <= 0 ||
      static_cast<size_t>(stride) > map_cells ||
      map_height > map_cells / static_cast<size_t>(stride))
    return Fail(error, "HXPM ep_glob stride is invalid for its map");
  if (glob.nRegions <= 0 || static_cast<size_t>(glob.nRegions) > kMaxRegions)
    return Fail(error, "HXPM ep_glob region count is outside BWAPI limits");

  const uintptr_t split_begin = reinterpret_cast<uintptr_t>(glob.bScratch);
  const uintptr_t split_end = reinterpret_cast<uintptr_t>(glob.pScratch);
  if (split_end < split_begin || (split_end - split_begin) % 6 != 0)
    return Fail(error, "HXPM ep_glob split cursor is invalid");
  const size_t split_count = static_cast<size_t>(split_end - split_begin) / 6;
  if (split_count > split_capacity || split_count > kMaxSplits)
    return Fail(error, "HXPM ep_glob split table is oversized");

  Metadata metadata;
  metadata.map_width = static_cast<uint16_t>(map_width);
  metadata.map_height = static_cast<uint16_t>(map_height);
  if (start_count != 0)
    metadata.starts.assign(starts, starts + start_count);
  metadata.map_tile_region_id.resize(static_cast<size_t>(map_width) * map_height);
  for (unsigned y = 0; y < map_height; ++y)
    for (unsigned x = 0; x < map_width; ++x)
      metadata.map_tile_region_id[y * map_width + x] =
          glob.wMap[y * static_cast<unsigned>(stride) + x];
  metadata.split_tiles.resize(split_count);
  for (size_t i = 0; i < split_count; ++i) {
    const uint8_t *split = glob.bScratch + i * 6;
    metadata.split_tiles[i].minitile_mask = Read16(split);
    metadata.split_tiles[i].region1 = Read16(split + 2);
    metadata.split_tiles[i].region2 = Read16(split + 4);
  }
  metadata.regions.resize(static_cast<size_t>(glob.nRegions));
  for (size_t i = 0; i < metadata.regions.size(); ++i) {
    const auto &source = glob.aRegions[i];
    Region &region = metadata.regions[i];
    region.access_flags = source.wGroup;
    region.group = source.wArea;
    region.tile_count = source.wTiles;
    region.path_count = source.bPathFirst;
    region.neighbor_count = source.bPathCount;
    region.defence_priority = source.bAreaLinks;
    region.neighbor_property = static_cast<uint8_t>(source.nExtraPaths);
    region.center_x = static_cast<int32_t>(source.nX);
    region.center_y = static_cast<int32_t>(source.nY);
    region.left = static_cast<int32_t>(source.nMinX);
    region.top = static_cast<int32_t>(source.nMinY);
    region.right = static_cast<int32_t>(source.nMaxX);
    region.bottom = static_cast<int32_t>(source.nMaxY);
    if (region.path_count > region.neighbor_count)
      return Fail(error, "HXPM ep_glob path count exceeds neighbor count");
    if (region.neighbor_count != 0) {
      const bool inline_paths = WordRangeContains(
          source.wInlinePaths, 10, source.pPaths, region.neighbor_count);
      const bool pool_paths = WordRangeContains(glob.wPathPool, pool_words, source.pPaths,
                                                region.neighbor_count);
      if (!inline_paths && !pool_paths)
        return Fail(error, "HXPM ep_glob neighbor pointer is outside inline/pool storage");
      region.neighbors.resize(region.neighbor_count);
      for (size_t j = 0; j < region.neighbors.size(); ++j)
        region.neighbors[j] = source.pPaths[j];
    }
  }
  return Encode(metadata, out, error);
}

}  // namespace pluto_hxpm

#endif  // PLUTO_BRIDGE_HXPM_H
