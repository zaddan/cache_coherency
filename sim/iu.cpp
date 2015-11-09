// iu.cpp
//   by Derek Chiou
//      March 4, 2007
// 

// STUDENTS: YOU ARE EXPECTED TO MAKE MOST OF YOUR MODIFICATIONS IN THIS FILE.
// for 382N-10

#include "types.h"
#include "helpers.h"
#include "my_fifo.h"
#include "cache.h"
#include "iu.h"

// added by Ben
#include "directory.h"
#include <stdlib.h>
#include <assert.h>

// Rule 
// - IU can return at most one reply to the cache per cycle
// - IU can access memory no more than 6 times per cycle
// - Each access is no more than one cache-line large
// - If you add a queue, accessing the queue won't be counted as one access
// - additional space that can be used for additional structure (768 - 1023, inclusive)
// - except for communication, all happens in a cycle
// - fully blocking cache: only one outstanding cache miss per process can be in progress
// - only one core/processor per network node
// - need to support up to 32 processors

extern args_t args;

iu_t::iu_t(int __node) {
  node = __node;
  dir = new directory(__node, 2, 3, LG_CACHE_LINE_SIZE, args.num_procs, 1);
  proc_retried = false;
  for (int i = 0; i < MEM_SIZE; ++i) 
    for (int j = 0; j < CACHE_LINE_SIZE; ++j)
      mem[i][j] = 0;
  address_tag_shift = LG_CACHE_LINE_SIZE;
}

void iu_t::bind(cache_t *c, network_t *n) {
  cache = c;
  net = n;
}

void iu_t::check_constraints() {
  if (mem_accss_per_cycle > 6) {
    NOTE_ARGS(("[ERROR] mem_accss_per_cycle: expected <= 6 , actual = %d", mem_accss_per_cycle));
  } else if (rqst_from_cache_per_cycle > 1) {
    NOTE_ARGS(("[ERROR] rqst_from_cache_per_cycle: expected <= 1 , actual = %d", rqst_from_cache_per_cycle));
  } else if (accss_cache_per_cycle > 1) {
    NOTE_ARGS(("[ERROR] accss_cache_per_cycle: expected <= 1 , actual = %d", accss_cache_per_cycle));
  } else if (rcv_from_net_per_cycle > 1) {
    NOTE_ARGS(("[ERROR] rcv_from_net_per_cycle: expected <= 1 , actual = %d", rcv_from_net_per_cycle));
  } else if (snd_to_net_per_cycle > 1) {
    NOTE_ARGS(("[ERROR] snd_to_net_per_cycle: expected <= 1 , actual = %d", snd_to_net_per_cycle));
  }
}

void iu_t::advance_one_cycle() {
  mem_accss_per_cycle = 0;
  rqst_from_cache_per_cycle = 0;
  accss_cache_per_cycle = 0;
  rcv_from_net_per_cycle = 0;
  snd_to_net_per_cycle = 0;


  // fixed priority: reply from network
  if (net->from_net_p(node, REPLY)) {
    process_net_reply(net->from_net(node, REPLY));

  } else if (net->from_net_p(node, REQUEST)) {
    process_net_request(net->from_net(node, REQUEST));
  
  } else {
    if (proc_retried) {
      fsm(proc_fd);
        
    } else if (proc_cmd_p && !proc_cmd_processed_p) {
      proc_cmd_processed_p = true;
      process_proc_request(proc_cmd);

    }

    if (proc_fd.net_success) {
      return;
    }

    if (!rply_queue.empty()) { 
      // on-going reply still exist
      unsigned size = rply_queue.size();

      for (unsigned i = 0; i < size; ++i) {
        fsm_data_t fd = rply_queue.front();
        rply_queue.pop_front();
        fsm(fd);
        if (fd.net_success)
          // only one network request per cycle is allowed
          return;; 
      }

    } 
    
    if (!rqst_queue.empty()) { 
      // on-going request still exist
      unsigned size = rqst_queue.size();

      for (unsigned i = 0; i < size; ++i) {
        fsm_data_t fd = rqst_queue.front();
        rqst_queue.pop_front();
        fsm(fd);
        if (fd.net_success)
          // only one network request per cycle is allowed
          return; 
      }
    } 

    check_constraints();
  }
}

// processor side

// this interface method only takes and buffers a request from the
// processor.
bool iu_t::from_proc(proc_cmd_t pc) {
  ++rqst_from_cache_per_cycle;

  if (!proc_cmd_p) {
    proc_cmd_p = true;
    proc_cmd = pc;

    proc_cmd_processed_p = false;
    return(false);
  } else {
    return(true);
  }
}

#ifdef ORIGINAL_CODE
bool iu_t::process_proc_request(proc_cmd_t pc) {
  int dest = gen_node(pc.addr);
  int lcl = gen_local_cache_line(pc.addr);

  NOTE_ARGS(("%d: addr = %d, dest = %d", node, pc.addr, dest));

  if (dest == node) { // local

    ++local_accesses;
    proc_cmd_p = false; // clear proc_cmd
    
    switch(pc.busop) {
    case READ:
      copy_cache_line(pc.data, mem[lcl]);

      cache->reply(pc);
      return(false);
      
    case WRITE:
      copy_cache_line(mem[lcl], pc.data);
      return(false);
      
    case INVALIDATE:
      // ***** FYTD *****
      return(false);  // need to return something for now
      break;
    }
    
  } else { // global
    ++global_accesses;
    net_cmd_t net_cmd;

    net_cmd.src = node;
    net_cmd.dest = dest;
    net_cmd.proc_cmd = pc;

    return(net->to_net(node, REQUEST, net_cmd));
  }
}
#else


