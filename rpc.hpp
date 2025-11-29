#pragma once
#include <atomic>
#include <string>

struct RPCResponse
{
    bool ok;
    std::string body;
};

RPCResponse rpc_send(const std::string &ip, int port, const std::string &msg);
int rpc_start_listener(int port); // returns server socket
void rpc_accept_loop(int server_fd, void (*handler)(int, const std::string&));

extern std::atomic<bool> RUNNING; // global flag to control RPC listener loop