// port_lanstream.cpp -- a browser player's seat on the host (docs/modules/lan.md,
// "Streamed seats over WebRTC").
// Presentation and transport only: the seat's game is any seat's.
//
// The room starts this exe again with no window, its sound to nowhere and the player's name
// (--lan-stream <device>:<token>, StartStream); it plays the browser's seat as an installed
// copy plays its own, over WebRTC (libdatachannel): H.264, Opus and a data channel for the
// input, gathering done before the offer, so a LAN needs no trickle. The picture is made I420
// on the game's thread and encoded on a thread of its own, at most 30 a second, by NVENC
// (loaded at run time through CUDA) or OpenH264; a lost picture brings a whole one (PLI). The
// sound is the mixer's after its clamp, 48 kHz Opus in 20 ms frames, encoded on the mixer's
// thread. The input's lines (lan.md, "Protocols"; button 0 left, 1 middle, 2 right) become
// window messages (WinMsgRemoteMouse, WinMsgRemoteKey). On the score screen, with nobody to
// press Continue (the stream left, the browser gone, or two minutes passed), the seat presses
// Enter itself, so it is back in the room for the next match.
#include "port_lanstream.h"
#include "../platform/port_log.h"  // PortLogger
#if !defined(__EMSCRIPTEN__)  // a browser seat streams nothing: the stubs at the end
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <rtc/rtc.hpp>
#include <opus/opus.h>
#include <wels/codec_api.h>
#include <windows.h>
#ifndef _WIN32
#define GUID_DEFINED  // compat/windows.h's GUID, which NVIDIA's header would define again
#endif
#include <ffnvcodec/dynlink_loader.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "../platform/port_paths.h"  // PortCommandOptions
#include "../platform/port_util.h"   // PortNowMs
#include "../render/port_present.h"  // PortPresentRemoteInput
#include "../data/port_sound.h"  // PortSoundForeground, PortSoundTap
#include "../platform/port_json.h"
#include "port_lanclient.h"
#include "port_lanfeed.h"
#include "port_lanproto.h"
#include "../platform/port_winmsg.h"
#include "../tools/port_startup.h"  // PortStartupEnded

