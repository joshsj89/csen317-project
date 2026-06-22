#include <chrono>
#include <iostream>
#include <random>
#include <sstream>
#include <thread>
#include "chord.hpp"
#include "rpc.hpp"
#include "utils.hpp"

ChordNode::ChordNode(std::string ip_address, int port_number, int m_bits)
    : ip(ip_address), port(port_number), m(m_bits)
{
    id = hash_str(ip + ":" + std::to_string(port)) % (1ULL << m);

    // Initialize predecessor to null
    predecessor_id = 0;
    predecessor_ip = "";
    predecessor_port = 0;

    // Initialize successor to self
    successor_id = id;
    successor_ip = ip;
    successor_port = port;

    finger_table.resize(m);
    for (int k = 0; k < m; ++k)
    {
        finger_table[k].start = (id + (1ULL << k)) % (1ULL << m);
        finger_table[k].node_id = id;
        finger_table[k].node_ip = ip;
        finger_table[k].node_port = port;
    }

    // Initialize successor list
    successor_list.resize(replication_factor, Finger{id, ip, port});
}

void ChordNode::join(std::string bootstrap_ip, int bootstrap_port)
{
    joining = true; // prevent stabilization during join

    // first node in the network
    if (bootstrap_ip == ip && bootstrap_port == port)
    {
        std::lock_guard<std::mutex> lock(state_mutex);

        predecessor_id = 0;
        predecessor_ip = "";
        predecessor_port = 0;

        successor_id = id;
        successor_ip = ip;
        successor_port = port;

        pending_key_transfer = false; // no key transfer needed for first node

        std::cout << "[join] First node in the network." << std::endl;
        joining = false;
        return;
    }

    // Joining an existing network
    std::cout << "[join] Contacting bootstrap node " << bootstrap_ip << ":" << bootstrap_port << "..." << std::endl;

    std::string msg = "FIND_SUCCESSOR " + std::to_string(id);
    RPCResponse resp = rpc_send(bootstrap_ip, bootstrap_port, msg);

    if (!resp.ok)
    {
        bool success = false;
        for (int attempt = 0; attempt < max_retries && !success; ++attempt)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100 * (attempt + 1))); // brief pause before retry

            resp = rpc_send(bootstrap_ip, bootstrap_port, msg);

            if (resp.ok)
                success = true;
        }

        if (!success)
        {
            std::cerr << "[join] Failed to join the network after " << max_retries << " attempts: " << resp.body << std::endl;
            joining = false;
            return;
        }
    }

    u64 succ_id;
    std::string succ_ip;
    int succ_port;
    std::istringstream iss(resp.body);
    iss >> succ_id >> succ_ip >> succ_port;

    if (iss.fail())
    {
        std::cerr << "[join] Invalid FIND_SUCCESSOR response: " << resp.body << std::endl;
        joining = false;
        return;
    }

    {
        std::lock_guard<std::mutex> lock(state_mutex);

        successor_id = succ_id;
        successor_ip = succ_ip;
        successor_port = succ_port;
    }

    // Request key transfer from new successor
    std::string transfer_msg = "TRANSFER_KEYS " + std::to_string(id) + " " + ip + " " + std::to_string(port);
    RPCResponse transfer_resp = rpc_send(successor_ip, successor_port, transfer_msg);

    if (!transfer_resp.ok)
    {
        const int max_retries = 3;
        bool success = false;
        for (int attempt = 0; attempt < max_retries && !success; ++attempt)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100 * (attempt + 1))); // brief pause before retry

            transfer_resp = rpc_send(successor_ip, successor_port, transfer_msg);

            if (transfer_resp.ok)
                success = true;
        }

        if (!success)
        {
            std::cerr << "[join] Failed to transfer keys from successor after " << max_retries << " attempts: " << transfer_resp.body << std::endl;
        }
    }

    // Notify the successor so it can update its predecessor immediately
    try
    {
        std::string notify_msg = "NOTIFY " + std::to_string(id) + " " + ip + " " + std::to_string(port);
        RPCResponse notify_resp = rpc_send(successor_ip, successor_port, notify_msg);

        if (!notify_resp.ok)
        {
            const int max_retries = 3;
            bool success = false;
            for (int attempt = 0; attempt < max_retries && !success; ++attempt)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(100 * (attempt + 1))); // brief pause before retry

                notify_resp = rpc_send(successor_ip, successor_port, notify_msg);

                if (notify_resp.ok)
                    success = true;
            }
        }
    } catch (...)
    {
        // ignore notify failures; stabilization will recover
    }

    pending_key_transfer = true;
    stable_rounds = 0;

    std::cout << "[join] Joined the network. Predecessor: " << predecessor_id << " (" << predecessor_ip << ":" << predecessor_port << "), Successor: " << successor_id << " (" << successor_ip << ":" << successor_port << ")" << std::endl;

    joining = false;

    // Predecessor will also be set/validated during stabilization
    // Optionally recompute fingers to speed convergence
    // recompute_finger_table();
}