// check if other request is already working on the cacheline
// that corresponds to addr
bool iu_t::dir_avail(fsm_data_t &fd) {
  int proc_fd_tag = proc_fd.pc.addr >> address_tag_shift;
  int addr_tag = fd.pc.addr >> address_tag_shift;

  bool holder_is_proc_fd = (proc_retried) and (proc_fd_tag == addr_tag);
  return((!exist_in(rqst_queue, fd) and !exist_in(rply_queue, fd) and !holder_is_proc_fd) or (fd.node_id == node));
}

// By using an address, search through the rqst_queue and 
// return the matching fsm_data_t
fsm_data_t *iu_t::get_dir_holder(address_t addr) {
  std::list<fsm_data_t>::iterator itr;
  int tag = addr >> address_tag_shift;

  for (itr = rqst_queue.begin(); itr != rqst_queue.end(); ++itr) {
    fsm_data_t fd = *itr;
    int tag_comp = fd.pc.addr >> address_tag_shift;
    if (tag == tag_comp) {
      // currently corresponding directory is on update
      return &(*itr); 
    }
  }

  for (itr = rply_queue.begin(); itr != rply_queue.end(); ++itr) {
    fsm_data_t fd = *itr;
    int tag_comp = fd.pc.addr >> address_tag_shift;
    if (tag == tag_comp) {
      // currently corresponding directory is on update
      return &(*itr); 
    }
  }

  int proc_fd_tag = (proc_fd.pc.addr) >> address_tag_shift;
  if (tag == proc_fd_tag) {
    return &proc_fd;
  }

  // should always check with dir_avail() first,
  // and then call this function
  ASSERT(0); 
}

// Search the list and see if there is the match with ele
// Not sure what to compare to find the match
bool iu_t::exist_in(std::list<fsm_data_t> &lst, fsm_data_t ele) {
  std::list<fsm_data_t>::iterator itr;
  int tag = (ele.pc.addr) >> address_tag_shift;

  for (itr = lst.begin(); itr != lst.end(); ++itr) {
    fsm_data_t fd = *itr;
    int tag_comp = (fd.pc.addr) >> address_tag_shift;
    if (tag == tag_comp) {
      return(true); 
    }
  }

  return(false);
}

// Search the list and find the match with ele
// Then, dequeue it from the queue
void iu_t::dequeue_from(std::list<fsm_data_t> &lst, fsm_data_t ele) {
  std::list<fsm_data_t>::iterator itr;
  int tag = (ele.pc.addr) >> address_tag_shift;

  for (itr = lst.begin(); itr != lst.end(); ++itr) {
    fsm_data_t fd = *itr;
    int tag_comp = (fd.pc.addr) >> address_tag_shift;
    if (tag == tag_comp) {
      lst.erase(itr);
      return;
    }
  }

  ASSERT(0);
}

// See if I am a homesite for the give address
bool iu_t::i_am_homesite(address_t addr) {
  int homesite = gen_node(addr);
  return(homesite == node);
}

void iu_t::print_state_info(fsm_data_t &fd) {
  int gcl = (fd.pc.addr) >> address_tag_shift;
  NOTE_ARGS(("[NODE: %d][TAG: %d][STATE: %s][FROM: %d]", node, gcl, STATES_STRING[fd.state], fd.node_id));
}

