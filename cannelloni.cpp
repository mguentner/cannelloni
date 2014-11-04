/*
 * This file is part of cannelloni, a SocketCAN over Ethernet tunnel.
 *
 * Copyright (C) 2014 Maximilian Güntner <maximilian.guentner@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/signalfd.h>
#include <netinet/in.h>
#include <net/if.h>
#include <arpa/inet.h>

#include "connection.h"
#include "logging.h"

#define CANNELLONI_VERSION 0.1

using namespace cannelloni;

void printUsage() {
  std::cout << "cannelloni " << CANNELLONI_VERSION << std::endl;
  std::cout << "Usage: cannelloni OPTIONS" << std::endl;
  std::cout << "Available options:" << std::endl;
  std::cout << "\t -l PORT \t\t listening port, default: 20000" << std::endl;
  std::cout << "\t -L IP   \t\t listening IP, default: 0.0.0.0" << std::endl;
  std::cout << "\t -r PORT \t\t remote port, default: 20000" << std::endl;
  std::cout << "\t -I INTERFACE \t\t can interface, default: vcan0" << std::endl;
  std::cout << "\t -t timeout \t\t buffer timeout for can messages (ms), default: 100" << std::endl;
  std::cout << "\t -h      \t\t display this help text" << std::endl;
  std::cout << "Mandatory options:" << std::endl;
  std::cout << "\t -R IP   \t\t remote IP" << std::endl;
}

int main(int argc, char** argv) {
  int opt;
  bool remoteIPSupplied = false;
  char remoteIP[INET_ADDRSTRLEN] = "127.0.0.1";
  uint16_t remotePort = 20001;
  char localIP[INET_ADDRSTRLEN] = "0.0.0.0";
  uint16_t localPort = 20000;
  std::string canInterface = "vcan0";
  uint32_t bufferTimeout = 100;

  while ((opt = getopt(argc, argv, "l:L:r:R:I:t:h")) != -1) {
    switch(opt) {
      case 'l':
        localPort = strtoul(optarg, NULL, 10);
        break;
      case 'L':
        strncpy(localIP, optarg, INET_ADDRSTRLEN);
        break;
      case 'r':
        remotePort = strtoul(optarg, NULL, 10);
        break;
      case 'R':
        strncpy(remoteIP, optarg, INET_ADDRSTRLEN);
        remoteIPSupplied = true;
        break;
      case 'I':
        canInterface = std::string(optarg);
        break;
      case 't':
        bufferTimeout = strtoul(optarg, NULL, 10);
        break;
      case 'h':
        printUsage();
        return 0;
      default:
        printUsage();
        return -1;
    }
  }
  if (!remoteIPSupplied) {
    std::cout << "Error: Remote IP not supplied" << std::endl;
    printUsage();
    return -1;
  }

  struct sockaddr_in remoteAddr;
  struct sockaddr_in localAddr;
  /* We use the signalfd() system call to create a
   * file descriptor to receive signals */
  sigset_t signalMask;
  struct signalfd_siginfo signalFdInfo;
  int signalFD;

  /* Prepare the signalMask */
  sigemptyset(&signalMask);
  sigaddset(&signalMask, SIGTERM);
  sigaddset(&signalMask, SIGINT);
  /* Block these signals... */
  if (sigprocmask(SIG_BLOCK, &signalMask, NULL) == -1) {
    lerror << "sigprocmask error" << std::endl;
    return -1;
  }
  /* ...since we want to receive them through signalFD */
  signalFD = signalfd(-1, &signalMask, 0);
  if (signalFD == -1) {
    lerror << "signalfd error" << std::endl;
    return -1;
  }

  bzero(&remoteAddr, sizeof(sockaddr_in));
  bzero(&localAddr, sizeof(sockaddr_in));

  remoteAddr.sin_family = AF_INET;
  remoteAddr.sin_port = htons(remotePort);
  inet_pton(AF_INET, remoteIP, &remoteAddr.sin_addr);

  localAddr.sin_family = AF_INET;
  localAddr.sin_port = htons(localPort);
  inet_pton(AF_INET, localIP, &localAddr.sin_addr);

  UDPThread *udpThread = new UDPThread(remoteAddr, localAddr);
  CANThread *canThread = new CANThread(canInterface);
  udpThread->setCANThread(canThread);
  canThread->setUDPThread(udpThread);
  udpThread->setTimeout(bufferTimeout);
  udpThread->start();
  canThread->start();
  while (1) {
    ssize_t receivedBytes = read(signalFD, &signalFdInfo, sizeof(struct signalfd_siginfo));
    if (receivedBytes != sizeof(struct signalfd_siginfo)) {
      lerror << "signalfd read error" << std::endl;
      break;
    }
    /* Currently we only receive SIGTERM and SIGINT but we check nonetheless */
    if (signalFdInfo.ssi_signo == SIGTERM || signalFdInfo.ssi_signo == SIGINT) {
      linfo << "Received signal " << signalFdInfo.ssi_signo << ": Exiting" << std::endl;
      break;
    }
  }

  udpThread->stop();
  canThread->stop();

  delete udpThread;
  delete canThread;
  close(signalFD);
  return 0;
}
