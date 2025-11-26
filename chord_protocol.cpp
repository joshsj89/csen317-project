#include <cstdint>
#include <iostream>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <map>
#include <functional>
#include <algorithm>
using namespace std;

// Simple in-process Chord DHT simulator
// - Consistent hashing with m-bit identifier space
// - Finger tables computed globally (no background stabilization)
// - put/get map keys to the node that is successor of the key hash
// - Basic stabilization and rebalancing after node joins/leaves

using u64 = uint64_t;

int replication_factor = 3;

struct Node
{
    u64 id; // [0, 2^m)
    string name;
    u64 successor;
    u64 predecessor;
    vector<u64> finger;                 // size m
    unordered_map<string, string> data; // stored key-value pairs

    Node() = default;
    Node(u64 _id, const string &_name, int m) : id(_id), name(_name), successor(_id), predecessor(_id), finger(m, _id) {}
};

struct ChordNetwork // manages nodes and routing
{
    int m; // identifier bits
    u64 ring_size;
    map<u64, Node> nodes; // sorted by id

    ChordNetwork(int mbits = 8) : m(mbits), ring_size(1ULL << mbits) {}

    u64 hash_str(const string &s) const
    {
        // portable hash -> reduce to ring_size
        size_t h = std::hash<string>{}(s);
        return (u64)(h % ring_size);
    }

    bool has_node(u64 id) const { return nodes.find(id) != nodes.end(); }

    u64 add_node(const string &name) // Adds node to the ring and recomputes finger tables
    {
        u64 id = hash_str(name);
        // if collision, perturb name until unique
        string tname = name;
        int attempt = 1;
        while (has_node(id))
        {
            tname = name + "#" + to_string(attempt++);
            id = hash_str(tname);
        }

        nodes.emplace(id, Node(id, tname, m));

        recompute_finger_tables(); // update all finger tables
        transfer_keys_to_new_node(id); // move keys from successor to new node
        recalculate_replication_after_join(id); // ensure replication is correct
        // stabilize_all();

        return id;
    }

    void remove_node(u64 id) // Removes node from the ring and recomputes finger tables
    {
        if (!has_node(id))
            throw runtime_error("node not present");

        Node &leaving_node = nodes.at(id);
        u64 succ_id = leaving_node.successor;

        if (has_node(succ_id))
        {
            Node &succ_node = nodes.at(succ_id);

            // transfer all data to successor
            for (auto &kv : leaving_node.data)
                succ_node.data[kv.first] = kv.second;
        }
        
        nodes.erase(id);

        recompute_finger_tables();
        recalculate_replication_after_leave(id); // ensure replication is correct
        // stabilize_all();
    }

    // find the successor node id for given identifier (the first node with id >= key, wrapping)
    u64 find_successor_id(u64 key) const
    {
        if (nodes.empty())
            throw runtime_error("no nodes in network");
        auto it = nodes.lower_bound(key);
        if (it == nodes.end())
            return nodes.begin()->first;
        return it->first;
    }

    vector<u64> get_successor_list(u64 node_id, int count) const
    {
        vector<u64> succs;

        if (nodes.empty() || !has_node(node_id) || count <= 0)
            return succs;

        auto it = nodes.find(node_id);
        for (int i = 0; i < count; i++)
        {
            if (it == nodes.end())
                it = nodes.begin();

            succs.push_back(it->first);
            ++it;
        }

        return succs;
    }

    void recompute_finger_tables() // Recomputes successor, predecessor, and finger tables for all nodes
    {
        if (nodes.empty())
            return;
        // compute successor and predecessor for each node and fill finger tables
        vector<u64> ids;
        ids.reserve(nodes.size());
        for (auto &p : nodes)
            ids.push_back(p.first);

        auto next_idx = [&](size_t i)
        { return (i + 1) % ids.size(); };
        auto prev_idx = [&](size_t i)
        { return (i + ids.size() - 1) % ids.size(); };

        for (size_t i = 0; i < ids.size(); ++i)
        {
            u64 id = ids[i];
            Node &node = nodes.at(id);
            node.successor = ids[next_idx(i)];
            node.predecessor = ids[prev_idx(i)];
            // rebuild finger table
            for (int k = 0; k < m; ++k)
            {
                u64 start = (id + (1ULL << k)) % ring_size;
                node.finger[k] = find_successor_id(start);
            }
        }
    }

