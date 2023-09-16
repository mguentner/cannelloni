#pragma once
#include <arpa/inet.h>
#include <cstdint>
#include <string>

struct SocketStringAddress {
    std::string ipAddress;
    uint16_t port;
    uint8_t addressFamily;
};

bool parseAddress(const char *address_str, struct sockaddr *sock_addr, int addr_family);

SocketStringAddress getSocketAddress(const struct sockaddr_storage* addr);
std::string formatSocketAddress(const SocketStringAddress& socketAddress);