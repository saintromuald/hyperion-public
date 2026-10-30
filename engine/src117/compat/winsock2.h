// winsock2.h, off Windows: Winsock's names for POSIX sockets, for the live
// channel (port_control.cpp).
#pragma once
#include "windows.h"
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

typedef intptr_t SOCKET;  // a descriptor, pointer-sized as Windows' SOCKET, which the live channel passes as a thread's argument
#define INVALID_SOCKET (-1)
#define SOCKET_ERROR (-1)
#define closesocket close
#define ioctlsocket(s, cmd, argp) ioctl((s), (cmd), (argp))
#define WSAEWOULDBLOCK EWOULDBLOCK
#define WSAECONNRESET ECONNRESET
#define WSAEINTR EINTR
#define WSAEINPROGRESS EINPROGRESS
#define SD_SEND SHUT_WR
#define SD_BOTH SHUT_RDWR
typedef struct WSAData {
  WORD wVersion;
  WORD wHighVersion;
  char szDescription[257];
  char szSystemStatus[129];
  unsigned short iMaxSockets;
  unsigned short iMaxUdpDg;
  char *lpVendorInfo;
} WSADATA, *LPWSADATA;
static inline int WSAStartup(WORD, LPWSADATA pData)
{
  memset(pData, 0, sizeof *pData);
  return 0;
}
static inline int WSACleanup(void)
{
  return 0;
}
static inline int WSAGetLastError(void)
{
  return errno;
}
// Winsock's select ignores its first argument; POSIX's watches the descriptors below it.
static inline int OcSelect(int, fd_set *pReads, fd_set *pWrites, fd_set *pErrors, struct timeval *pWait)
{
  return select(FD_SETSIZE, pReads, pWrites, pErrors, pWait);
}
#define select OcSelect
