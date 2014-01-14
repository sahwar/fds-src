#ifndef __NET_SESS_RESP_SVR_H__
#define __NET_SESS_RESP_SVR_H__
#include <arpa/inet.h>
#include "fdsp/FDSP_ConfigPathReq.h"
#include "fdsp/FDSP_constants.h"
#include "fdsp/FDSP_ControlPathResp.h"
#include "fdsp/FDSP_DataPathResp.h"
#include "fdsp/FDSP_MetaDataPathResp.h"
#include "fdsp/FDSP_types.h"
#include "fdsp/FDSP_ControlPathReq.h"
#include "fdsp/FDSP_MetaDataPathReq.h"
#include "fdsp/FDSP_SessionReq.h"
#include "fdsp/FDSP_DataPathReq.h"
#include <thrift/concurrency/ThreadManager.h>
#include <thrift/concurrency/PosixThreadFactory.h>
#include <thrift/protocol/TBinaryProtocol.h>
#include <thrift/server/TThreadPoolServer.h>
#include <thrift/transport/TServerSocket.h>
#include <thrift/transport/TBufferTransports.h>
#include <thrift/transport/TTransportUtils.h>

#include <stdexcept>
#include <sstream>
#include <iostream>

using namespace ::apache::thrift;
using namespace ::apache::thrift::protocol;
using namespace ::apache::thrift::transport;
using namespace ::apache::thrift::server;
using namespace ::apache::thrift::concurrency;

using namespace FDS_ProtocolInterface;
using namespace std;
using namespace fds;

class fdspDataPathRespReceiver: public Runnable {
public:
  fdspDataPathRespReceiver(boost::shared_ptr<TProtocol> prot,
                   boost::shared_ptr<FDSP_DataPathRespIf> hdlr)
    : prot_(prot),
      processor_(new FDSP_DataPathRespProcessor(hdlr))
  {
  }

  void run() {
    /* wait for connection to be established */
    /* for now just busy wait */
    while (!prot_->getTransport()->isOpen()) {
      printf("fdspDataPathRespReceiver: waiting for transport...\n");
      usleep(500);
    }

    try {
      for (;;) {
        if (!processor_->process(prot_, prot_, NULL) ||
            !prot_->getTransport()->peek()) {
          break;
        }
      }
    }
    catch (TException& tx) {
      printf("fdspDataPathResponse Receiver exception");
    }
  }


public:
  boost::shared_ptr<TProtocol> prot_;
  boost::shared_ptr<TProcessor> processor_;
};

class fdspMetaDataPathRespReceiver: public Runnable {
public:
  fdspMetaDataPathRespReceiver(boost::shared_ptr<TProtocol> prot,
                   boost::shared_ptr<FDSP_MetaDataPathRespIf> hdlr)
    : prot_(prot),
      processor_(new FDSP_MetaDataPathRespProcessor(hdlr))
  {
  }

  void run() {
    /* wait for connection to be established */
    /* for now just busy wait */
    while (!prot_->getTransport()->isOpen()) {
      printf("fdspMetaDataPathRespReceiver: waiting for transport...\n");
      usleep(500);
    }

    try {
      for (;;) {
        if (!processor_->process(prot_, prot_, NULL) ||
            !prot_->getTransport()->peek()) {
          break;
        }
      }
    }
    catch (TException& tx) {
      printf("fdspMetaDataPathResponse Receiver exception");
    }
  }


public:
  boost::shared_ptr<TProtocol> prot_;
  boost::shared_ptr<TProcessor> processor_;
};


class fdspControlPathRespReceiver: public Runnable {
public:
  fdspControlPathRespReceiver(boost::shared_ptr<TProtocol> prot,
                   boost::shared_ptr<FDSP_ControlPathRespIf> hdlr)
    : prot_(prot),
      processor_(new FDSP_ControlPathRespProcessor(hdlr))
  {
  }

  void run() {
    /* wait for connection to be established */
    /* for now just busy wait */
    while (!prot_->getTransport()->isOpen()) {
      printf("fdspControlPathRespReceiver: waiting for transport...\n");
      usleep(500);
    }

    try {
      for (;;) {
        if (!processor_->process(prot_, prot_, NULL) ||
            !prot_->getTransport()->peek()) {
          break;
        }
      }
    }
    catch (TException& tx) {
      printf("fdspControlPathResponse Receiver exception");
    }
  }

public:
  boost::shared_ptr<TProtocol> prot_;
  boost::shared_ptr<TProcessor> processor_;
};

#if 0
class fdspConfigPathRespReceiver: public Runnable {
public:
  fdspConfigPathRespReceiver(boost::shared_ptr<TProtocol> prot,
                   boost::shared_ptr<FDSP_ConfigPathRespIf> hdlr)
    : prot_(prot),
      processor_(new fdspConfigPathRespProcessor(hdlr))
  {
  }

  void run() {
    /* wait for connection to be established */
    /* for now just busy wait */
    while (!prot_->getTransport()->isOpen()) {
      printf("fdspConfigPathRespReceiver: waiting for transport...\n");
      usleep(500);
    }

    try {
      for (;;) {
        if (!processor_->process(prot_, prot_, NULL) ||
            !prot_->getTransport()->peek()) {
          break;
        }
      }
    }
    catch (TException& tx) {
      printf("fdspConfigPathResponse Receiver exception");
    }
  }

public:
  boost::shared_ptr<TProtocol> prot_;
  boost::shared_ptr<TProcessor> processor_;
};
#endif

#endif