void ChordNode::leave()
{
    isLeaving = true;

    u64 successor_id_local;
    std::string successor_ip_local;
    int successor_port_local;
    u64 predecessor_id_local;
    std::string predecessor_ip_local;
    int predecessor_port_local;
    {
        std::lock_guard<std::mutex> lock(state_mutex);

        successor_id_local = successor_id;
        successor_ip_local = successor_ip;
        successor_port_local = successor_port;

        predecessor_id_local = predecessor_id;
        predecessor_ip_local = predecessor_ip;
        predecessor_port_local = predecessor_port;
    }

    if (successor_id_local != id) // if not the only node, transfer keys and notify neighbors
    {
        std::vector<std::pair<std::string, std::string>> primary_data_copy;
        {
            std::lock_guard<std::mutex> lock(data_mutex);

            for (const auto &kv : data)
            {
                u64 key_id = hash_str(kv.first) % (1ULL << m);

                // Find true owner of the key
                auto [owner_id, owner_ip, owner_port] = find_successor(key_id);

                // only transfer if this node is the owner (i.e. primary replica)
                if (owner_id == id)
                    primary_data_copy.emplace_back(kv.first, kv.second);
            }
        }
        
        for (const auto &kv : primary_data_copy)
        {
            // Transfer all keys to successor
            bool transferred = false;
            for (int attempt = 0; attempt < max_retries && !transferred; ++attempt)
            {
                std::string transfer_msg = "PUT " + kv.first + " " + kv.second;
                RPCResponse transfer_resp = rpc_send(successor_ip_local, successor_port_local, transfer_msg);

                if (transfer_resp.ok)
                    transferred = true;
                else
                    std::this_thread::sleep_for(std::chrono::milliseconds(100 * (attempt + 1))); // brief pause before retry
            }

            // Remove transferred keys from local store
            if (transferred)
            {
                std::lock_guard<std::mutex> lock(data_mutex);

                data.erase(kv.first);
            }
            else
            {
                std::cerr << "[leave] Failed to transfer key '" << kv.first << "' to successor after " << max_retries << " attempts." << std::endl;
            }
        }

        if (predecessor_id_local != 0 && predecessor_id_local != id)
        {
            // Tell successor to update its predecessor
            std::string notify_msg = "SET_PREDECESSOR " + std::to_string(predecessor_id_local) + " " + predecessor_ip_local + " " + std::to_string(predecessor_port_local);

            rpc_send(successor_ip_local, successor_port_local, notify_msg);
        }

    }

    if (predecessor_id_local != 0 && predecessor_id_local != id)
    {
        // Tell predecessor to update its successor
        std::string notify_msg = "SET_SUCCESSOR " + std::to_string(successor_id_local) + " " + successor_ip_local + " " + std::to_string(successor_port_local);

        rpc_send(predecessor_ip_local, predecessor_port_local, notify_msg);
    }

    // Clear predecessor and successor
    {
        std::lock_guard<std::mutex> lock(state_mutex);

        predecessor_id = 0;
        predecessor_ip = "";
        predecessor_port = 0;

        successor_id = id;
        successor_ip = ip;
        successor_port = port;
    }

    RUNNING = false; // stop RPC listener

    std::cout << "[leave] Node " << id << " (" << ip << ":" << port << ") left gracefully." << std::endl;
}

