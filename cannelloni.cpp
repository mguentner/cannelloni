/*
 * This file is part of cannelloni, a SocketCAN over Ethernet tunnel.
 *
 * Copyright (C) 2014-2017 Maximilian Güntner <code@sourcediver.org>
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

#include <iomanip>

#include <net/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <sys/signalfd.h>

#include "config.h"
#include "udpthread.h"

#ifdef SCTP_SUPPORT
#include "sctpthread.h"
#endif

#include "canthread.h"
#include "framebuffer.h"
#include "logging.h"
#include "csvmapparser.h"
#include "make_unique.h"
#include <memory>

#define CANNELLONI_VERSION "1.0.1"

using namespace cannelloni;

void printUsage() {
  std::cout << "cannelloni Release: " << CANNELLONI_VERSION << std::endl;
  std::cout << "Usage: cannelloni OPTIONS" << std::endl;
  std::cout << "Available options:" << std::endl;
#ifdef SCTP_SUPPORT
  std::cout << "\t -S ROLE \t\t enable SCTP transport." << std::endl;
  std::cout << "\t\t\t c : act as client" << std::endl;
  std::cout << "\t\t\t s : act as server" << std::endl;
#endif
  std::cout << "\t -l PORT \t\t listening port, default: 20000" << std::endl;
  std::cout << "\t -L IP   \t\t listening IP, default: 0.0.0.0" << std::endl;
  std::cout << "\t -r PORT \t\t remote port, default: 20000" << std::endl;
  std::cout << "\t -I INTERFACE \t\t can interface, default: vcan0" << std::endl;
  std::cout << "\t -t timeout \t\t buffer timeout for can messages (us), default: 100000" << std::endl;
  std::cout << "\t -T table.csv \t\t path to csv with individual timeouts" << std::endl;
  std::cout << "\t -s           \t\t enable frame sorting" << std::endl;
  std::cout << "\t -p           \t\t no peer checking" << std::endl;
  std::cout << "\t -d [cubt]\t\t enable debug, can be any of these: " << std::endl;
  std::cout << "\t\t\t c : enable debugging of can frames" << std::endl;
#ifdef SCTP_SUPPORT
  std::cout << "\t\t\t u : enable debugging of udp/sctp frames" << std::endl;
#else
  std::cout << "\t\t\t u : enable debugging of udp frames" << std::endl;
#endif
  std::cout << "\t\t\t b : enable debugging of internal buffer structures" << std::endl;
  std::cout << "\t\t\t t : enable debugging of internal timers" << std::endl;
  std::cout << "\t -h      \t\t display this help text" << std::endl;
  std::cout << "Mandatory options:" << std::endl;
  std::cout << "\t -R IP   \t\t remote IP" << std::endl;
}

int main(int argc, char** argv) {
  int opt;
  bool remoteIPSupplied = false;
  bool sortUDP = false;
  bool checkPeer = true;
  bool useSCTP = false;
#ifdef SCTP_SUPPORT
  SCTPThreadRole sctpRole = CLIENT;
#endif
  char remoteIP[INET_ADDRSTRLEN] = "127.0.0.1";
  uint16_t remotePort = 20000;
  char localIP[INET_ADDRSTRLEN] = "0.0.0.0";
  uint16_t localPort = 20000;
  std::string canInterface = "vcan0";
  uint32_t bufferTimeout = 100000;
  std::string timeoutTableFile;
  /* Key is CAN ID, Value is timeout in us */
  std::map<uint32_t, uint32_t> timeoutTable;

  struct debugOptions_t debugOptions = { /* can */ 0, /* udp */ 0, /* buffer */ 0, /* timer */ 0 };

#ifdef SCTP_SUPPORT
  const std::string argument_options = "S:l:L:r:R:I:t:T:d:hsp";
#else
  const std::string argument_options = "Sl:L:r:R:I:t:T:d:hsp";
