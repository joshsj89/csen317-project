#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

using u64 = uint64_t;

struct FingerEntry
{
    u64 start; // start of the interval
    u64 node_id;
    std::string node_ip;
    int node_port;
};

class ChordNode
{
public:
    u64 id;
    std::string ip;
    int port;

    u64 successor_id;;
    std::string successor_ip;
    int successor_port;

    u64 predecessor_id;
    std::string predecessor_ip;
    int predecessor_port;

    int m; // number of bits in identifier space
    std::vector<FingerEntry> finger_table;
    std::unordered_map<std::string, std::string> data; // key-value store / files

    ChordNode(std::string ip_address, int port_number, int m_bits);

    void join(std::string bootstrap_ip, int bootstrap_port);
    void leave();
    void notify(u64 nid, std::string nip, int nport);

    void stabilize();
    void fix_fingers();
    std::tuple<u64,std::string,int> find_successor(u64 id);
    void check_predecessor();
    std::tuple<u64,std::string,int> closest_preceding_finger(u64 id);
    void recompute_finger_table();
    bool is_alive(std::string check_ip, int check_port);
};

extern bool isLeaving; // global flag to indicate if node is leaving