namespace {

using Clock = std::chrono::steady_clock;

const int kFps = 30;
const int kVideoPayload = 102, kAudioPayload = 111;
const uint32_t kVideoSsrc = 0x48590001, kAudioSsrc = 0x48590002;
const int kOpusRate = 48000, kOpusFrame = 960;  // 20 ms

const PortLogger Log("hyperion_lan.log", PortLogger::SHARED, "stream: ");

// ---------------------------------------------------------------------------
// The peer: made on the game's thread; libdatachannel's threads hand what they hear to the
// game's through these, under g_lock.

struct Peer {
  std::shared_ptr<rtc::PeerConnection> pc;
  std::shared_ptr<rtc::Track> video, audio;
  std::shared_ptr<rtc::DataChannel> input;
};

std::mutex g_lock;
std::shared_ptr<Peer> g_peer;
std::deque<std::string> g_offers;  // the seat's descriptions, gathered, to go to the browser
std::deque<std::string> g_inputs;  // the browser's input, in order
std::string g_peerState = "new";
int g_nGen;  // the peer's number: an older one's offer is not sent

std::atomic<bool> g_bKey{true};      // a whole picture next (a new peer, the browser's PLI)
std::atomic<bool> g_bVideoOpen{false};
std::atomic<int> g_nKbps{3000};

bool g_bStarted;
Clock::time_point g_t0;  // the stream's beginning, its pictures' and sound's times from it
// the game's end, and the score screen after it until it is continued
bool g_bEnded, g_bLeft;
Clock::time_point g_tEnded, g_tPressed;

void MakePeer(void)
{
  rtc::Configuration config;  // the LAN: the host's own addresses, no STUN or TURN
  auto p = std::make_shared<Peer>();
  p->pc = std::make_shared<rtc::PeerConnection>(config);
  int nGen;
  {
    std::lock_guard<std::mutex> lock(g_lock);
    nGen = ++g_nGen;
  }
  std::weak_ptr<rtc::PeerConnection> wpc = p->pc;
  p->pc->onGatheringStateChange([wpc, nGen](rtc::PeerConnection::GatheringState s) {
    if (s != rtc::PeerConnection::GatheringState::Complete)
      return;
    if (auto pc = wpc.lock())
      if (auto d = pc->localDescription()) {
        std::lock_guard<std::mutex> lock(g_lock);
        if (nGen == g_nGen)
          g_offers.push_back(std::string(*d));
      }
  });
  p->pc->onStateChange([nGen](rtc::PeerConnection::State s) {
    static const char *const kNames[] = {"new", "connecting", "connected", "disconnected", "failed", "closed"};
    const int n = static_cast<int>(s);
    std::lock_guard<std::mutex> lock(g_lock);
    if (nGen == g_nGen)
      g_peerState = n >= 0 && n < 6 ? kNames[n] : "?";
  });

  rtc::Description::Video video("video", rtc::Description::Direction::SendOnly);
  video.addH264Codec(kVideoPayload);
  video.addSSRC(kVideoSsrc, "hyperion", "hyperion", "video");
  p->video = p->pc->addTrack(video);
  auto vcfg = std::make_shared<rtc::RtpPacketizationConfig>(kVideoSsrc, "hyperion", kVideoPayload,
                                                            rtc::H264RtpPacketizer::ClockRate);
  auto vpack = std::make_shared<rtc::H264RtpPacketizer>(rtc::NalUnit::Separator::StartSequence, vcfg);
  vpack->addToChain(std::make_shared<rtc::RtcpSrReporter>(vcfg));
  vpack->addToChain(std::make_shared<rtc::RtcpNackResponder>());
  vpack->addToChain(std::make_shared<rtc::PliHandler>([] { g_bKey = true; }));
  p->video->setMediaHandler(vpack);
  p->video->onOpen([nGen] {
    std::lock_guard<std::mutex> lock(g_lock);
    if (nGen != g_nGen)
      return;
    g_bKey = true;
    g_bVideoOpen = true;
  });
  // an older peer's closing, which libdatachannel's thread may hand over after this one's track
  // has opened, says nothing about this one
  p->video->onClosed([nGen] {
    std::lock_guard<std::mutex> lock(g_lock);
    if (nGen == g_nGen)
      g_bVideoOpen = false;
  });

  rtc::Description::Audio audio("audio", rtc::Description::Direction::SendOnly);
  audio.addOpusCodec(kAudioPayload);
  audio.addSSRC(kAudioSsrc, "hyperion", "hyperion", "audio");
  p->audio = p->pc->addTrack(audio);
  auto acfg = std::make_shared<rtc::RtpPacketizationConfig>(kAudioSsrc, "hyperion", kAudioPayload,
                                                            rtc::OpusRtpPacketizer::DefaultClockRate);
  auto apack = std::make_shared<rtc::OpusRtpPacketizer>(acfg);
  apack->addToChain(std::make_shared<rtc::RtcpSrReporter>(acfg));
  p->audio->setMediaHandler(apack);

  p->input = p->pc->createDataChannel("input");
  p->input->onMessage([](rtc::binary) {},
                      [](std::string text) {
                        std::lock_guard<std::mutex> lock(g_lock);
                        // a frame's input is a few lines; this many unread is a page flooding
                        if (g_inputs.size() < 4096)
                          g_inputs.push_back(std::move(text));
                      });
  std::shared_ptr<Peer> old;
  {
    std::lock_guard<std::mutex> lock(g_lock);
    old = g_peer;
    g_peer = p;
    g_peerState = "new";
  }
  g_bVideoOpen = false;
  if (old)
    old->pc->close();
  p->pc->setLocalDescription();  // the offer; its candidates are gathered before it goes
  Log("a new peer (%d), its offer gathering", nGen);
}

std::shared_ptr<rtc::Track> TrackOf(bool bVideo)
{
  std::lock_guard<std::mutex> lock(g_lock);
  if (!g_peer)
    return nullptr;
  return bVideo ? g_peer->video : g_peer->audio;
}

// ---------------------------------------------------------------------------
// The picture.

struct Picture {
  int w = 0, h = 0;
  double dAt = 0.0;  // seconds since the stream's beginning
  std::vector<uint8_t> yuv;  // I420: Y w*h, then U and V (w/2)*(h/2) each
};

class Encoder {
 public:
  virtual ~Encoder() {}
  virtual bool Encode(const Picture &p, bool bKey, std::vector<uint8_t> *pOut) = 0;
  virtual const char *Name() const = 0;
};

// OpenH264, on the processor (C++ without its assembly here, tools/build_webrtc.py).
class SoftEncoder : public Encoder {
 public:
  ~SoftEncoder() override
  {
    if (m_p != NULL) {
      m_p->Uninitialize();
      WelsDestroySVCEncoder(m_p);
    }
  }
  bool Open(int w, int h, int nKbps)
  {
    if (WelsCreateSVCEncoder(&m_p) != 0 || m_p == NULL)
      return false;
    SEncParamExt e;
    m_p->GetDefaultParams(&e);
    e.iUsageType = SCREEN_CONTENT_REAL_TIME;
    e.iPicWidth = w;
    e.iPicHeight = h;
    e.iTargetBitrate = nKbps * 1000;
    e.iMaxBitrate = nKbps * 1500;
    e.iRCMode = RC_BITRATE_MODE;
    e.fMaxFrameRate = static_cast<float>(kFps);
    e.bEnableFrameSkip = true;
    e.uiIntraPeriod = 0;  // a whole picture only when one is asked for
    e.eSpsPpsIdStrategy = CONSTANT_ID;
    e.iSpatialLayerNum = 1;
    e.iTemporalLayerNum = 1;
    e.iMultipleThreadIdc = 4;
    SSpatialLayerConfig &l = e.sSpatialLayers[0];
    l.iVideoWidth = w;
    l.iVideoHeight = h;
    l.fFrameRate = static_cast<float>(kFps);
    l.iSpatialBitrate = e.iTargetBitrate;
    l.iMaxSpatialBitrate = e.iMaxBitrate;
    l.uiProfileIdc = PRO_BASELINE;
    l.sSliceArgument.uiSliceMode = SM_FIXEDSLCNUM_SLICE;
    l.sSliceArgument.uiSliceNum = 4;
    if (m_p->InitializeExt(&e) != 0)
      return false;
    int nFormat = videoFormatI420;
    m_p->SetOption(ENCODER_OPTION_DATAFORMAT, &nFormat);
    return true;
  }
  bool Encode(const Picture &p, bool bKey, std::vector<uint8_t> *pOut) override
  {
    if (bKey)
      m_p->ForceIntraFrame(true);
    SSourcePicture s;
    memset(&s, 0, sizeof s);
    s.iColorFormat = videoFormatI420;
    s.iPicWidth = p.w;
    s.iPicHeight = p.h;
    s.iStride[0] = p.w;
    s.iStride[1] = s.iStride[2] = p.w / 2;
    s.pData[0] = const_cast<uint8_t *>(p.yuv.data());
    s.pData[1] = s.pData[0] + p.w * p.h;
    s.pData[2] = s.pData[1] + (p.w / 2) * (p.h / 2);
    s.uiTimeStamp = static_cast<long long>(p.dAt * 1000.0);
    SFrameBSInfo info;
    memset(&info, 0, sizeof info);
    if (m_p->EncodeFrame(&s, &info) != cmResultSuccess)
      return false;
    pOut->clear();
    if (info.eFrameType == videoFrameTypeSkip)
      return true;
    for (int i = 0; i < info.iLayerNum; ++i) {
      const SLayerBSInfo &l = info.sLayerInfo[i];
      int n = 0;
      for (int k = 0; k < l.iNalCount; ++k)
        n += l.pNalLengthInByte[k];
      pOut->insert(pOut->end(), l.pBsBuf, l.pBsBuf + n);
    }
    return true;
  }
  const char *Name() const override { return "OpenH264"; }

