#include <Ice/Ice.h>
#include "FDSP.h"
#include <ObjectFactory.h>
#include <Ice/ObjectFactory.h>
#include <Ice/BasicStream.h>
#include <Ice/Object.h>
#include <IceUtil/Iterator.h>
#include "StorHvisorNet.h"
#include "list.h"
#include "StorHvisorCPP.h"
#include <arpa/inet.h>

using namespace std;
using namespace FDS_ProtocolInterface;
using namespace Ice;

extern StorHvCtrl *storHvisor;
int vvc_entry_update(vvc_vhdl_t vhdl, const char *blk_name, int num_segments, const doid_t **doid_list);

void StorHvCtrl::fbd_process_req_timeout(unsigned long arg)
{
  int trans_id;
  StorHvJournalEntry *txn;
  fbd_request_t *req;

  trans_id = (int)arg;
  txn = journalTbl->get_journal_entry(trans_id);
  StorHvJournalEntryLock je_lock(txn);
  if (txn->isActive()) {
    req = (fbd_request_t *)txn->write_ctx;
    if (req) { 
      txn->write_ctx = 0;
      printf(" Timing out, responding to  the block : %p \n ",req);
      txn->fbd_complete_req(req, -1);
    }
    txn->reset();
    journalTbl->release_trans_id(trans_id);
  }
  
}

int StorHvCtrl::fds_process_get_obj_resp(const FDSP_MsgHdrTypePtr& rd_msg, const FDSP_GetObjTypePtr& get_obj_rsp )
{
	fbd_request_t *req; 
	int trans_id;
//	int   data_size;
//	int64_t data_offset;
//	char  *data_buf;
	

	trans_id = rd_msg->req_cookie;
        StorHvJournalEntry *txn = journalTbl->get_journal_entry(trans_id);
	StorHvJournalEntryLock je_lock(txn);

	if (!txn->isActive()) {
	  cout << "Error: Journal Entry" << rd_msg->req_cookie <<  "  GetObjectResp for an inactive transaction" << std::endl;
	  return (0);
	}

	// TODO: check if incarnation number matches

	if (txn->trans_state != FDS_TRANS_GET_OBJ) {
          cout << "Error: Journal Entry" << rd_msg->req_cookie <<  "  GetObjectResp for a transaction not in GetObjResp" << std::endl;
	  return (0);
	}

	printf("Processing read response for trans %d\n", trans_id);
	req = (fbd_request_t *)txn->write_ctx;
	/*
	  - how to handle the  length miss-match ( requested  length and  recived legth from SM
	  - we will have to handle sending more data due to length difference
	*/

	/*
	 - respond to the block device- data ready 
	*/
	
	printf(" responding to  the block : %p \n ",req);
	if(req) {
          if (rd_msg->result == FDSP_ERR_OK) { 
              memcpy(req->buf, get_obj_rsp->data_obj.c_str(), req->len);
	      txn->fbd_complete_req(req, 0);
          } else {
	      txn->fbd_complete_req(req, -1);
          }
	}
	txn->reset();
	journalTbl->release_trans_id(trans_id);
	
	return 0;

}


int StorHvCtrl::fds_process_put_obj_resp(const FDSP_MsgHdrTypePtr& rx_msg, const FDSP_PutObjTypePtr& put_obj_rsp )
{
  int trans_id = rx_msg->req_cookie; 
  StorHvJournalEntry *txn = journalTbl->get_journal_entry(trans_id);
  StorHvJournalEntryLock je_lock(txn);

  // Check sanity here, if this transaction is valid and matches with the cookie we got from the message

  if (!(txn->isActive())) {
    cout << "Error: Journal Entry" << rx_msg->req_cookie <<  "  PutObjectResp for an inactive transaction" << std::endl;
    return (0);
  }

	if (rx_msg->msg_code == FDSP_MSG_PUT_OBJ_RSP) {
	    txn->fds_set_smack_status(rx_msg->src_ip_lo_addr);
	    printf(" Recvd SM PutObj RSP ;\n");
	    fds_move_wr_req_state_machine(rx_msg);
        }

	return 0;
}

