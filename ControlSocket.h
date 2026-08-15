#pragma once

#ifdef ENABLE_CONTROL_SOCKET

#include <string>

class GameState;

class ControlSocket
{
public:
    explicit ControlSocket(GameState& game_state);
    ~ControlSocket();

    ControlSocket(const ControlSocket&) = delete;
    ControlSocket& operator=(const ControlSocket&) = delete;

    void poll();
    const std::string& path() const { return socket_path; }

private:
    GameState& game_state;
    int listen_fd = -1;
    std::string socket_path;

    std::string execute(const std::string& request);
};

#endif