// FSM for homesite
void iu_t::fsm(fsm_data_t &fd) {
  proc_cmd_t pc = fd.pc;
  int dest = gen_node(pc.addr);
  int lcl = gen_local_cache_line(pc.addr);
  fd.net_success = false;

  /*--- FIRST_CYCLE ---*/
  if (fd.state == FIRST_CYCLE) { // first cycle
    print_state_info(fd);
    if (!dir_avail(fd)) { 
      fsm_data_t *new_fd = new fsm_data_t(fd);
      fsm_data_t *fd_ptr = get_dir_holder(pc.addr);
      ASSERT(fd_ptr);
      
      while (fd_ptr->waiting_rqst != NULL) {
        fd_ptr = fd_ptr->waiting_rqst;
      }

      fd_ptr->waiting_rqst = new_fd;

      NOTE("[DIR_CONFLICT] Add to waiting list:");
      print_state_info(*new_fd);

      return;
    }

    if ((pc.busop == READ) and (pc.permit_tag == EXCLUSIVE)) { 
      // normal read request
      mem_rd(fd);
    } else if ((pc.busop == READ) and (pc.permit_tag == MODIFIED)) { 
      // normal write request (RWITM)
      mem_wr(fd);
    } else if ((pc.busop == WRITE) and (pc.permit_tag == EXCLUSIVE)) { 
      // write-back request
      mem_wb(fd);
    } else if ((pc.busop == INVALIDATE) and (pc.permit_tag == EXCLUSIVE)) {
      // invalidate a clean cacheline
      invalidate(fd);
    } else { 
      // undefined request
      printf("[Error] undefined request received from proc.\n");
      printf("     command: [BUSOP: %s][PERMIT_TAG: %s]\n", BUSOP_STR[pc.busop], PERMIT_TAG_STR[pc.permit_tag]);
      exit(1);
    } 
  }

  /*--- UPDATE_MY_DIR ---*/
  if (fd.state == UPDATE_MY_DIR) {
    if (mem_accss_per_cycle + 1 <= 6) {
      NOTE_ARGS(("UPDATE MY DIRECTORY: [NODE: %d][PERMIT_TAG: %s][FOR_NODE: %d][TAG: %d]", node, PERMIT_TAG_STR[fd.my_next_permit_tag], fd.node_id, (fd.pc.addr) >> address_tag_shift));

      dir->update(node, fd.pc.addr, fd.my_next_permit_tag);
      fd.state = REPLY_TO_RQSTR;
    } else {
      fd.state = UPDATE_MY_DIR;
      retry_rqst_next_cycle(fd);
      return;
    }
  }

  /*--- READY_HMST ---*/
  if (fd.state == READY_HMST) {
    print_state_info(fd);
    // @ put into the queue
    // rqst_queue.push_back(fd);
    fd.state = SEND_REQ_HMST;
  }
  
  /*--- SEND_REQ_HMST ---*/
  if (fd.state == SEND_REQ_HMST) {
    print_state_info(fd);
    // @ send request to the homesite
    net_cmd_t net_cmd;
    net_cmd.src = node;
    net_cmd.dest = dest;
    net_cmd.proc_cmd = fd.pc;

    fd.net_success = net->to_net(node, REQUEST, net_cmd);
    if (fd.net_success) {
      NOTE("to_net -> succeeded!");
      fd.state = WAIT_REPLY_HMST;
    } else {
      NOTE("to_net -> failed!");
      fd.state = SEND_REQ_HMST;
    }

    retry_rqst_next_cycle(fd);
    return;
  }

  /*--- WAIT_REPLY_HMST ---*/
  if (fd.state == WAIT_REPLY_HMST) {
    // do nothing
    print_state_info(fd);
    
    fd.state = WAIT_REPLY_HMST;
    retry_rqst_next_cycle(fd);
    return;
  }

  /*--- SEND_REPLY_HMST ---*/
  if (fd.state == SEND_REPLY_HMST) {
    print_state_info(fd);
    net_cmd_t net_cmd;
    net_cmd.src = node;
    net_cmd.dest = dest;
    net_cmd.proc_cmd = pc;
    
    fd.net_success = net->to_net(node, REPLY, net_cmd);
    if (fd.net_success) {
      NOTE("to_net -> succeeded!");
      fd.state = JUST_REPLY_PROC; // without directory update
    } else {
      NOTE("to_net -> failed!");
      fd.state = SEND_REPLY_HMST;
    }
      
    retry_rply_next_cycle(fd);
    return;
  }

  /*--- READY_REM ---*/
  if (fd.state == READY_REM) {
    print_state_info(fd);
    // @ put into the queue
    fd.state = SEND_REQ_REM;
  }
  
  /*--- SEND_REQ_REM ---*/
  if (fd.state == SEND_REQ_REM) {
    print_state_info(fd);
    /*-- One node is in M state --*/
    /** Expected action: WB, M->S **/

    // @ network command in this case: [READ][SHARED]
    fd.pc.busop = READ;
    fd.pc.permit_tag = SHARED;

    // @ send write back request to the node who has a copy in M state
    int net_dst = fd.dir_rsp.m_dst;
    net_cmd_t net_cmd;

    net_cmd.src = node;
    net_cmd.dest = net_dst;
    net_cmd.proc_cmd = fd.pc;

    fd.net_success = net->to_net(node, REQUEST, net_cmd);
    if (fd.net_success) {
      NOTE("to_net -> succeeded!");
      fd.state = WAIT_REPLY_REM;
    } else {
      NOTE("to_net -> failed!");
      fd.state = SEND_REQ_REM;
    }

    retry_rqst_next_cycle(fd);
    return;
  }

  /*--- WAIT_REPLY_REM ---*/
  if (fd.state == WAIT_REPLY_REM) {
    print_state_info(fd);
    // do nothing
    // If reply comes in, this state should be changed
    
    fd.state = WAIT_REPLY_REM;
    retry_rqst_next_cycle(fd);
    return;
  } 

  /*--- REPLY_ARRIVED_REM ---*/
  if (fd.state == REPLY_ARRIVED_REM) {
    print_state_info(fd);
    // write back data (mem[lcl] <- pc.data)
    if (mem_accss_per_cycle + 1 <= 6) {
      ++mem_accss_per_cycle;
      copy_cache_line(mem[lcl], pc.data);
      if (fd.node_id == node) {
        fd.state = REPLY_TO_PROC;
      } else {
        fd.state = REPLY_TO_RQSTR;
      }
    } else {
      fd.state = REPLY_ARRIVED_REM;
      retry_rqst_next_cycle(fd);
      return;
    } 
  }

  /*--- READY_REE ---*/
  if (fd.state == READY_REM) {
    print_state_info(fd);
    fd.state = SEND_REQ_REE;
  }
  
  /*--- SEND_REQ_REE ---*/
  if (fd.state == SEND_REQ_REE) {
    print_state_info(fd);
    /*-- One node is in E state --*/
    //** Expected action: E->S **/

    // @ network command in this case: [INVALIDATE][SHARED]
    fd.pc.busop = INVALIDATE;
    fd.pc.permit_tag = SHARED;

    // @ send request to the node who that has a copy in E state
    int net_dst = fd.dir_rsp.e_dst;
    net_cmd_t net_cmd;

    net_cmd.src = node;
    net_cmd.dest = net_dst;
    net_cmd.proc_cmd = pc;

    fd.net_success = net->to_net(node, REQUEST, net_cmd);

    if (fd.net_success) {
      NOTE("to_net -> succeeded!");
      fd.state = WAIT_REPLY_REE;
    } else {
      NOTE("to_net -> failed!");
      fd.state = SEND_REQ_REE;
    }

    retry_rqst_next_cycle(fd);
    return;
  }

  /*--- WAIT_REPLY_REE ---*/
  if (fd.state == WAIT_REPLY_REE) {
    print_state_info(fd);
    /*-- One node is in E state --*/
    // do nothing
    // If reply comes in, this state should be changed

    fd.state = WAIT_REPLY_REE;
    retry_rqst_next_cycle(fd);
    return;
  } 

  /*--- REPLY_ARRIVED_REE ---*/
  if (fd.state == REPLY_ARRIVED_REE) {
    print_state_info(fd);
    fd.pc.permit_tag = SHARED;

    if (fd.node_id == node) {
      fd.state = REPLY_TO_PROC;
    } else {
      fd.state = REPLY_TO_RQSTR;
    }
  }

  /*--- READY_RMM ---*/
  if (fd.state == READY_REM) {
    print_state_info(fd);
    // @ put into the queue
    fd.state = SEND_REQ_RMM;
  }
  
  /*--- SEND_REQ_RMM ---*/
  if (fd.state == SEND_REQ_RMM) {
    print_state_info(fd);
    /*-- another node is in M state --*/
    //** expected action: make the node write back and invalidate **//

    // @ network command in this case: [WRITE][MODIFIED]
    pc.busop = WRITE;
    pc.permit_tag = MODIFIED;

    // @ send write back request to the node who has a copy in M state
    int net_dst = fd.dir_rsp.m_dst;
    net_cmd_t net_cmd;

    net_cmd.src = node;
    net_cmd.dest = net_dst;
    net_cmd.proc_cmd = pc;

    fd.net_success = net->to_net(node, REQUEST, net_cmd);

    if (fd.net_success) {
      NOTE("to_net -> succeeded!");
      fd.state = WAIT_REPLY_RMM;
    } else {
      NOTE("to_net -> failed!");
      fd.state = SEND_REQ_RMM;
    }

    retry_rqst_next_cycle(fd);
    return;
  }

  /*--- WAIT_REPLY_RMM ---*/
  if (fd.state == WAIT_REPLY_RMM) {
    print_state_info(fd);
    // do nothing
    // If reply comes in, this state should be changed

    fd.state = WAIT_REPLY_RMM;
    retry_rqst_next_cycle(fd);
    return;
  }

  /*--- REPLY_ARRIVED_RMM ---*/
  if (fd.state == REPLY_ARRIVED_RMM) {
    print_state_info(fd);
    if (fd.node_id == node) {
      fd.state = REPLY_TO_PROC;
    } else {
      fd.state = REPLY_TO_RQSTR;
    }
  }

  /*--- READY_RMES ---*/
  if (fd.state == READY_RMES) {
    print_state_info(fd);
    // TODO: make sure if the default value of e_dst is -1
    fd.num_to_inv = (fd.dir_rsp.e_dst != -1) ? 1 : fd.dir_rsp.s_count;

    fd.state = SEND_REQ_RMES;
  }

  /*--- SEND_REQ_RMES ---*/
  if (fd.state == SEND_REQ_RMES) {
    print_state_info(fd);
    /*-- at least one E/S states --*/
    //** expected action: invalidate them all **// 

    // @ network command in this case: [INVALIDATE][MODIFIED]
    pc.busop = INVALIDATE;
    pc.permit_tag = MODIFIED;

    net_cmd_t net_cmd;
    net_cmd.src = node;
    net_cmd.proc_cmd = pc;

    // @ send request to the nodes that are in E or S states
    if (fd.dir_rsp.e_dst != -1) { 
      // one node in E state
      net_cmd.dest = fd.dir_rsp.e_dst;
      fd.net_success = net->to_net(node, REQUEST, net_cmd);
      
      if (fd.net_success) {
        NOTE("to_net -> succeeded!");
        fd.dir_rsp.e_dst = -1;
      } else {
        NOTE("to_net -> failed!");
        fd.state = SEND_REQ_RMES;
        retry_rqst_next_cycle(fd);
      }

      return;
    } else if (!fd.dir_rsp.s_dst_list.empty()) { 
      // some nodes in S state
      net_cmd.dest = fd.dir_rsp.s_dst_list.front();
      fd.net_success = net->to_net(node, REQUEST, net_cmd);
      
      if (fd.net_success) {
        fd.dir_rsp.s_dst_list.pop_front();
      } else {
        fd.state = SEND_REQ_RMES;
      }

      retry_rqst_next_cycle(fd);
      return;
    } else {
      fd.state = WAIT_REPLY_RMES;
    }
  }

  /*--- WAIT_REPLY_RMES ---*/
  if (fd.state == WAIT_REPLY_RMES) {
    print_state_info(fd);
    if (fd.num_to_inv == 0) {
      fd.state = REPLY_ARRIVED_RMES;
    } else {
      fd.state = WAIT_REPLY_RMES;
      retry_rqst_next_cycle(fd);
    }
  }

  /*--- REPLY_ARRIVED_RMES ---*/
  if (fd.state == REPLY_ARRIVED_RMES) {
    print_state_info(fd);
    if (node == fd.node_id) {
      // I am the original requester
      fd.state = REPLY_TO_PROC;
    } else {
      // Another node is the original requester
      fd.state = REPLY_TO_RQSTR;
    }
  }

  /*--- REPLY_TO_PROC ---*/
  if (fd.state == REPLY_TO_PROC) {
    print_state_info(fd);
    if (accss_cache_per_cycle + 1 <= 1) {
      ++accss_cache_per_cycle;
      cache->reply(fd.pc); 
      NOTE_ARGS(("cache->reply: [PERMIT_TAG: %s]", PERMIT_TAG_STR[fd.pc.permit_tag]));
      for (unsigned i = 0; i < CACHE_LINE_SIZE; ++i) {
        NOTE_ARGS(("fd.pc.data[%d] = %d", i, fd.pc.data[i]));
      }
      proc_cmd_p = false;

      fd.state = UPDATE_DIR;
    } else {
      fd.state = REPLY_TO_PROC;
      retry_rqst_next_cycle(fd);
      return;
    }
  }

  /*--- REPLY_TO_RQSTR ---*/
  if (fd.state == REPLY_TO_RQSTR) {
    print_state_info(fd);
    net_cmd_t net_cmd;
    net_cmd.dest = fd.node_id;
    net_cmd.src = node;
    net_cmd.proc_cmd = fd.pc;

    fd.net_success = net->to_net(node, REPLY, net_cmd);
    if (fd.net_success) {
      NOTE("to_net -> succeeded!");
      fd.state = WAIT_REPLY_RQSTR;
    } else {
      NOTE("to_net -> failed!");
      fd.state = REPLY_TO_RQSTR;
    }

    retry_rply_next_cycle(fd);

    return;
  }

  /*--- WAIT_REPLY_RQSTR ---*/
  if (fd.state == WAIT_REPLY_RQSTR) {
    print_state_info(fd);
    // do nothing
    
    fd.state = WAIT_REPLY_RQSTR;
    retry_rqst_next_cycle(fd);
  }

  /*--- WRITE_BACK_RQST ---*/
  if (fd.state == WRITE_BACK_RQST) {
    if (accss_cache_per_cycle + 1 <= 1) {
      cache_access_response_t car;
      cache->cache_access(fd.pc.addr, INVALID, &car);
      read_cacheline(car, fd.pc.data); 
      ++accss_cache_per_cycle;
      fd.state = CHANGE_PERMIT_TAG;
    } else {
      fd.state = WRITE_BACK_RQST;
      retry_rply_next_cycle(fd);
    }

    return;
  }

  /*--- CHANGE_PERMIT_TAG ---*/
  if (fd.state == CHANGE_PERMIT_TAG) {
    if (accss_cache_per_cycle + 1 <= 1) {
      cache_access_response_t car;
      cache->cache_access(fd.pc.addr, INVALID, &car);
      cache->modify_permit_tag(car, fd.my_next_permit_tag);
      ++accss_cache_per_cycle;
      fd.state = JUST_REPLY_RQSTR;
    } else {
      fd.state = CHANGE_PERMIT_TAG;
      retry_rply_next_cycle(fd);
    }
  }

  /*--- JUST_REPLY_RQSTR ---*/
  if (fd.state == JUST_REPLY_RQSTR) {
    print_state_info(fd);
    net_cmd_t net_cmd;
    net_cmd.dest = fd.node_id;
    net_cmd.src = node;
    net_cmd.proc_cmd = fd.pc;

    fd.net_success = net->to_net(node, REPLY, net_cmd);
    if (fd.net_success) {
      fd.state = FINISH;
    } else {
      fd.state = JUST_REPLY_RQSTR;
      retry_rply_next_cycle(fd);
    }

    return;
  }

  /*--- JUST_REPLY_PROC ---*/
  if (fd.state == JUST_REPLY_PROC) {
    print_state_info(fd);
    if (accss_cache_per_cycle + 1 <= 1) {
      ++accss_cache_per_cycle;
      cache->reply(fd.pc);
      proc_cmd_p = false;
      for (unsigned i = 0; i < CACHE_LINE_SIZE; ++i) {
        NOTE_ARGS(("fd.pc.data[%d] = %d", i, fd.pc.data[i]));
      }
      fd.state = FINISH;
    } else {
      fd.state = JUST_REPLY_PROC;
      retry_rqst_next_cycle(fd);
      return;
    }
  }

  /*--- UPDATE_DIR ---*/
  if (fd.state == UPDATE_DIR) {
    print_state_info(fd);
    // update directory here
    if (mem_accss_per_cycle + 1 <= 6) {
      dir->update(fd.node_id, fd.pc.addr, fd.next_permit_tag);
      NOTE_ARGS(("UPDATE DIRECTORY: [NODE: %d][PERMIT_TAG: %s][FOR_NODE: %d][TAG: %d]", node, PERMIT_TAG_STR[fd.next_permit_tag], fd.node_id, (fd.pc.addr) >> address_tag_shift));
      ++mem_accss_per_cycle;
      fd.state = FINISH;
    } else {
      fd.state = UPDATE_DIR;
      retry_rqst_next_cycle(fd);
      return;
    }
  }

  /*--- FINISH ---*/
  if (fd.state == FINISH) {
    print_state_info(fd);
    // @ check if fd is in rqst_queue 
    // @ if it is in the queue, dequeue it
    if(exist_in(rqst_queue, fd)) { 
      dequeue_from(rqst_queue, fd); 
    }

    // @ check if fd is in rply_queue 
    // @ if it is in the queue, dequeue it
    if(exist_in(rply_queue, fd)) {
      dequeue_from(rply_queue, fd);
    }

    // If there is a waiting request on the same cache line, 
    // put it in the rqst_queue
    if (fd.waiting_rqst != NULL) {
      // rqst_queue.push_back(*(fd.waiting_rqst));
      retry_rqst_next_cycle(*(fd.waiting_rqst));
    }

    if (fd.node_id == node) {
      fd.waiting_rqst = NULL;
      proc_retried = false;
    }
  }
}

