#pragma once
#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

using u64 = uint64_t;
using Finger = std::tuple<u64, std::string, int>; // node_id, ip, port

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

    u64 successor_id;
    std::string successor_ip;
    int successor_port;

    u64 predecessor_id;
    std::string predecessor_ip;
    int predecessor_port;

    int m; // number of bits in identifier space
    std::vector<FingerEntry> finger_table;
    std::vector<Finger> successor_list;
    std::unordered_map<std::string, std::string> data; // key-value store / files

    std::mutex successor_list_mutex;
    std::mutex finger_table_mutex;
    std::mutex state_mutex; // protects predecessor and successor
    std::mutex data_mutex; // protects data store

    // --- Metrics Counters ---
    std::atomic<uint64_t> metric_find_successor_calls{0};
    std::atomic<uint64_t> metric_find_successor_hops{0};
    std::atomic<uint64_t> metric_put_requests{0};
    std::atomic<uint64_t> metric_get_requests{0};

    ChordNode(std::string ip_address, int port_number, int m_bits);

    void join(std::string bootstrap_ip, int bootstrap_port);
    void leave();
    void put(const std::string &key, const std::string &value);
    std::string get(const std::string &key);
    void transfer_keys(u64 new_node_id, std::string new_node_ip, int new_node_port);
    void notify(u64 nid, std::string nip, int nport);

    void stabilize();
    void fix_fingers();
    Finger find_successor(u64 id);
    void check_predecessor();
    void check_successor();
    Finger closest_preceding_finger(u64 id);
    void recompute_finger_table();
    bool is_alive(std::string check_ip, int check_port);

private:
    const int max_retries = 3;
    bool pending_key_transfer = false;
    int stable_rounds = 0; // number of consecutive stable rounds
    static constexpr int STABLE_ROUNDS_REQUIRED = 3;
};

extern std::atomic<bool> joining; // global flag to indicate if node is joining
extern std::atomic<bool> isLeaving; // global flag to indicate if node is leaving
extern std::atomic<bool> has_transferred_keys; // global flag to indicate if keys have been transferred
extern const int replication_factor; // number of replicas for each key