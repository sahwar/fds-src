#include "fds_qos.h"
#include <map>

namespace fds {

  class WFQQueueDesc {

  public:

    fds_uint32_t queue_id;
    fds_uint32_t queue_rate;
    fds_uint32_t queue_priority;
    fds_uint32_t rate_based_weight;
    fds_uint32_t priority_based_weight;

    std::vector<fds_uint32_t> rate_based_rr_spots;
    int num_priority_based_ios_dispatched; // number of ios dispatched in the current round of priority based WFQ;

    FDS_VolumeQueue *queue;
    fds_uint32_t num_pending_ios;
    fds_uint32_t num_outstanding_ios;

    WFQQueueDesc(fds_uint32_t q_id,
		 FDS_VolumeQueue *que,
		 fds_uint32_t q_rate,
		 fds_uint32_t q_pri)
      : queue_id(q_id),
	queue(que),
	queue_rate(q_rate),
	queue_priority(q_pri) {
      rate_based_rr_spots.clear();
    }

  };

  class QoSWFQDispatcher : public FDS_QoSDispatcher {

  private:

    fds_uint32_t total_capacity;
    int num_queues;
    std::map<fds_uint32_t, WFQQueueDesc *> queue_desc_map;
    std::vector<fds_uint32_t> rate_based_qlist;
    fds_uint32_t next_rate_based_spot;
    fds_uint32_t next_priority_based_queue;

    fds_uint32_t priority_to_wfq_weight(fds_uint32_t priority) {
      assert((priority >= 0) && (priority <= 10));
      fds_uint32_t weight = 0;

      weight = (11-priority);
      return weight;
      
    }

    fds_uint32_t getNextQueueInPriorityWFQList(fds_uint32_t queue_id) {
	auto it = queue_desc_map.find(queue_id);
	if (it != queue_desc_map.end())
	  it++;
	if (it == queue_desc_map.end()) {
	  it = queue_desc_map.begin();
	}
	fds_uint32_t next_queue = it->first;
	return next_queue;
    }

    void ioProcessForEnqueue(fds_uint32_t queue_id, FDS_IOType *io)
    {
      FDS_QoSDispatcher::ioProcessForEnqueue(queue_id, io);
      WFQQueueDesc *qd = queue_desc_map[queue_id];
      assert(qd->num_pending_ios >= 0);
      qd->num_pending_ios ++;
    }

    void ioProcessForDispatch(fds_uint32_t queue_id, FDS_IOType *io)
    {
      FDS_QoSDispatcher::ioProcessForDispatch(queue_id, io);
      WFQQueueDesc *qd = queue_desc_map[queue_id];
      qd->num_pending_ios --;
      assert(qd->num_pending_ios >= 0);
    }

    fds_uint32_t getNextQueueForDispatch() {
      
      // Step 1: Let's first look at the current rate_based slot to if there is a valid non-empty queue there
      fds_uint32_t next_queue = rate_based_qlist[next_rate_based_spot];
      next_rate_based_spot = (next_rate_based_spot + 1) % total_capacity;
      if (next_queue != 0) {
	WFQQueueDesc *next_qd = queue_desc_map[next_queue];
	if (next_qd->num_pending_ios > 0)
	  return next_queue;
      }

      // Step 2: Rate based spot was empty. Let's now pick the next q in the priority based WFQ list
      next_queue = next_priority_based_queue;
      WFQQueueDesc *next_qd = (queue_desc_map.count(next_queue) == 0)? NULL:queue_desc_map[next_queue];
      while ((!next_qd) || (next_qd->num_pending_ios <= 0)) {
	if (next_qd)
	  next_qd->num_priority_based_ios_dispatched = 0;
	next_queue = next_priority_based_queue = getNextQueueInPriorityWFQList(next_queue);
	next_qd = (queue_desc_map.count(next_queue) == 0)? NULL:queue_desc_map[next_queue];
	if (next_qd)
	  next_qd->num_priority_based_ios_dispatched = 0;
      }

      assert(queue_desc_map.count(next_queue) != 0);
      assert(next_qd != NULL);
      assert(next_qd->num_pending_ios > 0);

      // Step2 end: next_queue is the queue we are going to dispatch from. next_qd is it's descriptor

      // Step 3: Now we will update the priority based WFQ state for the next iteration

      next_qd->num_priority_based_ios_dispatched ++;

      if (next_qd->num_priority_based_ios_dispatched == next_qd->priority_based_weight) {
	next_qd->num_priority_based_ios_dispatched = 0;
	next_priority_based_queue = getNextQueueInPriorityWFQList(next_queue);
	if (queue_desc_map.count(next_priority_based_queue) > 0) {
	  next_qd = queue_desc_map[next_priority_based_queue];
	  next_qd->num_priority_based_ios_dispatched = 0;
	}
      }

      return next_queue;

    }

