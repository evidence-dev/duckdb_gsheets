#pragma once

// Small cross-platform wrapper around the BSD sockets API. Windows (Winsock2)
// differs from POSIX in socket handle type, option-value pointer type, and
// function names (closesocket/recv/send vs close/read/write); this header
// hides those differences so callers can write one code path. Shared by the
// production OAuth listener and its unit/integration tests.

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#endif

namespace duckdb {
namespace sheets {

#ifdef _WIN32
using socket_t = SOCKET;
static constexpr socket_t INVALID_SOCKET_VALUE = INVALID_SOCKET;

inline bool InitSockets() {
	WSADATA wsa_data;
	return WSAStartup(MAKEWORD(2, 2), &wsa_data) == 0;
}
inline void CleanupSockets() {
	WSACleanup();
}
inline void CloseSocket(socket_t s) {
	closesocket(s);
}
inline int SocketRecv(socket_t s, char *buf, int len) {
	return recv(s, buf, len, 0);
}
inline int SocketSend(socket_t s, const char *buf, int len) {
	return send(s, buf, len, 0);
}
#else
using socket_t = int;
static constexpr socket_t INVALID_SOCKET_VALUE = -1;

inline bool InitSockets() {
	return true;
}
inline void CleanupSockets() {
}
inline void CloseSocket(socket_t s) {
	close(s);
}
inline int SocketRecv(socket_t s, char *buf, int len) {
	return static_cast<int>(read(s, buf, static_cast<size_t>(len)));
}
inline int SocketSend(socket_t s, const char *buf, int len) {
	return static_cast<int>(write(s, buf, static_cast<size_t>(len)));
}
#endif

} // namespace sheets
} // namespace duckdb
