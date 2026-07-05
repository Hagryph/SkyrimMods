#pragma once
#include "PCH.h"

namespace hag::ipc {

// Localhost-only TCP debug server. Line-based text protocol; one client at a time.
// Read-only commands (ping/base/read/readb) run on this thread with SEH guards.
// Mutating/executing commands will be marshaled to the game main thread (added next).
class Server {
public:
    static Server& Get();
    void Start(std::uint16_t port, std::string token);
    void Stop();

private:
    void Run();
    void Serve(std::uintptr_t clientSock);
    std::string Dispatch(const std::string& line);

    std::thread       m_thread;
    std::atomic<bool> m_running{false};
    std::uint16_t     m_port = 0;
    std::string       m_token;
};

}  // namespace hag::ipc