    // route a lookup from start node to the owner of key; returns hops and final node id
    pair<int, u64> route_lookup(u64 start_id, u64 key) const
    {
        if (nodes.empty())
            throw runtime_error("no nodes");
        if (!has_node(start_id))
            throw runtime_error("start node not present");

        u64 curr = start_id;
        int hops = 0;

        // simple loop with limit to avoid infinite loops in buggy state
        int max_hop = max(50, (int)nodes.size() * 5);
        while (hops < max_hop)
        {
            ++hops;
            const Node &n = nodes.at(curr);
            u64 succ = n.successor;
            // if key is in (curr, succ] then succ is the owner
            if (in_interval_inclusive(curr, succ, key))
            {
                return {hops, succ};
            }
            // otherwise pick the closest preceding finger
            u64 next = curr;
            for (int k = m - 1; k >= 0; --k)
            {
                u64 f = n.finger[k];
                if (in_interval_open(curr, key, f)) // finger is in (curr, key)
                {
                    next = f;
                    break;
                }
            }
            if (next == curr)
            {
                // fallback to successor to make progress
                next = succ;
            }
            if (next == curr)
                break;
            curr = next;
        }

        // if we didn't find via fingers, use global find_successor
        return {hops, find_successor_id(key)};
    }

    // helper: tests whether x in (a, b]
    bool in_interval_inclusive(u64 a, u64 b, u64 x) const
    {
        if (a < b)
            return (x > a && x <= b);
        if (a > b)
            return (x > a || x <= b);
        // a == b: interval is entire ring (single node)
        return true;
    }
    // helper: tests whether x in (a, b)
    bool in_interval_open(u64 a, u64 b, u64 x) const
    {
        if (a < b)
            return (x > a && x < b);
        if (a > b)
            return (x > a || x < b);
        return x != a; // full ring except a
    }

    // store key->value into responsible node
    void put(const string &key, const string &value)
    {
        u64 k = hash_str(key);
        u64 owner = find_successor_id(k);

        // store in owner and its successors for replication
        auto succs_ids = get_successor_list(owner, replication_factor);
        for (u64 node_id : succs_ids)
            nodes.at(node_id).data[key] = value;
    }

    // get value and the node that serves it; empty string if not found
    pair<string, u64> get(const string &key) const
    {
        u64 k = hash_str(key);
        u64 owner = find_successor_id(k);

        // check owner and its successors for replication
        auto succs_ids = get_successor_list(owner, replication_factor);
        for (u64 node_id : succs_ids)
        {
            const Node &n = nodes.at(node_id);
            auto it = n.data.find(key);
            if (it != n.data.end())
                return {it->second, node_id};
        }
        
        return {string(), owner}; // not found
    }

    // get file from a specified start node, returning content and hops taken
    pair<string, int> get_from(u64 start_id, const string &key) const
    {
        // local lookup first
        const Node &start = nodes.at(start_id);
        if (start.data.count(key))
            return {start.data.at(key), 0};

        // routing lookup
        u64 k = hash_str(key);
        auto [hops, owner_id] = route_lookup(start_id, k);

        // check data at owner
        const Node &owner = nodes.at(owner_id);
        if (owner.data.count(key))
            return {owner.data.at(key), hops};
            
        // not found
        return {string(), hops};
    }

    /***************** Stabilization & Rebalancing *************************/

    // Stabilize a given node
    void stabilize_node(u64 node_id)
    {
        if (!has_node(node_id))
            return;

        Node &n = nodes.at(node_id);
        u64 succ_id = n.successor;

        if (!has_node(succ_id))
            return;

        Node &succ = nodes.at(succ_id);

        // Check if successor's predecessor is a better successor
        u64 succ_pred_id = succ.predecessor;
        if (has_node(succ_pred_id) && in_interval_open(n.id, succ_id, succ_pred_id))
            n.successor = succ_pred_id; // better successor found

        // Notify successor that n might be its predecessor
        if (has_node(n.successor))
        {
            Node &new_succ = nodes.at(n.successor);
            if (new_succ.predecessor != new_succ.id || in_interval_open(new_succ.predecessor, new_succ.id, n.id))
                new_succ.predecessor = n.id;
        }
    }

    // Refresh finger entries for a given node
    void refresh_fingers_node(u64 node_id)
    {
        if (!has_node(node_id))
            return;

        Node &n = nodes.at(node_id);
        for (int k = 0; k < m; ++k)
        {
            u64 start = (n.id + (1ULL << k)) % ring_size;
            n.finger[k] = find_successor_id(start);
        }
    }

