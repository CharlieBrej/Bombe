#include "ControlSocket.h"

#ifdef ENABLE_CONTROL_SOCKET

#include "GameState.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>

#include <fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

static std::string default_control_socket_path()
{
    const char* configured_path = std::getenv("BOMBE_CONTROL_SOCKET");
    if (configured_path && configured_path[0])
        return configured_path;
    const char* runtime_dir = std::getenv("XDG_RUNTIME_DIR");
    if (runtime_dir && runtime_dir[0])
        return std::string(runtime_dir) + "/bombe-control.sock";
    return "/tmp/bombe-control-" + std::to_string(getuid()) + ".sock";
}

static sockaddr_un socket_address(const std::string& path)
{
    sockaddr_un address = {};
    address.sun_family = AF_UNIX;
    if (path.size() >= sizeof(address.sun_path))
        throw std::runtime_error("control socket path is too long: " + path);
    std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
    return address;
}

static std::runtime_error socket_error(const std::string& operation)
{
    return std::runtime_error(operation + ": " + std::strerror(errno));
}

static void remove_stale_socket(const std::string& path, const sockaddr_un& address)
{
    struct stat info = {};
    if (lstat(path.c_str(), &info) != 0)
    {
        if (errno == ENOENT)
            return;
        throw socket_error("cannot inspect " + path);
    }
    if (!S_ISSOCK(info.st_mode))
        throw std::runtime_error("refusing to replace non-socket path: " + path);

    const int probe = socket(AF_UNIX, SOCK_STREAM, 0);
    if (probe < 0)
        throw socket_error("cannot create socket probe");
    const int connected = connect(probe, reinterpret_cast<const sockaddr*>(&address), sizeof(address));
    const int connect_errno = errno;
    close(probe);
    if (connected == 0)
        throw std::runtime_error("another Bombe instance is listening on " + path);
    if (connect_errno != ECONNREFUSED && connect_errno != ENOENT)
    {
        errno = connect_errno;
        throw socket_error("cannot connect to existing socket " + path);
    }
    if (unlink(path.c_str()) != 0 && errno != ENOENT)
        throw socket_error("cannot remove stale socket " + path);
}

ControlSocket::ControlSocket(GameState& game_state_) : game_state(game_state_)
{
    socket_path = default_control_socket_path();
    const sockaddr_un address = socket_address(socket_path);
    remove_stale_socket(socket_path, address);

    listen_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (listen_fd < 0)
        throw socket_error("cannot create control socket");

    if (bind(listen_fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0)
    {
        const std::runtime_error error = socket_error("cannot bind " + socket_path);
        close(listen_fd);
        listen_fd = -1;
        throw error;
    }
    if (chmod(socket_path.c_str(), S_IRUSR | S_IWUSR) != 0)
    {
        const std::runtime_error error = socket_error("cannot secure " + socket_path);
        close(listen_fd);
        listen_fd = -1;
        unlink(socket_path.c_str());
        throw error;
    }
    if (listen(listen_fd, 4) != 0)
    {
        const std::runtime_error error = socket_error("cannot listen on " + socket_path);
        close(listen_fd);
        listen_fd = -1;
        unlink(socket_path.c_str());
        throw error;
    }
    if (fcntl(listen_fd, F_SETFL, fcntl(listen_fd, F_GETFL) | O_NONBLOCK) != 0)
    {
        const std::runtime_error error = socket_error("cannot make control socket non-blocking");
        close(listen_fd);
        listen_fd = -1;
        unlink(socket_path.c_str());
        throw error;
    }
}

ControlSocket::~ControlSocket()
{
    if (listen_fd >= 0)
        close(listen_fd);
    if (!socket_path.empty())
        unlink(socket_path.c_str());
}

std::string ControlSocket::execute(const std::string& request)
{
    if (request == "state")
        return game_state.grid->debug_dump();
    if (request == "rules")
        return game_state.debug_rules_dump();
    if (request == "hint")
    {
        std::string response;
        switch (game_state.start_hint())
        {
        case GameState::HintStartResult::STARTED:
            response = "Hint started.\n";
            break;
        case GameState::HintStartResult::ALREADY_RUNNING:
            response = "Hint is already running.\n";
            break;
        case GameState::HintStartResult::BOARD_BUSY:
            response = "The board, regions, or rules are still being processed; retry shortly.\n";
            break;
        case GameState::HintStartResult::NO_TARGETS:
            response = "No cells are currently provable from the shown regions.\n";
            break;
        case GameState::HintStartResult::NO_HINTS_LEFT:
            return "error: no hints remaining\n";
        }
        return response + game_state.debug_hint_dump();
    }
    if (request == "hint-status")
        return game_state.debug_hint_dump();
    if (request == "hint-clear")
    {
        game_state.clear_hint();
        return "Hint cleared.\n" + game_state.debug_hint_dump();
    }
    if (request == "add-rule")
        return "error: add-rule requires a single-line JSON rule; see README.md for the schema\n";
    const std::string add_rule_prefix = "add-rule ";
    if (request.compare(0, add_rule_prefix.size(), add_rule_prefix) == 0)
    {
        try
        {
            return game_state.add_rule_from_json(request.substr(add_rule_prefix.size()));
        }
        catch (const std::exception& error)
        {
            return std::string("error: cannot add rule: ") + error.what() + '\n';
        }
    }
    if (request == "ping")
        return "pong\n";
    if (request == "help")
        return "Commands: state, rules, add-rule JSON, hint, hint-status, hint-clear, ping, help\n";
    return "error: unknown command: " + request + "\n";
}

void ControlSocket::poll()
{
    while (true)
    {
        const int client = accept(listen_fd, NULL, NULL);
        if (client < 0)
        {
            if (errno == EINTR)
                continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                return;
            return;
        }

#ifdef SO_NOSIGPIPE
        const int suppress_sigpipe = 1;
        if (setsockopt(client, SOL_SOCKET, SO_NOSIGPIPE, &suppress_sigpipe,
                       sizeof(suppress_sigpipe)) != 0)
        {
            close(client);
            continue;
        }
#endif

        timeval timeout = {};
        timeout.tv_usec = 100000;
        setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

        char buffer[4096];
        std::string request;
        std::string response;
        bool request_complete = false;
        while (!request_complete)
        {
            const ssize_t received = recv(client, buffer, sizeof(buffer), 0);
            if (received > 0)
            {
                request.append(buffer, received);
                const std::string::size_type end = request.find_first_of("\r\n");
                if (end != std::string::npos)
                {
                    request.resize(end);
                    request_complete = true;
                }
                if (request.size() > 65536)
                {
                    response = "error: request exceeds 65536 bytes\n";
                    request_complete = true;
                }
            }
            else if (received == 0)
                request_complete = true;
            else if (errno != EINTR)
            {
                response = "error: incomplete request\n";
                request_complete = true;
            }
        }
        if (response.empty())
            response = request.empty() ? "error: empty request\n" : execute(request);

        size_t sent = 0;
        while (sent < response.size())
        {
#ifdef MSG_NOSIGNAL
            const ssize_t count = send(client, response.data() + sent, response.size() - sent, MSG_NOSIGNAL);
#else
            const ssize_t count = send(client, response.data() + sent, response.size() - sent, 0);
#endif
            if (count > 0)
                sent += count;
            else if (count < 0 && errno == EINTR)
                continue;
            else
                break;
        }
        close(client);
    }
}

#endif