int StorHvCtrl::fds_process_update_catalog_resp(const FDSP_MsgHdrTypePtr& rx_msg, 
                                                const FDSP_UpdateCatalogTypePtr& cat_obj_rsp )
{
  int trans_id;

	trans_id = rx_msg->req_cookie; 
        StorHvJournalEntry *txn = journalTbl->get_journal_entry(trans_id);
	StorHvJournalEntryLock je_lock(txn);

	// Check sanity here, if this transaction is valid and matches with the cookie we got from the message

	if (!(txn->isActive())) {
	  cout << "Error: Journal Entry" << rx_msg->req_cookie <<  "  UpdCatResp for an inactive transaction" << std::endl;
	  return (0);
	}

	if (cat_obj_rsp->dm_operation == FDS_DMGR_TXN_STATUS_OPEN) {
		txn->fds_set_dmack_status(rx_msg->src_ip_lo_addr);
		printf(" Recvd DM OpenTrans RSP ;\n");
	} else {
		txn->fds_set_dm_commit_status(rx_msg->src_ip_lo_addr);
		printf(" Recvd DM CommitTrans RSP ;\n");
	}

	fds_move_wr_req_state_machine(rx_msg);

	return (0);

}

// Warning: Assumes that caller holds the lock to the transaction
int StorHvCtrl::fds_move_wr_req_state_machine(const FDSP_MsgHdrTypePtr& rx_msg) {

	FDS_ProtocolInterface::FDSP_UpdateCatalogTypePtr upd_obj_req = new FDSP_UpdateCatalogType;
	FDSP_MsgHdrTypePtr   sm_msg_ptr;
	ObjectID obj_id;
	int node=0;
	fbd_request_t *req; 
	int trans_id;
	FDS_RPC_EndPoint *endPoint = NULL;
	FDSP_MsgHdrTypePtr     wr_msg;



	trans_id = rx_msg->req_cookie; 
        StorHvJournalEntry *txn = journalTbl->get_journal_entry(trans_id);

	req = (fbd_request_t *)txn->write_ctx;
	wr_msg = txn->dm_msg;
	sm_msg_ptr = txn->sm_msg;

	printf("State transition attempted for transaction %d: current state - %d, sm_ack - %d, dm_ack - %d, dm_commits - %d\n",
	       trans_id, txn->trans_state, txn->sm_ack_cnt, txn->dm_ack_cnt, txn->dm_commit_cnt); 


       switch(txn->trans_state)  {
	 case FDS_TRANS_OPEN :
         {
	  	if ((txn->sm_ack_cnt < FDS_MIN_ACK) || (txn->dm_ack_cnt < FDS_MIN_ACK)) 
          	{
	    		 break;
	  	}
	  	printf(" Rx: State Transition to OPENED : Received min ack from  DM and SM. \n\n");
	  	txn->trans_state = FDS_TRANS_OPENED;
	 }	 
         break;

         case  FDS_TRANS_OPENED :
         {
	    	if (txn->dm_commit_cnt >= FDS_MIN_ACK) 
		{
	        	printf(" Rx: State Transition to COMMITTED  : Received min commits from  DM \n\n ");
	    		txn->trans_state = FDS_TRANS_COMMITTED;
	    	}
         }
	 if (txn->dm_commit_cnt < txn->num_dm_nodes)
	   break;
	 // else fall through to next case.

	 case FDS_TRANS_COMMITTED :
         {
	    if((txn->sm_ack_cnt == txn->num_sm_nodes) && (txn->dm_commit_cnt == txn->num_dm_nodes))
	    {
	   	printf(" Rx: State Transition to SYCNED  : Received all acks and commits from  DM and SM. Ending req %p \n\n ", req);
	    	txn->trans_state = FDS_TRANS_SYNCED;

	    	/*
	      	-  add the vvc entry
	      	-  If we are thinking of adding the cache , we may have to keep a copy on the cache 
	    	*/
	    

        	obj_id.SetId(txn->data_obj_id.hash_high, txn->data_obj_id.hash_low);
        	storHvisor->volCatalogCache->Update((fds_uint64_t)rx_msg->glob_volume_id, (fds_uint64_t)req->sec*HVISOR_SECTOR_SIZE, obj_id);


	    	// destroy the txn, reclaim the space and return from here	    
	    	txn->trans_state = FDS_TRANS_EMPTY;
	    	txn->write_ctx = 0;
	    	// del_timer(txn->p_ti);
	    	if (req) {
	      		txn->fbd_complete_req(req, 0);
	    	}
		txn->reset();
		journalTbl->release_trans_id(trans_id);
                return(0);
	    }
          }
          break;

          default : 
             break;
       }

     if (txn->trans_state > FDS_TRANS_OPEN) {
	/*
	  -  browse through the list of the DM nodes that sent the open txn response .
          -  send  DM - commit request 
	 */
		
	for (node = 0; node < txn->num_dm_nodes; node++)
	{
	   if (txn->dm_ack[node].ack_status) 
	   {
	     if ((txn->dm_ack[node].commit_status) == FDS_CLS_ACK)
		 {
		   upd_obj_req->dm_operation = FDS_DMGR_TXN_STATUS_COMMITED;
		   upd_obj_req->dm_transaction_id = 1;
           	   upd_obj_req->data_obj_id.hash_high = txn->data_obj_id.hash_high;
           	   upd_obj_req->data_obj_id.hash_low =  txn->data_obj_id.hash_low;

      
           	   storHvisor->rpcSwitchTbl->Get_RPC_EndPoint(txn->dm_ack[node].ipAddr, FDSP_DATA_MGR, &endPoint);
		   txn->dm_ack[node].commit_status = FDS_COMMIT_MSG_SENT;

		   if (endPoint) {
		 	endPoint->fdspDPAPI->begin_UpdateCatalogObject(wr_msg, upd_obj_req);
			printf("Hvisor: Sent async UpdCatObjCommit req to DM at 0x%x\n", (unsigned int) txn->dm_ack[node].ipAddr);
		   } else {
		     printf("No end point found for DM at ip 0x%x\n", (unsigned int) txn->dm_ack[node].ipAddr);
		   }

	     }
	  }
       }
    }
    return 0;
}


