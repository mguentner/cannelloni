#include "inet_address.h"
#include "logging.h"
#include <arpa/inet.h>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>

bool parseAddress(const char *address_str, struct sockaddr *sock_addr, int addr_family = AF_INET) {
  if (address_str == NULL) {
    lerror << "address_str is NULL" << std::endl;
    return false;
  }
  // Try parsing as IPv4 / IPv6 address
  if (addr_family == AF_INET)  {
    struct sockaddr_in *addr = (struct sockaddr_in *)  sock_addr;
    if (inet_pton(addr_family, address_str, &addr->sin_addr) == 1) {
      addr->sin_family = AF_INET;
      return true;
    }
  } else if (addr_family == AF_INET6) {
    struct sockaddr_in6 *addr = (struct sockaddr_in6 *)  sock_addr;
    if (inet_pton(addr_family, address_str, &addr->sin6_addr) == 1) {
      addr->sin6_family = AF_INET6;
      return true;
    }
  } else {
    // unsupported addr_family, unlikely to happen
    return false;
  }

  // Resolve as DNS A/AAAA record
  struct addrinfo hints, *result, *p;
  std::memset(&hints, 0, sizeof(hints));
  hints.ai_family = addr_family;

  int status = getaddrinfo(address_str, nullptr, &hints, &result);
  if (status != 0) {
    lerror << "getaddrinfo error: " << gai_strerror(status) << std::endl;
    return false;
  }

  bool success = false;
  for (p = result; p != NULL; p = p->ai_next) {
    if (p->ai_family == addr_family) {
      std::memcpy(sock_addr, p->ai_addr, p->ai_addrlen);
      success = true;
      break;
    }
    // else: Invalid address family
  }
  freeaddrinfo(result);
  return success;
}

SocketStringAddress getSocketAddress(const struct sockaddr_storage* addr) {
    SocketStringAddress socketAddress;
    char ipString[INET6_ADDRSTRLEN];

    if (addr->ss_family == AF_INET) {
        const struct sockaddr_in* ipv4 = (const struct sockaddr_in*)addr;
        inet_ntop(AF_INET, &(ipv4->sin_addr), ipString, INET_ADDRSTRLEN);
        socketAddress.port = ntohs(ipv4->sin_port);
        socketAddress.addressFamily = AF_INET;
    } else if (addr->ss_family == AF_INET6) {
        const struct sockaddr_in6* ipv6 = (const struct sockaddr_in6*)addr;
        inet_ntop(AF_INET6, &(ipv6->sin6_addr), ipString, INET6_ADDRSTRLEN);
        socketAddress.port = ntohs(ipv6->sin6_port);
        socketAddress.addressFamily = AF_INET6;
    } else {
        socketAddress.port = 0;
        socketAddress.ipAddress = "Unsupported address family";
        socketAddress.addressFamily = AF_UNSPEC;
        return socketAddress;
    }
    socketAddress.ipAddress = std::string(ipString);
    return socketAddress;
}


std::string formatSocketAddress(const SocketStringAddress& socketAddress) {
    std::string formattedAddress;

    if (socketAddress.addressFamily == AF_INET6) {
        formattedAddress = "[" + socketAddress.ipAddress + "]";
    } else {
        formattedAddress = socketAddress.ipAddress;
    }

    formattedAddress += ":" + std::to_string(socketAddress.port);

    return formattedAddress;
}