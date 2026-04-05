// ws-client.h
//
// Platform-agnostic WebSocket client interface.
// Each platform provides its own implementation (e.g. ws-client-win.cc).

#ifndef WS_CLIENT_H_
#define WS_CLIENT_H_

#include <string>

struct WsClient;

// Connect to a WebSocket server at the given URL (ws:// or wss://).
// Returns nullptr on failure.
WsClient *WsConnect(const std::string &url);

// Send a UTF-8 text message. Returns true on success.
bool WsSend(WsClient *ws, const std::string &text);

// Receive a UTF-8 text message (blocking). Returns true on success.
// On server close or error, returns false.
bool WsRecv(WsClient *ws, std::string *out);

// Close the WebSocket connection and release all resources.
void WsClose(WsClient *ws);

#endif  // WS_CLIENT_H_
