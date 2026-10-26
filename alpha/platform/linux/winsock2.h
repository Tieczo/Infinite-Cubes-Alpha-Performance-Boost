// Winsock on POSIX sockets, for the native Linux build.
#pragma once
#include <windows.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

typedef int SOCKET;
#define INVALID_SOCKET (-1)
#define SOCKET_ERROR (-1)
#define SD_RECEIVE SHUT_RD
#define SD_SEND SHUT_WR
#define SD_BOTH SHUT_RDWR
typedef struct sockaddr SOCKADDR;
typedef struct sockaddr_in SOCKADDR_IN;

struct WSADATA {
    WORD wVersion;
};
// Writing to a closed connection must fail with an error instead of ending the process (SIGPIPE).
inline int WSAStartup(WORD, WSADATA *)
{
    signal(SIGPIPE, SIG_IGN);
    return 0;
}
inline int WSACleanup() { return 0; }
inline int WSAGetLastError() { return errno; }
// Closing a Winsock socket ends the calls other threads are blocked in (accept, recv); on Linux only
// shutdown does.
inline int closesocket(SOCKET socket)
{
    shutdown(socket, SHUT_RDWR);
    return close(socket);
}
inline int ioctlsocket(SOCKET socket, long command, unsigned long *argument)
{
    int value = (int)*argument;
    return ioctl(socket, command, &value);
}
