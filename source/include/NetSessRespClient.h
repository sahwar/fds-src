#ifndef __NET_SESS_RESP_CLIENT_H__
#define __NET_SESS_RESP_CLIENT_H__
#include <stdio.h>
#include <stdexcept>
#include <sstream>
#include <iostream>
#include "fds_types.h"
#include <arpa/inet.h>
#include <thrift/transport/TTransportUtils.h>
#include <thrift/concurrency/PosixThreadFactory.h>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <thrift/concurrency/ThreadManager.h>
#include <thrift/concurrency/PosixThreadFactory.h>
#include <thrift/protocol/TBinaryProtocol.h>
#include <thrift/server/TThreadPoolServer.h>
#include <thrift/transport/TSocket.h>
#include <thrift/transport/TServerSocket.h>
#include <thrift/transport/TBufferTransports.h>
#include <thrift/transport/TTransportUtils.h>

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
#include "fdsp/FDSP_DataPathReq.h"


using namespace apache::thrift;
using namespace apache::thrift::protocol;
using namespace apache::thrift::transport;
using namespace apache::thrift::concurrency;

using namespace std;
using namespace fds;
using namespace FDS_ProtocolInterface;

typedef void (*set_client_t)(boost::shared_ptr<TTransport> transport, void* context);

class FDSP_DataPathReqHandler : virtual public FDSP_DataPathReqIf {
 public:
  FDSP_DataPathReqHandler()
  {
    // Your initialization goes here
  }

  void setClient(boost::shared_ptr<TTransport> trans)
  {
    printf("FDSP_DataPathReqHandler: set DataPathRespClient\n");
    protocol_.reset(new TBinaryProtocol(trans));
    client.reset(new FDSP_DataPathRespClient(protocol_));
  }

public:
  boost::shared_ptr<TProtocol> protocol_;
  boost::shared_ptr<FDSP_DataPathRespClient> client;
};


class FdsDataPathReqProcessorFactory: public TProcessorFactory {
public:
  FdsDataPathReqProcessorFactory(const boost::shared_ptr<FDSP_DataPathReqIfFactory> &handlerFactory,
                                 set_client_t set_client_cb, void* context)
    : handler_factory(handlerFactory), set_client_hdlr(set_client_cb), context_(context) {}

  boost::shared_ptr<TProcessor> getProcessor(const TConnectionInfo& connInfo)
  {
    set_client_hdlr(connInfo.transport, context_);

    ReleaseHandler<FDSP_DataPathReqIfFactory> cleanup(handler_factory);
    boost::shared_ptr<FDSP_DataPathReqIf> handler(handler_factory->getHandler(connInfo), cleanup);
    boost::shared_ptr<TProcessor> processor(new FDSP_DataPathReqProcessor(handler));
    return processor;
  }

protected:
  boost::shared_ptr<FDSP_DataPathReqIfFactory> handler_factory;

private:
  set_client_t set_client_hdlr;
  void* context_;
};


#if 0
class FDSP_MetaDataPathReqHandler : virtual public FDSP_MetaDataPathReqIf {
 public:
  FDSP_MetaDataPathReqHandler()
  {
    // Your initialization goes here
  }

  void setClient(boost::shared_ptr<TTransport> trans)
  {
    printf("FDSP_MetaDataPathReqHandler: set DataPathRespClient\n");
    protocol_.reset(new TBinaryProtocol(trans));
    client.reset(new FDSP_DataPathRespClient(protocol_));
  }

public:
  boost::shared_ptr<TProtocol> protocol_;
  boost::shared_ptr<FDSP_MetaDataPathRespClient> client;
};


class FdsMetaDataPathReqProcessorFactory: public TProcessorFactory {
public:
  FdsMetaDataPathReqProcessorFactory(const boost::shared_ptr<FDSP_MetaDataPathReqIfFactory> &handlerFactory)
    : handler_factory(handlerFactory) {}