    // Transfer keys from successor to new node after join
    // Time Complexity: O(F * R) where F = files on successor, R = replication factor; not dependent on N
    void transfer_keys_to_new_node(u64 new_node_id)
    {
        if (!has_node(new_node_id))
            return;

        Node &new_node = nodes.at(new_node_id);
        u64 succ_id = new_node.successor;

        if (!has_node(succ_id))
            return;

        Node &succ = nodes.at(succ_id);

        vector<string> to_move; // keys to move to new node
        to_move.reserve(succ.data.size());

        // Identify keys that should be moved to the new node
        for (auto &kv : succ.data)
        {
            const string &key = kv.first;
            u64 k = hash_str(key);

            // if key should be moved to new node
            if (in_interval_inclusive(new_node.predecessor, new_node.id, k))
                to_move.push_back(key);
        }

        // Move keys to new node and replicate locally
        for (const string &key : to_move)
        {
            string value = succ.data[key];
            
            new_node.data[key] = value;
            succ.data.erase(key);

            auto replica_ids = get_successor_list(new_node_id, replication_factor);
            for (u64 rid : replica_ids)
                if (rid != new_node_id)
                    nodes.at(rid).data[key] = value;
        }
    }

    // Rebalance files among nodes after stabilization
    // Time Complexity: O(N * F * log N) where N = number of nodes, F = average files per node
    void rebalance_files()
    {
        if (nodes.empty())
            return;

        map<u64, vector<pair<string, string>>> moves;

        for (auto &p : nodes)
        {
            u64 node_id = p.first;
            Node & node = p.second;

            vector<string> to_remove; // keys that don't belong to this node anymore
            to_remove.reserve(node.data.size());

            for (auto &kv : node.data)
            {
                const string &key = kv.first;
                u64 k = hash_str(key);
                u64 owner_id = find_successor_id(k);
                if (owner_id != node_id)
                {
                    moves[owner_id].emplace_back(key, kv.second);
                    to_remove.push_back(key);
                }
            }

            // Remove moved keys from current node
            for (const string &key : to_remove)
                node.data.erase(key);
        }

        // Apply the moves
        for (auto &e : moves)
        {
            u64 owner_id = e.first;
            
            if (!has_node(owner_id)) // owner node might have been removed
                continue;

            Node &owner = nodes.at(owner_id);
            for (auto &kv : e.second)
                owner.data[kv.first] = kv.second;
        }
    }

    // Stabilize all nodes and rebalance files
    void stabilize_all()
    {
        if (nodes.empty())
            return;

        vector<u64> node_ids;
        node_ids.reserve(nodes.size());
        for (auto &p : nodes)
            node_ids.push_back(p.first);

        // Stabilize each node (one pass)
        for (u64 id : node_ids)
            stabilize_node(id);

        // Refresh finger tables after stabilization
        for (u64 id : node_ids)
            refresh_fingers_node(id);

        // Rebalance files to move data to correct owners
        // Note: Disabled because incremental transfer is done on join
        // rebalance_files();
    }

    /***********************************************************************/

    // Recalculate replication of files across the network (global recalculation)
    // Time Complexity: O(N^2 * F) where N = number of nodes, F = average files per node
    /*
    void recalculate_replication()
    {
        for (auto &p : nodes)
        {
            u64 owner_id = p.first;
            auto correct_succs_ids = get_successor_list(owner_id, replication_factor);

            // whitelist set for quick lookup
            unordered_set<u64> allowed(correct_succs_ids.begin(), correct_succs_ids.end());

            for (auto &q : nodes)
            {
                u64 node_id = q.first;
                Node &node = q.second;

                vector<string> to_remove;

                for (auto &kv : node.data)
                {
                    u64 k = hash_str(kv.first);
                    u64 real_owner_id = find_successor_id(k);

                    if (real_owner_id == owner_id && !allowed.count(node_id))
                        to_remove.push_back(kv.first);
                }

                for (auto &key : to_remove)
                    node.data.erase(key);
            }
        }
    }
    */

