#ifdef ENABLE_CONTROL_SOCKET

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

static std::string default_socket_path()
{
    const char* configured_path = std::getenv("BOMBE_CONTROL_SOCKET");
    if (configured_path && configured_path[0])
        return configured_path;
    const char* runtime_dir = std::getenv("XDG_RUNTIME_DIR");
    if (runtime_dir && runtime_dir[0])
        return std::string(runtime_dir) + "/bombe-control.sock";
    return "/tmp/bombe-control-" + std::to_string(getuid()) + ".sock";
}

static void usage(const char* program)
{
    std::cerr << "Usage: " << program
              << " [--socket PATH] {state|rules|add-rule JSON|hint|hint-status|hint-clear|ping|help}\n";
}

int main(int argc, char* argv[])
{
    std::string path = default_socket_path();
    int argument = 1;
    if (argument < argc && std::string(argv[argument]) == "--socket")
    {
        if (++argument >= argc)
        {
            usage(argv[0]);
            return 2;
        }
        path = argv[argument++];
    }
    if (argument >= argc)
    {
        usage(argv[0]);
        return 2;
    }

    sockaddr_un address = {};
    address.sun_family = AF_UNIX;
    if (path.size() >= sizeof(address.sun_path))
    {
        std::cerr << "Socket path is too long: " << path << '\n';
        return 2;
    }
    std::memcpy(address.sun_path, path.c_str(), path.size() + 1);

    const int connection = socket(AF_UNIX, SOCK_STREAM, 0);
    if (connection < 0)
    {
        std::cerr << "Cannot create socket: " << std::strerror(errno) << '\n';
        return 1;
    }
    if (connect(connection, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0)
    {
        std::cerr << "Cannot connect to " << path << ": " << std::strerror(errno) << '\n';
        close(connection);
        return 1;
    }

    std::string request = argv[argument++];
    while (argument < argc)
        request += " " + std::string(argv[argument++]);
    request += '\n';
    size_t sent = 0;
    while (sent < request.size())
    {
#ifdef MSG_NOSIGNAL
        const ssize_t count = send(connection, request.data() + sent, request.size() - sent, MSG_NOSIGNAL);
#else
        const ssize_t count = send(connection, request.data() + sent, request.size() - sent, 0);
#endif
        if (count > 0)
            sent += count;
        else if (count < 0 && errno == EINTR)
            continue;
        else
        {
            std::cerr << "Cannot send request: " << std::strerror(errno) << '\n';
            close(connection);
            return 1;
        }
    }
    shutdown(connection, SHUT_WR);

    std::string response_prefix;
    char buffer[4096];
    while (true)
    {
        const ssize_t count = recv(connection, buffer, sizeof(buffer), 0);
        if (count > 0)
        {
            if (response_prefix.size() < 6)
                response_prefix.append(buffer, std::min<size_t>(count, 6 - response_prefix.size()));
            std::cout.write(buffer, count);
        }
        else if (count == 0)
            break;
        else if (errno != EINTR)
        {
            std::cerr << "Cannot read response: " << std::strerror(errno) << '\n';
            close(connection);
            return 1;
        }
    }
    close(connection);
    return response_prefix == "error:" ? 1 : 0;
}

#endif
