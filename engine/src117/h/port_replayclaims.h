// port_replayclaims.h -- atomic per-upload-job claims shared by app instances.
#ifndef PORT_REPLAYCLAIMS_H
#define PORT_REPLAYCLAIMS_H

#include <cstdint>
#include <string>

namespace ReplayClaims {
bool ClaimOwned(const std::string &path, std::string &owner, int64_t staleAfterSeconds, std::string *pError = nullptr);
bool Claim(const std::string &path, int64_t staleAfterSeconds, std::string *pError = nullptr);
bool Heartbeat(const std::string &path, const std::string &owner);
// Removes only the claimant's own file; another instance's live claim is left alone.
void Release(const std::string &path, const std::string &owner);
}  // namespace ReplayClaims

#endif
