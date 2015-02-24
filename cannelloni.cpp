/*
 * This file is part of cannelloni, a SocketCAN over Ethernet tunnel.
 *
 * Copyright (C) 2014-2015 Maximilian Güntner <maximilian.guentner@gmail.com>
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

#include "connection.h"
#include "framebuffer.h"
#include "logging.h"
#include "csvmapparser.h"

#define CANNELLONI_VERSION 0.4

using namespace cannelloni;

void printUsage() {
  std::cout << "cannelloni " << CANNELLONI_VERSION << std::endl;
  std::cout << "Usage: cannelloni OPTIONS" << std::endl;
  std::cout << "Available options:" << std::endl;
  std::cout << "\t -l PORT \t\t listening port, default: 20000" << std::endl;
  std::cout << "\t -L IP   \t\t listening IP, default: 0.0.0.0" << std::endl;
  std::cout << "\t -r PORT \t\t remote port, default: 20000" << std::endl;
  std::cout << "\t -I INTERFACE \t\t can interface, default: vcan0" << std::endl;
  std::cout << "\t -t timeout \t\t buffer timeout for can messages (us), default: 100000" << std::endl;
  std::cout << "\t -T table.csv \t\t path to csv with individual timeouts" << std::endl;
  std::cout << "\t -s           \t\t disable frame sorting" << std::endl;
  std::cout << "\t -d [cubt]\t\t enable debug, can be any of these: " << std::endl;
  std::cout << "\t\t\t c : enable debugging of can frames" << std::endl;
  std::cout << "\t\t\t u : enable debugging of udp frames" << std::endl;
  std::cout << "\t\t\t b : enable debugging of interal buffer structures" << std::endl;
  std::cout << "\t\t\t t : enable debugging of interal timers" << std::endl;
  std::cout << "\t -h      \t\t display this help text" << std::endl;
  std::cout << "\t -R IP   \t\t remote IP" << std::endl;
  std::cout << "\t -c seconds \t\t client timeout Second if remote Ip not supplied" << std::endl;
}

int main(int argc, char** argv) {
  int opt;
  bool remoteIPSupplied = false;
  bool sortUDP = true;
  char remoteIP[INET_ADDRSTRLEN] = "127.0.0.1";
  uint16_t remotePort = 20000;
  char localIP[INET_ADDRSTRLEN] = "0.0.0.0";
  uint16_t localPort = 20000;
  std::string canInterface = "vcan0";
  uint32_t bufferTimeout = 100000;
  std::string timeoutTableFile;
  /* Key is CAN ID, Value is timeout in us */
  std::map<uint32_t, uint32_t> timeoutTable;
  uint32_t clientTimeout = 0;

  struct debugOptions_t debugOptions = { /* can */ 0, /* udp */ 0, /* buffer */ 0, /* timer */ 0 };

  while ((opt = getopt(argc, argv, "l:L:r:R:I:t:T:d:c:hs")) != -1) {
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
      case 'c':
        clientTimeout =  strtoul(optarg, NULL, 10);
		break;
      case 'h':
        printUsage();
        return 0;
      case 's':
        sortUDP = false;
        break;
      default:
        printUsage();
        return -1;
    }
  }
  if (!remoteIPSupplied) {
    std::cout << "Remote IP not supplied , will be bound to the first incoming connection" << std::endl;
  }
  if (!remoteIPSupplied && clientTimeout == 0) {
    std::cout << "Remote IP not selected an inactivity connection either. Timeout will be set to default" << std::endl;
  }
  if (bufferTimeout == 0) {
    std::cout << "Error: Only non-zero timeouts are allowed" << std::endl;
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
      linfo << "No custom timeout table specified, using " << bufferTimeout << " us for all frames." << std::endl;
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
  if (remoteIPSupplied) {
    inet_pton(AF_INET, remoteIP, &remoteAddr.sin_addr);
  } else {
    /*ip not supplied, set to any*/
    remoteAddr.sin_addr.s_addr  = INADDR_ANY;
  }

  localAddr.sin_family = AF_INET;
  localAddr.sin_port = htons(localPort);
  inet_pton(AF_INET, localIP, &localAddr.sin_addr);

  UDPThread *udpThread = new UDPThread(debugOptions, remoteAddr, localAddr, sortUDP);
  if (!remoteIPSupplied) {
    if ( ! udpThread->setClientConnectionTimeoutSec(clientTimeout)) {
      return -1;
    }
  }
  CANThread *canThread = new CANThread(debugOptions, canInterface);
  FrameBuffer *udpFrameBuffer = new FrameBuffer(1000,16000);
  FrameBuffer *canFrameBuffer = new FrameBuffer(1000,16000);
  udpThread->setCANThread(canThread);
  udpThread->setFrameBuffer(udpFrameBuffer);
  udpThread->setTimeoutTable(timeoutTable);
  canThread->setUDPThread(udpThread);
  canThread->setFrameBuffer(canFrameBuffer);
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
  udpThread->join();
  canThread->stop();
  canThread->join();

  delete udpThread;
  delete udpFrameBuffer;
  delete canThread;
  delete canFrameBuffer;
  close(signalFD);
  return 0;
}
