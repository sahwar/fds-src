#ifndef __Net_Session_h__
#define __Net_Session_h__
#include <concurrency/Mutex.h>
#include <stdio.h>
#include <iostream>
#include <fds_types.h>
#include <arpa/inet.h>
#include <thrift/concurrency/ThreadManager.h>
#include <thrift/concurrency/PosixThreadFactory.h>
#include <thrift/protocol/TBinaryProtocol.h>
#include <thrift/server/TThreadPoolServer.h>
#include <thrift/transport/TServerSocket.h>
#include <thrift/transport/TBufferTransports.h>
#include <thrift/transport/TTransportUtils.h>
#include <fdsp/FDSP_DataPathReq.h>
#include <fdsp/FDSP_DataPathResp.h>
#include <fdsp/FDSP_MetaDataPathReq.h>
#include <fdsp/FDSP_MetaDataPathResp.h>
#include <NetSessRespClient.h>
#include <NetSessRespSvr.h>


using namespace ::apache::thrift;
using namespace ::apache::thrift::protocol;
using namespace ::apache::thrift::transport;
using namespace ::apache::thrift::concurrency;

using namespace FDS_ProtocolInterface;
using namespace std;
using namespace fds;

typedef void  (*sessionErrorCallback)(string ip_addr, 
                                      FDSP_MgrIdType mgrId, 
                                      int channel, int errno, 
                                      std::string errMsg); 

class netSession {
public:
        netSession();
	netSession(const std::string& node_name, int port, 
                         FDS_ProtocolInterface::FDSP_MgrIdType local_mgr_id,
                         FDS_ProtocolInterface::FDSP_MgrIdType remote_mgr_id);
	netSession(int  ip_addr_int, int port,
                         FDS_ProtocolInterface::FDSP_MgrIdType local_mgr_id,
                         FDS_ProtocolInterface::FDSP_MgrIdType remote_mgr_id);
        static string ipAddr2String(int ipaddr);
        static int ipString2Addr(string ipaddr_str);
        void setSessionErrHandler(sessionErrorCallback cback);

        ~netSession();
    	void     endSession();

	int 		node_index;
	int 		channel_number;
	short   	proto_type;
	std::string 	ip_addr_str;
        int 		ip_addr;
	fds_uint32_t    port_num;
        int role; // Server or Client side binding
        FDS_ProtocolInterface::FDSP_MgrIdType mgrId;
        sessionErrorCallback cback;

        netSession& operator=(const netSession& rhs) {
          if (this != &rhs) {
            node_index = rhs.node_index;
            proto_type = rhs.proto_type;
            ip_addr_str = rhs.ip_addr_str;
            ip_addr    = rhs.ip_addr;
            mgrId      = rhs.mgrId;
          }
          return *this;
        }
};

class netClientSession : virtual public netSession { 

public:
        boost::shared_ptr<TTransport> socket;
        boost::shared_ptr<TTransport> transport;
        boost::shared_ptr<TProtocol> protocol;
        boost::shared_ptr<TThreadPoolServer> server;

        netClientSession(string node_name, int port, FDSP_MgrIdType local_mgr,
                         FDSP_MgrIdType remote_mgr) : netSession(node_name, port, local_mgr, remote_mgr) {
            socket.reset(new TSocket(node_name, port));
            transport.reset( new TBufferedTransport(socket));
            protocol.reset( new TBinaryProtocol(transport));
        }

        ~netClientSession() {
           delete protocol;
           delete transport;
           delete socket;
        }
};

class netDataPathClientSession : public netClientSession { 
public :
	FDSP_DataPathReqClient fdspDPAPI;
        int num_threads;
        boost::shared_ptr<FDSP_DataPathRespIf> fdspDataPathResp;
        boost::shared_ptr<Thread> th;
        boost::shared_ptr<fdspDataPathRespReceiver> msg_recv; 
        shared_ptr<TProcessor> processor;

        netDataPathClientSession(string ip_addr_str, 
                                 int port, int num_threads, void *respSvrObj) 
                                 : netClientSession(ip_addr_str, port, FDSP_STOR_HVISOR, FDSP_STOR_MGR)  { 
           fdspDPAPI = new FDSP_DataPathReqClient;
           fdspDataPathResp.reset(reinterpret_cast<FDSP_DataPathRespIf *> respSvrObj);
            processor.reset(new FDSP_DataPathRespProcessor(fdspDataPathResp));
            PosixThreadFactory threadFactory(PosixThreadFactory::ROUND_ROBIN,
                                   PosixThreadFactory::NORMAL,
                                   num_threads,
                                   false);
            msg_recv = new fdspDataPathRespReceiver(protocol, fdspDataPathResp);
            th = threadFactory.newThread(msg_recv);
            th->start();
           transport->open();
        }
        ~netDataPathClientSession() {
           transport->close();
        }
};

class netServerSession: public netSession { 
public :
  boost::shared_ptr<TServerTransport> serverTransport;
  boost::shared_ptr<TTransportFactory> transportFactory;
  boost::shared_ptr<TProtocolFactory> protocolFactory;
  boost::shared_ptr<ThreadManager> threadManager;
  boost::shared_ptr<PosixThreadFactory> threadFactory;

  netServerSession(int num_threads) { 
       serverTransport  = new TServerSocket(port);
       transportFactory = new TBufferedTransportFactory();
       protocolFactory = new TBinaryProtocolFactory();


       threadManager = ThreadManager::newSimpleThreadManager(num_threads);
       boost::shared_ptr<PosixThreadFactory> threadFactory =
       boost::shared_ptr<PosixThreadFactory>(new PosixThreadFactory());
       threadManager->threadFactory(threadFactory);
       threadManager->start();
  }


