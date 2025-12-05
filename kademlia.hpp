#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <memory>
#include <deque>
#include <optional>
#include <set>

using u64 = uint64_t;

// 160-bit node ID for Kademlia (20 bytes)
constexpr int ID_BITS = 160;
constexpr int ID_BYTES = ID_BITS / 8;

using NodeID = std::array<uint8_t, ID_BYTES>;

// XOR distance between two node IDs
NodeID xor_distance(const NodeID &a, const NodeID &b);

// Compare two node IDs (for ordering)
int compare_node_id(const NodeID &a, const NodeID &b);

// Convert string to NodeID (hash-based)
NodeID hash_to_node_id(const std::string &s);

// Convert NodeID to hex string for display
std::string node_id_to_hex(const NodeID &id);

// Check if distance1 < distance2 (closer to target)
bool is_closer(const NodeID &dist1, const NodeID &dist2);

// Contact info for a node
struct Contact
{
    NodeID id;
    std::string ip;
    int port;

    Contact() : id{}, ip(""), port(0) {}
    Contact(const NodeID &_id, const std::string &_ip, int _port)
        : id(_id), ip(_ip), port(_port) {}

    bool operator==(const Contact &other) const
    {
        return id == other.id;
    }
};

// K-bucket: stores up to k contacts
struct KBucket
{
    std::deque<Contact> contacts; // front = least recently seen, back = most recently seen
    int k;                        // bucket size

    KBucket(int bucket_size = 20) : k(bucket_size) {}

    // Try to add a contact; returns true if added or updated
    bool add_contact(const Contact &contact);

    // Remove a contact
    void remove_contact(const NodeID &id);

    // Move contact to back (mark as recently seen)
    void update_contact(const NodeID &id);

    // Get all contacts
    std::vector<Contact> get_contacts() const;

    // Check if bucket is full
    bool is_full() const { return contacts.size() >= static_cast<size_t>(k); }

    // Check if contact exists
    bool has_contact(const NodeID &id) const;
};

// Routing table with k-buckets
class RoutingTable
{
public:
    NodeID self_id;
    int k;                        // bucket size
    std::vector<KBucket> buckets; // one bucket per bit (160 buckets)

    RoutingTable(const NodeID &id, int bucket_size = 20);

    // Add or update a contact
    void add_contact(const Contact &contact);

    // Remove a contact
    void remove_contact(const NodeID &id);

    // Get k closest contacts to target ID
    std::vector<Contact> find_closest(const NodeID &target, int count) const;

    // Get all contacts
    std::vector<Contact> get_all_contacts() const;

    // Get bucket index for a given ID
    int get_bucket_index(const NodeID &id) const;

private:
    // Calculate XOR distance and determine which bucket an ID belongs to
    int bucket_for_id(const NodeID &id) const;
};

// RPC message types
enum class RPCType
{
    PING,
    STORE,
    FIND_NODE,
    FIND_VALUE
};

// RPC request/response structures
struct RPCRequest
{
    RPCType type;
    NodeID sender_id;
    std::string sender_ip;
    int sender_port;

    // For FIND_NODE / FIND_VALUE
    NodeID target_id;

    // For STORE / FIND_VALUE
    std::string key;
    std::string value;
};

struct RPCResponse
{
    bool ok;
    RPCType request_type;

    // For PING
    bool is_alive;

    // For FIND_NODE
    std::vector<Contact> contacts;

    // For FIND_VALUE
    bool value_found;
    std::string value;
};

// Kademlia Node
class KademliaNode
{
public:
    NodeID id;
    std::string ip;
    int port;

    RoutingTable routing_table;
    std::unordered_map<std::string, std::string> data; // key-value store

    int k;     // bucket size (typical: 20)
    int alpha; // concurrency parameter (typical: 3)

    KademliaNode(const std::string &name, const std::string &ip_address, int port_number,
                 int bucket_size = 20, int concurrency = 3);

    // RPC handlers
    RPCResponse handle_ping(const RPCRequest &req);
    RPCResponse handle_store(const RPCRequest &req);
    RPCResponse handle_find_node(const RPCRequest &req);
    RPCResponse handle_find_value(const RPCRequest &req);

    // Core operations
    void store(const std::string &key, const std::string &value);
    std::optional<std::string> find_value(const std::string &key);
    std::vector<Contact> find_node(const NodeID &target);

    // Lookup algorithm (iterative, alpha-parallel)
    std::vector<Contact> iterative_find_node(const NodeID &target, int &hops);
    std::optional<std::string> iterative_find_value(const std::string &key, int &hops);

    // Node join
    void join(const Contact &bootstrap_node);

    // Bucket maintenance
    void refresh_bucket(int bucket_index);

private:
    // Helper for parallel queries
    struct LookupState
    {
        NodeID target;
        std::set<NodeID> queried;       // nodes already queried
        std::vector<Contact> shortlist; // k closest known nodes
        bool converged;
    };

    void update_shortlist(LookupState &state, const std::vector<Contact> &new_contacts);
    std::vector<Contact> get_next_alpha_contacts(LookupState &state);
};

struct KademliaNetwork
{
    int k;                                               // bucket size (default: 20)
    int alpha;                                           // concurrency (default: 3)
    int replication;                                     // replication factor
    std::map<NodeID, std::shared_ptr<KademliaNode>> nodes;         // all nodes in the network
    std::unordered_map<std::string, std::vector<NodeID>> key_locations; // track where keys are stored

    KademliaNetwork(int bucket_size = 20, int concurrency = 3, int repl = 3)
        : k(bucket_size), alpha(concurrency), replication(repl) {}

    // Add a new node to the network
    NodeID add_node(const std::string &name, const std::string &ip = "127.0.0.1", int port = 0);

    // Remove a node from the network
    void remove_node(const NodeID &id);

    // Simulate node join with routing table population
    void perform_join(const NodeID &new_node_id, const Contact &bootstrap);

    // Global helper: find k closest nodes to a target (used for simulation)
    std::vector<Contact> find_closest_nodes_global(const NodeID &target, int count) const;

    // Store a key-value pair in the network (on k closest nodes)
    void put(const std::string &key, const std::string &value);
    // Get a value from the network
    std::pair<std::string, NodeID> get(const std::string &key) const;

    // Simulate a lookup from a starting node (returns hops and target node)
    std::pair<int, NodeID> route_lookup(const NodeID &start_id, const NodeID &target_id) const;

    // Get value from a specific starting node (simulate lookup)
    std::pair<std::string, int> get_from(const NodeID &start_id, const std::string &key) const;

    // Dump network state
    void dump_state(std::ostream &os) const;
};

// Global configuration
constexpr int DEFAULT_K = 20;
constexpr int DEFAULT_ALPHA = 3;
constexpr int REPLICATION_PARAM = 3; // number of nodes to store each key on
