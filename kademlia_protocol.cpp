#include <cstdint>
#include <iostream>
#include <string>
#include <vector>
#include <unordered_map>
#include <map>
#include <algorithm>
#include <memory>
#include <set>
#include "kademlia.hpp"

using namespace std;

// Simple in-process Kademlia DHT simulator
// - 160-bit identifier space
// - XOR distance metric
// - k-bucket routing tables
// - Alpha-parallel iterative lookups
// - RPCs: PING, STORE, FIND_NODE, FIND_VALUE

struct KademliaNetwork
{
    int k;                                               // bucket size (default: 20)
    int alpha;                                           // concurrency (default: 3)
    int replication;                                     // replication factor
    map<NodeID, shared_ptr<KademliaNode>> nodes;         // all nodes in the network
    unordered_map<string, vector<NodeID>> key_locations; // track where keys are stored

    KademliaNetwork(int bucket_size = 20, int concurrency = 3, int repl = 3)
        : k(bucket_size), alpha(concurrency), replication(repl) {}

    // Add a new node to the network
    NodeID add_node(const string &name, const string &ip = "127.0.0.1", int port = 0)
    {
        auto node = make_shared<KademliaNode>(name, ip, port, k, alpha);
        NodeID id = node->id;

        // Check for ID collision (very unlikely with 160 bits)
        string node_name = name;
        int attempt = 1;
        while (nodes.find(id) != nodes.end())
        {
            node_name = name + "#" + to_string(attempt++);
            node = make_shared<KademliaNode>(node_name, ip, port + attempt, k, alpha);
            id = node->id;
        }

        nodes[id] = node;

        // If not the first node, join through a bootstrap node
        if (nodes.size() > 1)
        {
            // Pick a random existing node as bootstrap
            auto it = nodes.begin();
            if (it->first == id)
                ++it; // skip self
            if (it != nodes.end())
            {
                Contact bootstrap(it->first, it->second->ip, it->second->port);
                perform_join(id, bootstrap);
            }
        }

        return id;
    }

    // Remove a node from the network
    void remove_node(const NodeID &id)
    {
        auto it = nodes.find(id);
        if (it == nodes.end())
        {
            throw runtime_error("node not present");
        }

        // Transfer data to other nodes (replication)
        auto &node = it->second;
        for (const auto &kv : node->data)
        {
            // Remove this node from key locations
            auto &locs = key_locations[kv.first];
            locs.erase(remove(locs.begin(), locs.end(), id), locs.end());
        }

        // Remove from all routing tables
        for (auto &p : nodes)
        {
            if (p.first != id)
            {
                p.second->routing_table.remove_contact(id);
            }
        }

        nodes.erase(it);
    }

    // Simulate node join with routing table population
    void perform_join(const NodeID &new_node_id, const Contact &bootstrap)
    {
        auto it = nodes.find(new_node_id);
        if (it == nodes.end())
            return;

        auto &new_node = it->second;

        // Add bootstrap to routing table
        new_node->routing_table.add_contact(bootstrap);

        // Bootstrap node learns about new node
        auto bootstrap_it = nodes.find(bootstrap.id);
        if (bootstrap_it != nodes.end())
        {
            Contact new_contact(new_node_id, new_node->ip, new_node->port);
            bootstrap_it->second->routing_table.add_contact(new_contact);
        }

        // Perform lookup for own ID to populate routing table
        // This simulates the iterative_find_node but we do it globally for simulation
        vector<Contact> closest = find_closest_nodes_global(new_node_id, k);

        for (const auto &contact : closest)
        {
            new_node->routing_table.add_contact(contact);

            // Those nodes also learn about the new node
            auto node_it = nodes.find(contact.id);
            if (node_it != nodes.end())
            {
                Contact new_contact(new_node_id, new_node->ip, new_node->port);
                node_it->second->routing_table.add_contact(new_contact);
            }
        }

        // Populate routing table with diverse contacts
        int contacts_added = closest.size();
        if (contacts_added < k && nodes.size() > 1)
        {
            // Add more random nodes to ensure good coverage
            for (const auto &p : nodes)
            {
                if (p.first != new_node_id && contacts_added < k * 2)
                {
                    Contact c(p.first, p.second->ip, p.second->port);
                    new_node->routing_table.add_contact(c);

                    Contact new_contact(new_node_id, new_node->ip, new_node->port);
                    p.second->routing_table.add_contact(new_contact);

                    contacts_added++;
                }
            }
        }
    }

