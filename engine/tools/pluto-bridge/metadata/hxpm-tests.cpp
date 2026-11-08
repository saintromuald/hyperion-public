#include "sai_PathCreate.h"
#include "hxpm.h"

#include <stdint.h>

#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

// The real headers use these map-limit globals through EP_STRIDE. No game code runs.
int g_epStride = 4;
int g_epRegionCap = EP_REGIONS_117;
int g_epBoxScale = 1;
int g_epAreaNodes = EP_AREA_NODES_117;
int g_epAreaQueue = EP_AREA_QUEUE_117;
int g_epLocalNodes = EP_LOCAL_NODES_117;
int g_epLocalQueue = EP_LOCAL_QUEUE_117;
int g_posShift = 16;
int g_posMaskU = 0xFFFF;

namespace hxpm = pluto_hxpm;

namespace {

void Expect(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

void Put16(std::vector<uint8_t> &bytes, size_t offset, uint16_t value) {
  Expect(offset + 2 <= bytes.size(), "test patch offset");
  bytes[offset] = static_cast<uint8_t>(value);
  bytes[offset + 1] = static_cast<uint8_t>(value >> 8);
}

void Put32(std::vector<uint8_t> &bytes, size_t offset, uint32_t value) {
  Expect(offset + 4 <= bytes.size(), "test patch offset");
  for (unsigned i = 0; i < 4; ++i)
    bytes[offset + i] = static_cast<uint8_t>(value >> (i * 8));
}

struct Fixture {
  std::unique_ptr<ep_glob_t> glob{new ep_glob_t{}};
  std::vector<hxpm::Start> starts{{0, 0}, {3, 2}};

  Fixture() {
    glob->nRegions = 3;
    // Two connected regions, one inaccessible region, and one split tile.
    const uint16_t cells[3][4] = {
        {0, 0, 1, 2},
        {0, 0x2000, 1, 2},
        {0, 1, 1, 2},
    };
    for (unsigned y = 0; y < 3; ++y)
      for (unsigned x = 0; x < 4; ++x)
        glob->wMap[y * EP_STRIDE + x] = cells[y][x];

    // ep_split_t is packed as mask, region1, region2 in bScratch.
    glob->bScratch[0] = 0x5A;
    glob->bScratch[1] = 0xA5;
    glob->bScratch[2] = 0;
    glob->bScratch[3] = 0;
    glob->bScratch[4] = 1;
    glob->bScratch[5] = 0;
    glob->pScratch = glob->bScratch + 6;

    ep_region_t &first = glob->aRegions[0];
    first.wGroup = 0x1FF9;  // accessible, higher ground
    first.wArea = 7;
    first.wTiles = 5;
    first.bPathFirst = 1;
    first.bPathCount = 2;
    first.pPaths = first.wInlinePaths;
    first.wInlinePaths[0] = 1;  // order is significant
    first.wInlinePaths[1] = 2;
    first.nX = 16 * 256;
    first.nY = 24 * 256;
    first.nMinX = 0;
    first.nMinY = 0;
    first.nMaxX = 63;
    first.nMaxY = 95;
    first.bAreaLinks = 2;
    first.nExtraPaths = 1;

    ep_region_t &second = glob->aRegions[1];
    second.wGroup = 0x1FFB;  // accessible, lower ground
    second.wArea = 7;
    second.wTiles = 4;
    second.bPathFirst = 1;
    second.bPathCount = 1;
    glob->wPathPool[3] = 0;
    second.pPaths = glob->wPathPool + 3;  // exercise pool, not inline, decoding
    second.nX = 80 * 256;
    second.nY = 48 * 256;
    second.nMinX = 32;
    second.nMinY = 8;
    second.nMaxX = 111;
    second.nMaxY = 87;
    second.bAreaLinks = 1;
    second.nExtraPaths = 0;

    ep_region_t &inaccessible = glob->aRegions[2];
    inaccessible.wGroup = 0x1FFD;
    inaccessible.wArea = 0x4000;
    inaccessible.wTiles = 3;
    inaccessible.pPaths = inaccessible.wInlinePaths;
    inaccessible.nX = 112 * 256;
    inaccessible.nY = 40 * 256;
    inaccessible.nMinX = 96;
    inaccessible.nMinY = 0;
    inaccessible.nMaxX = 127;
    inaccessible.nMaxY = 95;
  }

  std::vector<uint8_t> Serialize() const {
    std::vector<uint8_t> bytes;
    std::string error;
    const bool ok = hxpm::Serialize(*glob, 4, 3, starts.data(), starts.size(), EP_STRIDE,
                                    EP_MAP_CELLS, EP_SPLIT_RECS, EP_POOL_WORDS, bytes, &error);
    Expect(ok, error.c_str());
    return bytes;
  }
};

hxpm::Metadata Decode(const std::vector<uint8_t> &bytes) {
  hxpm::Metadata metadata;
  std::string error;
  const hxpm::ParseResult result = hxpm::Parse(bytes, metadata, &error);
  Expect(result == hxpm::ParseResult::kOk, error.c_str());
  return metadata;
}

void ExpectInvalid(const std::vector<uint8_t> &bytes, const char *message) {
  hxpm::Metadata metadata;
  std::string error;
  Expect(hxpm::Parse(bytes, metadata, &error) == hxpm::ParseResult::kInvalid, message);
  Expect(!error.empty(), "invalid HXPM must report a reason");
}

void TestRoundTripAndDeterminism() {
  Fixture fixture;
  const std::vector<uint8_t> encoded = fixture.Serialize();
  const std::vector<uint8_t> encoded_again = fixture.Serialize();
  Expect(encoded == encoded_again, "HXPM serialization is not deterministic");
  Expect(hxpm::Read32(encoded.data()) == hxpm::kMagic, "HXPM magic");
  Expect(hxpm::Read16(encoded.data() + 4) == 1, "HXPM version");
  Expect(hxpm::Read16(encoded.data() + 6) == 32, "HXPM fixed header size");
  Expect(encoded.size() == 32 + hxpm::Read32(encoded.data() + 28), "HXPM payload size");

  const hxpm::Metadata metadata = Decode(encoded);
  Expect(metadata.map_width == 4 && metadata.map_height == 3, "map dimensions roundtrip");
  Expect(metadata.starts.size() == 2 && metadata.starts[1].x == 3 &&
             metadata.starts[1].y == 2,
         "every map start roundtrip");
  Expect(metadata.map_tile_region_id.size() == 12 &&
             metadata.map_tile_region_id[5] == 0x2000,
         "mapTileRegionId and split id roundtrip");
  Expect(metadata.split_tiles.size() == 1 &&
             metadata.split_tiles[0].minitile_mask == 0xA55A &&
             metadata.split_tiles[0].region1 == 0 && metadata.split_tiles[0].region2 == 1,
         "split tile roundtrip");
  Expect(metadata.regions.size() == 3, "region count roundtrip");
  const hxpm::Region &first = metadata.regions[0];
  Expect(first.access_flags == 0x1FF9 && first.group == 7 && first.tile_count == 5,
         "access/group/tileCount roundtrip");
  Expect(first.path_count == 1 && first.neighbor_count == 2 &&
             first.defence_priority == 2 && first.neighbor_property == 1,
         "path/neighbor/defence/property roundtrip");
  Expect(first.center_x == 16 * 256 && first.center_y == 24 * 256,
         "center fixed-point roundtrip");
  Expect(first.left == 0 && first.top == 0 && first.right == 63 && first.bottom == 95,
         "bounds roundtrip");
  Expect(first.neighbors == std::vector<uint16_t>({1, 2}), "neighbor order roundtrip");
  Expect(metadata.regions[1].neighbors == std::vector<uint16_t>({0}),
         "pool neighbor contents roundtrip");
  Expect(metadata.regions[0].group == metadata.regions[1].group,
         "two regions remain connected");
  Expect(metadata.regions[2].access_flags == 0x1FFD && metadata.regions[2].neighbors.empty(),
         "inaccessible region roundtrip");

  std::vector<uint8_t> reencoded;
  Expect(hxpm::Encode(metadata, reencoded), "decoded metadata re-encodes");
  Expect(reencoded == encoded, "roundtrip bytes are identical");
}

void TestMalformedMetadata() {
  Fixture fixture;
  const std::vector<uint8_t> encoded = fixture.Serialize();
  const size_t tiles = 32 + fixture.starts.size() * 4;
  const size_t splits = tiles + 12 * 2;
  const size_t regions = splits + 6;
  const size_t first_neighbors = regions + hxpm::kRegionBytes;

  std::vector<uint8_t> bad = encoded;
  Put16(bad, first_neighbors, 3);
  ExpectInvalid(bad, "invalid neighbor id must be rejected");

  bad = encoded;
  Put16(bad, tiles, 3);
  ExpectInvalid(bad, "invalid direct tile region must be rejected");

  bad = encoded;
  Put16(bad, tiles, 0x2001);
  ExpectInvalid(bad, "invalid split tile reference must be rejected");

  bad = encoded;
  Put16(bad, splits + 2, 3);
  ExpectInvalid(bad, "invalid split region must be rejected");

  bad = encoded;
  bad[regions + 6] = 3;  // pathCount > neighborCount
  ExpectInvalid(bad, "invalid path count must be rejected");

  bad = encoded;
  Put16(bad, 12, 5001);
  ExpectInvalid(bad, "oversized region count must be rejected");

  bad = encoded;
  Put32(bad, 16, 25001);
  ExpectInvalid(bad, "oversized split count must be rejected");

  bad = encoded;
  Put32(bad, 24, static_cast<uint32_t>(hxpm::kMaxNeighborIds + 1));
  ExpectInvalid(bad, "oversized neighbor total must be rejected");

  bad = encoded;
  Put32(bad, 28, static_cast<uint32_t>(bad.size()));  // should be size - 32
  ExpectInvalid(bad, "inconsistent payload size must be rejected");

  bad = encoded;
  bad.push_back(0);
  ExpectInvalid(bad, "trailing metadata must be rejected");
}

void TestEveryTruncation() {
  Fixture fixture;
  const std::vector<uint8_t> encoded = fixture.Serialize();
  for (size_t length = 0; length < encoded.size(); ++length) {
    const std::vector<uint8_t> prefix(encoded.begin(), encoded.begin() + length);
    hxpm::Metadata metadata;
    const hxpm::ParseResult result = hxpm::Parse(prefix, metadata);
    Expect(result == hxpm::ParseResult::kTruncated, "every truncated HXPM prefix is rejected");
  }
}

void TestStreamGatesAndLegacyFallback() {
  hxpm::StreamState complete;
  Expect(complete.state() == hxpm::StreamMetadataState::kWaiting, "initial stream state");
  Expect(complete.AcceptMetadata(4, 3), "first HXPM accepted");
  Expect(complete.state() == hxpm::StreamMetadataState::kComplete, "complete metadata state");
  Expect(!complete.AcceptMetadata(4, 3), "duplicate frame metadata rejected");
  Expect(complete.AcceptFrame(4, 3), "matching frame accepted");
  Expect(!complete.AcceptMetadata(4, 3), "metadata after a frame rejected");
  Expect(!complete.AcceptFrame(5, 3), "changed map cannot reuse stale metadata");

  hxpm::StreamState legacy;
  Expect(legacy.AcceptFrame(4, 3), "legacy frame accepted without HXPM");
  Expect(legacy.state() == hxpm::StreamMetadataState::kLegacyIncomplete,
         "legacy absence is explicitly incomplete");
  Expect(legacy.AcceptFrame(4, 3), "subsequent legacy frame accepted");
  Expect(!legacy.AcceptMetadata(4, 3), "late metadata cannot complete a legacy match");
  Expect(!legacy.AcceptFrame(4, 4), "legacy map changes are also rejected");
}

void TestProducerRejectsInvalidStorageWithoutOutput() {
  Fixture fixture;
  fixture.glob->aRegions[0].pPaths = reinterpret_cast<WORD *>(static_cast<uintptr_t>(1));
  std::vector<uint8_t> output{0x42};
  std::string error;
  Expect(!hxpm::Serialize(*fixture.glob, 4, 3, fixture.starts.data(), fixture.starts.size(),
                          EP_STRIDE, EP_MAP_CELLS, EP_SPLIT_RECS, EP_POOL_WORDS, output, &error),
         "invalid ep_glob neighbor pointer rejected");
  Expect(output == std::vector<uint8_t>({0x42}), "failed serialization leaves output untouched");
  Expect(fixture.glob->nRegions == 3 && fixture.glob->aRegions[0].wInlinePaths[0] == 1,
         "failed serialization does not touch ep_glob");
}

}  // namespace

int main() {
  try {
    TestRoundTripAndDeterminism();
    TestMalformedMetadata();
    TestEveryTruncation();
    TestStreamGatesAndLegacyFallback();
    TestProducerRejectsInvalidStorageWithoutOutput();
    std::cout << "HXPM tests passed: roundtrip, malformed, truncation, stream gates, legacy\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "HXPM test failure: " << error.what() << '\n';
    return 1;
  }
}
