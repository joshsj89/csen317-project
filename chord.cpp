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
    for (int i = 0; i < m; ++i)
    {
        finger_table[i].start = (id + (1ULL << i)) % (1ULL << m);
        finger_table[i].node = id;
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

    // Predecessor will be set during stabilization
}

void ChordNode::notify(u64 nid, std::string nip, int nport)
{
    if (predecessor_ip.empty() ||
        (nid > predecessor_id && nid < id))
    {
        predecessor_id = nid;
        predecessor_ip = nip;
        predecessor_port = nport;
    }
}

std::tuple<u64, std::string, int> ChordNode::find_successor(u64 id)
{
    return {successor_id, successor_ip, successor_port};
}