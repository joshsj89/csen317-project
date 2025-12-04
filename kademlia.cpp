#include "kademlia.hpp"
#include <algorithm>
#include <cstring>
#include <functional>
#include <random>
#include <bitset>

// XOR distance between two node IDs
NodeID xor_distance(const NodeID &a, const NodeID &b)
{
    NodeID result;
    for (int i = 0; i < ID_BYTES; ++i)
    {
        result[i] = a[i] ^ b[i];
    }
    return result;
}

// Compare two node IDs (returns -1 if a < b, 0 if a == b, 1 if a > b)
int compare_node_id(const NodeID &a, const NodeID &b)
{
    for (int i = 0; i < ID_BYTES; ++i)
    {
        if (a[i] < b[i])
            return -1;
        if (a[i] > b[i])
            return 1;
    }
    return 0;
}

// Check if dist1 < dist2 (i.e., dist1 is closer)
bool is_closer(const NodeID &dist1, const NodeID &dist2)
{
    return compare_node_id(dist1, dist2) < 0;
}

// Convert string to NodeID using hash
NodeID hash_to_node_id(const std::string &s)
{
    NodeID result = {};

    // Use std::hash and expand to 160 bits
    std::hash<std::string> hasher;
    size_t h = hasher(s);

    // Fill the NodeID with hash-derived bytes
    for (int i = 0; i < ID_BYTES; ++i)
    {
        // Mix the hash with different seeds for each byte
        h = h * 1103515245 + 12345 + (i * 7919); // simple PRNG mixing
        result[i] = static_cast<uint8_t>(h & 0xFF);
    }

    return result;
}

// Convert NodeID to hex string
std::string node_id_to_hex(const NodeID &id)
{
    const char hex_chars[] = "0123456789abcdef";
    std::string result;
    result.reserve(ID_BYTES * 2);

    for (int i = 0; i < ID_BYTES; ++i)
    {
        result += hex_chars[id[i] >> 4];
        result += hex_chars[id[i] & 0x0F];
    }

    return result;
}

/******************** KBucket Implementation ********************/

bool KBucket::add_contact(const Contact &contact)
{
    // Check if contact already exists
    for (auto it = contacts.begin(); it != contacts.end(); ++it)
    {
        if (it->id == contact.id)
        {
            // Move to back (most recently seen)
            contacts.erase(it);
            contacts.push_back(contact);
            return true;
        }
    }

    // If bucket not full, add to back
    if (!is_full())
    {
        contacts.push_back(contact);
        return true;
    }

    // Bucket is full - in real implementation, we'd ping the least recently seen
    // For simulation, just don't add (could also evict)
    return false;
}

void KBucket::remove_contact(const NodeID &id)
{
    for (auto it = contacts.begin(); it != contacts.end(); ++it)
    {
        if (it->id == id)
        {
            contacts.erase(it);
            return;
        }
    }
}

void KBucket::update_contact(const NodeID &id)
{
    for (auto it = contacts.begin(); it != contacts.end(); ++it)
    {
        if (it->id == id)
        {
            Contact c = *it;
            contacts.erase(it);
            contacts.push_back(c);
            return;
        }
    }
}

std::vector<Contact> KBucket::get_contacts() const
{
    return std::vector<Contact>(contacts.begin(), contacts.end());
}

bool KBucket::has_contact(const NodeID &id) const
{
    for (const auto &c : contacts)
    {
        if (c.id == id)
            return true;
    }
    return false;
}

/******************** RoutingTable Implementation ********************/

RoutingTable::RoutingTable(const NodeID &id, int bucket_size)
    : self_id(id), k(bucket_size)
{
    // Create ID_BITS buckets (one for each bit position)
    for (int i = 0; i < ID_BITS; ++i)
    {
        buckets.emplace_back(k);
    }
}

int RoutingTable::bucket_for_id(const NodeID &id) const
{
    // Find the position of the most significant differing bit
    NodeID dist = xor_distance(self_id, id);

    // Find the highest bit set in the distance
    for (int byte_idx = 0; byte_idx < ID_BYTES; ++byte_idx)
    {
        if (dist[byte_idx] != 0)
        {
            // Find the highest bit in this byte
            uint8_t byte = dist[byte_idx];
            int bit_pos = 7;
            while (bit_pos >= 0)
            {
                if (byte & (1 << bit_pos))
                {
                    return (byte_idx * 8) + (7 - bit_pos);
                }
                --bit_pos;
            }
        }
    }

    // Distance is 0 (same ID) - shouldn't happen
    return -1;
}

