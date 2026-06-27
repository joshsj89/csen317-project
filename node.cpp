#include <arpa/inet.h>
#include <format> // C++20 for std::format
#include <iostream>
#include <readline/history.h>
#include <readline/readline.h>
#include <sstream>
#include <sys/socket.h>
#include <thread>
#include <unistd.h> // for close()
#include "chord.hpp"
#include "rpc.hpp"
#include "utils.hpp"

ChordNode *SELF = nullptr;
std::atomic<bool> RUNNING = true;
std::atomic<bool> joining = false;
std::atomic<bool> isLeaving = false;
std::atomic<bool> has_transferred_keys = false;
const int replication_factor = 3;

const unsigned REPAIR_INTERVAL = 5; // 5 seconds

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
        u64 pred_id_local;
        std::string pred_ip_local;
        int pred_port_local;

        {
            std::lock_guard<std::mutex> lock(SELF->state_mutex);

            pred_id_local = SELF->predecessor_id;
            pred_ip_local = SELF->predecessor_ip;
            pred_port_local = SELF->predecessor_port;
        }

        std::string reply =
            "OK " + std::to_string(pred_id_local) + " " +
            pred_ip_local + " " +
            std::to_string(pred_port_local) + "\n";

        send(client_fd, reply.c_str(), reply.size(), 0);
    }
    else if (cmd == "GET_SUCCESSOR") // get immediate successor
    {
        u64 succ_id_local;
        std::string succ_ip_local;
        int succ_port_local;

        {
            std::lock_guard<std::mutex> lock(SELF->state_mutex);

            succ_id_local = SELF->successor_id;
            succ_ip_local = SELF->successor_ip;
            succ_port_local = SELF->successor_port;
        }

        std::string reply =
            "OK " + std::to_string(succ_id_local) + " " +
            succ_ip_local + " " +
            std::to_string(succ_port_local) + "\n";

        send(client_fd, reply.c_str(), reply.size(), 0);
    }
    else if (cmd == "GET_SUCCESSOR_LIST")
    {
        std::vector<Finger> local_successors;
        {
            std::lock_guard<std::mutex> lock(SELF->successor_list_mutex);
            local_successors = SELF->successor_list; // copy to avoid holding lock during checks
        }

        std::string reply = "OK";
        for (const auto &[succ_id, succ_ip, succ_port] : local_successors)
        {
            reply += " " + std::to_string(succ_id) + " " + succ_ip + " " + std::to_string(succ_port);
        }
        reply += "\n";

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
    else if (cmd == "FIND_SUCCESSOR") // find successor for a given id
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

        std::lock_guard<std::mutex> lock(SELF->state_mutex);

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

        std::lock_guard<std::mutex> lock(SELF->state_mutex);

        SELF->successor_id = sid;
        SELF->successor_ip = sip;
        SELF->successor_port = sport;

        std::string reply = "OK successor_set\n";
        send(client_fd, reply.c_str(), reply.size(), 0);
    }
    else if (cmd == "PUT") // Store and replicate (with owner check)
    {
        std::string key, value;
        ss >> key;
        std::getline(ss, value); // read the rest of the line as value (allow spaces)
        if (!value.empty() && value[0] == ' ') 
            value.erase(0, 1); // remove leading space

        SELF->replicate_key(key, value);

        std::string reply = "OK stored\n";
        send(client_fd, reply.c_str(), reply.size(), 0);
    }
    else if (cmd == "PUT_PRIMARY") // Store primary replica directly
    {
        std::string key, value;
        ss >> key;
        std::getline(ss, value); // read the rest of the line as value (allow spaces)
        if (!value.empty() && value[0] == ' ') 
            value.erase(0, 1); // remove leading space

        // store primary replica locally
        {
            std::lock_guard<std::mutex> lock(SELF->data_mutex);

            SELF->data[key] = {value, true}; // mark as primary replica
        }

        std::string reply = "OK primary_stored\n";
        send(client_fd, reply.c_str(), reply.size(), 0);
    }
    else if (cmd == "PUT_REPLICA") // Store replica directly (no owner check)
    {
        std::string key, value;
        ss >> key;
        std::getline(ss, value); // read the rest of the line as value (allow spaces)
        if (!value.empty() && value[0] == ' ') 
            value.erase(0, 1); // remove leading space

        // store replica locally
        {
            std::lock_guard<std::mutex> lock(SELF->data_mutex);

            SELF->data[key] = {value, false}; // mark as replica (not primary)
        }

        std::string reply = "OK replica_stored\n";
        send(client_fd, reply.c_str(), reply.size(), 0);
    }
    else if (cmd == "GET")
    {
        std::string key;
        ss >> key;

        // check locally first
        if (SELF->data.count(key) && SELF->data[key].primary)
        {
            auto dataItem = SELF->data[key];
            std::string reply = "OK " + dataItem.value + "\n";
            send(client_fd, reply.c_str(), reply.size(), 0);
        }
        else
        {
            std::string reply = "ERR not_found\n";
            send(client_fd, reply.c_str(), reply.size(), 0);
        }
    }
    else if (cmd == "TRANSFER_KEYS")
    {
        u64 new_node_id;
        std::string new_node_ip;
        int new_node_port;
        ss >> new_node_id >> new_node_ip >> new_node_port;

        SELF->transfer_keys(new_node_id, new_node_ip, new_node_port);

        std::string reply = "OK keys_transferred\n";
        send(client_fd, reply.c_str(), reply.size(), 0);
    }
    else if (cmd == "DOCKER_JOIN")
    {
        std::string bootstrap_ip;
        int bootstrap_port;
        ss >> bootstrap_ip >> bootstrap_port;

        SELF->join(bootstrap_ip, bootstrap_port);

        std::string reply = "OK joined\n";
        send(client_fd, reply.c_str(), reply.size(), 0);
    }
    else if (cmd == "DOCKER_LEAVE")
    {
        SELF->leave();

        std::string reply = "OK left\n";
        send(client_fd, reply.c_str(), reply.size(), 0);
    }
    else if (cmd == "DOCKER_INFO")
    {
        std::cout << "Node ID: " << SELF->id << std::endl;
        std::cout << "IP: " << SELF->ip << std::endl;
        std::cout << "Port: " << SELF->port << std::endl;
        std::cout << "Predecessor: " << SELF->predecessor_id << " (" << SELF->predecessor_ip << ":" << SELF->predecessor_port << ")" << std::endl;
        std::cout << "Successor: " << SELF->successor_id << " (" << SELF->successor_ip << ":" << SELF->successor_port << ")" << std::endl;

        std::string reply = "OK info_displayed\n";
        send(client_fd, reply.c_str(), reply.size(), 0);
    }
    else if (cmd == "DOCKER_PUT")
    {
        std::string key, value;
        ss >> key;
        std::getline(ss, value); // read the rest of the line as value (allow spaces)
        if (!value.empty() && value[0] == ' ') 
            value.erase(0, 1); // remove leading space

        if (value.empty())
        {
            std::cout << "Value cannot be empty." << std::endl;

            std::string reply = "ERR empty_value\n";
            send(client_fd, reply.c_str(), reply.size(), 0);

            close(client_fd);
            return;
        }

        SELF->put(key, value);

        std::cout << "Stored key '" << key << "'." << std::endl;

        std::string reply = "OK stored\n";
        send(client_fd, reply.c_str(), reply.size(), 0);
    }
    else if (cmd == "DOCKER_GET")
    {        
        std::string key;
        std::string value;
        ss >> key;

        // check locally first
        if (SELF->data.count(key) && SELF->data[key].primary)
            value = SELF->data[key].value;
        else
            value = SELF->get(key);

        if (!value.empty())
        {
            std::cout << std::format("{:<10} : {}\n", key, value);

            std::string reply = "OK " + value + "\n";
            send(client_fd, reply.c_str(), reply.size(), 0);
        }
        else
        {
            std::cout << "Key '" << key << "' not found." << std::endl;

            std::string reply = "ERR not_found\n";
            send(client_fd, reply.c_str(), reply.size(), 0);
        }

    }
    else if (cmd == "DOCKER_FILES")
    {
        std::cout << "Stored files/keys:" << std::endl;
        for (const auto &[key, dataItem] : SELF->data)
            std::cout << std::format("{:<10} : {}\n", key, dataItem);

        std::string reply = "OK files_listed\n";
        send(client_fd, reply.c_str(), reply.size(), 0);
    }
    else if (cmd == "GET_METRICS")
    {
        std::string reply =
            "OK " +
            std::to_string(SELF->metric_find_successor_calls.load()) + " " +
            std::to_string(SELF->metric_find_successor_hops.load()) + "\n";

        send(client_fd, reply.c_str(), reply.size(), 0);
    }
    else if (cmd == "IS_STABLE")
    {
        bool stable = SELF->is_stable();

        std::string reply = "OK " + std::string(stable ? "1" : "0") + "\n";
        send(client_fd, reply.c_str(), reply.size(), 0);
    }
    else if (cmd == "GET_FINGERS")
    {
        std::vector<FingerEntry> local_fingers;
        {
            std::lock_guard<std::mutex> lock(SELF->finger_table_mutex);
            local_fingers = SELF->finger_table; // copy to avoid holding lock during checks
        }

        std::string reply = "OK\n";
        for (const auto &f : local_fingers)
        {
            reply += std::to_string(f.start) + " " +
                     std::to_string(f.node_id) + " " +
                     f.node_ip + " " +
                     std::to_string(f.node_port) + "\n";
        }

        send(client_fd, reply.c_str(), reply.size(), 0);
    }
    else if (cmd == "LIST_KEYS")
    {
        std::vector<std::pair<std::string, DataItem>> local_data;
        {
            std::lock_guard<std::mutex> lock(SELF->data_mutex);
            for (const auto &[key, item] : SELF->data)
                local_data.push_back({key, item}); // copy to avoid holding lock during checks
        }

        std::string reply = "OK\n";
        for (const auto &[key, item] : local_data)
        {
            reply += key + " " + (item.primary ? "primary" : "replica") + "\n";
        }

        send(client_fd, reply.c_str(), reply.size(), 0);
    }
    else if (cmd == "GET_RING")
    {
        u64 succ_id_local;
        u64 pred_id_local;
        {
            std::lock_guard<std::mutex> lock(SELF->state_mutex);

            succ_id_local = SELF->successor_id;
            pred_id_local = SELF->predecessor_id;
        }

        std::string reply = "OK " + std::to_string(pred_id_local) + " " + std::to_string(SELF->id) + " " + std::to_string(succ_id_local) + "\n";
        send(client_fd, reply.c_str(), reply.size(), 0);
    }
    else if (cmd == "HASH")
    {
        std::string input;
        ss >> input;

        u64 hash_value = hash_str(input) % (1ULL << SELF->m);

        std::string reply = "OK " + std::to_string(hash_value) + "\n";
        send(client_fd, reply.c_str(), reply.size(), 0);
    }
    else if (cmd == "GET_NODE_ID")
    {
        std::string reply = "OK " + std::to_string(SELF->id) + "\n";
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

    unsigned repair_counter = 0;

    const char *bootstrap_env = std::getenv("BOOTSTRAP");
    if (bootstrap_env != nullptr)
    {
        std::string bootstrap_str(bootstrap_env);
        
        std::stringstream ss(bootstrap_str);

        std::string bootstrap_ip;
        int bootstrap_port;
        ss >> bootstrap_ip >> bootstrap_port;

        node.join(bootstrap_ip, bootstrap_port);
    }

    // Start RPC listener thread
    int server_fd = rpc_start_listener(port);

    std::thread listener([&]() {
        rpc_accept_loop(server_fd, handle_rpc);
    });

    // Stabilization thread
    std::thread stabilizer([&]() {
        while (RUNNING)
        {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            node.stabilize();
            node.fix_fingers();
            node.check_predecessor();
            node.check_successor();

            repair_counter++;
            if (repair_counter >= REPAIR_INTERVAL) // repair replicas every REPAIR_INTERVAL seconds
            {
                node.repair_primary_status();
                node.repair_replicas();
                repair_counter = 0;
            }
        }
    });

    const char *autorun_env = std::getenv("AUTORUN");
    if (autorun_env != nullptr)
    {
        // Skip CLI
        while (RUNNING)
        {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
    else
    {
        // CLI loop
        while (true)
        {
            char *input = readline("ChordNode> ");

            if (!input)
                input = strdup("leave"); // EOF (Ctrl+D)

            if (*input)
                add_history(input); // add non-empty commands to history

            std::string line(input);
            free(input);

            std::stringstream ss(line);
            std::string cmd;
            ss >> cmd;
    
            if (cmd == "join")
            {
                std::string bootstrap_ip;
                int bootstrap_port;
                ss >> bootstrap_ip >> bootstrap_port;
                node.join(bootstrap_ip, bootstrap_port);
                // std::cout << "Joined the network via " << bootstrap_ip << ":" << bootstrap_port << std::endl;
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
            else if (cmd == "fingers")
            {
                std::lock_guard<std::mutex> lock(node.finger_table_mutex);
                std::cout << "Finger Table:" << std::endl;
                for (size_t i = 0; i < node.finger_table.size(); ++i)
                {
                    const auto &entry = node.finger_table[i];
                    std::cout << "  [" << i << "] start: " << entry.start
                              << ", node_id: " << entry.node_id
                              << ", ip: " << entry.node_ip
                              << ", port: " << entry.node_port << std::endl;
                }
            }
            else if (cmd == "successors")
            {
                std::lock_guard<std::mutex> lock(node.successor_list_mutex);
                std::cout << "Successor List:" << std::endl;
                for (size_t i = 0; i < node.successor_list.size(); ++i)
                {
                    const auto &[succ_id, succ_ip, succ_port] = node.successor_list[i];
                    std::cout << "  [" << i << "] node_id: " << succ_id
                              << ", ip: " << succ_ip
                              << ", port: " << succ_port << std::endl;
                }
            }
            else if (cmd == "put")
            {
                std::string key, value;
                ss >> key;
                std::getline(ss, value); // read the rest of the line as value (allow spaces)
                if (!value.empty() && value[0] == ' ') 
                    value.erase(0, 1); // remove leading space
    
                if (value.empty())
                {
                    std::cout << "Value cannot be empty." << std::endl;
                    continue;
                }
    
                node.put(key, value);
                std::cout << "Stored key '" << key << "'." << std::endl;
            }
            else if (cmd == "get")
            {
                std::string key;
                std::string value;
                ss >> key;
                
                // check locally first
                if (node.data.count(key) && node.data[key].primary)
                    value = node.data[key].value;
                else
                    value = node.get(key);
    
                if (!value.empty())
                    std::cout << std::format("{:<10} : {}\n", key, value);
                else
                    std::cout << "Key '" << key << "' not found." << std::endl;
            }
            else if (cmd == "files")
            {
                std::cout << "Stored files/keys:" << std::endl;
                std::cout << "------------------" << std::endl;
                for (const auto &[key, dataItem] : node.data)
                    std::cout << std::format("{:<10} : {}\n", key, dataItem);
                std::cout << "------------------" << std::endl;
            }
            else if (cmd == "help")
            {
                std::cout << "Commands:\n";
                std::cout << "  join <bootstrap_ip> <bootstrap_port>    - Join the Chord network\n";
                std::cout << "  leave                                   - Leave the Chord network\n";
                std::cout << "  info                                    - Display node information\n";
                std::cout << "  fingers                                 - Display finger table\n";
                std::cout << "  successors                              - Display successor list\n";
                std::cout << "  put <key> <value>                       - Store a key-value pair\n";
                std::cout << "  get <key>                               - Retrieve value for a key\n";
                std::cout << "  files                                   - List stored keys\n";
                std::cout << "  exit                                    - Exit the program\n";
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
    }

    RUNNING = false; // stop stabilization thread

    shutdown(server_fd, SHUT_RDWR); // unblock accept()
    close(server_fd);

    // Wait for threads to finish
    listener.join();
    stabilizer.join();

    return 0;
}