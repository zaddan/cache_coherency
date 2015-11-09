// test.cpp
//   Derek Chiou
//     May 19, 2007

// STUDENTS: YOU ARE EXPECTED TO PUT YOUR TESTS IN THIS FILE, ALONG WITH PROC.CPP.

#include <stdio.h>
#include "types.h"
#include "generic_error.h"
#include "helpers.h"
#include "cache.h"
#include "test.h"
#include <stdlib.h>
#include "iu.h"
using namespace std;
extern args_t args;
extern cache_t **caches;
extern iu_t **ius;
int reqSatisfied;
extern const int LG_INTERLEAVE_SIZE;

test_args_t test_args;

req_list_t **reqListLog; //request list Log. the size is initilized similar to req_list

test_t test;

void logReq(int reqCounter, int node, int cycle){
  //printf("\n\n\n%d %d %d\n", reqCounter, node,cycle);
  reqListLog[node][reqCounter].addr = test.reqList[node][reqCounter].addr;
  reqListLog[node][reqCounter].data = test.reqList[node][reqCounter].data;
  reqListLog[node][reqCounter].cmd = test.reqList[node][reqCounter].cmd;
  reqListLog[node][reqCounter].cycle = cycle;
}

//void printReqListLog(int proc_id) {
//  for (int i =0; i<proc_id; i++) {
//    printf("----PROC---- %d \n", i); 
//    for (int j =0; j<test.listSize[i]; j++) {
//      printf("cmd: %d ",reqListLog[i][j].cmd);
//      printf("addr: %d ",reqListLog[i][j].addr);
//      printf("data: %d ",reqListLog[i][j].data);
//      printf("delay: %d ",reqListLog[i][j].delay);
//      printf("cyle: %d \n",reqListLog[i][j].cycle);
//    }
//  } 
//}
//
void test_t::init(int test_num) {
  reqSatisfied =  0;
  reqList =  (req_list_t**)malloc(sizeof(req_list_t*)*args.num_procs);
  listSize = (int*) malloc(sizeof(int)*args.num_procs);
  reqCounterList = (int*) malloc(sizeof(int)*args.num_procs);
  reqListLog =  (req_list_t**)malloc(sizeof(req_list_t*)*args.num_procs);
  assert(listSize);

  local_mem_offset = 1 <<  LG_INTERLEAVE_SIZE;
  //global_addr_space = MEM_SIZE * CACHE_LINE_SIZE * args.num_procs + 1;
  //global_addr_space = 64;
  global_addr_space = 1000;
  switch(test_num) {
    case 0:
      test_args.addr_range = MEM_SIZE;
      break;

    //each processor acceses 10th location in local memory
    case 1:
      for (int i = 0; i < args.num_procs ; ++i ) {
        listSize[i] = 2;
        reqList[i] =  (req_list_t*)malloc(sizeof(req_list_t)*(listSize[i]));
        assert(reqList[i]);

        int addr = i * local_mem_offset + 10; // to ensure every access is local
        reqList[i][0] = {1, addr , 200, 5, 0};
        reqList[i][1] = {0, addr ,   0, 3, 0};
      }
      break;

      // processor 0 write to address 0, everybody else reads
    case 2:
      for (int i = 0; i < args.num_procs ; ++i ) {
        listSize[i] = 1;
        reqList[i] =  (req_list_t*)malloc(sizeof(req_list_t)*(listSize[i]));
        assert(reqList[i]);

        reqList[i][0] = { (i==0?1:0), 0 , (i==0?200:0) , 0, 0};
      }
      break;

      //all processors write to and then read from global address 0
    case 3:
      for (int i = 0; i < args.num_procs ; ++i ) {
        listSize[i] = 2;
        reqList[i] =  (req_list_t*)malloc(sizeof(req_list_t)*(listSize[i]));
        assert(reqList[i]);

        reqList[i][0] = {1, 0 , i, 0, 0};
        reqList[i][1] = {0, 0 , 0, 0, 0};
      }
      break;

      //each processor write to all local memory locations and then read them back (needs replacement)
    case 4:
      for (int i = 0; i < args.num_procs ; ++i ) {
        listSize[i] = local_mem_offset*2;
        reqList[i] =  (req_list_t*)malloc(sizeof(req_list_t)*(listSize[i]));
        assert(reqList[i]);

        for (int j = 0; j < local_mem_offset ; j++ ) {
          int addr_offset = i * local_mem_offset;
          reqList[i][j] = {1, addr_offset+j, j, 0, 0};
        }
        for (int j = 0; j < local_mem_offset ; j++ ) {
          int addr_offset = i * local_mem_offset;
          reqList[i][local_mem_offset+j] = {0, addr_offset+j, 0, 0, 0};
        }
      }
      break;

      //processor 0 write to all global memory address space and reads back (needs replacement)
      //other processors do nothing
    case 5:
      for (int i = 0; i < args.num_procs ; ++i ) {
        listSize[i] = (i==0)?global_addr_space*2:0;
        reqList[i] =  (req_list_t*)malloc(sizeof(req_list_t)*(listSize[i]));

        if (i==0) {
          assert(reqList[i]);
          for (int j = 0; j < global_addr_space ; j++ ) {
            reqList[i][j] = {1, j, j, 0, 0};
          }
          for (int j = 0; j < global_addr_space ; j++ ) {
            reqList[i][global_addr_space+j] = {0, j, 0, 0, 0};
          }
        }
      }
      break;

      //all processors write their node number to all global memory address space and read them back (many valid answers)
    case 6:
      for (int i = 0; i < args.num_procs ; ++i ) {
        listSize[i] = global_addr_space*2;
        reqList[i] =  (req_list_t*)malloc(sizeof(req_list_t)*(listSize[i]));
        assert(reqList[i]);

        for (int j = 0; j < global_addr_space ; j++ ) {
          reqList[i][j] = {1, j, j, 0, 0};
        }
        for (int j = 0; j < global_addr_space ; j++ ) {
          reqList[i][global_addr_space+j] = {0, j, 0, 0, 0};
        }
      }
      break;

    // node i writes to (i+1), i+1 read from i
    case 7:
      for (int i = 0; i < args.num_procs ; ++i ) {
        listSize[i] = 2;
        reqList[i] =  (req_list_t*)malloc(sizeof(req_list_t)*(listSize[i]));
        assert(reqList[i]);

        int addr_w = ((i+1+args.num_procs)%args.num_procs) * local_mem_offset + 10; // 10th location at next neighbor
        int addr_r = ((i-1+args.num_procs)%args.num_procs) * local_mem_offset + 10; // 10th location at previous neighbor
        reqList[i][0] = {1, addr_w , i, 0, 0};
        reqList[i][1] = {0, addr_r , 0,   900, 0};
      }
      break;

    // write to invalid HS, other caches invalid.
    case 8:
      for (int i = 0; i < args.num_procs ; ++i ) {
        listSize[i] = (i==0)?1:0;
        reqList[i] =  (req_list_t*)malloc(sizeof(req_list_t)*(listSize[i]));

        if (i==0) {
          assert(reqList[i]);
          reqList[i][0] = {1, 0, 10, 0, 0};
        }
      }
      break;
      
    // write to invalid HS, 1 other cache exclusive.
    case 9:
      for (int i = 0; i < args.num_procs ; ++i ) {
        listSize[i] = (i < 2) ? 1 : 0;
        reqList[i] =  (req_list_t*)malloc(sizeof(req_list_t)*(listSize[i]));

        if (i < 2) {
          assert(reqList[i]);
        }
        reqList[i][0] = {1, 0, 10, 0, 0};
      }
      break;
   case 10: //INVALID->EXCLUSIVE//every processor ends up being exclusive
      for (int i = 0; i < args.num_procs ; ++i ) {
        listSize[i] = 1;
        reqList[i] =  (req_list_t*)malloc(sizeof(req_list_t)*(listSize[i]));
        //reqListLog[i] =  (req_list_t*)malloc(sizeof(req_list_t)*(test.listSize[i]));
        assert(reqList[i]);
        reqList[i][0] = { 0 , i<<LG_CACHE_LINE_SIZE, i*7, 0 , i} ;
        //reqListLog[i][0] = { 0 , i<<LG_CACHE_LINE_SIZE, i*7, 0 , i} ;
      }
      break;
   case 11: //INVALID->MODIFIED //every processor ends up being MODIFIED 
      for (int i = 0; i < args.num_procs ; ++i ) {
        listSize[i] = 1;
        reqList[i] =  (req_list_t*)malloc(sizeof(req_list_t)*(listSize[i]));
        //reqListLog[i] =  (req_list_t*)malloc(sizeof(req_list_t)*(test.listSize[i]));
        assert(reqList[i]);
        reqList[i][0] = { 1 , i<<LG_CACHE_LINE_SIZE, i*7, 0 , i} ;
        //reqListLog[i][0] = { 1 , i<<LG_CACHE_LINE_SIZE, i*7, 0 , i} ;
      }
      break;
   case 12://MODIFY, MODIFY
     for (int i = 0; i < args.num_procs ; ++i ) {
        listSize[i] = 2;
        reqList[i] =  (req_list_t*)malloc(sizeof(req_list_t)*(listSize[i]));
        //reqListLog[i] =  (req_list_t*)malloc(sizeof(req_list_t)*(test.listSize[i]));
        assert(reqList[i]);
         
        reqList[i][0] = { 1 , i<<LG_CACHE_LINE_SIZE, 0, 0 , 2*i} ;
        //reqListLog[i][0] = { 1 , i<<LG_CACHE_LINE_SIZE, 0, 0 , 2*i} ;
        reqList[i][1] = { 1 , i<<LG_CACHE_LINE_SIZE, i+3, 100 , 2*i+1} ;
        //reqListLog[i][1] = { 1 , i<<LG_CACHE_LINE_SIZE, i+3, 0 , 2*i+1} ;
     }
      break;
   case 13:  //MODIFY -> INVALID
      //even processors modify, odd processor modify the same address
           // as the previous even processors = > even processor = INVALID, Odd = MODIFIED
      for (int i = 0; i < args.num_procs ; ++i ) {
        listSize[i] = 1;
        reqList[i] =  (req_list_t*)malloc(sizeof(req_list_t)*(listSize[i]));
        //reqListLog[i] =  (req_list_t*)malloc(sizeof(req_list_t)*(test.listSize[i]));
        assert(reqList[i]);
        if (i %2 == 0) {
            reqList[i][0] = { 1 , i<<LG_CACHE_LINE_SIZE, 7*i, 0 , i} ;
            //reqListLog[i][0] = { 1 , i<<LG_CACHE_LINE_SIZE, 7*i, 0 , i} ;

        }else{
            reqList[i][0] = { 1 , (i-1)<<LG_CACHE_LINE_SIZE, 7*i, 100 , i} ;
            //reqListLog[i][0] = { 1 , (i-1)<<LG_CACHE_LINE_SIZE, 7*i, 0 , i} ;
        }
      }
      break;
   //MODIFY->exclusive doesn't make sense 
   //SHARED->SHARED is not importatnt
   //SHARED->EXCLUSIVE is not possible
   case 14: //MODIFIED->SHARED->SHARED-> INVALID(OR MODIFIED)
      assert(args.num_procs>=4); 
      for (int i = 0; i < args.num_procs ; ++i ) {
          listSize[i] = 1;
          reqList[i] =  (req_list_t*)malloc(sizeof(req_list_t)*(listSize[i]));
          //reqListLog[i] =  (req_list_t*)malloc(sizeof(req_list_t)*(test.listSize[i]));
          assert(reqList[i]);
          if (i %4 == 0) {
              reqList[i][0] = { 1 , i<<LG_CACHE_LINE_SIZE, i, i + 0 , i} ;
              //reqListLog[i][0] = { 1 , i<<LG_CACHE_LINE_SIZE, i, 0 , i} ;
          }
          if (i %4 == 1) {
              reqList[i][0] = { 0 , (i-1)<<LG_CACHE_LINE_SIZE , i, 100 , i} ;
              //reqListLog[i][0] = { 0 , (i-1)<<LG_CACHE_LINE_SIZE , i, 0 , i} ;
          }
          if (i %4 == 2) {
              reqList[i][0] = { 0 , (i -2)<<LG_CACHE_LINE_SIZE, i, 200 , i} ;
              //reqListLog[i][0] = { 0 , (i -2)<<LG_CACHE_LINE_SIZE, i, 0 , i} ;
          }
          if (i %4 == 3) {
              reqList[i][0] = { 1 , (i-3)<<LG_CACHE_LINE_SIZE, 7*i, 400 , i} ;
              //reqListLog[i][0] = { 1 , (i-3)<<LG_CACHE_LINE_SIZE, 7*i, 0 , i} ;
          }
        } 
        break;
    case 15: //EXCLUSIVE->INVALID
      for (int i = 0; i < args.num_procs ; ++i ) {
          listSize[i] = 1;
          reqList[i] =  (req_list_t*)malloc(sizeof(req_list_t)*(listSize[i]));
          //reqListLog[i] =  (req_list_t*)malloc(sizeof(req_list_t)*(test.listSize[i]));
          assert(reqList[i]);
          if (i %2 == 0) {
              reqList[i][0] = { 1 , i<<LG_CACHE_LINE_SIZE, i, 0 , i} ;
              reqList[i][0] = { 0 , i<<LG_CACHE_LINE_SIZE, i, 100 , i} ;
              //reqListLog[i][0] = { 0 , i<<LG_CACHE_LINE_SIZE, i, 0 , i} ;
          }else{
              reqList[i][0] = { 1 , (i-1)<<LG_CACHE_LINE_SIZE, i, 200 , i} ;
              reqList[i][0] = { 1 , (i-1)<<LG_CACHE_LINE_SIZE, i, 400 , i} ;
              //reqListLog[i][0] = { 1 , (i-1)<<LG_CACHE_LINE_SIZE, i, 0 , i} ;
          }
        } 
        break;
    case 16: //EXCLUSIVE->MODIFIED
      for (int i = 0; i < args.num_procs ; ++i ) {
          listSize[i] = 2;
          reqList[i] =  (req_list_t*)malloc(sizeof(req_list_t)*(listSize[i]));
          //reqListLog[i] =  (req_list_t*)malloc(sizeof(req_list_t)*(test.listSize[i]));
          assert(reqList[i]);
          reqList[i][0] = { 0 , i<<LG_CACHE_LINE_SIZE, i, 0 , 2*i} ;
          //reqListLog[i][0] = { 0 , i<<LG_CACHE_LINE_SIZE, i, 0 , 2*i} ;
          reqList[i][1] = { 1 , i<<LG_CACHE_LINE_SIZE, i, 100 , 2*i+1} ;
          //reqListLog[i][1] = { 1 , i<<LG_CACHE_LINE_SIZE, i, 0 , 2*i+1} ;
      } 
      break;
    case 17: //EXCLUSIVE->SHARED
      for (int i = 0; i < args.num_procs ; ++i ) {
          listSize[i] = 1;
          reqList[i] =  (req_list_t*)malloc(sizeof(req_list_t)*(listSize[i]));
          //reqListLog[i] =  (req_list_t*)malloc(sizeof(req_list_t)*(test.listSize[i]));
          assert(reqList[i]);
          if (i %2 == 0) {
              reqList[i][0] = { 0 , i<<LG_CACHE_LINE_SIZE, i, 0 , i} ;
              //reqListLog[i][0] = { 0 , i<<LG_CACHE_LINE_SIZE, i, 0 , i} ;
          }else{
              reqList[i][0] = { 0 , (i-1)<<LG_CACHE_LINE_SIZE, i, 100 , i} ;
              //reqListLog[i][0] = { 0 , (i-1)<<LG_CACHE_LINE_SIZE, i, 0 , i} ;
          }
        } 
        break;
    default:
      ERROR("don't recognize this test");
  }

  for (int i =0; i<args.num_procs; i++) {
    reqListLog[i] =  (req_list_t*)malloc(sizeof(req_list_t)*(test.listSize[i]));
    reqCounterList[i] = -1;
  }
}