int RoutingTable::get_bucket_index(const NodeID &id) const
{
    return bucket_for_id(id);
}

void RoutingTable::add_contact(const Contact &contact)
{
    if (contact.id == self_id)
        return; // don't add ourselves

    int bucket_idx = bucket_for_id(contact.id);
    if (bucket_idx >= 0 && bucket_idx < ID_BITS)
    {
        buckets[bucket_idx].add_contact(contact);
    }
}

void RoutingTable::remove_contact(const NodeID &id)
{
    int bucket_idx = bucket_for_id(id);
    if (bucket_idx >= 0 && bucket_idx < ID_BITS)
    {
        buckets[bucket_idx].remove_contact(id);
    }
}

std::vector<Contact> RoutingTable::find_closest(const NodeID &target, int count) const
{
    std::vector<Contact> all_contacts = get_all_contacts();

    // Sort by XOR distance to target
    std::sort(all_contacts.begin(), all_contacts.end(),
              [&target](const Contact &a, const Contact &b)
              {
                  NodeID dist_a = xor_distance(a.id, target);
                  NodeID dist_b = xor_distance(b.id, target);
                  return is_closer(dist_a, dist_b);
              });

    // Return top k contacts
    if (all_contacts.size() > static_cast<size_t>(count))
    {
        all_contacts.resize(count);
    }

    return all_contacts;
}

std::vector<Contact> RoutingTable::get_all_contacts() const
{
    std::vector<Contact> result;
    for (const auto &bucket : buckets)
    {
        auto contacts = bucket.get_contacts();
        result.insert(result.end(), contacts.begin(), contacts.end());
    }
    return result;
}

/******************** KademliaNode Implementation ********************/

KademliaNode::KademliaNode(const std::string &name, const std::string &ip_address,
                           int port_number, int bucket_size, int concurrency)
    : id(hash_to_node_id(name)), ip(ip_address), port(port_number), routing_table(id, bucket_size), k(bucket_size), alpha(concurrency)
{
}

/******************** RPC Handlers ********************/

RPCResponse KademliaNode::handle_ping(const RPCRequest &req)
{
    // Update routing table with sender info
    Contact sender(req.sender_id, req.sender_ip, req.sender_port);
    routing_table.add_contact(sender);

    RPCResponse resp;
    resp.ok = true;
    resp.request_type = RPCType::PING;
    resp.is_alive = true;
    return resp;
}

RPCResponse KademliaNode::handle_store(const RPCRequest &req)
{
    // Store the key-value pair
    data[req.key] = req.value;

    // Update routing table with sender info
    Contact sender(req.sender_id, req.sender_ip, req.sender_port);
    routing_table.add_contact(sender);

    RPCResponse resp;
    resp.ok = true;
    resp.request_type = RPCType::STORE;
    return resp;
}

RPCResponse KademliaNode::handle_find_node(const RPCRequest &req)
{
    // Return k closest nodes to target
    std::vector<Contact> closest = routing_table.find_closest(req.target_id, k);

    // Update routing table with sender info
    Contact sender(req.sender_id, req.sender_ip, req.sender_port);
    routing_table.add_contact(sender);

    RPCResponse resp;
    resp.ok = true;
    resp.request_type = RPCType::FIND_NODE;
    resp.contacts = closest;
    return resp;
}

RPCResponse KademliaNode::handle_find_value(const RPCRequest &req)
{
    RPCResponse resp;
    resp.ok = true;
    resp.request_type = RPCType::FIND_VALUE;

    // Check if we have the value
    auto it = data.find(req.key);
    if (it != data.end())
    {
        resp.value_found = true;
        resp.value = it->second;
    }
    else
    {
        // Return k closest nodes
        NodeID key_id = hash_to_node_id(req.key);
        resp.value_found = false;
        resp.contacts = routing_table.find_closest(key_id, k);
    }

    // Update routing table with sender info
    Contact sender(req.sender_id, req.sender_ip, req.sender_port);
    routing_table.add_contact(sender);

    return resp;
}

/******************** Core Operations ********************/

void KademliaNode::store(const std::string &key, const std::string &value)
{
    // Store locally
    data[key] = value;
}

std::optional<std::string> KademliaNode::find_value(const std::string &key)
{
    auto it = data.find(key);
    if (it != data.end())
    {
        return it->second;
    }
    return std::nullopt;
}

std::vector<Contact> KademliaNode::find_node(const NodeID &target)
{
    return routing_table.find_closest(target, k);
}

/******************** Helper for Lookup State ********************/