 private:
  ISVCEncoder *m_p = NULL;
};

// NVENC, through CUDA, from the driver's own libraries. A graphics card has a few encoder sessions
// at once; with none free the processor's encoder takes over.
class NvEncoder : public Encoder {
 public:
  ~NvEncoder() override
  {
    if (m_enc != NULL) {
      if (m_in != NULL)
        m_fn.nvEncDestroyInputBuffer(m_enc, m_in);
      if (m_out != NULL)
        m_fn.nvEncDestroyBitstreamBuffer(m_enc, m_out);
      m_fn.nvEncDestroyEncoder(m_enc);
    }
    if (m_ctx != NULL)
      m_cu->cuCtxDestroy(m_ctx);
    nvenc_free_functions(&m_nv);
    cuda_free_functions(&m_cu);
  }
  bool Open(int w, int h, int nKbps, std::string *pWhy)
  {
    if (cuda_load_functions(&m_cu, NULL) < 0 || nvenc_load_functions(&m_nv, NULL) < 0) {
      *pWhy = "no NVIDIA driver";
      return false;
    }
    CUdevice dev;
    if (m_cu->cuInit(0) != CUDA_SUCCESS || m_cu->cuDeviceGet(&dev, 0) != CUDA_SUCCESS ||
        m_cu->cuCtxCreate(&m_ctx, 0, dev) != CUDA_SUCCESS) {
      *pWhy = "no CUDA device";
      return false;
    }
    memset(&m_fn, 0, sizeof m_fn);
    m_fn.version = NV_ENCODE_API_FUNCTION_LIST_VER;
    if (m_nv->NvEncodeAPICreateInstance(&m_fn) != NV_ENC_SUCCESS) {
      *pWhy = "the driver's NVENC is older than its headers";
      return false;
    }
    NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS open;
    memset(&open, 0, sizeof open);
    open.version = NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS_VER;
    open.deviceType = NV_ENC_DEVICE_TYPE_CUDA;
    open.device = m_ctx;
    open.apiVersion = NVENCAPI_VERSION;
    if (m_fn.nvEncOpenEncodeSessionEx(&open, &m_enc) != NV_ENC_SUCCESS) {
      m_enc = NULL;
      *pWhy = "no encoder session free";
      return false;
    }
    // H.264's baseline, which every browser decodes, or its main where a card has no baseline
    static const GUID *const kProfiles[] = {&NV_ENC_H264_PROFILE_BASELINE_GUID, &NV_ENC_H264_PROFILE_MAIN_GUID};
    for (const GUID *pProfile : kProfiles) {
      NV_ENC_PRESET_CONFIG preset;
      memset(&preset, 0, sizeof preset);
      preset.version = NV_ENC_PRESET_CONFIG_VER;
      preset.presetCfg.version = NV_ENC_CONFIG_VER;
      if (m_fn.nvEncGetEncodePresetConfigEx(m_enc, NV_ENC_CODEC_H264_GUID, NV_ENC_PRESET_P3_GUID,
                                            NV_ENC_TUNING_INFO_ULTRA_LOW_LATENCY, &preset) != NV_ENC_SUCCESS) {
        *pWhy = "no H.264 preset";
        return false;
      }
      m_cfg = preset.presetCfg;
      m_cfg.profileGUID = *pProfile;
      m_cfg.gopLength = NVENC_INFINITE_GOPLENGTH;
      m_cfg.frameIntervalP = 1;
      m_cfg.rcParams.rateControlMode = NV_ENC_PARAMS_RC_CBR;
      m_cfg.rcParams.averageBitRate = static_cast<uint32_t>(nKbps) * 1000;
      m_cfg.rcParams.maxBitRate = m_cfg.rcParams.averageBitRate;
      m_cfg.rcParams.vbvBufferSize = m_cfg.rcParams.averageBitRate / kFps;
      m_cfg.rcParams.vbvInitialDelay = m_cfg.rcParams.vbvBufferSize;
      m_cfg.encodeCodecConfig.h264Config.idrPeriod = NVENC_INFINITE_GOPLENGTH;
      m_cfg.encodeCodecConfig.h264Config.repeatSPSPPS = 1;
      NV_ENC_INITIALIZE_PARAMS init;
      memset(&init, 0, sizeof init);
      init.version = NV_ENC_INITIALIZE_PARAMS_VER;
      init.encodeGUID = NV_ENC_CODEC_H264_GUID;
      init.presetGUID = NV_ENC_PRESET_P3_GUID;
      init.tuningInfo = NV_ENC_TUNING_INFO_ULTRA_LOW_LATENCY;
      init.encodeWidth = init.darWidth = init.maxEncodeWidth = static_cast<uint32_t>(w);
      init.encodeHeight = init.darHeight = init.maxEncodeHeight = static_cast<uint32_t>(h);
      init.frameRateNum = kFps;
      init.frameRateDen = 1;
      init.enablePTD = 1;
      init.encodeConfig = &m_cfg;
      if (m_fn.nvEncInitializeEncoder(m_enc, &init) == NV_ENC_SUCCESS) {
        m_pProfile = pProfile == kProfiles[0] ? "baseline" : "main";
        break;
      }
    }
    if (m_pProfile == NULL) {
      *pWhy = "the encoder did not start";
      return false;
    }
    NV_ENC_CREATE_INPUT_BUFFER in;
    memset(&in, 0, sizeof in);
    in.version = NV_ENC_CREATE_INPUT_BUFFER_VER;
    in.width = static_cast<uint32_t>(w);
    in.height = static_cast<uint32_t>(h);
    in.bufferFmt = NV_ENC_BUFFER_FORMAT_IYUV;
    NV_ENC_CREATE_BITSTREAM_BUFFER out;
    memset(&out, 0, sizeof out);
    out.version = NV_ENC_CREATE_BITSTREAM_BUFFER_VER;
    if (m_fn.nvEncCreateInputBuffer(m_enc, &in) != NV_ENC_SUCCESS) {
      *pWhy = "no encoder buffers";
      return false;
    }
    m_in = in.inputBuffer;  // kept before the next one is made, so a failure there still frees it
    if (m_fn.nvEncCreateBitstreamBuffer(m_enc, &out) != NV_ENC_SUCCESS) {
      *pWhy = "no encoder buffers";
      return false;
    }
    m_out = out.bitstreamBuffer;
    m_w = w;
    m_h = h;
    return true;
  }
  bool Encode(const Picture &p, bool bKey, std::vector<uint8_t> *pOut) override
  {
    NV_ENC_LOCK_INPUT_BUFFER lock;
    memset(&lock, 0, sizeof lock);
    lock.version = NV_ENC_LOCK_INPUT_BUFFER_VER;
    lock.inputBuffer = m_in;
    if (m_fn.nvEncLockInputBuffer(m_enc, &lock) != NV_ENC_SUCCESS)
      return false;
    // IYUV: Y rows at the pitch, then U and V at half of it
    uint8_t *pDst = static_cast<uint8_t *>(lock.bufferDataPtr);
    const uint32_t nPitch = lock.pitch;
    const uint8_t *pSrc = p.yuv.data();
    for (int y = 0; y < m_h; ++y)
      memcpy(pDst + y * nPitch, pSrc + y * m_w, static_cast<size_t>(m_w));
    pDst += nPitch * m_h;
    pSrc += m_w * m_h;
    for (int plane = 0; plane < 2; ++plane) {
      for (int y = 0; y < m_h / 2; ++y)
        memcpy(pDst + y * (nPitch / 2), pSrc + y * (m_w / 2), static_cast<size_t>(m_w / 2));
      pDst += (nPitch / 2) * (m_h / 2);
      pSrc += (m_w / 2) * (m_h / 2);
    }
    m_fn.nvEncUnlockInputBuffer(m_enc, m_in);
    NV_ENC_PIC_PARAMS pic;
    memset(&pic, 0, sizeof pic);
    pic.version = NV_ENC_PIC_PARAMS_VER;
    pic.inputBuffer = m_in;
    pic.bufferFmt = NV_ENC_BUFFER_FORMAT_IYUV;
    pic.inputWidth = static_cast<uint32_t>(m_w);
    pic.inputHeight = static_cast<uint32_t>(m_h);
    pic.inputPitch = nPitch;
    pic.outputBitstream = m_out;
    pic.pictureStruct = NV_ENC_PIC_STRUCT_FRAME;
    pic.inputTimeStamp = m_qwFrame++;
    if (bKey)
      pic.encodePicFlags = NV_ENC_PIC_FLAG_FORCEIDR | NV_ENC_PIC_FLAG_OUTPUT_SPSPPS;
    if (m_fn.nvEncEncodePicture(m_enc, &pic) != NV_ENC_SUCCESS)
      return false;
    NV_ENC_LOCK_BITSTREAM bits;
    memset(&bits, 0, sizeof bits);
    bits.version = NV_ENC_LOCK_BITSTREAM_VER;
    bits.outputBitstream = m_out;
    if (m_fn.nvEncLockBitstream(m_enc, &bits) != NV_ENC_SUCCESS)
      return false;
    const uint8_t *pBits = static_cast<const uint8_t *>(bits.bitstreamBufferPtr);
    pOut->assign(pBits, pBits + bits.bitstreamSizeInBytes);
    m_fn.nvEncUnlockBitstream(m_enc, m_out);
    return true;
  }
  const char *Name() const override { return "NVENC"; }
  const char *Profile() const { return m_pProfile; }