void test_t::finish(int test_num) {
  double hr;
  cache_access_response_t car;
  int data, addr;

  for (int i = 0; i < args.num_procs; ++i) {
    switch(test_num) {
      case 0:
        //outputs are not deterministic since input is random, compare with behavioral model
        break;

      case 1:
        addr = i * local_mem_offset + 10; // to ensure every access is local
        //verifying the result 
        assert(test.reqList[i][1].data == 200);
        //get_permit_tag(&car, caches[0], addr); 
        //ASSERT(car.permit_tag == MODIFIED);
        //get_data(caches[0], addr, &data);
        //ASSERT(data == 200);
        break;

      case 2:
        assert(test.reqList[i][0].data == 200);
        break;

      case 3:
        break;
      case 4:
        for (int j = 0; j < local_mem_offset ; j++ ) {
          assert(test.reqList[i][local_mem_offset+j].data == j);
        }
        break;

      case 5:
        if ( i == 0 ) {
          for (int j = 0; j < global_addr_space ; j++ ) {
            assert(test.reqList[i][global_addr_space+j].data == j);
          }
        }
        break;

      case 6:
        //needs sequential consistency check, or compare with behavioral model
        break;

      case 7:
        assert(test.reqList[i][1].data == ((i-2+args.num_procs)%args.num_procs));
        break;

      default:
        break; 
        //ERROR("don't recognize this test");
    }
  }
  printf("passed\n");

}