void KademliaNode::update_shortlist(LookupState &state, const std::vector<Contact> &new_contacts)
{
    // Add new contacts to shortlist
    for (const auto &contact : new_contacts)
    {
        // Don't add ourselves
        if (contact.id == id)
            continue;

        // Check if already in shortlist
        bool found = false;
        for (const auto &existing : state.shortlist)
        {
            if (existing.id == contact.id)
            {
                found = true;
                break;
            }
        }

        if (!found)
        {
            state.shortlist.push_back(contact);
        }
    }

    // Sort by distance to target
    std::sort(state.shortlist.begin(), state.shortlist.end(),
              [&state](const Contact &a, const Contact &b)
              {
                  NodeID dist_a = xor_distance(a.id, state.target);
                  NodeID dist_b = xor_distance(b.id, state.target);
                  return is_closer(dist_a, dist_b);
              });

    // Keep only k closest
    if (state.shortlist.size() > static_cast<size_t>(k))
    {
        state.shortlist.resize(k);
    }
}

std::vector<Contact> KademliaNode::get_next_alpha_contacts(LookupState &state)
{
    std::vector<Contact> result;

    for (const auto &contact : state.shortlist)
    {
        if (state.queried.find(contact.id) == state.queried.end())
        {
            result.push_back(contact);
            if (result.size() >= static_cast<size_t>(alpha))
            {
                break;
            }
        }
    }

    return result;
}

/******************** Iterative Lookup Algorithms ********************/

std::vector<Contact> KademliaNode::iterative_find_node(const NodeID &target, int &hops)
{
    LookupState state;
    state.target = target;
    state.converged = false;
    hops = 0;

    // Initialize shortlist with k closest known nodes
    state.shortlist = routing_table.find_closest(target, k);

    if (state.shortlist.empty())
    {
        return state.shortlist; // No nodes known
    }

    // Iterative lookup
    int max_iterations = 50; // prevent infinite loops
    int iterations = 0;

    while (!state.converged && iterations < max_iterations)
    {
        ++iterations;

        // Get alpha unqueried nodes closest to target
        std::vector<Contact> to_query = get_next_alpha_contacts(state);

        if (to_query.empty())
        {
            // No more nodes to query - converged
            state.converged = true;
            break;
        }

        // Mark them as queried
        for (const auto &contact : to_query)
        {
            state.queried.insert(contact.id);
        }

        // In simulation, we'd send parallel queries here
        // For now, just count as hops
        hops += static_cast<int>(to_query.size());

        // In real implementation: send FIND_NODE RPCs to these contacts
        // and collect responses, then update shortlist
        // For simulation purposes, this is handled by the network simulator
    }

    return state.shortlist;
}

std::optional<std::string> KademliaNode::iterative_find_value(const std::string &key, int &hops)
{
    NodeID key_id = hash_to_node_id(key);

    // Check local store first
    auto local = find_value(key);
    if (local.has_value())
    {
        hops = 0;
        return local;
    }

    LookupState state;
    state.target = key_id;
    state.converged = false;
    hops = 0;

    // Initialize shortlist with k closest known nodes
    state.shortlist = routing_table.find_closest(key_id, k);

    if (state.shortlist.empty())
    {
        return std::nullopt; // No nodes known
    }

    // Iterative lookup
    int max_iterations = 50;
    int iterations = 0;

    while (!state.converged && iterations < max_iterations)
    {
        ++iterations;

        // Get alpha unqueried nodes closest to target
        std::vector<Contact> to_query = get_next_alpha_contacts(state);

        if (to_query.empty())
        {
            state.converged = true;
            break;
        }

        // Mark them as queried
        for (const auto &contact : to_query)
        {
            state.queried.insert(contact.id);
        }

        hops += static_cast<int>(to_query.size());

        // In real implementation: send FIND_VALUE RPCs
        // If any returns the value, we're done
        // Otherwise, update shortlist with returned contacts
    }

    return std::nullopt; // Value not found
}

/******************** Node Join ********************/

void KademliaNode::join(const Contact &bootstrap_node)
{
    // Add bootstrap node to routing table
    routing_table.add_contact(bootstrap_node);

    // Perform a lookup for our own ID to populate routing table
    int hops = 0;
    iterative_find_node(id, hops);

    // Refresh all buckets (in full implementation, this would be done periodically)
    for (int i = 0; i < ID_BITS; ++i)
    {
        refresh_bucket(i);
    }
}

void KademliaNode::refresh_bucket(int bucket_index)
{
    // Generate a random ID that would fall in this bucket
    // For simulation, we'll skip the actual refresh
    // In real implementation: perform node lookup for random ID in bucket range
    (void)bucket_index; // suppress unused warning
}