void ChordNode::put(const std::string &key, const std::string &value)
{
    metric_put_requests++;

    u64 key_id = hash_str(key) % (1ULL << m);
    auto [owner_id, owner_ip, owner_port] = find_successor(key_id);

    // if this node is not the owner, forward request
    if (owner_id != id)
    {
        std::string msg = "PUT " + key + " " + value;
        RPCResponse resp = rpc_send(owner_ip, owner_port, msg);

        if (!resp.ok)
        {
            bool success = false;

            for (int attempt = 0; attempt < max_retries && !success; ++attempt)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(100 * (attempt + 1))); // brief pause before retry

                resp = rpc_send(owner_ip, owner_port, msg);

                if (resp.ok)
                    success = true;
            }
        }
        return;
    }

    // store locally
    {
        std::lock_guard<std::mutex> lock(data_mutex);
        data[key] = value;
    }

    // replicate to successor nodes
    std::vector<Finger> replication_targets;
    u64 succ_id_local = id;
    std::string succ_ip_local = ip;
    int succ_node_port = port;
    {
        std::lock_guard<std::mutex> lock(state_mutex);

        succ_id_local = successor_id;
        succ_ip_local = successor_ip;
        succ_node_port = successor_port;
    }

    if (succ_id_local == id)
        return;

    replication_targets.emplace_back(succ_id_local, succ_ip_local, succ_node_port);
    
    while ((int)replication_targets.size() < replication_factor)
    {
        auto [replica_node_id, replica_node_ip, replica_node_port] = replication_targets.back();

         // if self, stop replicating further
        if (replica_node_id == id)
            break;
        
        std::string rep_msg = "GET_SUCCESSOR";
        RPCResponse rep_resp = rpc_send(replica_node_ip, replica_node_port, rep_msg);

        if (!rep_resp.ok)
            break;

        std::istringstream rep_iss(rep_resp.body);
        u64 nid;
        std::string nip;
        int nport;
        rep_iss >> nid >> nip >> nport;

        if (rep_iss.fail())
            break;

        bool duplicate = false;
        for (const auto &t : replication_targets)
        {
            auto [existing_id, _, __] = t;
            if (existing_id == nid)
            {
                duplicate = true;
                break;
            }
        }

        if (duplicate)
            break;

        replication_targets.emplace_back(nid, nip, nport);
    }

    for (auto &t : replication_targets)
    {
        auto [replica_node_id, replica_node_ip, replica_node_port] = t;

        if (replica_node_id == id)
            continue;

        std::string msg = "PUT_REPLICA " + key + " " + value;
        rpc_send(replica_node_ip, replica_node_port, msg);
    }
}

std::string ChordNode::get(const std::string &key)
{
    metric_get_requests++;

    u64 key_id = hash_str(key) % (1ULL << m);
    auto [owner_id, owner_ip, owner_port] = find_successor(key_id);

    // if this node is the owner, retrieve locally
    if (owner_id == id)
    {
        std::lock_guard<std::mutex> lock(data_mutex);

        if (data.count(key))
            return data[key];
        else
            return ""; // key not found
    }

    std::string msg = "GET " + key;
    RPCResponse resp = rpc_send(owner_ip, owner_port, msg);

    if (!resp.ok)
    {
        if (resp.body == "not_found")
            return ""; // key not found

        bool success = false;

        for (int attempt = 0; attempt < max_retries && !success; ++attempt)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100 * (attempt + 1))); // brief pause before retry

            resp = rpc_send(owner_ip, owner_port, msg);

            if (resp.ok)
            {
                success = true;
                break;
            }
            else if (resp.body == "not_found")
            {
                return ""; // key not found
            }
        }
    }

    if (!resp.ok)
    {
        return ""; // failed to get value
    }

    // remove the newline at the end if present
    if (!resp.body.empty() && resp.body.back() == '\n')
        resp.body.pop_back();

    return resp.body;
}

