// iu.h
//   by Derek Chiou
//      March 4, 2007
// 

// STUDENTS: YOU ARE ALLOWED TO MODIFY THIS FILE, BUT YOU SHOULDN'T NEED TO MODIFY MUCH, IF ANYTHING.
// for 382N-10

#ifndef IU_H
#define IU_H
#include "types.h"
#include "my_fifo.h"
#include "cache.h"
#include "network.h"
#include "directory.h"
#include <list>

struct fsm_data_t {
  unsigned state; // current state
  unsigned node_id; // original requester's ID
  unsigned num_to_inv;
  bool net_success;
  proc_cmd_t pc;
  dir_response_t dir_rsp;
  fsm_data_t* waiting_rqst;
  permit_tag_t next_permit_tag;
  permit_tag_t my_next_permit_tag;
  bool retried;
};

class iu_t {
  int node;

  int local_accesses;
  int global_accesses;


  cache_t *cache;
  network_t *net;

  bool proc_cmd_p;
  proc_cmd_t proc_cmd;

  bool proc_cmd_processed_p;

  // processor side
  bool process_proc_request(proc_cmd_t proc_cmd);

  // network side
  bool process_net_request(net_cmd_t net_cmd);
  bool process_net_reply(net_cmd_t net_cmd);

 public:
  data_t mem[MEM_SIZE];
  iu_t(int __node);

  void bind(cache_t *c, network_t *n);

  void advance_one_cycle();
  void print_stats();

  // processor side
  bool from_proc(proc_cmd_t pc);
  
  // network side
  bool from_net(net_cmd_t nc);
  
  // added by Ben
 private:
  unsigned mem_accss_per_cycle;
  unsigned rqst_from_cache_per_cycle;
  unsigned accss_cache_per_cycle;
  unsigned rcv_from_net_per_cycle;
  unsigned snd_to_net_per_cycle;
 
  std::list<fsm_data_t> rply_queue;
  std::list<fsm_data_t> rqst_queue;
  directory *dir;
  int address_tag_shift;
  fsm_data_t proc_fd;
  bool proc_retried;

  void fsm(fsm_data_t &fd);
  void mem_rd(fsm_data_t &fd);
  void mem_wr(fsm_data_t &fd);
  void mem_wb(fsm_data_t &fd);
  void invalidate(fsm_data_t &fd);
  bool is_homesite_rqst(proc_cmd_t pc);
  bool dir_avail(fsm_data_t &fd);
  fsm_data_t *get_dir_holder(address_t addr);
  bool exist_in(std::list<fsm_data_t> &list, fsm_data_t ele);
  void dequeue_from(std::list<fsm_data_t> &list, fsm_data_t ele);
  bool i_am_homesite(address_t addr);
  void read_cacheline (cache_access_response_t car, data_t data);
  void print_state_info(fsm_data_t &fd);
  void check_constraints();
  void retry_rqst_next_cycle(fsm_data_t &fd);
  void retry_rply_next_cycle(fsm_data_t &fd);
};

enum STATES {
  FIRST_CYCLE,
  UPDATE_MY_DIR,
  READY_HMST,
  SEND_REQ_HMST,
  WAIT_REPLY_HMST,
  SEND_REPLY_HMST,
  READY_REM,
  SEND_REQ_REM,
  WAIT_REPLY_REM,
  REPLY_ARRIVED_REM,
  READY_REE,
  SEND_REQ_REE,
  WAIT_REPLY_REE,
  REPLY_ARRIVED_REE,
  READY_RMM,
  SEND_REQ_RMM,
  WAIT_REPLY_RMM,
  REPLY_ARRIVED_RMM,
  READY_RMES,
  SEND_REQ_RMES,
  WAIT_REPLY_RMES,
  REPLY_ARRIVED_RMES,
  REPLY_TO_PROC,
  REPLY_TO_RQSTR,
  WAIT_REPLY_RQSTR,
  WRITE_BACK_RQST,
  CHANGE_PERMIT_TAG,
  JUST_REPLY_RQSTR,
  JUST_REPLY_PROC,
  UPDATE_DIR,
  FINISH,
  NUM_STATES
};

const char* const STATES_STRING[] = {
  "FIRST_CYCLE",
  "UPDATE_MY_DIR",
  "READY_HMST",
  "SEND_REQ_HMST",
  "WAIT_REPLY_HMST",
  "SEND_REPLY_HMST",
  "READY_REM",
  "SEND_REQ_REM",
  "WAIT_REPLY_REM",
  "REPLY_ARRIVED_REM",
  "READY_REE",
  "SEND_REQ_REE",
  "WAIT_REPLY_REE",
  "REPLY_ARRIVED_REE",
  "READY_RMM",
  "SEND_REQ_RMM",
  "WAIT_REPLY_RMM",
  "REPLY_ARRIVED_RMM",
  "READY_RMES",
  "SEND_REQ_RMES",
  "WAIT_REPLY_RMES",
  "REPLY_ARRIVED_RMES",
  "REPLY_TO_PROC",
  "REPLY_TO_RQSTR",
  "WAIT_REPLY_RQSTR",
  "WRITE_BACK_RQST",
  "CHANGE_PERMIT_TAG",
  "JUST_REPLY_RQSTR",
  "JUST_REPLY_PROC",
  "UPDATE_DIR",
  "FINISH"
};

const char* const PERMIT_TAG_STR[] = {
  "INVALID",
  "SHARED",
  "EXCLUSIVE",
  "MODIFIED"
};

const char* const BUSOP_STR[] = {
  "READ",
  "WRITE",
  "INVALIDATE"
};

#endif