void FDSP_DataPathRespCbackI::GetObjectResp(const FDSP_MsgHdrTypePtr& msghdr, const FDSP_GetObjTypePtr& get_obj, const Ice::Current&) {
  printf("Received get obj response for txn %d\n", msghdr->req_cookie); 
  storHvisor->fds_process_get_obj_resp(msghdr, get_obj);
}

void FDSP_DataPathRespCbackI::PutObjectResp(const FDSP_MsgHdrTypePtr& msghdr, const FDSP_PutObjTypePtr& put_obj, const Ice::Current&) {
  printf("Received put obj response for txn %d\n", msghdr->req_cookie); 
  storHvisor->fds_process_put_obj_resp(msghdr, put_obj);
}

void FDSP_DataPathRespCbackI::UpdateCatalogObjectResp(const FDSP_MsgHdrTypePtr& fdsp_msg, const FDSP_UpdateCatalogTypePtr& update_cat, const Ice::Current &) {
  printf("Received upd cat obj response for txn %d\n", fdsp_msg->req_cookie); 
	 storHvisor->fds_process_update_catalog_resp(fdsp_msg, update_cat);
}

void FDSP_DataPathRespCbackI::QueryCatalogObjectResp(
    const FDSP_MsgHdrTypePtr& fdsp_msg_hdr,
    const FDSP_QueryCatalogTypePtr& cat_obj_req,
    const Ice::Current &) {
    int num_nodes;
    int node_ids[64];
    int node_state = -1;
    uint32_t node_ip = 0;
    ObjectID obj_id;
    int doid_dlt_key;
    FDS_RPC_EndPoint *endPoint = NULL;
    uint64_t doid_high;
    int trans_id = fdsp_msg_hdr->req_cookie;
    fbd_request_t *req;

    
    std::cout << "GOT A QUERY RESPONSE!" << std::endl;

        StorHvJournalEntry *journEntry = storHvisor->journalTbl->get_journal_entry(trans_id);

        if (journEntry == NULL) {
          cout << "Journal Entry  " << trans_id <<  "QueryCatalogObjResp not found" << std::endl;
           return;
        }

	StorHvJournalEntryLock je_lock(journEntry);
	if (!journEntry->isActive()) {
	  return;
	}
   
        if (journEntry->op !=  FDS_IO_READ) { 
          cout << "Journal Entry  " << fdsp_msg_hdr->req_cookie <<  "  QueryCatalogObjResp for a non IO_READ transaction" << std::endl;
           req = (fbd_request_t *)journEntry->write_ctx;
           journEntry->trans_state = FDS_TRANS_EMPTY;
           journEntry->write_ctx = 0;
           if(req) {
             journEntry->fbd_complete_req(req, -1);
           }
	   journEntry->reset();
	   storHvisor->journalTbl->release_trans_id(trans_id);
          return;
        }

        if (journEntry->trans_state != FDS_TRANS_VCAT_QUERY_PENDING) {
            cout << "Journal Entry  " << fdsp_msg_hdr->req_cookie <<  "  QueryCatalogObjResp for a transaction node in Query Pending " << std::endl;
           req = (fbd_request_t *)journEntry->write_ctx;
           journEntry->trans_state = FDS_TRANS_EMPTY;
           journEntry->write_ctx = 0;
           if(req) {
             journEntry->fbd_complete_req(req, -1);
           }
	   journEntry->reset();
	   storHvisor->journalTbl->release_trans_id(trans_id);
           return;
        }
	
	// If Data Mgr does not have an entry, simply return 0s.
	if (fdsp_msg_hdr->result != FDS_ProtocolInterface::FDSP_ERR_OK) {
	  cout << "Journal Entry  " << fdsp_msg_hdr->req_cookie <<  ":  QueryCatalogObjResp returned error " << std::endl;
           req = (fbd_request_t *)journEntry->write_ctx;
           journEntry->trans_state = FDS_TRANS_EMPTY;
           journEntry->write_ctx = 0;
           if(req) {
	     memset(req->buf, 0, req->len);
             journEntry->fbd_complete_req(req, 0);
           }
	   journEntry->reset();
	   storHvisor->journalTbl->release_trans_id(trans_id);
           return;
	}

        doid_high = cat_obj_req->data_obj_id.hash_high;
        doid_dlt_key = doid_high >> 56;

         FDS_ProtocolInterface::FDSP_GetObjTypePtr get_obj_req = new FDSP_GetObjType;
         // Lookup the Primary SM node-id/ip-address to send the GetObject to
         storHvisor->dataPlacementTbl->getDLTNodesForDoidKey(doid_dlt_key, node_ids, &num_nodes);
         if(num_nodes == 0) {
            cout << "DataPlace Error : no nodes in DLT :Jrnl Entry" << fdsp_msg_hdr->req_cookie <<  "QueryCatalogObjResp " << std::endl;
           req = (fbd_request_t *)journEntry->write_ctx;
           journEntry->trans_state = FDS_TRANS_EMPTY;
           journEntry->write_ctx = 0;
           if(req) {
             journEntry->fbd_complete_req(req, 0);
           }
	   journEntry->reset();
	   storHvisor->journalTbl->release_trans_id(trans_id);
           return;
         }
         storHvisor->dataPlacementTbl->getNodeInfo(node_ids[0], &node_ip, &node_state);
         //
         // *****CAVEAT: Modification reqd
         // ******  Need to find out which is the primary SM and send this out to that SM. ********
         storHvisor->rpcSwitchTbl->Get_RPC_EndPoint(node_ip, FDSP_STOR_MGR, &endPoint);
         
        // RPC Call GetObject to StorMgr
        fdsp_msg_hdr->msg_code = FDSP_MSG_GET_OBJ_REQ;
        fdsp_msg_hdr->msg_id =  1;
	// fdsp_msg_hdr->src_ip_lo_addr = SRC_IP;
	fdsp_msg_hdr->dst_ip_lo_addr = node_ip;
        get_obj_req->data_obj_id.hash_high = cat_obj_req->data_obj_id.hash_high;
        get_obj_req->data_obj_id.hash_low = cat_obj_req->data_obj_id.hash_low;
        get_obj_req->data_obj_len = journEntry->data_obj_len;
        if (endPoint) {
            endPoint->fdspDPAPI->begin_GetObject(fdsp_msg_hdr, get_obj_req);
	    cout << "Sent Async getObj req to SM at " << node_ip << endl;
            journEntry->trans_state = FDS_TRANS_GET_OBJ;
        }
   
        obj_id.SetId( cat_obj_req->data_obj_id.hash_high,cat_obj_req->data_obj_id.hash_low);
        storHvisor->volCatalogCache->Update((fds_uint64_t)fdsp_msg_hdr->glob_volume_id, (fds_uint64_t)cat_obj_req->volume_offset, obj_id);
}