void ChordNode::transfer_keys(u64 new_node_id, std::string new_node_ip, int new_node_port)
{
    std::vector<std::string> keys_snapshot;
    {
        std::lock_guard<std::mutex> lock(data_mutex);

        keys_snapshot.reserve(data.size());

        for (const auto &kv : data)
            keys_snapshot.push_back(kv.first);
    }
    
    std::vector<std::pair<std::string, std::string>> keys_to_transfer;
    // Identify keys that should be transferred to the new node
    for (const auto &key : keys_snapshot)
    {
        u64 key_id = hash_str(key) % (1ULL << m);

        // Find true owner of the key
        auto [owner_id, owner_ip, owner_port] = find_successor(key_id);

        // only transfer if this node is the owner (i.e. primary replica)
        if (owner_id != id)
            continue;

        bool should_transfer = false;
        {
            std::lock_guard<std::mutex> lock(state_mutex);

            // if predecessor is null, skip
            if (predecessor_id == 0)
                continue;

            // new node is predecessor, no transfer needed
            if (predecessor_id == new_node_id)
                continue;

            // if key is in (predecessor_id, new_node_id]
            if (in_interval_inclusive(predecessor_id, new_node_id, key_id))
                should_transfer = true;
        }

        if (!should_transfer)
            continue;
        
        std::string value;
        {
            std::lock_guard<std::mutex> dlock(data_mutex);

            value = data[key];
        }

        keys_to_transfer.emplace_back(key, value);
    }

    // Transfer keys to the new node
    for (const auto &kv : keys_to_transfer)
    {
        bool transferred = false;

        for (int attempt = 0; attempt < max_retries && !transferred; ++attempt)
        {
            std::string msg = "PUT_DIRECT " + kv.first + " " + kv.second;
            RPCResponse resp = rpc_send(new_node_ip, new_node_port, msg);

            if (resp.ok)
                transferred = true;
            else
                std::this_thread::sleep_for(std::chrono::milliseconds(100 * (attempt + 1))); // brief pause before retry
        }

        if (transferred)
        {
            std::lock_guard<std::mutex> dlock(data_mutex);
            
            // Remove key from local store after successful transfer
            data.erase(kv.first);
        }
        else
        {
            std::cerr << "[transfer_keys] Failed to transfer key '" << kv.first << "' after " << max_retries << " attempts: " << new_node_ip << ":" << new_node_port << std::endl;
        }
    }
}

// Notify called by other nodes to inform about potential predecessor
void ChordNode::notify(u64 nid, std::string nip, int nport)
{
    std::lock_guard<std::mutex> lock(state_mutex);
    
    if (predecessor_id == 0 || // null predecessor
        in_interval_open(predecessor_id, id, nid))
    {
        predecessor_id = nid;
        predecessor_ip = nip;
        predecessor_port = nport;
    }

    if (successor_id == id)
    {
        // if successor is self, update to new node
        successor_id = nid;
        successor_ip = nip;
        successor_port = nport;
    }
}