  ~netServerSession() {
  }
};

class netDataPathServerSession : public netServerSession { 
public:
        boost::shared_ptr<FDSP_DataPathReqIf> handler;
        boost::shared_ptr<FDSP_DataPathReqIfSingletonFactory> handlerFactory; 
        boost::shared_ptr<TProcessorFactory> processorFactory;
        boost::shared_ptr<TThreadPoolServer> server;
        boost::shared_ptr<TProtocol> protocol_;
        boost::shared_ptr<FDSP_DataPathRespClient> client;

        netDataPathServerSession(const boost::shared_ptr<FDSP_DataPathReqIf& resp_iface) {
          handler.reset(resp_iface);
          handlerFactory.reset(new FDSP_DataPathReqIfSingletonFactory(handler));
          processorFactory.reset(new FdsDataPathReqProcessorFactory(handlerFactory, setClient, this));
        }
 
       // Called from within thrift and the right context is passed - nothing to do in the application modules of thrift
       static void setClient(boost::shared_ptr<TTransport> transport, void* context) {
        printf("netSessionServer: set DataPathRespClient\n");
        netDataPathServerSession* self = reinterpret_cast<netDataPathServerSession *>(context);
        self->setClientInternal(transport);
      }

      void setClientInternal(boost::shared_ptr<TTransport> transport) {
        printf("netSessionServer internal: set DataPathRespClient\n");
        protocol_.reset(new TBinaryProtocol(transport));
        client.reset(new FDSP_DataPathRespClient(protocol_));
      }

      boost::shared_ptr<FDSP_DataPathRespClient> getClient() {
        return client;
      }

      void listenServer() { 

          server.reset(new TThreadPoolServer (processorFactory,
                                          serverTransport,
                                          transportFactory,
                                          protocolFactory,
                                          threadManager));

          printf("Starting the server...\n");
          server->serve();
      }

       ~netDataPathServerSession() {
       }

};


inline std::ostream& operator<<(std::ostream& out, const netSession& ep) {
  out << "Network endpoint ";
  if (ep.mgrId == FDSP_DATA_MGR) {
    out << "DM";
  } else if (ep.mgrId == FDSP_STOR_MGR) {
    out << "SM";
  } else if (ep.mgrId == FDSP_ORCH_MGR) {
    out << "OM";
  } else {
    assert(ep.mgrId == FDSP_STOR_HVISOR);
    out << "SH";
  }
  out << " with IP " << ep.ip_addr
      << " and port " << ep.port_num;
  return out;
}

class netSessionTbl {
public :
    netSessionTbl(std::string _src_node_name, int _src_ipaddr, int _port, int _num_threads) :
        src_node_name(_src_node_name), src_ipaddr(_src_ipaddr), port(_port), num_threads(_num_threads) {
    }

    netSessionTbl() {
        sessionTblMutex = new fds_mutex("RPC Tbl mutex"); 
     };
    ~netSessionTbl();
    
    int src_ipaddr;
    std::string src_node_name;
    int port;
    int num_threads;
    FDSP_MgrIdType localMgrId;
    FDSP_MgrIdType remoteMgrId;

   // Server Side Local variables 

    boost::shared_ptr<ThreadManager> threadManager;
    boost::shared_ptr<PosixThreadFactory> threadFactory;

    std::unordered_map<std::string dest_name_key, netSession *>    sessionTbl;
    fds_mutex   *sessionTblMutex;

    netSession* setupClientSession(std::string dest_node_name, 
                             int port, 
                             FDS_ProtocolInterface::FDSP_MgrIdType local_mgr_id,
                             FDS_ProtocolInterface::FDSP_MgrIdType remote_mgr_id) ;

    netSession* setupServerSession(std::string dest_node_name, 
                             int port, 
                             FDS_ProtocolInterface::FDSP_MgrIdType local_mgr_id,
                             FDS_ProtocolInterface::FDSP_MgrIdType remote_mgr_id) ;

// Client Procedures
    netSession*       startSession(int  dst_ipaddr, int port, 
                                   FDSP_MgrIdType mgr_id, int num_channels, void *respSvrObj);

    netSession*       startSession(std::string dst_node_name, 
                                   int port, FDSP_MgrIdType mgr_id, 
                                   int num_channels, void *respSvr);

    void 	      endSession(int  dst_ip_addr, FDSP_MgrIdType);

    void 	      endSession(string  dst_node_name, FDSP_MgrIdType);

    void 	      endSession(netSession *);

// client side getSession
    netSession*       getSession(int dst_ip_addr, FDSP_MgrIdType mgrId);

    netSession*       getSession(string dst_node_name, FDSP_MgrIdType mgrId);

// Server side getSession
    netSession*       getServerSession(std::string dst_node_name, FDSP_MgrIdType mgrId);

    netSession*       getServerSession(std::string dst_node_name, FDSP_MgrIdType mgrId);
   
// Server Procedures
    // Create a new server session, pass in the remote_mgr_id that this service/server provides for
    netSession*       createServerSession(int  local_ipaddr, 
                                          int port, 
                                          std::string local_node_name,
                                          FDSP_MgrIdType remote_mgr_id, 
                                          void *respHandlerObj);

// Blocking call equivalent to .run or .serve
    void              listenServer(netSession* server_session);

    void              endServerSession(netSession *server_session );
};

#endif
