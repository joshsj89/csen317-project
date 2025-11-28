#include <arpa/inet.h>
#include <cstring>
#include <iostream>
#include <sys/socket.h>
#include <unistd.h>
#include "rpc.hpp"

RPCResponse rpc_send(const std::string &ip, int port, const std::string &msg)
{
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0)
        return {false, "socket creation failed"};

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    if (inet_pton(AF_INET, ip.c_str(), &server_addr.sin_addr) <= 0)
        return {false, "invalid IP address"};

    if (connect(sock, (sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        close(sock);
        return {false, "connection failed"};
    }

    send(sock, msg.c_str(), msg.size(), 0);
    send(sock, "\n", 1, 0); // message delimiter

    char buffer[4096];
    int n = recv(sock, buffer, sizeof(buffer) - 1, 0);
    if (n < 0)
    {
        close(sock);
        return {false, "receive failed"};
    }

    buffer[n] = '\0';
    std::string resp(buffer);
    close(sock);

    if (resp.rfind("OK", 0) == 0)
        return {true, resp.substr(3)}; // skip "OK "
    else
        return {false, resp.substr(4)};
}

int rpc_start_listener(int port)
{
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0)
    {
        std::cerr << "Socket creation failed\n";
        exit(1);
    }

    int opt = 1;
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
    while (true)
    {
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(server_fd, (sockaddr *)&client_addr, &client_len);
        if (client_fd < 0)
        {
            std::cerr << "Accept failed\n";
            continue;
        }

        char buffer[4096];
        int n = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
        if (n < 0)
        {
            std::cerr << "Receive failed\n";
            close(client_fd);
            continue;
        }

        buffer[n] = '\0';
        std::string msg(buffer);

        handler(client_fd, msg);
    }
}