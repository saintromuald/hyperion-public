// json_stub.cpp -- port_json.cpp's quoting, which VS2013 cannot build (no snprintf): the same bytes.
#include <string>

std::string PortJsonQuote(const std::string &s)
{
  static const char s_szHex[] = "0123456789abcdef";
  std::string out = "\"";
  for (size_t i = 0; i < s.size(); ++i) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    if (c == '"' || c == '\\') {
      out += '\\';
      out += static_cast<char>(c);
    } else if (c < 0x20) {
      out += "\\u00";
      out += s_szHex[c >> 4];
      out += s_szHex[c & 15];
    } else {
      out += static_cast<char>(c);
    }
  }
  return out + "\"";
}
