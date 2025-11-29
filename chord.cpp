#include <chrono>
#include <iostream>
#include <random>
#include <sstream>
#include "chord.hpp"
#include "rpc.hpp"
#include "utils.hpp"

ChordNode::ChordNode(std::string ip_address, int port_number, int m_bits)
    : ip(ip_address), port(port_number), m(m_bits)
{
    id = hash_str(ip + ":" + std::to_string(port)) % (1ULL << m);

    predecessor_id = id;
    predecessor_ip = ip;
    predecessor_port = port;

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
}

void ChordNode::join(std::string bootstrap_ip, int bootstrap_port)
{
    // first node in the network
    if (bootstrap_ip == ip && bootstrap_port == port)
    {
        predecessor_id = id;
        predecessor_ip = ip;
        predecessor_port = port;

        successor_id = id;
        successor_ip = ip;
        successor_port = port;

        std::cout << "[join] First node in the network." << std::endl;
        return;
    }

    // Joining an existing network
    std::cout << "[join] Contacting bootstrap node " << bootstrap_ip << ":" << bootstrap_port << "..." << std::endl;

    std::string msg = "FIND_SUCCESSOR " + std::to_string(id);
    RPCResponse resp = rpc_send(bootstrap_ip, bootstrap_port, msg);

    if (!resp.ok)
    {
        std::cerr << "[join] Failed to join the network: " << resp.body << std::endl;
        return;
    }

    u64 succ_id;
    std::string succ_ip;
    int succ_port;
    std::istringstream iss(resp.body);
    iss >> succ_id >> succ_ip >> succ_port;

    if (iss.fail())
    {
        std::cerr << "[join] Invalid FIND_SUCCESSOR response: " << resp.body << std::endl;
        return;
    }

    successor_id = succ_id;
    successor_ip = succ_ip;
    successor_port = succ_port;

    std::cout << "[join] Joined the network. Successor: " << successor_id << " (" << successor_ip << ":" << successor_port << ")" << std::endl;

    // Notify the successor so it can update its predecessor immediately
    try
    {
        std::string notify_msg = "NOTIFY " + std::to_string(id) + " " + ip + " " + std::to_string(port);
        rpc_send(successor_ip, successor_port, notify_msg);
    } catch (...)
    {
        // ignore notify failures; stabilization will recover
    }

    // Predecessor will also be set/validated during stabilization
    // Optionally recompute fingers to speed convergence
    // recompute_finger_table();
}

void ChordNode::leave()
{
    isLeaving = true;

    if (successor_id != id)
    {
        // Transfer keys to successor
        // for (auto &kv : data)
        // {
        //     std::string msg = "STORE " + kv.first + " " + kv.second;
        //     rpc_send(successor_ip, successor_port, msg);
        // }

        // Tell successor to update its predecessor
        std::string notify_msg = "SET_PREDECESSOR " + std::to_string(predecessor_id) + " " + predecessor_ip + " " + std::to_string(predecessor_port);
        rpc_send(successor_ip, successor_port, notify_msg);
    }

    if (predecessor_id != id)
    {
        // Tell predecessor to update its successor
        std::string notify_msg = "SET_SUCCESSOR " + std::to_string(successor_id) + " " + successor_ip + " " + std::to_string(successor_port);
        rpc_send(predecessor_ip, predecessor_port, notify_msg);
    }

    RUNNING = false; // stop RPC listener

    std::cout << "[leave] Node " << id << " (" << ip << ":" << port << ") is gracefully leaving the network." << std::endl;
}

void ChordNode::notify(u64 nid, std::string nip, int nport)
{
    if (predecessor_id == id ||
        in_interval_open(predecessor_id, id, nid))
    {
        predecessor_id = nid;
        predecessor_ip = nip;
        predecessor_port = nport;
    }
}

