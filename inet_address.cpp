#include "inet_address.h"
#include "logging.h"
#include <arpa/inet.h>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <netdb.h>
#include <netinet/in.h>

bool parseAddress(const char *address_str, struct sockaddr_in &sock_addr) {
  // Try parsing as IPv4 address
  if (inet_pton(AF_INET, address_str, &(sock_addr.sin_addr)) == 1) {
    sock_addr.sin_family = AF_INET;
    return true;
  }

  // Resolve as DNS A/AAAA record
  struct addrinfo hints, *result;
  std::memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;

  int status = getaddrinfo(address_str, nullptr, &hints, &result);
  if (status != 0) {
    lerror << "getaddrinfo error: " << gai_strerror(status) << std::endl;
    return false;
  }

  // Use the first resolved address
  if (result->ai_family == AF_INET) {
    std::memcpy(&sock_addr, result->ai_addr, result->ai_addrlen);
    freeaddrinfo(result);
    return true;
  } else {
    lerror << "Invalid address family" << std::endl;
    freeaddrinfo(result);
    return false;
  }
}
