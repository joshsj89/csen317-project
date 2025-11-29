#include <arpa/inet.h>
#include <iostream>
#include <sstream>
#include <sys/socket.h>
#include <thread>
#include <unistd.h> // for close()
#include "chord.hpp"
#include "rpc.hpp"

ChordNode *SELF = nullptr;
bool RUNNING = true;
bool isLeaving = false;

void handle_rpc(int client_fd, const std::string &msg)
{
    std::stringstream ss(msg);
    std::string cmd;
    ss >> cmd; // read command (first word)

    if (cmd == "PING")
    {
        std::string reply = "OK alive\n";

        send(client_fd, reply.c_str(), reply.size(), 0);
    }
    else if (cmd == "GET_PREDECESSOR")
    {
        std::string reply =
            "OK " + std::to_string(SELF->predecessor_id) + " " +
            SELF->predecessor_ip + " " +
            std::to_string(SELF->predecessor_port) + "\n";

        send(client_fd, reply.c_str(), reply.size(), 0);
    }
    else if (cmd == "GET_INFO")
    {
        std::string reply =
            "OK " + std::to_string(SELF->id) + " " +
            SELF->ip + " " +
            std::to_string(SELF->port) + "\n";

        send(client_fd, reply.c_str(), reply.size(), 0);
    }
    else if (cmd == "NOTIFY")
    {
        u64 nid;
        std::string nip;
        int nport;
        ss >> nid >> nip >> nport;

        SELF->notify(nid, nip, nport);

        std::string reply = "OK notified\n";
        send(client_fd, reply.c_str(), reply.size(), 0);
    }
    else if (cmd == "FIND_SUCCESSOR")
    {
        u64 key;
        ss >> key;

        auto [succ_id, succ_ip, succ_port] = SELF->find_successor(key);

        std::string reply =
            "OK " + std::to_string(succ_id) + " " + succ_ip + " " + std::to_string(succ_port) + "\n";

        send(client_fd, reply.c_str(), reply.size(), 0);
    }
    else if (cmd == "SET_PREDECESSOR")
    {
        u64 pid;
        std::string pip;
        int pport;
        ss >> pid >> pip >> pport;

        SELF->predecessor_id = pid;
        SELF->predecessor_ip = pip;
        SELF->predecessor_port = pport;

        std::string reply = "OK predecessor_set\n";
        send(client_fd, reply.c_str(), reply.size(), 0);
    }
    else if (cmd == "SET_SUCCESSOR")
    {
        u64 sid;
        std::string sip;
        int sport;
        ss >> sid >> sip >> sport;

        SELF->successor_id = sid;
        SELF->successor_ip = sip;
        SELF->successor_port = sport;

        std::string reply = "OK successor_set\n";
        send(client_fd, reply.c_str(), reply.size(), 0);
    }
    else
    {
        std::string reply = "ERR unknown_command\n";
        send(client_fd, reply.c_str(), reply.size(), 0);
    }

    close(client_fd);
}

int main(int argc, char **argv)
{
    if (argc < 4)
    {
        std::cout << "Usage: " << argv[0] << " <ip> <port> <m_bits>" << std::endl;
        return 1;
    }

    std::string ip = argv[1];
    int port = std::stoi(argv[2]);
    int m_bits = std::stoi(argv[3]);

    ChordNode node(ip, port, m_bits);
    SELF = &node;

    // Start RPC listener thread
    int server_fd = rpc_start_listener(port);

    std::thread listener([&]() {
        rpc_accept_loop(server_fd, handle_rpc);
    });

    // Stabilization thread
    std::thread stabilizer([&]() {
        while (RUNNING)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            node.stabilize();
            node.fix_fingers();
        }
    });

    // CLI loop
    while (true)
    {
        std::cout << "ChordNode> ";
        // std::string line;
        // if (!std::getline(std::cin, line))
        //     break;
        // std::stringstream ss(line);
        std::string cmd;
        std::cin >> cmd;

        if (cmd == "join")
        {
            std::string bootstrap_ip;
            int bootstrap_port;
            std::cin >> bootstrap_ip >> bootstrap_port;
            node.join(bootstrap_ip, bootstrap_port);
            std::cout << "Joined the network via " << bootstrap_ip << ":" << bootstrap_port << std::endl;
        }
        else if (cmd == "leave")
        {
            node.leave();
            break;
        }
        else if (cmd == "info")
        {
            std::cout << "Node ID: " << node.id << std::endl;
            std::cout << "IP: " << node.ip << std::endl;
            std::cout << "Port: " << node.port << std::endl;
            std::cout << "Predecessor: " << node.predecessor_id << " (" << node.predecessor_ip << ":" << node.predecessor_port << ")" << std::endl;
            std::cout << "Successor: " << node.successor_id << " (" << node.successor_ip << ":" << node.successor_port << ")" << std::endl;
        }
        else if (cmd == "exit")
        {
            break;
        }
        else
        {
            std::cout << "Unknown command." << std::endl;
        }
    }

    RUNNING = false; // stop stabilization thread

    shutdown(server_fd, SHUT_RDWR); // unblock accept()
    close(server_fd);

    // Wait for threads to finish
    listener.join();
    stabilizer.join();


    return 0;
}