void test_t::print(int proc_id) {
  for (int i =0; i<proc_id; i++) {
    printf("----PROC---- %d \n", i);
    for (int j =0; j<listSize[i]; j++) {
      printf("cmd: %d ",reqList[i][j].cmd);
      printf("addr: %d ",reqList[i][j].addr);
      printf("data: %d ",reqList[i][j].data);
      printf("delay: %d ",reqList[i][j].delay);
      printf("cyle: %d \n",reqList[i][j].cycle);
    }
  }
  printf("%d\n", caches[0]->tags[0][0].permit_tag);
  printf("%d\n", ius[0]->mem[0][10]);
  /*for (int i =0; i<args.num_procs; i++) {
    printf("----PROC---- %d \n", i);
    for (int j =0; j<test.listSize[i]; j++) {
      printf("cmd: %d ",reqListLog[i][j].cmd);
      printf("addr: %d ",reqListLog[i][j].addr);
      printf("data: %d ",reqListLog[i][j].data);
      printf("delay: %d ",reqListLog[i][j].delay);
      printf("cyle: %d \n",reqListLog[i][j].cycle);
    }
  }*/
}

void init_test() {
  test.init(args.test);
  initBehavModel(); 
}

bool all_reqs_satisfied() {
  int totalNumReq = 0; 
  for (int i =0; i < args.num_procs; i++) {//get the totalNumReq
      totalNumReq += test.listSize[i];  
  } 
  
  printf("%d\n", reqSatisfied); 
     if (reqSatisfied < totalNumReq) {
   ERROR("number of requsts applied on the caches, where not equal to the number of request asked for the test. This either means that the test was not run long enough or the system hung somewhere which is a failure.");
  }
}


void finish_test() {
    // printf("----------------------------printing the reqList ------------------------\n"); 
  //test.print(args.num_procs);
#ifdef VERIFY_WITH_BEHAV_MODEL_FULLY
        verifyTestFully();
#endif 
  all_reqs_satisfied();
  test.finish(args.test);
 

}