 private:
  CudaFunctions *m_cu = NULL;
  NvencFunctions *m_nv = NULL;
  CUcontext m_ctx = NULL;
  NV_ENCODE_API_FUNCTION_LIST m_fn;
  NV_ENC_CONFIG m_cfg;
  void *m_enc = NULL;
  NV_ENC_INPUT_PTR m_in = NULL;
  NV_ENC_OUTPUT_PTR m_out = NULL;
  int m_w = 0, m_h = 0;
  uint64_t m_qwFrame = 0;
  const char *m_pProfile = NULL;
};

std::unique_ptr<Encoder> MakeEncoder(int w, int h, int nKbps)
{
  const char *pszWho = getenv("HYPERION_STREAM_ENCODER");
  if (pszWho == NULL || strcmp(pszWho, "openh264") != 0) {
    std::unique_ptr<NvEncoder> nv(new NvEncoder);
    std::string why;
    if (nv->Open(w, h, nKbps, &why)) {
      Log("NVENC encodes %dx%d at %d kbps (%s)", w, h, nKbps, nv->Profile());
      return std::move(nv);
    }
    Log("NVENC: %s; OpenH264 encodes", why.c_str());
  }
  std::unique_ptr<SoftEncoder> soft(new SoftEncoder);
  if (soft->Open(w, h, nKbps)) {
    Log("OpenH264 encodes %dx%d at %d kbps", w, h, nKbps);
    return std::move(soft);
  }
  Log("no encoder for %dx%d", w, h);
  return nullptr;
}

// The game's thread leaves the latest picture here; the encoder's takes it.
std::mutex g_picLock;
std::condition_variable g_picReady;
Picture g_pic;
bool g_bPic;
bool g_bStopEncoder;  // the process winding down: the encoder's thread leaves its loop
std::thread g_encoder;
Clock::time_point g_tLastPic;

// The palette's colours as Y, U and V (BT.601, video range), made again when it changes.
uint8_t g_lastPalette[1024];
int g_y[256], g_u[256], g_v[256];

// What has been sent, for the readings.
std::atomic<uint32_t> g_dwFrames{0}, g_dwBytes{0}, g_dwEncUs{0};
std::mutex g_nameLock;
std::string g_encoderName = "none";
int g_nPicW, g_nPicH;

void EncodeLoop(void)
{
  std::unique_ptr<Encoder> enc;
  int nW = 0, nH = 0, nKbps = 0;
  Clock::time_point tRetry;  // with no encoder at all, when another one may be made
  Picture pic;
  std::vector<uint8_t> out;
  for (;;) {
    {
      std::unique_lock<std::mutex> lock(g_picLock);
      g_picReady.wait(lock, [] { return g_bPic || g_bStopEncoder; });
      if (g_bStopEncoder)
        return;
      std::swap(pic, g_pic);
      g_bPic = false;
    }
    const int nWant = g_nKbps;
    const bool bChanged = pic.w != nW || pic.h != nH || nWant != nKbps;
    if (bChanged || (!enc && Clock::now() >= tRetry)) {
      enc.reset();
      enc = MakeEncoder(pic.w, pic.h, nWant);
      nW = pic.w;
      nH = pic.h;
      nKbps = nWant;
      g_bKey = true;
      // a machine with no encoder of either kind is not asked again for a few seconds: a try for
      // every picture loads and unloads the driver's libraries thirty times a second and logs it
      if (!enc)
        tRetry = Clock::now() + std::chrono::seconds(5);
      std::lock_guard<std::mutex> lock(g_nameLock);
      g_encoderName = enc ? enc->Name() : "none";
      g_nPicW = nW;
      g_nPicH = nH;
    }
    if (!enc)
      continue;
    const bool bKey = g_bKey.exchange(false);
    const Clock::time_point t = Clock::now();
    if (!enc->Encode(pic, bKey, &out)) {
      Log("%s could not encode a picture", enc->Name());
      enc.reset();
      continue;
    }
    g_dwEncUs += static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - t).count());
    if (out.empty())
      continue;
    std::shared_ptr<rtc::Track> video = TrackOf(true);
    if (!video || !video->isOpen())
      continue;
    try {
      video->sendFrame(reinterpret_cast<const rtc::byte *>(out.data()), out.size(),
                       rtc::FrameInfo(std::chrono::duration<double>(pic.dAt)));
      ++g_dwFrames;
      g_dwBytes += static_cast<uint32_t>(out.size());
    } catch (const std::exception &e) {
      Log("a picture was not sent: %s", e.what());
    }
  }
}