void ChordNode::stabilize()
{
    // don't stabilize if node is joining or leaving
    if (joining || isLeaving)
        return;

    u64 succ_id_local;
    std::string succ_ip_local;
    int succ_port_local;

    {
        std::lock_guard<std::mutex> lock(state_mutex);

        succ_id_local = successor_id;
        succ_ip_local = successor_ip;
        succ_port_local = successor_port;
    }

    if (succ_id_local == id)
    {
        // increment stable rounds if successor is self
        stable_rounds = std::min(stable_rounds + 1, STABLE_ROUNDS_REQUIRED);
        return;
    }

    // Get successor's predecessor
    std::string msg = "GET_PREDECESSOR";
    RPCResponse resp = rpc_send(succ_ip_local, succ_port_local, msg);

    // If successor failed to respond, we could try to find the next alive finger
    if (!resp.ok)
    {
        bool found_alive = false;
        std::vector<FingerEntry> local_finger_table;
        {
            std::lock_guard<std::mutex> flock(finger_table_mutex);

            local_finger_table = finger_table; // copy to avoid holding lock during RPCs
        }

        for (int k = 0; k < m; ++k)
        {
            auto f = local_finger_table[k];
            if (f.node_id != id && is_alive(f.node_ip, f.node_port))
            {
                std::lock_guard<std::mutex> slock(state_mutex);
                
                successor_id = f.node_id;
                successor_ip = f.node_ip;
                successor_port = f.node_port;

                found_alive = true;
                break;
            }
        }

        // If no fingers are alive, point to self
        if (!found_alive)
        {
            std::lock_guard<std::mutex> lock(state_mutex);

            successor_id = id;
            successor_ip = ip;
            successor_port = port;
        }

        stable_rounds = 0; // reset stable rounds on failure
        return;
    }

    // Parse successor's predecessor info
    u64 succ_pred_id = 0;
    std::string succ_pred_ip;
    int succ_pred_port = 0;

    std::istringstream iss(resp.body);
    iss >> succ_pred_id >> succ_pred_ip >> succ_pred_port;    

    // Check if successor's predecessor is a better successor
    // bool better_successor = false;
    {
        std::lock_guard<std::mutex> lock(state_mutex);

        if (succ_pred_id != 0 &&
            in_interval_open(id, successor_id, succ_pred_id))
        {
            if (is_alive(succ_pred_ip, succ_pred_port))
            {
                successor_id = succ_pred_id;
                successor_ip = succ_pred_ip;
                successor_port = succ_pred_port;
                // better_successor = true;
            }
        }
    }

    // Notify successor that we might be its predecessor
    {
        std::lock_guard<std::mutex> lock(state_mutex);

        std::string notify_msg = "NOTIFY " + std::to_string(id) + " " + ip + " " + std::to_string(port);
        rpc_send(successor_ip, successor_port, notify_msg);
    }

    // If we still have a pending key-transfer request (from join), ask our successor to transfer keys to us
    if (pending_key_transfer)
    {
        // try a few times to request transfer from successor
        const int max_retries = 3;
        bool transfer_requested = false;
        for (int attempt = 0; attempt < max_retries && !transfer_requested; ++attempt)
        {
            u64 s_id;
            std::string s_ip;
            int s_port;
            {
                std::lock_guard<std::mutex> lock(state_mutex);
                s_id = successor_id;
                s_ip = successor_ip;
                s_port = successor_port;
            }

            if (s_id == id) { // nothing to request from self
                pending_key_transfer = false;
                transfer_requested = true;
                break;
            }

            // send TRANSFER_KEYS <new_node_id> <new_node_ip> <new_node_port> to successor
            std::string transfer_req = "TRANSFER_KEYS " + std::to_string(id) + " " + ip + " " + std::to_string(port);
            RPCResponse tresp = rpc_send(s_ip, s_port, transfer_req);
            if (tresp.ok)
            {
                pending_key_transfer = false;
                transfer_requested = true;
                break;
            }
            else
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(100 * (attempt + 1)));
            }
        }
        if (!transfer_requested)
        {
            // leave pending_key_transfer true so next stabilize attempt retries
            stable_rounds = 0;
            return;
        }
    }

    // If we reach here, we consider this stabilize iteration successful
    stable_rounds = std::min(stable_rounds + 1, STABLE_ROUNDS_REQUIRED);

    // Rebuild successor_list only once we've reached enough stable rounds
    if (stable_rounds >= STABLE_ROUNDS_REQUIRED)
    {
        // rebuild successor_list from successor chain
        std::vector<Finger> new_successors;
        {
            std::lock_guard<std::mutex> lock(state_mutex);
            // start from immediate successor
            new_successors.emplace_back(successor_id, successor_ip, successor_port);
        }

        // expand successor list by repeatedly asking for successor of successor (safe-forward)
        while ((int)new_successors.size() < replication_factor)
        {
            auto [curr_id, curr_ip, curr_port] = new_successors.back();

            if (curr_id == id) // no further nodes
            {
                new_successors.emplace_back(id, ip, port);
                continue;
            }

            // ask curr for its successor (GET_INFO / or use FIND_SUCCESSOR for next key)
            RPCResponse r = rpc_send(curr_ip, curr_port, "GET_INFO");
            if (!r.ok)
            {
                // if RPC failed, push self to fill remaining slots
                new_successors.emplace_back(id, ip, port);
                continue;
            }

            // r.body expected to be: "<id> <ip> <port>\n"
            std::istringstream iss(r.body);
            u64 nid; std::string nip; int nport;
            iss >> nid >> nip >> nport;
            if (iss.fail())
            {
                new_successors.emplace_back(id, ip, port);
                continue;
            }

            // if it returned the same as we already have (loop), fill remaining with self
            bool duplicate = false;
            for (auto &f : new_successors) if (std::get<0>(f) == nid) { duplicate = true; break; }
            if (duplicate)
            {
                new_successors.emplace_back(id, ip, port);
                continue;
            }

            new_successors.emplace_back(nid, nip, nport);
        }

        {
            std::lock_guard<std::mutex> slock(successor_list_mutex);
            successor_list = std::move(new_successors);
        }
    }
}