void iu_t::invalidate(fsm_data_t &fd) {
  proc_cmd_t pc = fd.pc;
  int dest = gen_node(pc.addr);
  int lcl = gen_local_cache_line(pc.addr);

  if (i_am_homesite(pc.addr)) { 
    fd.next_permit_tag = INVALID;
    if (fd.node_id == node)
      fd.state = REPLY_TO_PROC;
    else
      fd.state = REPLY_TO_RQSTR;

  } else {
    fd.state = READY_HMST;
  }
}

void iu_t::retry_rqst_next_cycle(fsm_data_t &fd) {
  if (fd.node_id != node) {
    // this is the request from another node
    rqst_queue.push_back(fd);
  } else {
    // this is the request from this node
    proc_retried = true;
  }
}

void iu_t::retry_rply_next_cycle(fsm_data_t &fd) {
  if (fd.node_id != node ) {
    rply_queue.push_back(fd);
  } else {
    proc_retried = true;
  }
}

// TODO: counting local_accesses and global accesses
// return value: 1(retry), 0(finished)
void iu_t::mem_rd(fsm_data_t &fd) {
  int dest = gen_node(fd.pc.addr);
  int lcl = gen_local_cache_line(fd.pc.addr);
  dir_response_t dir_rsp;

  if (i_am_homesite(fd.pc.addr)) { 
    // I am the homesite and processing this node's read
    if (mem_accss_per_cycle + 1 <= 6) {
      ++mem_accss_per_cycle;
      dir_rsp = dir->lookup(fd.pc.addr);
      fd.dir_rsp = dir_rsp;
      NOTE_ARGS(("dir->lookup: [STATE: %s]", DIR_STATE_STR[dir_rsp.state]));
    } else {
      fd.state = FIRST_CYCLE;
      retry_rqst_next_cycle(fd);
      
      return;
    }

    switch (dir_rsp.state) {
      case ALL_I:
        /*--  I only have a copy or none has a copy --*/
        // @ set permit tag to E
        fd.pc.permit_tag = EXCLUSIVE;
        fd.next_permit_tag = EXCLUSIVE;

        // @ read from memory and send back to proc
        if (mem_accss_per_cycle + 1 <= 6) {
          ++mem_accss_per_cycle;
          copy_cache_line(fd.pc.data, mem[lcl]);
          if (fd.node_id == node) {
            fd.state = REPLY_TO_PROC;
          } else {
            fd.state = REPLY_TO_RQSTR;
          }
        } else {
          fd.state = FIRST_CYCLE;
          
        }
        
        return;

      case ME_E:
        if (fd.node_id == node) {
          ASSERT(0); // this should be hit in cache
        } else {
          cache_access_response_t car;
          data_t data;

          fd.next_permit_tag = SHARED;
          fd.pc.permit_tag = SHARED;

          if ((accss_cache_per_cycle + 1 <= 1) 
              and (mem_accss_per_cycle + 1 <= 6)) {
            ++accss_cache_per_cycle;
            cache->cache_access(fd.pc.addr, INVALID, &car);

            ++mem_accss_per_cycle;
            copy_cache_line(fd.pc.data, mem[lcl]);
            fd.my_next_permit_tag = SHARED;
            cache->modify_permit_tag(car, SHARED);
            fd.state = UPDATE_MY_DIR;
          } else {
            fd.state = FIRST_CYCLE;
            retry_rqst_next_cycle(fd);
          }
        }

        return;

      case SOME_S:
        /*-- Two or more have a copy and are in S state --*/
        // @ set permit tag to S
        fd.pc.permit_tag = SHARED;
        fd.next_permit_tag = SHARED;

        // @ read from memory and send back to proc
        if (mem_accss_per_cycle + 1 <= 6) {
          ++mem_accss_per_cycle;
          copy_cache_line(fd.pc.data, mem[lcl]);
        } else {
          fd.state = FIRST_CYCLE;
          retry_rqst_next_cycle(fd);

          return;
        }

        if (fd.node_id == node) {
          fd.state = REPLY_TO_PROC;
        } else {
          fd.state = REPLY_TO_RQSTR;
        }

        return;

      case ONE_M:
        /*-- One node is in M state --*/
        /** Expected action: WB, M->S **/
        fd.state = READY_REM;
        fd.next_permit_tag = SHARED;
        return; 

      case ONE_E:
        /*-- One node is in E state --*/
        //** Expected action: E->S **/
        fd.state = READY_REE;
        fd.next_permit_tag = SHARED;
        return; 

      case ME_M:
        /*-- other's read request and I am recent modifier --*/
        ASSERT(fd.node_id != node);
        cache_access_response_t car;
        data_t data;

        fd.next_permit_tag = SHARED;
        fd.pc.permit_tag = SHARED;
        fd.my_next_permit_tag = SHARED;

        if ((accss_cache_per_cycle + 1 <= 1) 
            and (mem_accss_per_cycle + 1 <= 6)) {
          cache->cache_access(fd.pc.addr, INVALID, &car);
          read_cacheline(car, fd.pc.data); 
          copy_cache_line(mem[lcl], fd.pc.data);

          // M -> S
          cache->modify_permit_tag(car, SHARED);
          fd.state = UPDATE_MY_DIR;

        } else {
          fd.state = FIRST_CYCLE;
          retry_rqst_next_cycle(fd);

        }

        return;

      default:
        printf("[Error] undefined state for processing [READ][EXCLUSIVE]\n");
        break;
    }
  } else { 
    // I am not the homesite
    fd.state = READY_HMST; 
  }
}