#endif

  while ((opt = getopt(argc, argv, argument_options.c_str())) != -1) {
    switch(opt) {
#ifdef SCTP_SUPPORT
      case 'S':
        switch (optarg[0]) {
          case 's':
          case 'S':
            sctpRole = SERVER;
            useSCTP = true;
            break;
          case 'c':
          case 'C':
            sctpRole = CLIENT;
            useSCTP = true;
            break;
          default:
            std::cout << "Usage Error: " << std::endl
                      << "-S only accepts [s]erver or [c]lient" << std::endl;
            printUsage();
            return -1;
        }
        break;
#else
      case'S':
            std::cout << "Usage Error: " << std::endl
                      << "SCTP Transport is not supported in this build." << std::endl
                                                                          << std::endl;
            printUsage();
            return -1;
#endif
      case 'l':
        localPort = strtoul(optarg, NULL, 10);
        break;
      case 'L':
        strncpy(localIP, optarg, INET_ADDRSTRLEN-1);
        localIP[INET_ADDRSTRLEN-1] = '\0';
        break;
      case 'r':
        remotePort = strtoul(optarg, NULL, 10);
        break;
      case 'R':
        strncpy(remoteIP, optarg, INET_ADDRSTRLEN-1);
        remoteIP[INET_ADDRSTRLEN-1] = '\0';
        remoteIPSupplied = true;
        break;
      case 'I':
        canInterface = std::string(optarg);
        break;
      case 't':
        bufferTimeout = strtoul(optarg, NULL, 10);
        break;
      case 'T':
        timeoutTableFile = std::string(optarg);
        break;
      case 'd':
        if (strchr(optarg, 'c'))
          debugOptions.can = 1;
        if (strchr(optarg, 'u'))
          debugOptions.udp = 1;
        if (strchr(optarg, 'b'))
          debugOptions.buffer = 1;
        if (strchr(optarg, 't'))
          debugOptions.timer = 1;
        break;
      case 'h':
        printUsage();
        return 0;
      case 's':
        sortUDP = true;
        break;
      case 'p':
        checkPeer = false;
        break;
      default:
        printUsage();
        return -1;
    }
  }
#ifdef SCTP_SUPPORT
  if (!remoteIPSupplied && !(useSCTP && sctpRole == SERVER)) {

    std::cout << "Usage Error: " << std::endl
              << "Remote IP not supplied" << std::endl
                                          << std::endl;
    printUsage();
    return -1;
  }
#else
  if (!remoteIPSupplied) {
    std::cout << "Usage Error: " << std::endl
              << "Remote IP not supplied" << std::endl
                                          << std::endl;
    printUsage();
    return -1;
  }
#endif
  if (bufferTimeout == 0) {
    std::cout << "Usage Error: " << std::endl
              << "Only non-zero timeouts are allowed" << std::endl
                                                      << std::endl;
    printUsage();
    return -1;
  }

  if (!timeoutTableFile.empty()) {
    CSVMapParser<uint32_t,uint32_t> mapParser;
    if(!mapParser.open(timeoutTableFile)) {
      lerror << "Unable to open " << timeoutTableFile << "." << std::endl;
      return -1;
    }
    if(!mapParser.parse()) {
      lerror << "Error while parsing " << timeoutTableFile << "." << std::endl;
      return -1;
    }
    if(!mapParser.close()) {
      lerror << "Error while closing" << timeoutTableFile << "." << std::endl;
      return -1;
    }
    timeoutTable = mapParser.read();
  }

  if (debugOptions.timer) {
    if (timeoutTable.empty()) {
      linfo << "No custom timeout table specified, using "
            << bufferTimeout << " us for all frames." << std::endl;
    } else {
      linfo << "Custom timeout table loaded: " << std::endl;
      linfo << "*---------------------*" << std::endl;
      linfo << "|  ID  | Timeout (us) |" << std::endl;
      std::map<uint32_t,uint32_t>::iterator it;
      for (it=timeoutTable.begin(); it!=timeoutTable.end(); ++it)
        linfo << "|" << std::setw(6) << it->first << "|" << std::setw(14) << it->second << "| " << std::endl;
      linfo << "*---------------------*" << std::endl;
      linfo << "Other Frames:" << bufferTimeout << " us." << std::endl;
    }
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

  std::unique_ptr<UDPThread> netThread;
  if (useSCTP) {
#ifdef SCTP_SUPPORT
    netThread = std::make_unique<SCTPThread>(debugOptions, remoteAddr, localAddr, sortUDP, remoteIPSupplied, sctpRole);
#endif
  } else {
    netThread = std::make_unique<UDPThread>(debugOptions, remoteAddr, localAddr, sortUDP, checkPeer);
  }
  auto canThread = std::make_unique<CANThread>(debugOptions, canInterface);
  auto netFrameBuffer = std::make_unique<FrameBuffer>(1000,16000);
  auto canFrameBuffer = std::make_unique<FrameBuffer>(1000,16000);
  netThread->setPeerThread(canThread.get());
  netThread->setFrameBuffer(netFrameBuffer.get());
  netThread->setTimeoutTable(timeoutTable);
  canThread->setPeerThread(netThread.get());
  canThread->setFrameBuffer(canFrameBuffer.get());
  netThread->setTimeout(bufferTimeout);
  netThread->start();
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

  netThread->stop();
  netThread->join();
  canThread->stop();
  canThread->join();

  /* Clear/free pools once all threads are joined */
  netFrameBuffer->clearPool();
  canFrameBuffer->clearPool();

  close(signalFD);
  return 0;
}