Finger ChordNode::find_successor(u64 id_to_find)
{
    metric_find_successor_calls++;

    u64 succ_id_local;
    std::string succ_ip_local;
    int succ_port_local;
    {
        std::lock_guard<std::mutex> lock(state_mutex);

        succ_id_local = successor_id;
        succ_ip_local = successor_ip;
        succ_port_local = successor_port;
    }
    
    // Check if id is in (this->id, successor_id]
    if (in_interval_inclusive(this->id, succ_id_local, id_to_find))
    {
        return {succ_id_local, succ_ip_local, succ_port_local};
    }
    
    // otherwise pick the closest preceding finger
    auto [next_id, next_ip, next_port] = closest_preceding_finger(id_to_find);

    // if next is self, fallback to successor
    if (next_id == this->id)
        return {succ_id_local, succ_ip_local, succ_port_local};

    // Forward RPC to next node to find successor
    metric_find_successor_hops++;
    std::string msg = "FIND_SUCCESSOR " + std::to_string(id_to_find);
    RPCResponse resp = rpc_send(next_ip, next_port, msg);

    if (!resp.ok)
        return {succ_id_local, succ_ip_local, succ_port_local}; // fallback to current successor

    u64 found_id;
    std::string found_ip;
    int found_port;
    std::istringstream iss(resp.body);
    iss >> found_id >> found_ip >> found_port;

    if (iss.fail())
        return {succ_id_local, succ_ip_local, succ_port_local}; // fallback to current successor

    return {found_id, found_ip, found_port};
}

Finger ChordNode::closest_preceding_finger(u64 id_to_find)
{
    std::vector<FingerEntry> local_finger_table;
    {
        std::lock_guard<std::mutex> lock(finger_table_mutex);
        
        local_finger_table = finger_table; // copy to avoid holding lock during is_alive checks
    }

    u64 self_id_local;
    std::string self_ip_local;
    int self_port_local;
    {
        std::lock_guard<std::mutex> lock(state_mutex);

        self_id_local = this->id;
        self_ip_local = this->ip;
        self_port_local = this->port;
    }
    
    for (int k = m - 1; k >= 0; --k)
    {
        u64 f_id = local_finger_table[k].node_id;
        std::string f_ip = local_finger_table[k].node_ip;
        int f_port = local_finger_table[k].node_port;

        // Check if finger is in (self_id, id_to_find)
        if (in_interval_open(self_id_local, id_to_find, f_id))
        {
            if (is_alive(f_ip, f_port))
                return {f_id, f_ip, f_port};
        }
    }

    // If no finger found, return self
    return {self_id_local, self_ip_local, self_port_local};
}

void ChordNode::check_predecessor()
{
    // don't stabilize if node is joining or leaving
    if (joining || isLeaving)
        return;

    u64 predecessor_id_local;
    std::string predecessor_ip_local;
    int predecessor_port_local;
    {
        std::lock_guard<std::mutex> lock(state_mutex);

        predecessor_id_local = predecessor_id;
        predecessor_ip_local = predecessor_ip;
        predecessor_port_local = predecessor_port;
    }
    
    // If predecessor is self or null, nothing to do
    if (predecessor_id_local == 0 || predecessor_id_local == id)
        return;

    // If predecessor is dead, clear it
    if (!is_alive(predecessor_ip_local, predecessor_port_local))
    {
        std::lock_guard<std::mutex> lock(state_mutex);
        
        predecessor_id = 0;
        predecessor_ip = "";
        predecessor_port = 0;
    }
}

