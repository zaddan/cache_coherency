// proc.cpp
//   by Derek Chiou
//      March 4, 2007
// 

// STUDENTS: YOU ARE EXPECTED TO MODIFY THIS FILE TO INSERT YOUR
// TESTS.  HOWEVER, YOU MUST BE ABLE TO ACCEPT OTHER PROC.CPP FILES,
// AS I WILL BE REPLACING YOUR PROC.CPP WITH MINE (AND YOUR FELLOW
// STUDENTS') FOR TESTING PURPOSES.

// for 382N-10



#include <stdio.h>
#include <stdlib.h>
#include "generic_error.h"
#include "cache.h"
#include "proc.h"
#include "test.h"

extern req_list_t **reqListLog;
extern int reqSatisfied;
proc_t::proc_t(int __p) {
  proc = __p;
  init();
}

void proc_t::init() {
  response.retry_p = false;
  ld_p = false;
  log_next = false;
}

void proc_t::bind(cache_t *c) {
  cache = c;
}

// ***** FYTD ***** 

// this is just a simple random test.  I'm not giving
// you any more test cases than this.  You will be tested on the
// correctness and performance of your solution.


void proc_t::advance_one_cycle() {
  int data;
  int data_to_store;
  int loaded_data;

  int prev_addr;
  int temp_addr;
  //static int reqCounter = -1;
  cache_access_response_t car;
  int *reqCounter = &test.reqCounterList[proc];

  //bool done_taking_logs = true; 
  switch (args.test) {
    case 0:
      if (!response.retry_p) {
        addr = random() % test_args.addr_range;
        ld_p = ((random() % 2) == 0);
      }
      if (ld_p) response = cache->load(addr, 0, &data, response.retry_p);
      else      response = cache->store(addr, 0, cur_cycle, response.retry_p);
      break;

    case 1: case 2: case 3: case 4: case 5: case 6: case 7: case 8: case 9: case 10: case 11: case 12: case 13: case 14: case 15: case 16: case 17: case 18:
      if (!response.retry_p) {

        if ( log_next == true ) {
          reqSatisfied +=1; 
            logReq(*reqCounter,  proc, cur_cycle - 1);
#ifdef VERIFY_WITH_BEHAV_MODEL_PER_INSTRUCTION 
          verifyTestPerInstruction(proc);
#endif 
        }

        if (*reqCounter >= test.listSize[proc] - 1) { //ending test
          log_next = false;
          break;
        }

        if( test.reqList[proc][*reqCounter+1].delay > 0) {
          test.reqList[proc][*reqCounter+1].delay -= 1;
          log_next = false;
          break;
        }
        *reqCounter+=1;
        log_next = true;
      }
      if (test.reqList[proc][*reqCounter].cmd == 1){
        response = cache->store(test.reqList[proc][*reqCounter].addr, 0, test.reqList[proc][*reqCounter].data, response.retry_p);
      }else {
        response = cache->load(test.reqList[proc][*reqCounter].addr, 0, &test.reqList[proc][*reqCounter].data, response.retry_p);
      }
      break;

    default:
      ERROR("don't know this test case");
  }
}