  public:

    QoSWFQDispatcher(FDS_QoSControl *ctrlr, fds_uint32_t total_server_rate) {
      parent_ctrlr = ctrlr;
      qda_log = new fds_log("qda", "logs");
      qda_mutex = new fds_mutex("QoSWFQDispatchAlgorithm");
      num_pending_ios = 0;

      total_capacity = total_server_rate;
      rate_based_qlist.clear();
      next_rate_based_spot = 0;
      for (int i = 0; i < total_capacity; i++) {
	rate_based_qlist.push_back(0); // "0" is a special queue_id indicating this spot is assigned to no queue. Open to be used for priority based WFQ assignment
      }
      num_queues = 0;
      queue_desc_map.clear();
      next_priority_based_queue = 0;
    }

    virtual Error registerQueue(fds_uint32_t queue_id, FDS_VolumeQueue *queue, fds_uint32_t queue_rate, fds_uint32_t queue_priority) {

      Error err(ERR_OK);

      assert(queue_id > 0);

      WFQQueueDesc *qd = new WFQQueueDesc(queue_id, queue, queue_rate, queue_priority);
      qd->rate_based_weight = queue_rate;
      qd->priority_based_weight = priority_to_wfq_weight(queue_priority);
      qd->num_pending_ios = 0;
      qd->num_outstanding_ios = 0;
      qd->num_priority_based_ios_dispatched = 0;

      qda_mutex->lock();
      if (queue_desc_map.count(queue_id) != 0) {
	qda_mutex->unlock();
	delete qd;
	err = ERR_DUPLICATE;
	return err;
      }
 

      // Now fill the vacant spots in the rate based qlist based on the queue_rate
      // Start at the first vacant spot and fill at intervals of capacity/queue_rate.
      fds_uint32_t current_spot = 0;
      for (int i = 0; i < queue_rate; i++) {
	fds_uint32_t num_spots_searched = 0;
	while ((rate_based_qlist[current_spot] != 0) && (num_spots_searched < total_capacity)) {
	  current_spot = (current_spot+1) % total_capacity;
	  num_spots_searched ++;
	}
	assert(rate_based_qlist[current_spot] == 0);
	rate_based_qlist[current_spot] = queue_id;
	qd->rate_based_rr_spots.push_back(current_spot);
	current_spot = (current_spot + (int) total_capacity/queue_rate) % total_capacity;
      }
      queue_desc_map[queue_id] = qd;
      queue_map[queue_id] = queue;
      qda_mutex->unlock();

      return err;

    }

    virtual Error deregisterQueue(fds_uint32_t queue_id) {

      Error err(ERR_OK);

      qda_mutex->lock();
      if  (queue_desc_map.count(queue_id) == 0) {
	qda_mutex->unlock();
	err = ERR_INVALID_ARG;
	return err;
      }
      WFQQueueDesc *qd = queue_desc_map[queue_id];
      for (int i = 0; i < qd->queue_rate; i++) {
	fds_uint32_t next_rate_based_spot = qd->rate_based_rr_spots[i];
	rate_based_qlist[next_rate_based_spot] = 0; // This is now a free spot, that could be used for priority-based WFQ assignment
      }
      if (next_priority_based_queue == queue_id) {
	next_priority_based_queue = getNextQueueInPriorityWFQList(queue_id);
	WFQQueueDesc *next_qd = queue_desc_map[next_priority_based_queue];
	next_qd->num_priority_based_ios_dispatched = 0;
      }

      queue_desc_map.erase(queue_id);
      queue_map.erase(queue_id);
      qda_mutex->unlock();
      delete qd;

      return err;

    }
    
  };
}