void iu_t::mem_wr(fsm_data_t &fd) {
  proc_cmd_t pc = fd.pc;
  int dest = gen_node(pc.addr);
  int lcl = gen_local_cache_line(pc.addr);

  if (i_am_homesite(pc.addr)) {
    // I am homesite
    // @ lookup directory
    dir_response_t dir_rsp = dir->lookup(pc.addr);
    fd.dir_rsp = dir_rsp;
    ++mem_accss_per_cycle;

    switch(dir_rsp.state) {
      case ME_M:
        if (fd.node_id == node) {
          // I am the requester and I have the dirty cacheline
          ASSERT(0); // this should be cache hit
        }  else {
          // I am not the requester and I have the dirty cacheline
          data_t data;
          cache_access_response_t car;

          fd.next_permit_tag = MODIFIED;
          fd.pc.permit_tag = MODIFIED;

          if ((accss_cache_per_cycle + 1 <= 1) 
              and (mem_accss_per_cycle + 1 <= 6)) {
            ++accss_cache_per_cycle;
            cache->cache_access(pc.addr, INVALID, &car);

            ++mem_accss_per_cycle;
            dir->update(node, fd.pc.addr, INVALID);
            NOTE_ARGS(("UPDATE DIRECTORY: [NODE: %d][PERMIT_TAG: INVALID][FOR_NODE: %d][TAG: %d]", node, fd.node_id, gen_local_cache_line(fd.pc.addr)));
            read_cacheline(car, fd.pc.data); 
            cache->modify_permit_tag(car, INVALID);

            fd.my_next_permit_tag = INVALID;
            fd.state = UPDATE_MY_DIR;
          } else {
            fd.state = FIRST_CYCLE;
            retry_rqst_next_cycle(fd);
          }

        }
        return;

      case ME_E:
        if (fd.node_id == node) {
          fd.next_permit_tag = MODIFIED;
          fd.pc.permit_tag = MODIFIED;
          fd.state = REPLY_TO_PROC;
        } else {
          // I am not the requester and I have the clean cacheline
          cache_access_response_t car;
          data_t data;

          fd.next_permit_tag = MODIFIED;
          fd.pc.permit_tag = MODIFIED;

          if ((accss_cache_per_cycle + 1 <= 1) 
              and (mem_accss_per_cycle + 1 <= 6)) {
            ++accss_cache_per_cycle;
            cache->cache_access(pc.addr, INVALID, &car);

            ++mem_accss_per_cycle;
            dir->update(node, fd.pc.addr, INVALID);
            copy_cache_line(fd.pc.data, mem[lcl]);
          
            NOTE_ARGS(("UPDATE DIRECTORY: [NODE: %d][PERMIT_TAG: INVALID][FOR_NODE: %d][TAG: %d]", node, fd.node_id, gen_local_cache_line(fd.pc.addr)));

            for (unsigned i = 0; i < CACHE_LINE_SIZE; ++i) {
              NOTE_ARGS(("fd.pc.data[%d] = %d", i, fd.pc.data[i]));
            }

            cache->modify_permit_tag(car, INVALID);
            fd.my_next_permit_tag = INVALID;
            fd.state = UPDATE_MY_DIR;
          } else {
            fd.state = FIRST_CYCLE;
            retry_rqst_next_cycle(fd);
          }
 
        }
        return;

      case ALL_I:
        /*-- I am the only owner or none of us is owner --*/
        // @ read cacheline from memory
        if (mem_accss_per_cycle + 1 <= 6) {
          ++mem_accss_per_cycle;
          copy_cache_line(fd.pc.data, mem[lcl]);
        } else {
          fd.state = FIRST_CYCLE;
          retry_rqst_next_cycle(fd);
          return;
        }

        // @ update directory (M/E/I -> M)
        // dir->update(node, pc.addr, MODIFIED);
        fd.next_permit_tag = MODIFIED;
        fd.pc.permit_tag = MODIFIED;

        if (fd.node_id == node)
          fd.state = REPLY_TO_PROC;
        else
          fd.state = REPLY_TO_RQSTR;

        return;

      case ONE_M:
        fd.state = READY_RMM;
        fd.next_permit_tag = MODIFIED;
        fd.pc.permit_tag = MODIFIED;
        return;
        
      case ONE_E:
      case SOME_S:
        /*-- at least one E/S states --*/
        //** expected action: invalidate them all **// 
        fd.state = READY_RMES;
        fd.next_permit_tag = MODIFIED;
        fd.pc.permit_tag = MODIFIED;
        return;

      default:
        printf("[Error] No matching with the state of directory lookup response.\n");
        exit(1);
        break;
    }
  } else {
    fd.state = READY_HMST;
  }
}