    void recalculate_replication_after_join(u64 new_node_id)
    {
        if (!has_node(new_node_id))
            return;

        Node &new_node = nodes.at(new_node_id);

        vector<string> owned_keys;
        for (auto &kv : new_node.data)
        {
            u64 k = hash_str(kv.first);

            // only keys for which this new node is the owner
            if (find_successor_id(k) == new_node_id)
                owned_keys.push_back(kv.first);
        }

        // new replica targets
        auto succs_ids = get_successor_list(new_node_id, replication_factor);

        // replicate owned keys to successors
        for (auto &key : owned_keys)
        {
            string &value = new_node.data[key];

            // ensure replication on successors
            for (u64 node_id : succs_ids)
            {
                if (node_id == new_node_id) // skip self
                    continue;
                nodes.at(node_id).data[key] = value;
            }
        }

        // remove keys from nodes that are no longer valid replicas
        if (succs_ids.size() >= replication_factor)
        {
            u64 old_replica_id = succs_ids.back(); // last in the list
            Node &old_replica_node = nodes.at(old_replica_id);

            for (auto it = old_replica_node.data.begin(); it != old_replica_node.data.end(); )
            {
                u64 k = hash_str(it->first);
                if (find_successor_id(k) != old_replica_id)
                    it = old_replica_node.data.erase(it);
                else
                    ++it;
            }
        }
    }

    // Recalculate replication of files across the network after a node leaves
    // Time Complexity: O(T * log N) where T = total files in network, N = number of nodes
    void recalculate_replication_after_leave(u64 departed_node_id)
    {
        if (nodes.empty())
            return;

        // Examine each node, but only remove invalid replicas
        for (auto &p : nodes)
        {
            u64 node_id = p.first;
            Node &node = p.second;

            vector<string> to_remove;

            for (auto &kv : node.data)
            {
                u64 k = hash_str(kv.first);
                u64 owner_id = find_successor_id(k);

                // Compute new successor/replica list for the owner
                auto succs_ids = get_successor_list(owner_id, replication_factor);
                unordered_set<u64> allowed(succs_ids.begin(), succs_ids.end());

                // if this node is no longer a valid replica for the key
                if (!allowed.count(node_id))
                    to_remove.push_back(kv.first);
            }

            // Remove invalid replicas
            for (auto &key : to_remove)
                node.data.erase(key);
        }
    }

    void dump_state(ostream &os) const // Output the current state of the network
    {
        os << "Chord network (m=" << m << ", ring=" << ring_size << ")\n";
        os << "Nodes:\n";
        for (auto &p : nodes)
        {
            const Node &n = p.second;
            os << "  " << n.name << " (id=" << n.id << ") succ=" << n.successor << " pred=" << n.predecessor << "\n";
            os << "    fingers: ";
            for (int k = 0; k < m; ++k)
                os << n.finger[k] << (k + 1 < m ? "," : "\n");
            if (!n.data.empty())
            {
                os << "    data:\n";
                for (auto &kv : n.data)
                    os << "      " << kv.first << " -> " << kv.second << "\n";
            }
        }

        os.flush();
    }
};

// Demo usage
int main()
{
    ios::sync_with_stdio(false);
    cin.tie(nullptr);

    // create network with m=8 (ids 0..255)
    ChordNetwork net(8);

    // add some nodes
    vector<string> names = {"node-A", "node-B", "node-C", "node-D"};
    vector<u64> ids;
    for (auto &n : names)
    {
        u64 id = net.add_node(n);
        ids.push_back(id);
    }

    cout << "Initial network:" << endl;
    net.dump_state(cout);

    // insert some files (keys)
    vector<pair<string, string>> files = {
        {"file1.txt", "contents-1"},
        {"image.png", "png-bytes"},
        {"video.mp4", "video-bytes"},
        {"notes.md", "some notes"}};
    for (auto &f : files)
        net.put(f.first, f.second);

    cout << "\nAfter inserting files:" << endl;
    net.dump_state(cout);

    // demonstrate lookups and routing
    cout << "\nLookup simulation:" << endl;
    for (auto &f : files)
    {
        u64 keyid = net.hash_str(f.first);
        // start lookup from each node and show hops
        for (auto sid : ids)
        {
            auto [contents, hops] = net.get_from(sid, f.first);
            cout << "  start=" << sid << " lookup(" << f.first << ", id=" << keyid << ") -> hops=" << hops << " present=" << !contents.empty() << endl;
        }
        auto [val, owner] = net.get(f.first);
        cout << "    final owner for " << f.first << " (id=" << keyid << ") = " << owner << " value_present=" << (!val.empty()) << endl;
    }

    // add a new node and show remapping
    cout << "\nAdding a new node 'node-E' and recomputing tables:" << endl;
    u64 newId = net.add_node("node-E");
    net.dump_state(cout);

    // show file ownership after the join
    cout << "\nFile ownership after join:" << endl;
    for (auto &f : files)
    {
        auto [val, owner] = net.get(f.first);
        cout << "  " << f.first << " -> owner=" << owner << " present=" << (!val.empty()) << endl;
    }

    return 0;
}