// The process winding down by itself (not through ExitProcess, which takes the thread with it):
// the encoder's thread is stopped and waited for while the locks, the picture and the peer it
// uses are still there. Start registers this once the stream is up, so it runs before them.
void StopEncoder(void)
{
  {
    std::lock_guard<std::mutex> lock(g_picLock);
    g_bStopEncoder = true;
  }
  g_picReady.notify_one();
  if (g_encoder.joinable())
    g_encoder.join();
}

// ---------------------------------------------------------------------------
// The sound: the mixer's, made 48 kHz (a straight line between its samples) and sent as Opus.
// All of it on the mixer's thread.

OpusEncoder *g_opus;
std::vector<float> g_pcm;  // 48 kHz stereo waiting for a whole frame
float g_lastSample[2];
double g_dPos;             // the next 48 kHz sample's place among the mixer's, from the last one
uint64_t g_qwSamples;      // 48 kHz samples sent

void SoundTap(const float *pSamples, int nFrames, int nRate)
{
  std::shared_ptr<rtc::Track> audio = TrackOf(false);
  if (!audio || !audio->isOpen() || nFrames <= 0)
    return;
  if (g_opus == NULL) {
    int nErr = 0;
    g_opus = opus_encoder_create(kOpusRate, 2, OPUS_APPLICATION_RESTRICTED_LOWDELAY, &nErr);
    if (g_opus == NULL)
      return;
    opus_encoder_ctl(g_opus, OPUS_SET_BITRATE(96000));
  }
  const double dStep = static_cast<double>(nRate) / kOpusRate;
  // index -1 is the last block's last sample
  while (g_dPos < nFrames - 1) {
    const int i = static_cast<int>(g_dPos < 0 ? -1 : g_dPos);
    const double f = g_dPos - i;
    for (int c = 0; c < 2; ++c) {
      const float a = i < 0 ? g_lastSample[c] : pSamples[i * 2 + c];
      const float b = pSamples[(i + 1) * 2 + c];
      g_pcm.push_back(static_cast<float>(a + (b - a) * f));
    }
    g_dPos += dStep;
  }
  g_dPos -= nFrames;
  g_lastSample[0] = pSamples[(nFrames - 1) * 2];
  g_lastSample[1] = pSamples[(nFrames - 1) * 2 + 1];
  unsigned char abPacket[1500];
  size_t nAt = 0;
  while (g_pcm.size() - nAt >= static_cast<size_t>(kOpusFrame * 2)) {
    const int n = opus_encode_float(g_opus, g_pcm.data() + nAt, kOpusFrame, abPacket, sizeof abPacket);
    nAt += kOpusFrame * 2;
    if (n <= 0)
      continue;
    try {
      audio->sendFrame(reinterpret_cast<const rtc::byte *>(abPacket), static_cast<size_t>(n),
                       rtc::FrameInfo(std::chrono::duration<double>(static_cast<double>(g_qwSamples) / kOpusRate)));
    } catch (const std::exception &) {
    }
    g_qwSamples += kOpusFrame;
  }
  g_pcm.erase(g_pcm.begin(), g_pcm.begin() + static_cast<std::ptrdiff_t>(nAt));
}