void iu_t::mem_wb(fsm_data_t &fd) {
  proc_cmd_t pc = fd.pc;
  int dest = gen_node(pc.addr);
  int lcl = gen_local_cache_line(pc.addr);
  
  if (i_am_homesite(pc.addr)) {
    // @ write back to memory
    if (mem_accss_per_cycle + 1 <= 6) {
      ++mem_accss_per_cycle;
      copy_cache_line(mem[lcl], fd.pc.data);
    } else {
      fd.state = FIRST_CYCLE;
      retry_rqst_next_cycle(fd);
      return;
    }

    // @ update directory (M->I)
    fd.next_permit_tag = INVALID;
    fd.pc.permit_tag = INVALID;

    if (fd.node_id == node)
      fd.state = REPLY_TO_PROC;
    else
      fd.state = REPLY_TO_RQSTR;

  } else {
    fd.state = READY_HMST; 
  }
}

bool iu_t::process_proc_request(proc_cmd_t pc) {
  // fsm_data_t fd;
  if (!proc_retried) {
    int lcl = gen_local_cache_line(pc.addr);
    NOTE_ARGS(("PROC REQUEST_ARRIVED: [BUSOP: %s][PERMIT_TAG: %s][TAG: %d]", BUSOP_STR[pc.busop], PERMIT_TAG_STR[pc.permit_tag], pc.addr >> address_tag_shift));
    proc_fd.state = FIRST_CYCLE;
    proc_fd.node_id = node;
    proc_fd.pc = pc;
    proc_fd.waiting_rqst = NULL;
    proc_retried = true;
  }

  fsm(proc_fd);

  return(proc_fd.net_success);
}