void ChordNode::stabilize()
{
    // don't stabilize if node is leaving
    if (isLeaving)
        return;

    // Get successor's predecessor
    std::string msg = "GET_PREDECESSOR";
    RPCResponse resp = rpc_send(successor_ip, successor_port, msg);

    // If successor failed to respond, we could try to find the next alive node
    if (!resp.ok)
    {
        for (int k = 0; k < m; ++k)
        {
            if (finger_table[k].node_id == id)
                continue;

            if (is_alive(finger_table[k].node_ip, finger_table[k].node_port) == false)
            {
                successor_id = id;
                successor_ip = ip;
                successor_port = port;

                break;
            }
        }

        // If no fingers are alive, point to self
        if (!is_alive(successor_ip, successor_port))
        {
            successor_id = id;
            successor_ip = ip;
            successor_port = port;

            return;
        }

        // Notify successor
        std::string notify_msg = "NOTIFY " + std::to_string(id) + " " + ip + " " + std::to_string(port);
        rpc_send(successor_ip, successor_port, notify_msg);
    }

    // Parse successor's predecessor info
    u64 succ_pred_id = 0;
    std::string succ_pred_ip;
    int succ_pred_port = 0;

    std::istringstream iss(resp.body);
    iss >> succ_pred_id >> succ_pred_ip >> succ_pred_port;
    if (iss.fail()) // invalid response
    {
        std::string notify_msg = "NOTIFY " + std::to_string(id) + " " + ip + " " + std::to_string(port);
        rpc_send(successor_ip, successor_port, notify_msg);
        
        return;
    }

    // Check if successor's predecessor is a better successor
    if (succ_pred_id != 0 &&
        in_interval_open(id, successor_id, succ_pred_id))
    {
        successor_id = succ_pred_id;
        successor_ip = succ_pred_ip;
        successor_port = succ_pred_port;
    }

    // Notify successor that we might be its predecessor
    std::string notify_msg = "NOTIFY " + std::to_string(id) + " " + ip + " " + std::to_string(port);
    rpc_send(successor_ip, successor_port, notify_msg);
}

std::tuple<u64, std::string, int> ChordNode::find_successor(u64 id)
{
    // Check if id is in (this->id, successor_id]
    if (in_interval_inclusive(this->id, successor_id, id))
    {
        return {successor_id, successor_ip, successor_port};
    }
    
    // otherwise pick the closest preceding finger
    auto [next_id, next_ip, next_port] = closest_preceding_finger(id);

    // RPC to next node to find successor
    std::string msg = "FIND_SUCCESSOR " + std::to_string(id);
    RPCResponse resp = rpc_send(next_ip, next_port, msg);

    if (!resp.ok)
        return {successor_id, successor_ip, successor_port}; // fallback to current successor

    u64 succ_id;
    std::string succ_ip;
    int succ_port;
    std::istringstream iss(resp.body);
    iss >> succ_id >> succ_ip >> succ_port;

    if (iss.fail())
        throw std::runtime_error("Invalid FIND_SUCCESSOR response: " + resp.body);

    return {succ_id, succ_ip, succ_port};
}

std::tuple<u64, std::string, int> ChordNode::closest_preceding_finger(u64 id)
{
    for (int k = m - 1; k >= 0; --k)
    {
        u64 f_id = finger_table[k].node_id;
        if (in_interval_open(this->id, id, f_id))
        {
            if (is_alive(finger_table[k].node_ip, finger_table[k].node_port))
                return {finger_table[k].node_id, finger_table[k].node_ip, finger_table[k].node_port};
        }
    }

    // If no finger found, return self
    return {this->id, this->ip, this->port};
}

void ChordNode::fix_fingers()
{
    static int next_finger = 0;
    u64 start = finger_table[next_finger].start;

    auto [finger_id, finger_ip, finger_port] = find_successor(start);
    finger_table[next_finger].node_id = finger_id;
    finger_table[next_finger].node_ip = finger_ip;
    finger_table[next_finger].node_port = finger_port;

    next_finger = (next_finger + 1) % m;
}

void ChordNode::recompute_finger_table()
{
    for (int k = 0; k < m; ++k)
    {
        u64 start = (id + (1ULL << k)) % (1ULL << m);
        auto [succ_id, succ_ip, succ_port] = find_successor(start);
        finger_table[k].node_id = succ_id;
        finger_table[k].node_ip = succ_ip;
        finger_table[k].node_port = succ_port;
    }
}

bool ChordNode::is_alive(std::string check_ip, int check_port)
{
    RPCResponse resp = rpc_send(check_ip, check_port, "PING");
    return resp.ok;
}