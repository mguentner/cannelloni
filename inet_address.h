#pragma once
#include <arpa/inet.h>

bool parseAddress(const char *address_str, struct sockaddr_in &sock_addr);