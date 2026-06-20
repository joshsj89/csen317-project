#include <arpa/inet.h>
#include <cstring>
#include <iostream>
#include <netdb.h> // for gethostbyname, hostent
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include "rpc.hpp"

RPCResponse rpc_send(const std::string &ip, int port, const std::string &msg)
{
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0)
        return {false, "socket_error"};

    struct timeval timeout;
    timeout.tv_sec = 1;  // 1 second timeout
    timeout.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof timeout);
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout, sizeof timeout);

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    if (inet_pton(AF_INET, ip.c_str(), &server_addr.sin_addr) <= 0)
    {
        // fallback: resolve hostname (if IP is a hostname, like in Docker)
        struct hostent *he = gethostbyname(ip.c_str());
        if (he == nullptr)
        {
            close(sock);
            return {false, "invalid hostname or IP"};
        }

        std::memcpy(&server_addr.sin_addr, he->h_addr_list[0], he->h_length);
    }

    if (connect(sock, (sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        close(sock);
        return {false, "connection_timeout"};
    }

    send(sock, msg.c_str(), msg.size(), 0);
    send(sock, "\n", 1, 0); // message delimiter

    char buffer[4096];
    int n = recv(sock, buffer, sizeof(buffer) - 1, 0);
    if (n < 0)
    {
        close(sock);
        return {false, "receive_timeout"};
    }

    buffer[n] = '\0';
    std::string resp(buffer);
    close(sock);

    if (resp.rfind("OK", 0) == 0)
        return {true, resp.substr(3)}; // skip "OK "
    else
        return {false, resp.substr(4)}; // skip "ERR "
}

int rpc_start_listener(int port)
{
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0)
    {
        std::cerr << "Socket creation failed\n";
        exit(1);
    }

    int opt = 1; // allow address reuse
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    if (bind(server_fd, (sockaddr *)&address, sizeof(address)) < 0)
    {
        std::cerr << "Bind failed\n";
        close(server_fd);
        exit(1);
    }

    if (listen(server_fd, 10) < 0)
    {
        std::cerr << "Listen failed\n";
        close(server_fd);
        exit(1);
    }

    return server_fd;
}

void rpc_accept_loop(int server_fd, void (*handler)(int, const std::string&))
{
    while (RUNNING)
    {
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(server_fd, (sockaddr *)&client_addr, &client_len);

        // Check if RUNNING flag was cleared during blocking accept call
        if (!RUNNING)
        {
            if (client_fd >= 0)
                close(client_fd);

            break;
        }

        if (client_fd < 0)
        {
            // std::cerr << "Accept failed\n";
            continue;
        }

        // Handle each client connection in a separate thread to allow concurrent processing
        // Allows a node to keep accepting and serving inbound RPCs while it waits on outbound calls
        std::thread([handler, client_fd]() {
            struct timeval timeout;
            timeout.tv_sec = 1;  // 1 second timeout
            timeout.tv_usec = 0;
            setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeout, sizeof(timeout));

            char buffer[4096];
            int n = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
            if (n < 0)
            {
                std::cerr << "Receive failed\n";
                close(client_fd);
                return;
            }

            buffer[n] = '\0';
            std::string msg(buffer);

            handler(client_fd, msg);
        }).detach();
    }
}