    // Global helper: find k closest nodes to a target (used for simulation)
    vector<Contact> find_closest_nodes_global(const NodeID &target, int count) const
    {
        vector<pair<NodeID, shared_ptr<KademliaNode>>> all_nodes;
        for (const auto &p : nodes)
        {
            if (p.first != target)
            { // exclude target itself
                all_nodes.push_back(p);
            }
        }

        // Sort by XOR distance
        sort(all_nodes.begin(), all_nodes.end(),
             [&target](const auto &a, const auto &b)
             {
                 NodeID dist_a = xor_distance(a.first, target);
                 NodeID dist_b = xor_distance(b.first, target);
                 return is_closer(dist_a, dist_b);
             });

        // Convert to contacts
        vector<Contact> result;
        for (size_t i = 0; i < all_nodes.size() && i < static_cast<size_t>(count); ++i)
        {
            result.emplace_back(all_nodes[i].first,
                                all_nodes[i].second->ip,
                                all_nodes[i].second->port);
        }

        return result;
    }

    // Store a key-value pair in the network (on k closest nodes)
    void put(const string &key, const string &value)
    {
        if (nodes.empty())
        {
            throw runtime_error("no nodes in network");
        }

        NodeID key_id = hash_to_node_id(key);
        vector<Contact> targets = find_closest_nodes_global(key_id, replication);

        // Store on the k closest nodes
        key_locations[key].clear();
        for (const auto &contact : targets)
        {
            auto it = nodes.find(contact.id);
            if (it != nodes.end())
            {
                it->second->data[key] = value;
                key_locations[key].push_back(contact.id);
            }
        }
    }

    // Get a value from the network
    pair<string, NodeID> get(const string &key) const
    {
        NodeID key_id = hash_to_node_id(key);
        vector<Contact> targets = find_closest_nodes_global(key_id, replication);

        // Check the closest nodes
        for (const auto &contact : targets)
        {
            auto it = nodes.find(contact.id);
            if (it != nodes.end())
            {
                auto val = it->second->find_value(key);
                if (val.has_value())
                {
                    return {val.value(), contact.id};
                }
            }
        }

        return {string(), NodeID{}}; // not found
    }

    // Simulate a lookup from a starting node (returns hops and target node)
    pair<int, NodeID> route_lookup(const NodeID &start_id, const NodeID &target_id) const
    {
        auto it = nodes.find(start_id);
        if (it == nodes.end())
        {
            throw runtime_error("start node not present");
        }

        const auto &start_node = it->second;

        // Simulate iterative lookup
        set<NodeID> queried;
        vector<Contact> shortlist = start_node->routing_table.find_closest(target_id, k);

        if (shortlist.empty())
        {
            // No known nodes - return target as best guess
            return {0, target_id};
        }

        int hops = 0;
        int max_iterations = 50;
        int iterations = 0;
        bool converged = false;

        while (!converged && iterations < max_iterations)
        {
            ++iterations;

            // Get alpha closest unqueried nodes
            vector<Contact> to_query;
            for (const auto &contact : shortlist)
            {
                if (queried.find(contact.id) == queried.end())
                {
                    to_query.push_back(contact);
                    if (to_query.size() >= static_cast<size_t>(alpha))
                    {
                        break;
                    }
                }
            }

            if (to_query.empty())
            {
                converged = true;
                break;
            }

            // Query these nodes (simulate RPC)
            for (const auto &contact : to_query)
            {
                queried.insert(contact.id);
                hops++;

                // Simulate FIND_NODE response
                auto node_it = nodes.find(contact.id);
                if (node_it != nodes.end())
                {
                    vector<Contact> returned = node_it->second->routing_table.find_closest(target_id, k);

                    // Merge into shortlist
                    for (const auto &new_contact : returned)
                    {
                        bool found = false;
                        for (const auto &existing : shortlist)
                        {
                            if (existing.id == new_contact.id)
                            {
                                found = true;
                                break;
                            }
                        }
                        if (!found)
                        {
                            shortlist.push_back(new_contact);
                        }
                    }

                    // Re-sort shortlist by distance
                    sort(shortlist.begin(), shortlist.end(),
                         [&target_id](const Contact &a, const Contact &b)
                         {
                             NodeID dist_a = xor_distance(a.id, target_id);
                             NodeID dist_b = xor_distance(b.id, target_id);
                             return is_closer(dist_a, dist_b);
                         });

                    // Keep only k closest
                    if (shortlist.size() > static_cast<size_t>(k))
                    {
                        shortlist.resize(k);
                    }
                }
            }
        }

        // Return closest node found
        if (!shortlist.empty())
        {
            return {hops, shortlist[0].id};
        }

        return {hops, target_id};
    }

    // Get value from a specific starting node (simulate lookup)
    pair<string, int> get_from(const NodeID &start_id, const string &key) const
    {
        auto it = nodes.find(start_id);
        if (it == nodes.end())
        {
            throw runtime_error("start node not present");
        }

        const auto &start_node = it->second;

        // Check local store first
        auto local = start_node->find_value(key);
        if (local.has_value())
        {
            return {local.value(), 0};
        }

        // Perform lookup
        NodeID key_id = hash_to_node_id(key);
        auto [hops, target_id] = route_lookup(start_id, key_id);

        // Check target node
        auto target_it = nodes.find(target_id);
        if (target_it != nodes.end())
        {
            auto val = target_it->second->find_value(key);
            if (val.has_value())
            {
                return {val.value(), hops};
            }
        }

        // Check other replicas
        vector<Contact> targets = find_closest_nodes_global(key_id, replication);
        for (const auto &contact : targets)
        {
            auto node_it = nodes.find(contact.id);
            if (node_it != nodes.end())
            {
                auto val = node_it->second->find_value(key);
                if (val.has_value())
                {
                    return {val.value(), hops + 1};
                }
            }
        }

        return {string(), hops};
    }