// ---------------------------------------------------------------------------
// The browser's input, on the game's thread.

void Input(const std::string &line)
{
  char c = 0;
  int a = 0, b = 0, x = 0, y = 0;
  const int n = sscanf(line.c_str(), "%c %d %d %d %d", &c, &a, &b, &x, &y);
  if (n < 1 || (n < 2 && c != 'l'))
    return;
  if (c == 'm' && n >= 3) {
    WinMsgRemoteMouse(0x0200, a, b);  // WM_MOUSEMOVE
  } else if ((c == 'd' || c == 'u' || c == 'D') && n >= 4) {
    // a, the button: 0 left, 1 middle, 2 right; WM_LBUTTONDOWN, WM_MBUTTONDOWN, WM_RBUTTONDOWN
    const unsigned int uDown = a == 2 ? 0x0204 : a == 1 ? 0x0207 : 0x0201;
    WinMsgRemoteMouse(0x0200, b, x);
    WinMsgRemoteMouse(c == 'd' ? uDown : c == 'u' ? uDown + 1 : uDown + 2, b, x);
  } else if (c == 'k' && n >= 3) {
    // the character as Windows-1252 has it; one it does not have is left out
    const unsigned int uChar = n >= 4 && x > 0 && x < 0x100 ? static_cast<unsigned int>(x) : 0;
    WinMsgRemoteKey(a, b != 0, uChar);
  } else if (c == 'q') {
    // the encoder is made again at each change, so a change goes through once a second at most
    static uint64_t s_qwLast;
    if (PortNowMs() - s_qwLast >= 1000) {
      s_qwLast = PortNowMs();
      g_nKbps = a < 500 ? 500 : a > 20000 ? 20000 : a;
    }
  } else if (c == 'l') {
    g_bLeft = true;
  }
}

