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
    u64 node;  // node responsible for the interval
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
    void stabilize();
    void fix_fingers();
    void notify(u64 nid, std::string nip, int nport);
    std::tuple<u64,std::string,int> find_successor(u64 id);
};