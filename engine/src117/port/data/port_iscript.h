// port_iscript.h -- a mod's image scripts as text, compiled into the bytes 1.17's
// iscript.bin holds (docs/mod-data.md; "iscript.txt"), with Hyperion's
// wide jumps and headers. The scripts are appended to 1.17's at
// start-up, after its last byte, so 1.17's own keep their offsets.
#ifndef PORT_ISCRIPT_H
#define PORT_ISCRIPT_H

#include <functional>
#include <string>
#include <vector>

// One script: its name, its header's type (how many animations it has) and each
// animation's label, empty for none. Its header's offset once compiled.
struct IscriptHeader {
  std::string name;
  int nType = 0;
  std::vector<std::string> anims;
  unsigned nHeader = 0;
};

// Names a mod's script text may use for its numbers: an image, a sprite or a flingy of the
// mod, or a weapon or a sound, by name. Returns -1 when the name is not one.
typedef std::function<int(const char *pszKind, const std::string &name)> IscriptNames;

// Compiles text into *pBlock, the bytes that go at offset nBase of the scripts' buffer: the
// code, then a header for each script. Every offset is a DWORD from the buffer's start (on
// 32 bits nBase plus the block stays under 65,536, as 1.17's records hold a WORD). False,
// with *pError naming the line, when the text does not compile.
bool IscriptCompile(const std::string &text, unsigned nBase, const IscriptNames &names,
                    std::vector<unsigned char> *pBlock, std::vector<IscriptHeader> *pHeaders,
                    std::string *pError);

// The animations a header of type nType has (1.17's types 0 to 30), or -1.
int IscriptAnimCount(int nType);

#endif
