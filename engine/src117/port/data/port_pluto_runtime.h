// port_pluto_runtime.h -- the official Pluto v2.0 runtime: where it lives, whether this
// build can run it, and the background download/repair used by first start and Settings.
#ifndef PORT_PLUTO_RUNTIME_H
#define PORT_PLUTO_RUNTIME_H

#include <stdint.h>
#include <string>

// HYPERION_PLUTO_FOLDER wins; otherwise the runtime is <PortDataFolder()>/pluto. The bridge,
// logs, configuration and bandit data share that folder but are not official install payloads.
std::string PortPlutoRuntimeFolder();

// NULL only when the OS/Rosetta and Wine 11 unified-runtime prerequisites pass. This is preflight,
// not proof of inference: actual engine readiness, replies, and shutdown are validated separately.
const char *PortPlutoInferenceCapabilityError();
// Download capability is separate so --no-sb does not disable an already-installed offline runtime.
const char *PortPlutoDownloadCapabilityError();
// Complete, read-only preflight: official payloads, Hyperion's bridge, and POSIX execute access.
bool PortPlutoRuntimeReady(std::string *pError = NULL);
// The exact Wine executable used by capability checking and process launch, resolved through PATH.
std::string PortPlutoWineExecutable();

enum PortPlutoInstallPhase {
  PORT_PLUTO_INSTALL_IDLE,
  PORT_PLUTO_INSTALL_READY,
  PORT_PLUTO_INSTALL_RUNNING,
  PORT_PLUTO_INSTALL_SUCCEEDED,
  PORT_PLUTO_INSTALL_FAILED,
  PORT_PLUTO_INSTALL_CANCELLED,
};

struct PortPlutoInstallStatus {
  PortPlutoInstallPhase phase;
  uint64_t cbReceived, cbTotal;
  std::string stage;  // downloading, verifying, or installing
  std::string error;
};

// Re-read the installed files when a setup surface opens. No-op while a worker is active.
void PortPlutoInstallRefresh();
PortPlutoInstallStatus PortPlutoInstallGetStatus();
// Starts the one-click official download, or asks an active one to cancel. In-game starts are
// refused. False means the action was refused or an existing worker was asked to cancel.
bool PortPlutoInstallStartOrCancel(bool bInGame);
void PortPlutoInstallCancel();
// Blocks a game transition or orderly shutdown until no installer worker can touch the runtime.
void PortPlutoInstallCancelAndWait();
// Stable, quantized UI text: whole percent / MiB only, never per-chunk floating point updates.
std::string PortPlutoInstallText(const PortPlutoInstallStatus &status);
std::string PortPlutoInstallButtonText(const PortPlutoInstallStatus &status);

#endif  // PORT_PLUTO_RUNTIME_H
