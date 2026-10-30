# Third-party code and fonts

Hyperion's own code is under the MIT license (`LICENSE`). It builds with the libraries below,
which the build fetches into the ignored `engine/third_party/` folder (`engine/tools/fetch_*.py`).
None of them is copied into this repository except where a row says so. Each keeps its own
license, and every package carries the licenses of what it contains.

| Library | Version | License | Used for |
|---|---|---|---|
| [SDL3](https://github.com/libsdl-org/SDL) | 3.4.16, with the patches in `engine/tools/sdl3-patches/` | zlib | window, input, sound, the GPU API |
| [StormLib](https://github.com/ladislav-zezula/StormLib) | 9.40 | MIT | MPQ archives, PKWARE implode and explode |
| [CascLib](https://github.com/ladislav-zezula/CascLib) | 3.0 | MIT | StarCraft: Remastered's CASC storage |
| [Clay](https://github.com/nicbarker/clay) | 0.14 | zlib | the menus' layout |
| [stb](https://github.com/nothings/stb) | commit `6e9f34d`, with the patches in `engine/tools/stb-patches/` | MIT or public domain | TrueType text, Ogg Vorbis, PNG and TGA |
| [dr_libs](https://github.com/mackron/dr_libs) | commit `dfe8377` | MIT-0 or public domain | MP3, FLAC and WAV |
| [libvpx](https://github.com/webmproject/libvpx), with libwebm | 1.17.0 | BSD-3-Clause, with Google's patent grant | VP8 and VP9 video; its configuration is committed in `engine/tools/libvpx/` |
| [dav1d](https://github.com/videolan/dav1d) | 1.5.4 | BSD-2-Clause | AV1 video; its configuration is committed in `engine/tools/dav1d/` |
| [libwebp](https://github.com/webmproject/libwebp) | 1.5.0 | BSD-3-Clause | WebP images |
| [DirectXShaderCompiler](https://github.com/microsoft/DirectXShaderCompiler) | 1.9.2607 | NCSA and Apache-2.0 with LLVM exceptions | compiles the shaders at build time; not shipped |
| [SPIRV-Cross](https://github.com/KhronosGroup/SPIRV-Cross) | vulkan-sdk-1.4.363.0 | Apache-2.0 | turns the shaders into Metal for the Mac at build time; not shipped |
| [libdatachannel](https://github.com/paullouisageneau/libdatachannel) | 0.24.6 | MPL-2.0 | the LAN room's streamed seats |
| [libjuice](https://github.com/paullouisageneau/libjuice) | pinned commit | MPL-2.0 | ICE, for the streamed seats |
| [usrsctp](https://github.com/sctplab/usrsctp), [libSRTP](https://github.com/cisco/libsrtp) | pinned commits | BSD-3-Clause | SCTP and SRTP, for the streamed seats |
| [plog](https://github.com/SergiusTheBest/plog), [nlohmann/json](https://github.com/nlohmann/json) | as libdatachannel pins them | MIT | libdatachannel's logging and JSON |
| [OpenH264](https://github.com/cisco/openh264) | as `fetch_webrtc.py` pins it | BSD-2-Clause | video for the streamed seats |
| [Mbed TLS](https://github.com/Mbed-TLS/mbedtls) | as `fetch_webrtc.py` pins it | Apache-2.0 | encryption for the streamed seats |
| [Opus](https://opus-codec.org/) | as `fetch_webrtc.py` pins it | BSD-3-Clause | sound for the streamed seats |

H.264, H.265 and AAC video are decoded by the system (Media Foundation on Windows, VideoToolbox
on a Mac, the player's own FFmpeg on Linux); Hyperion carries none of their code.

## Fonts

Built into the program. All are under the SIL Open Font License 1.1; the packages carry the
license texts in `docs/licenses/fonts.txt`.

- Saira, Michroma, Chakra Petch and IBM Plex Mono, from [Google Fonts](https://github.com/google/fonts)
- Noto Sans, Noto Sans JP, KR, SC and TC, cut to the characters Hyperion uses

## Hyperion's own media

The HUD sounds in `engine/huds/files/` were made for Hyperion with a sound generator; the prompt
for each is beside it. They are under this repository's license.

## The game's files

Hyperion contains none of StarCraft's art, sound, video or archives. It reads them from the
player's own install. The game's small built-in tables and dialog layouts are carried as
generated source (`engine/src117/gen/`), as decompilation projects carry them.