    // Dump network state
    void dump_state(ostream &os) const
    {
        os << "Kademlia network (k=" << k << ", alpha=" << alpha << ", nodes=" << nodes.size() << ")\n";
        os << "Nodes:\n";

        for (const auto &p : nodes)
        {
            const auto &node = p.second;
            string hex_id = node_id_to_hex(node->id);
            os << "  Node: " << hex_id.substr(0, 16) << "... (ip=" << node->ip
               << ", port=" << node->port << ")\n";

            // Show routing table size
            int rt_size = node->routing_table.get_all_contacts().size();
            os << "    Routing table: " << rt_size << " contacts\n";

            // Show data
            if (!node->data.empty())
            {
                os << "    Data (" << node->data.size() << " keys):\n";
                for (const auto &kv : node->data)
                {
                    os << "      " << kv.first << " -> " << kv.second << "\n";
                }
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

    // Create network with k=20, alpha=3, replication=3
    KademliaNetwork net(20, 3, 3);

    // Add nodes
    vector<string> names = {"node-A", "node-B", "node-C", "node-D", "node-E"};
    vector<NodeID> ids;

    cout << "Adding nodes to network..." << endl;
    for (const auto &name : names)
    {
        NodeID id = net.add_node(name);
        ids.push_back(id);
        cout << "  Added " << name << " (ID: " << node_id_to_hex(id).substr(0, 16) << "...)" << endl;
    }

    cout << "\nInitial network state:" << endl;
    net.dump_state(cout);

    // Insert some key-value pairs
    cout << "\nInserting key-value pairs..." << endl;
    vector<pair<string, string>> files = {
        {"file1.txt", "contents-1"},
        {"image.png", "png-bytes"},
        {"video.mp4", "video-bytes"},
        {"notes.md", "some notes"},
        {"data.csv", "csv-data"}};

    for (const auto &f : files)
    {
        net.put(f.first, f.second);
        cout << "  Stored: " << f.first << endl;
    }

    cout << "\nNetwork state after inserting files:" << endl;
    net.dump_state(cout);

    // Demonstrate lookups from each node
    cout << "\nLookup simulation (from each node):" << endl;
    for (const auto &f : files)
    {
        cout << "\nLooking up: " << f.first << endl;
        NodeID key_id = hash_to_node_id(f.first);
        cout << "  Key ID: " << node_id_to_hex(key_id).substr(0, 16) << "..." << endl;

        for (const auto &start_id : ids)
        {
            auto [contents, hops] = net.get_from(start_id, f.first);
            string start_hex = node_id_to_hex(start_id).substr(0, 8);
            cout << "    From node " << start_hex << "... -> hops=" << hops
                 << " found=" << (!contents.empty() ? "yes" : "no") << endl;
        }
    }

    // Show global ownership
    cout << "\nKey ownership (stored on which nodes):" << endl;
    for (const auto &f : files)
    {
        auto [val, owner_id] = net.get(f.first);
        if (!val.empty())
        {
            string owner_hex = node_id_to_hex(owner_id).substr(0, 16);
            cout << "  " << f.first << " -> " << owner_hex << "... (value=" << val << ")" << endl;
        }
    }

    // Add a new node and show rebalancing
    cout << "\n\nAdding new node 'node-F'..." << endl;
    NodeID new_id = net.add_node("node-F");
    cout << "  Added node-F (ID: " << node_id_to_hex(new_id).substr(0, 16) << "...)" << endl;

    cout << "\nNetwork state after adding node-F:" << endl;
    net.dump_state(cout);

    // Test lookups again
    cout << "\nLookup test after new node join:" << endl;
    string test_key = "file1.txt";
    for (const auto &start_id : ids)
    {
        auto [contents, hops] = net.get_from(start_id, test_key);
        string start_hex = node_id_to_hex(start_id).substr(0, 8);
        cout << "  From node " << start_hex << "... -> " << test_key
             << " hops=" << hops << " found=" << (!contents.empty() ? "yes" : "no") << endl;
    }

    // Performance comparison note
    cout << "\n\n=== Performance Notes ===" << endl;
    cout << "Kademlia advantages:" << endl;
    cout << "  - O(log n) hops for lookups" << endl;
    cout << "  - Parallel queries (alpha=" << net.alpha << ") speed up lookups" << endl;
    cout << "  - XOR metric provides symmetric routing" << endl;
    cout << "  - Flexible routing (multiple paths to destination)" << endl;
    cout << "  - Better resilience to node failures" << endl;

    return 0;
}