// The seat's window is its player's game, in front for them, which a window never shown never
// becomes by itself; port_present.cpp holds it active from then on (PortPresentRemoteInput).
void Activate(void)
{
  WinMsgPost(NULL, 0x001C, 1, 0);  // WM_ACTIVATEAPP
}

void Start(void)
{
  g_bStarted = true;
  g_t0 = Clock::now();
  rtc::InitLogger(rtc::LogLevel::Warning, [](rtc::LogLevel, std::string message) {
    Log("webrtc: %s", message.c_str());
  });
  PortPresentRemoteInput(true);
  PortSoundForeground(true);
  Activate();
  PortSoundTap(SoundTap);
  g_encoder = std::thread(EncodeLoop);
  atexit(StopEncoder);
  Log("the seat for %s on this machine; its browser's peer next", LanPlayerName().c_str());
}

bool Seat(void)
{
  return PortCommandOptions()->szLanStream[0] != 0;
}

}  // namespace

void LanStreamSignal(const PortJson &j)
{
  if (!Seat() || !g_bStarted)
    return;
  if (j["want"].b) {
    // a connection made anew each time: once in two seconds at most
    static uint64_t s_qwLast;
    if (PortNowMs() - s_qwLast >= 2000) {
      s_qwLast = PortNowMs();
      MakePeer();
    }
    return;
  }
  if (j["type"].Str() == "answer" && !j["sdp"].Str().empty()) {
    std::shared_ptr<Peer> p;
    {
      std::lock_guard<std::mutex> lock(g_lock);
      p = g_peer;
    }
    if (!p)
      return;
    try {
      p->pc->setRemoteDescription(rtc::Description(j["sdp"].Str(), "answer"));
      Log("the browser answered");
    } catch (const std::exception &e) {
      Log("the browser's answer was not taken: %s", e.what());
    }
  }
}

void LanStreamPump(bool bIn)
{
  if (!Seat())
    return;
  // the room gone: the seat goes too, as the room's copy does
  if (LanClientLost()) {
    PortStartupEnded(0);
    ExitProcess(0);
  }
  if (!bIn)
    return;
  if (!g_bStarted) {
    Start();
    MakePeer();
  }
  std::deque<std::string> offers, inputs;
  std::string state;
  {
    std::lock_guard<std::mutex> lock(g_lock);
    offers.swap(g_offers);
    inputs.swap(g_inputs);
    state = g_peerState;
  }
  for (const std::string &sdp : offers)
    LanClientSay("{\"t\":\"rtc\",\"type\":\"offer\",\"sdp\":" + PortJsonQuote(sdp) + "}");
  for (const std::string &line : inputs)
    Input(line);
  LanWatchFollowPump();
  // the score screen with nobody there to continue it: Enter, every two seconds until it has gone
  const Clock::time_point tNow = Clock::now();
  if (g_bEnded && (g_bLeft || state != "connected" || tNow - g_tEnded > std::chrono::minutes(2)) &&
      tNow - g_tPressed > std::chrono::seconds(2)) {
    g_tPressed = tNow;
    Log("the score screen continued for the player, who is not watching it");
    WinMsgRemoteKey(0x0D, true, 0);  // VK_RETURN
    WinMsgRemoteKey(0x0D, false, 0);
  }
  // the readings, every two seconds
  static Clock::time_point s_tSaid = Clock::now();
  const Clock::time_point t = Clock::now();
  const double dSecs = std::chrono::duration<double>(t - s_tSaid).count();
  if (dSecs >= 2.0) {
    s_tSaid = t;
    const uint32_t dwFrames = g_dwFrames.exchange(0), dwBytes = g_dwBytes.exchange(0), dwUs = g_dwEncUs.exchange(0);
    std::string name;
    int w, h;
    {
      std::lock_guard<std::mutex> lock(g_nameLock);
      name = g_encoderName;
      w = g_nPicW;
      h = g_nPicH;
    }
    char sz[320];
    snprintf(sz, sizeof sz,
             "{\"t\":\"streaming\",\"encoder\":%s,\"w\":%d,\"h\":%d,\"fps\":%.1f,\"kbps\":%.0f,\"encMs\":%.2f,"
             "\"peer\":%s,\"score\":%s}",
             PortJsonQuote(name).c_str(), w, h, dwFrames / dSecs, dwBytes * 8.0 / 1000.0 / dSecs,
             dwFrames ? dwUs / 1000.0 / dwFrames : 0.0, PortJsonQuote(state).c_str(), g_bEnded ? "true" : "false");
    LanClientSay(sz);
  }
}