#endif


#ifdef ORIGINAL_CODE
// receive a net request
bool iu_t::process_net_request(net_cmd_t net_cmd) {
  proc_cmd_t pc = net_cmd.proc_cmd;

  int lcl = gen_local_cache_line(pc.addr);
  int src = net_cmd.src;
  int dest = net_cmd.dest;

  // ***** FYTD *****
  // sanity check
  if (gen_node(pc.addr) != node) 
    ERROR("sent to wrong home site!");

  switch(pc.busop) {
  case READ: // assume local
    net_cmd.dest = src;
    net_cmd.src = dest;
    copy_cache_line(pc.data, mem[lcl]);
    net_cmd.proc_cmd = pc;


    return(net->to_net(node, REPLY, net_cmd));
      
  case WRITE:
    copy_cache_line(mem[lcl], pc.data);
    return(false);
      
  case INVALIDATE: 
    // ***** FYTD *****
    return(false);  // need to return something for now
  }
}
#else

bool iu_t::is_homesite_rqst(proc_cmd_t pc) {
  if (((pc.busop == READ) and (pc.permit_tag == EXCLUSIVE)) or
      ((pc.busop == WRITE) and (pc.permit_tag == EXCLUSIVE)) or
      ((pc.busop == READ) and (pc.permit_tag == MODIFIED)) or
      ((pc.busop == INVALIDATE) and (pc.permit_tag == EXCLUSIVE)))
    return true;
  else
    return false;
}