  boost::shared_ptr<TProcessor> getProcessor(const TConnectionInfo& connInfo)
  {
    FDSP_MetaDataPathReqHandler* ptr = dynamic_cast<FDSP_MetaDataPathReqHandler*>(handler_factory->getHandler(connInfo));
    ptr->setClient(connInfo.transport);
    

    ReleaseHandler<FDSP_MetaDataPathReqIfFactory> cleanup(handler_factory);
    boost::shared_ptr<FDSP_MetaDataPathReqIf> handler(handler_factory->getHandler(connInfo), cleanup);
    boost::shared_ptr<TProcessor> processor(new FDSP_MetaDataPathReqProcessor(handler));
    return processor;
  }

protected:
  boost::shared_ptr<FDSP_MetaDataPathReqIfFactory> handler_factory;
};

class FDSP_ControlPathReqHandler : virtual public FDSP_ControlPathReqIf {
 public:
  FDSP_ControlPathReqHandler()
  {
    // Your initialization goes here
  }

  void setClient(boost::shared_ptr<TTransport> trans)
  {
    printf("FDSP_ControlPathReqHandler: set DataPathRespClient\n");
    protocol_.reset(new TBinaryProtocol(trans));
    client.reset(new FDSP_ControlPathRespClient(protocol_));
  }

public:
  boost::shared_ptr<TProtocol> protocol_;
  boost::shared_ptr<FDSP_ControlPathRespClient> client;
};


class FdsControlPathReqProcessorFactory: public TProcessorFactory {
public:
  FdsControlPathReqProcessorFactory(const boost::shared_ptr<FDSP_ControlPathReqIfFactory> &handlerFactory)
    : handler_factory(handlerFactory) {}

  boost::shared_ptr<TProcessor> getProcessor(const TConnectionInfo& connInfo)
  {
    FDSP_ControlPathReqHandler* ptr = dynamic_cast<FDSP_ControlPathReqHandler*>(handler_factory->getHandler(connInfo));
    ptr->setClient(connInfo.transport);
    

    ReleaseHandler<FDSP_ControlPathReqIfFactory> cleanup(handler_factory);
    boost::shared_ptr<FDSP_ControlPathReqIf> handler(handler_factory->getHandler(connInfo), cleanup);
    boost::shared_ptr<TProcessor> processor(new FDSP_ControlPathReqProcessor(handler));
    return processor;
  }

protected:
  boost::shared_ptr<FDSP_ControlPathReqIfFactory> handler_factory;
};

class FDSP_ConfigPathReqHandler : virtual public FDSP_ConfigPathReqIf {
 public:
  FDSP_ConfigPathReqHandler()
  {
    // Your initialization goes here
  }

  void setClient(boost::shared_ptr<TTransport> trans)
  {
    printf("FDSP_MetaDataPathReqHandler: set DataPathRespClient\n");
    protocol_.reset(new TBinaryProtocol(trans));
    client.reset(new FDSP_ConfigPathRespClient(protocol_));
  }

public:
  boost::shared_ptr<TProtocol> protocol_;
  boost::shared_ptr<FDSP_ConfigPathRespClient> client;
};


class FdsConfigPathReqProcessorFactory: public TProcessorFactory {
public:
  FdsConfigPathReqProcessorFactory(const boost::shared_ptr<FDSP_ConfigPathReqIfFactory> &handlerFactory)
    : handler_factory(handlerFactory) {}

  boost::shared_ptr<TProcessor> getProcessor(const TConnectionInfo& connInfo)
  {
    FDSP_ConfigPathReqHandler* ptr = dynamic_cast<FDSP_ConfigPathReqHandler*>(handler_factory->getHandler(connInfo));
    ptr->setClient(connInfo.transport);
    

    ReleaseHandler<FDSP_ConfigPathReqIfFactory> cleanup(handler_factory);
    boost::shared_ptr<FDSP_ConfigPathReqIf> handler(handler_factory->getHandler(connInfo), cleanup);
    boost::shared_ptr<TProcessor> processor(new FDSP_ConfigPathReqProcessor(handler));
    return processor;
  }

protected:
  boost::shared_ptr<FDSP_ConfigPathReqIfFactory> handler_factory;
};
#endif
#endif