void ChordNode::check_successor()
{
    // don't stabilize if node is joining or leaving
    if (joining || isLeaving)
        return;
    
    u64 successor_id_local;
    std::string successor_ip_local;
    int successor_port_local;
    {
        std::lock_guard<std::mutex> lock(state_mutex);

        successor_id_local = successor_id;
        successor_ip_local = successor_ip;
        successor_port_local = successor_port;
    }

    // If successor is self, nothing to do
    if (successor_id_local == id)
        return;

    // If successor is alive, nothing to do
    if (is_alive(successor_ip_local, successor_port_local))
        return;

    // Try to find the next alive finger as new successor
    bool found_alive = false;
    {
        std::lock_guard<std::mutex> flock(finger_table_mutex);

        for (int k = 0; k < m; ++k)
        {
            auto f = finger_table[k];
            if (f.node_id != id && is_alive(f.node_ip, f.node_port))
            {
                std::lock_guard<std::mutex> slock(state_mutex);
                
                successor_id = f.node_id;
                successor_ip = f.node_ip;
                successor_port = f.node_port;

                found_alive = true;
                break;
            }
        }
    }

    // If no fingers are alive, point to self
    if (!found_alive)
    {
        std::lock_guard<std::mutex> slock(state_mutex);
        
        successor_id = id;
        successor_ip = ip;
        successor_port = port;
    }
}

void ChordNode::fix_fingers()
{
    static int next_finger = 0;
    u64 start;
    
    {
        std::lock_guard<std::mutex> lock(finger_table_mutex);

        start = finger_table[next_finger].start;
    }

    auto [finger_id, finger_ip, finger_port] = find_successor(start);

    // Update finger table entry
    {
        std::lock_guard<std::mutex> lock(finger_table_mutex);
    
        finger_table[next_finger].node_id = finger_id;
        finger_table[next_finger].node_ip = finger_ip;
        finger_table[next_finger].node_port = finger_port;
    
    }
    
    next_finger = (next_finger + 1) % m;

    // Rebuild successor list for replication only if node is stable
    if (joining || isLeaving)
        return;
    // {
    //     std::lock_guard<std::mutex> sl_lock(successor_list_mutex);

    //     successor_list.clear();
    
    //     // if this node is the only node, fill successor list with self
    //     if (successor_id == id)
    //     {
    //         for (int i = 0; i < replication_factor; ++i)
    //             successor_list.emplace_back(id, ip, port);
    
    //         return;
    //     }
    
    //     // Start from immediate successor
    //     Finger current_successor = {successor_id, successor_ip, successor_port};
    
    //     for (int i = 0; i < replication_factor; ++i)
    //     {
    //         successor_list.push_back(current_successor);
    
    //         u64 curr_id = std::get<0>(current_successor);
    //         u64 next_id = (curr_id + 1) % (1ULL << m);
    
    //         // find successor of next_id (which is the successor of curr_id)
    //         try
    //         {
    //             current_successor = find_successor(next_id);
    //         }
    //         catch(...)
    //         {
    //            // if RPC fails, fill remaining slots with self
    //               for (int j = i + 1; j < replication_factor; ++j)
    //                 successor_list.emplace_back(id, ip, port);
    
    //             break;
    //         }
            
    //     }
    // }
}

void ChordNode::recompute_finger_table()
{
    for (int k = 0; k < m; ++k)
    {
        u64 start = (id + (1ULL << k)) % (1ULL << m);
        auto [succ_id, succ_ip, succ_port] = find_successor(start);

        std::lock_guard<std::mutex> lock(finger_table_mutex);

        finger_table[k].node_id = succ_id;
        finger_table[k].node_ip = succ_ip;
        finger_table[k].node_port = succ_port;
    }
}

bool ChordNode::is_alive(std::string check_ip, int check_port)
{
    RPCResponse resp = rpc_send(check_ip, check_port, "PING");

    if (!resp.ok)
    {
        std::lock_guard<std::mutex> lock(finger_table_mutex);
        
        // Remove dead node from finger table
        for (int k = 0; k < m; ++k)
        {
            if (finger_table[k].node_ip == check_ip &&
                finger_table[k].node_port == check_port)
            {
                finger_table[k].node_id = id;
                finger_table[k].node_ip = ip;
                finger_table[k].node_port = port;
            }
        }
    }

    return resp.ok;
}

bool ChordNode::is_stable()
{
    std::lock_guard<std::mutex> lock(state_mutex);

    return stable_rounds >= STABLE_ROUNDS_REQUIRED;
}