void iu_t::read_cacheline (cache_access_response_t car, data_t data) {
  for (unsigned i = 0; i < CACHE_LINE_SIZE; ++i) {
    data[i] = cache->read_data(i, car);
  }
}

bool iu_t::process_net_request(net_cmd_t net_cmd) {
  ++rcv_from_net_per_cycle;
  proc_cmd_t pc = net_cmd.proc_cmd;
  fsm_data_t fd;
  fd.node_id = net_cmd.src;
  fd.pc = pc;
  fd.waiting_rqst = NULL;

  NOTE_ARGS(("NET REQUEST_ARRIVED: [FROM: %d][TO: %d][BUSOP: %s][PERMIT_TAG: %s]", net_cmd.src, net_cmd.dest, BUSOP_STR[fd.pc.busop], PERMIT_TAG_STR[fd.pc.permit_tag]));
  if (is_homesite_rqst(pc)) {
    fd.state = FIRST_CYCLE;

  } else if((pc.busop == READ) and (pc.permit_tag == SHARED)) {
    /** Expected action: WB, M -> S **/
    fd.my_next_permit_tag = SHARED;
    fd.state = WRITE_BACK_RQST;

  } else if ((pc.busop == INVALIDATE) and (pc.permit_tag == SHARED)) {
    /** Expected action: E -> S **/
    fd.my_next_permit_tag = SHARED;
    fd.state = CHANGE_PERMIT_TAG;

  } else if ((pc.busop == WRITE) and (pc.permit_tag == MODIFIED)) {
    /** Expected action: WB, M -> I **/
    fd.my_next_permit_tag = INVALID;
    fd.state = WRITE_BACK_RQST;
    
  } else if ((pc.busop == INVALIDATE) and (pc.permit_tag == MODIFIED)) {
    /** Expected action: * -> I **/
    fd.my_next_permit_tag = INVALID;
    fd.state = CHANGE_PERMIT_TAG;
  
  } else {
    printf("[Error] undefined request from network.\n");
    exit(1);
  }

  fsm(fd);

  return(fd.net_success);
}

#endif


#ifdef ORIGINAL_CODE
// receive a net reply
bool iu_t::process_net_reply(net_cmd_t net_cmd) {
  proc_cmd_t pc = net_cmd.proc_cmd;

  // ***** FYTD *****

  proc_cmd_p = false; // clear out request that this reply is a reply to

  switch(pc.busop) {
  case READ: // assume local
    cache->reply(pc);
    return(false);
      
  case WRITE:
  case INVALIDATE:
    ERROR("should not have gotten a reply back from a write or an invalidate, since we are incoherent");
    return(false);  // need to return something for now
  }
}

#else

bool iu_t::process_net_reply(net_cmd_t net_cmd) {
  ++rcv_from_net_per_cycle;
  proc_cmd_t pc = net_cmd.proc_cmd;
  fsm_data_t *fd = get_dir_holder(net_cmd.proc_cmd.addr);
  if (fd == NULL) {
    fd = &proc_fd;
  }

  NOTE_ARGS(("REPLY_ARRIVED: [FROM: %d][TO: %d][BUSOP: %s][PERMIT_TAG: %s]", net_cmd.src, net_cmd.dest, BUSOP_STR[pc.busop], PERMIT_TAG_STR[pc.permit_tag]));
  fd->pc = net_cmd.proc_cmd;

  if (fd->state == WAIT_REPLY_HMST) {
    fd->state = SEND_REPLY_HMST;
    for (unsigned i = 0; i < CACHE_LINE_SIZE; ++i) {
      NOTE_ARGS(("fd.pc.data[%d] = %d", i, fd->pc.data[i]));
    }
    ASSERT(fd == &proc_fd);
  } else if (fd->state == WAIT_REPLY_REM) {
    fd->state = REPLY_ARRIVED_REM;
  } else if (fd->state == WAIT_REPLY_REE) {
    fd->state = REPLY_ARRIVED_REE;
  } else if (fd->state == WAIT_REPLY_RMM) {
    fd->state = REPLY_ARRIVED_RMM;
  } else if (fd->state == WAIT_REPLY_RMES) {
    --fd->num_to_inv;
    assert(fd->num_to_inv >= 0);
  } else if (fd->state == WAIT_REPLY_RQSTR) {
    fd->state = UPDATE_DIR;
  } else {
    // undefined condition
    NOTE("[ERROR] undefined reply received.");
    NOTE_ARGS(("fd->state: [NODE: %d][STATE: %s]", node, STATES_STRING[fd->state]));
    ASSERT(0);
  }
}

#endif

void iu_t::print_stats() {
  printf("------------------------------\n");
  printf("%d: iu\n", node);
  
  printf("num local  accesses = %d\n", local_accesses);
  printf("num global accesses = %d\n", global_accesses);
}
