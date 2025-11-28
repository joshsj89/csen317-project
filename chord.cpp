#include <chrono>
#include <iostream>
#include <random>
#include "chord.hpp"
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
        return;

    // contact bootstrap node to find my successor
    // (network communication code would go here)
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