void LanStreamGameEnded(void)
{
  if (!g_bStarted)
    return;
  g_bEnded = true;
  g_bLeft = false;
  g_tEnded = Clock::now();
}

void LanStreamScoreDone(void)
{
  g_bEnded = false;
}

bool LanStreamWants(void)
{
  if (!g_bStarted || !g_bVideoOpen)
    return false;
  return Clock::now() - g_tLastPic >= std::chrono::milliseconds(1000 / kFps - 2);
}

void LanStreamFrame(const unsigned char *pPixels, int nWidth, int nHeight, int nPitch, const unsigned char *pPalette)
{
  if (pPixels == NULL || pPalette == NULL || nWidth < 16 || nHeight < 16)
    return;
  g_tLastPic = Clock::now();
  if (memcmp(g_lastPalette, pPalette, sizeof g_lastPalette) != 0) {
    memcpy(g_lastPalette, pPalette, sizeof g_lastPalette);
    for (int i = 0; i < 256; ++i) {
      const int r = pPalette[i * 4], g = pPalette[i * 4 + 1], b = pPalette[i * 4 + 2];
      g_y[i] = ((66 * r + 129 * g + 25 * b + 128) >> 8) + 16;
      g_u[i] = ((-38 * r - 74 * g + 112 * b + 128) >> 8) + 128;
      g_v[i] = ((112 * r - 94 * g - 18 * b + 128) >> 8) + 128;
    }
  }
  const int w = nWidth & ~1, h = nHeight & ~1;
  Picture pic;
  {
    // the encoder's last one back, to fill again
    std::lock_guard<std::mutex> lock(g_picLock);
    if (!g_bPic)
      std::swap(pic, g_pic);
  }
  pic.w = w;
  pic.h = h;
  pic.dAt = std::chrono::duration<double>(g_tLastPic - g_t0).count();
  pic.yuv.resize(static_cast<size_t>(w) * h * 3 / 2);
  uint8_t *pY = pic.yuv.data(), *pU = pY + w * h, *pV = pU + (w / 2) * (h / 2);
  for (int y = 0; y < h; y += 2) {
    const unsigned char *p0 = pPixels + y * nPitch, *p1 = p0 + nPitch;
    uint8_t *pY0 = pY + y * w, *pY1 = pY0 + w;
    uint8_t *pUr = pU + (y / 2) * (w / 2), *pVr = pV + (y / 2) * (w / 2);
    for (int x = 0; x < w; x += 2) {
      const int a = p0[x], b = p0[x + 1], c = p1[x], d = p1[x + 1];
      pY0[x] = static_cast<uint8_t>(g_y[a]);
      pY0[x + 1] = static_cast<uint8_t>(g_y[b]);
      pY1[x] = static_cast<uint8_t>(g_y[c]);
      pY1[x + 1] = static_cast<uint8_t>(g_y[d]);
      pUr[x / 2] = static_cast<uint8_t>((g_u[a] + g_u[b] + g_u[c] + g_u[d] + 2) >> 2);
      pVr[x / 2] = static_cast<uint8_t>((g_v[a] + g_v[b] + g_v[c] + g_v[d] + 2) >> 2);
    }
  }
  {
    std::lock_guard<std::mutex> lock(g_picLock);
    std::swap(g_pic, pic);
    g_bPic = true;
  }
  g_picReady.notify_one();
}
#else
void LanStreamSignal(const PortJson &) {}
void LanStreamPump(bool) {}
bool LanStreamWants(void) { return false; }
void LanStreamGameEnded(void) {}
void LanStreamScoreDone(void) {}
void LanStreamFrame(const unsigned char *, int, int, int, const unsigned char *) {}
#endif
