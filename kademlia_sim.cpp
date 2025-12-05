#include <iostream>
#include <string>
#include <vector>
#include <unordered_map>
#include <sstream>
#include "kademlia.hpp"

// =========================================================
//  GLOBAL NETWORK (PERSISTENT WHILE PROCESS IS RUNNING)
// =========================================================

static KademliaNetwork net(20, 3, 3);   // k=20, alpha=3, replication=3
static std::unordered_map<std::string, NodeID> name_to_id;

// Convert NodeID to short hex (first 16 chars)
static std::string short_hex(const NodeID &id) {
    return node_id_to_hex(id).substr(0, 16);
}

// =========================================================
//  PRINT HELP
// =========================================================

void print_usage() {
    std::cout <<
    "Commands:\n"
    "  ADD_NODE <name>\n"
    "  PUT <key> <value>\n"
    "  GET <key>\n"
    "  LOOKUP <startNodeID> <key>\n"
    "  REMOVE_NODE <nodeID>\n"
    "  STATE\n"
    "\n"
    "NodeID format for LOOKUP and REMOVE_NODE: 40-hex-digit string\n";
}

// =========================================================
//  PARSE A HEX STRING INTO NodeID
// =========================================================

bool parse_hex_id(const std::string &hex, NodeID &out) {
    if (hex.size() != 40) return false; // 20 bytes = 40 hex chars
    for (int i = 0; i < 20; i++) {
        std::string byte_str = hex.substr(i*2, 2);
        uint8_t byte = static_cast<uint8_t>(std::stoul(byte_str, nullptr, 16));
        out[i] = byte;
    }
    return true;
}

// =========================================================
//  COMMAND HANDLERS
// =========================================================

void cmd_add_node(const std::string &name) {
    NodeID id = net.add_node(name);
    name_to_id[name] = id;
    std::cout << node_id_to_hex(id) << std::endl;
}

void cmd_put(const std::string &key, const std::string &value) {
    net.put(key, value);
    std::cout << "OK stored" << std::endl;
}

void cmd_get(const std::string &key) {
    auto [value, owner] = net.get(key);
    if (value.empty()) {
        std::cout << "NOT_FOUND" << std::endl;
    } else {
        std::cout << "OK " << value << " owner=" 
                  << short_hex(owner) << std::endl;
    }
}

void cmd_lookup(const std::string &start_hex, const std::string &key) {
    NodeID start_id{};
    if (!parse_hex_id(start_hex, start_id)) {
        std::cout << "ERR invalid_node_id" << std::endl;
        return;
    }
    auto [value, hops] = net.get_from(start_id, key);
    std::cout << hops << std::endl;
}

void cmd_remove(const std::string &hexid) {
    NodeID id{};
    if (!parse_hex_id(hexid, id)) {
        std::cout << "ERR invalid_node_id" << std::endl;
        return;
    }
    try {
        net.remove_node(id);
        std::cout << "OK removed" << std::endl;
    } catch (...) {
        std::cout << "ERR no_such_node" << std::endl;
    }
}

void cmd_state() {
    net.dump_state(std::cout);
}

// =========================================================
//  MAIN
// =========================================================

int main(int argc, char *argv[]) {
    if (argc < 2) {
        print_usage();
        return 1;
    }

    std::string cmd = argv[1];

    if (cmd == "ADD_NODE") {
        if (argc < 3) { print_usage(); return 1; }
        cmd_add_node(argv[2]);
    }
    else if (cmd == "PUT") {
        if (argc < 4) { print_usage(); return 1; }
        cmd_put(argv[2], argv[3]);
    }
    else if (cmd == "GET") {
        if (argc < 3) { print_usage(); return 1; }
        cmd_get(argv[2]);
    }
    else if (cmd == "LOOKUP") {
        if (argc < 4) { print_usage(); return 1; }
        cmd_lookup(argv[2], argv[3]);
    }
    else if (cmd == "REMOVE_NODE") {
        if (argc < 3) { print_usage(); return 1; }
        cmd_remove(argv[2]);
    }
    else if (cmd == "STATE") {
        cmd_state();
    }
    else {
        print_usage();
        return 1;
    }

    return 0;
}
