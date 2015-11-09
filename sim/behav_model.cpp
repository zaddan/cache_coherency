//**********************************************************************************
//**********************************************************************************
//**********************************behavioral models*****************************
//**********************************************************************************
//**********************************************************************************
// the follwing functions are shared between behva cache_t and cache_t
// didn't need to re write them, possibly could have used inheritence
#include <stdio.h>
#include "types.h"
#include "generic_error.h"
#include "helpers.h"
#include "cache.h"
#include "test.h"
#include <stdlib.h>
#include <assert.h>
#include "iu.h"
#include "directory.h"
#include "test.h"

extern args_t args;
extern cache_t **caches;
extern req_list_t **reqList; //request list
extern req_list_t **reqListLog; //request list Log. the size is initilized similar to req_list


//only dump from here on
FILE *fp = fopen("logs.txt", "w+");
//fprintf(fp, "WARNING: THIS FILE ONLY CONTAINS THE CHANGES TO CACHE STATES WAS PROMISSED BY LOGS\n");
//
behav_cache_t  **behavCaches;
directory *behavDir;
data_t *behav_mem;

behav_cache_t::behav_cache_t(int __node, int __lg_assoc, int __lg_num_sets, int __lg_cache_line_size) {
  init(__node, __lg_assoc, __lg_num_sets, __lg_cache_line_size);
}

void behav_cache_t::init(int __node, int __lg_assoc, int __lg_num_sets, int __lg_cache_line_size) {
  node = __node;
  lg_assoc = __lg_assoc;
  lg_num_sets = __lg_num_sets;
  lg_cache_line_size = __lg_cache_line_size;

  full_hits = partial_hits = misses = 0;

  num_sets = (1 << lg_num_sets);
  assoc = (1 << lg_assoc);

  set_shift = lg_cache_line_size;
  set_mask = (1 << lg_num_sets) - 1;

  address_tag_shift = lg_cache_line_size + lg_num_sets;

  cache_line_mask = (1 << lg_cache_line_size) - 1;
  if ((cache_line_mask + 1) != CACHE_LINE_SIZE) {
    ERROR("inconsistent cache_line_size");
  }
  
  tags = new cache_line_t*[num_sets];

  for (int i = 0; i < num_sets; ++i) {
    tags[i] = new cache_line_t[assoc];

    for (int j = 0; j < assoc; ++j) {
      tags[i][j].permit_tag = INVALID;
      tags[i][j].replacement = -1;

      for (int k = 0; k < cache_line_size; ++k) {
	tags[i][j].data[k] = 0;
      }
    }
  }
}

void behav_cache_t::bind(iu_t *i) {
  iu = i;
}

void behav_cache_t::print_stats() {
  printf("------------------------------\n");
  printf("node %d\n", node);
  printf("full_hits      = %d\n", full_hits);
  printf("partial_hits   = %d\n", partial_hits);
  printf("misses         = %d\n", misses);
  printf("total accesses = %d\n", full_hits + partial_hits + misses);
  printf("hit rate = %f\n", hit_rate());
}

double behav_cache_t::hit_rate() {
  return((double)full_hits / (full_hits + partial_hits + misses));
}
  

address_tag_t behav_cache_t::gen_address_tag(address_t addr) {
  return(addr >> address_tag_shift);
}

int behav_cache_t::gen_offset(address_t addr) {
  return(addr & cache_line_mask);
}

int behav_cache_t::gen_set(address_t addr) {
  int set = (addr >> set_shift) & set_mask;
  NOTE_ARGS(("addr = %x, set_shift %d, set_mask %x, set %d\n", addr, set_shift, set_mask, set));
  return(set);
}


bool behav_cache_t::cache_access(address_t addr, permit_tag_t permit, cache_access_response_t *car) {
  int set = gen_set(addr);
  address_tag_t address_tag = gen_address_tag(addr);

  car->set = set;

  for (int a = 0; a < assoc; ++a) {
    if (tags[set][a].address_tag == address_tag) { // found in cache, check permit
      car->address_tag = address_tag;
      car->permit_tag = tags[set][a].permit_tag;
      car->way = a;
      return (car->permit_tag >= permit);
    }
  }

  car->permit_tag = INVALID;
  return(false);
}

void behav_cache_t::modify_permit_tag(cache_access_response_t car, permit_tag_t permit, address_t addr, int node) {
    tags[car.set][car.way].permit_tag = permit;

}

void behav_cache_t::cache_fill(cache_access_response_t car, data_t data) {
  tags[car.set][car.way].address_tag = car.address_tag;
  tags[car.set][car.way].permit_tag = car.permit_tag;

  NOTE_ARGS(("set tags[%d][%d].address_tag = %d", car.set, car.way, car.address_tag));
  NOTE_ARGS(("set tags[%d][%d].permit_tag  = %d", car.set, car.way, car.permit_tag));

  touch_replacement(car);

  for (int i = 0; i < CACHE_LINE_SIZE; ++i) 
    tags[car.set][car.way].data[i] = data[i];
}


void behav_cache_t::cache_fill_one_word(cache_access_response_t car, int data, address_t addr) {
  tags[car.set][car.way].address_tag = car.address_tag;
  tags[car.set][car.way].permit_tag = car.permit_tag;

  NOTE_ARGS(("set tags[%d][%d].address_tag = %d", car.set, car.way, car.address_tag));
  NOTE_ARGS(("set tags[%d][%d].permit_tag  = %d", car.set, car.way, car.permit_tag));

  touch_replacement(car);

  int offset = gen_offset(addr);
  tags[car.set][car.way].data[offset] = data;
}



int behav_cache_t::read_data(address_t addr, cache_access_response_t car) {
  int offset = gen_offset(addr);
  return(tags[car.set][car.way].data[offset]);
}
  

void behav_cache_t::write_data(address_t addr, cache_access_response_t car, int data) {
  int offset = gen_offset(addr);

  tags[car.set][car.way].data[offset] = data;
}

cache_access_response_t behav_cache_t::lru_replacement(address_t addr, address_t *addrTagToReplace, int *replacedPermitTag) {
  cache_access_response_t car;

  // already in the cache?
  if (cache_access(addr, INVALID, &car)) {
      *addrTagToReplace = car.address_tag; 
      *replacedPermitTag = car.permit_tag; 
      return(car);
  }


  bool done_p = false;
  int set = gen_set(addr);

  // find LRU way
  car.address_tag = gen_address_tag(addr);
  car.set = set;

  // see if you can find yourself
  for (int a = 0; a < assoc; ++a) {
    if (tags[set][a].replacement == -1) {
        *addrTagToReplace = tags[set][a].address_tag; 
        car.way = a;
        *replacedPermitTag = tags[set][a].permit_tag;
        return(car);
    } else if (tags[set][a].replacement == 0) {
        *addrTagToReplace = tags[set][a].address_tag; 
        *replacedPermitTag = tags[set][a].permit_tag;
        done_p = true;
      car.way = a;
    }else{
   ; 
    }
  }

  if (done_p) return(car);

  ERROR("should be a way that is LRU");

}

// perfect LRU
void behav_cache_t::touch_replacement(cache_access_response_t car) {
  int cur_replacement = tags[car.set][car.way].replacement;
  if (cur_replacement < 0) cur_replacement = 0;

  tags[car.set][car.way].replacement = assoc;

  for (int a = 0; a < assoc; ++a) {
    if (tags[car.set][a].replacement > cur_replacement) // demote
      --tags[car.set][a].replacement;
  }

  // consistency check
  bool found_zero_p = false;
  for (int a = 0; a < assoc; ++a) {
    if (tags[car.set][a].replacement == 0) 
      if (found_zero_p) {
	ERROR("touch_replacement: error\n"); 
      } else 
	found_zero_p = true;

    if (tags[car.set][a].replacement > assoc - 1) {
      ERROR("touch_replacement: error: assoc too large\n"); 
    }      
  }
}

//The following functions are called by directory
//when a load is called by the user, directory call this function
void behav_cache_t::directory_enforced_load(address_t addr, permit_tag_t permit_tag) {
    int replacedPermitTag; 
    address_tag_t tagToReplace;   
    bool hit_in_the_cache = false;
    cache_access_response_t car;
  int a;
  if (cache_access(addr, SHARED, &car)) { //if a hit, car will ccontain the permit tag, obvioulsy bigger than shared
    touch_replacement(car); //change to most recently used
  } else {  // miss, service request
      cache_access_response_t car = lru_replacement(addr, &tagToReplace, &replacedPermitTag); //find replacement, get set, way and address tag
      cache_access_response_t dummyCar;
      hit_in_the_cache = cache_access(addr, SHARED, &dummyCar) ;
      //if not hit in the cache, and the lru replacement tag not invalid, invalidate it
      //since we are kicking it out
      if(hit_in_the_cache != 1){  
          if (replacedPermitTag != INVALID) {
              int addrToKickOut = gen_address(tagToReplace, car.set, 0);
              //fprintf(fp, "address to Kick out is %d\n", addrToKickOut); 
              //fprintf(fp, "set is %d\n", car.set); 
              fprintf(fp, "*******node:%d    addr:%d    changed_from:%s    to:%s\n", node, addrToKickOut, PERMIT_TAG_STR[replacedPermitTag], PERMIT_TAG_STR[0]); 
              behavDir->update(node, addrToKickOut, INVALID);
          }
      } 
      
      fprintf(fp, "*******node:%d    addr:%d    changed_from:%s    to:%s\n", node, addr, PERMIT_TAG_STR[car.permit_tag], PERMIT_TAG_STR[permit_tag]); 
     car.permit_tag = permit_tag; //use the tag to update to (which is sent from directory)
     int *data;
     data = (int*) malloc(sizeof(int)*CACHE_LINE_SIZE);
     int lcl = addr >>LG_CACHE_LINE_SIZE;
     copy_cache_line_behav(data,  behav_mem[lcl]);
     fprintf(fp, "-------node:%d    addr:%d    data_changed_to:%d\n",node, addr, *data); 
     cache_fill(car, data); // the touch replacement happens inside cache_fill
   } 
}

address_t behav_cache_t::gen_address(address_tag_t tag, int set, int offset) {
  return ( (tag << address_tag_shift) + (set << set_shift) + offset );
}


//when a store is called by the user, directory call this function
void behav_cache_t::directory_enforced_store(address_t addr, permit_tag_t permit_tag, int data) {
    int replacedPermitTag;
    address_tag_t tagToReplace;   
    cache_access_response_t car = lru_replacement(addr, &tagToReplace, &replacedPermitTag); //find replacement, get set, way and address tag
    cache_access_response_t dummyCar;
    bool hit_in_the_cache = (cache_access(addr, SHARED, &dummyCar)) ;
    //fprintf(fp, "dummyCar %d\n", dummyCar.permit_tag); 
    if(hit_in_the_cache != 1){  
        if (replacedPermitTag != INVALID) {
            int addrToKickOut = gen_address(tagToReplace, car.set, 0);
            fprintf(fp, "*******node:%d    addr:%d    changed_from:%s    to:%s\n", node, addrToKickOut, PERMIT_TAG_STR[replacedPermitTag], PERMIT_TAG_STR[0]); 
            behavDir->update(node, addrToKickOut, INVALID);
        }
    } 
    fprintf(fp, "*******node:%d    addr:%d    changed_from:%s    to:%s\n", node, addr, PERMIT_TAG_STR[car.permit_tag], PERMIT_TAG_STR[permit_tag]); 
    car.permit_tag = permit_tag; //use the tag to update to (which is sent from directory)
    fprintf(fp, "-------node:%d    addr:%d    data_changed_to:%d\n",node, addr, data); 
  
  car.permit_tag = permit_tag; //use the tag to update to (which is sent from directory)
  cache_fill_one_word(car, data, addr); // the touch replacement happens inside cache_fill
   int lcl = (addr >>LG_CACHE_LINE_SIZE);
  data_t cache_data; 
  for (int i = 0; i < CACHE_LINE_SIZE; ++i) {
    cache_data[i] = tags[car.set][car.way].data[i]; 
  } 
  
  
  copy_cache_line_behav(behav_mem[lcl], cache_data); //not replying, but writing into memory,This is necessary so we keep our memory uptodate
  //copy_cache_line_behav(behav_mem[lcl], cache_data); //not replying, but writing into memory,This is necessary so we keep our memory uptodate
  //copy_cache_line(behav_mem[lcl], cache_data); //not replying, but writing into memory,This is necessary so we keep our memory uptodate
}

//used by directory to modify a cache state (without loading or storing)
void behav_cache_t::modify_cache_line_state(address_t addr, permit_tag_t permit_tag, int node, int mode) {
    cache_access_response_t car;
    
    cache_access(addr, INVALID, &car); //get the car, this is necessary for getting the way, set, ..
   if (mode == 1) {
      fprintf(fp, "*******node:%d    addr:%d    changed_from:%s    to:%s\n", node, addr, PERMIT_TAG_STR[car.permit_tag], PERMIT_TAG_STR[permit_tag]); 
    }
    modify_permit_tag(car, permit_tag, addr, node);
}

void copy_cache_line_behav(data_t dest, data_t src) {
  for (int i = 0; i < CACHE_LINE_SIZE; ++i) {
      dest[i] = src[i];}
}

      
//from here
bool doneWithAll(int *req_num_so_far) {
    for (int i = 0; i<args.num_procs; i++) {
        if (req_num_so_far[i] < test.listSize[i]) {
            return false;
        }
    }
    return true; 
}

//sore the log requests based on cycles 
void sort_logged_requests(req_coordinate_t *reqCordinateArray) {
    int *req_num_so_far;
    int resultCounter = 0; 
    req_num_so_far = (int*)malloc(sizeof(int)*args.num_procs);
    int node; 
    bool roundStart = true; 
    int counter = 0;  
    req_list_t min_for_the_round; 
    for (int i = 0; i<args.num_procs; i++) {
        req_num_so_far[i] = 0;
    }
        
    // while not done, keep sorting 
    // start the sorting process 
    while(doneWithAll(req_num_so_far) == 0) {
        for (int i =0; i < args.num_procs; i++) {
            if (req_num_so_far[i] < test.listSize[i]) {
                if (roundStart) {
                    roundStart = false; 
                    min_for_the_round =  reqListLog[i][req_num_so_far[i]];
                }
                if (reqListLog[i][req_num_so_far[i]].cycle <= min_for_the_round.cycle) {
                    min_for_the_round =  reqListLog[i][req_num_so_far[i]];
                    node = i;
                 }
             } 
         } 
        roundStart = true; 
        req_num_so_far[node] +=1; 
        reqCordinateArray[counter].node = node;
        reqCordinateArray[counter].req_num= req_num_so_far[node]-1; 
        counter+=1;
     } 
}

// printing the sorted requests (sorted based on cycle they went through)
void print_sorted_result(req_coordinate_t *coordinate) {
  int totalNumReq = 0; 
  for (int i =0; i < args.num_procs; i++) {//get the totalNumReq
      totalNumReq += test.listSize[i];  
    }
  for (int i = 0; i< totalNumReq; i++) {
      printf("cmd: %d ",reqListLog[coordinate[i].node][coordinate[i].req_num].cmd);
      printf("addr: %d ",reqListLog[coordinate[i].node][coordinate[i].req_num].addr);
      printf("data: %d ",reqListLog[coordinate[i].node][coordinate[i].req_num].data);
      printf("delay: %d ",reqListLog[coordinate[i].node][coordinate[i].req_num].delay);
      printf("cycle %d \n",reqListLog[coordinate[i].node][coordinate[i].req_num].cycle);
  
  }
}

// this function fills up the reqListLog as an example
void fillUpReqListLogAsAnExample(){
   printf("############################################################\n"); 
   printf("############################################################\n"); 
   printf("###########################ATTENTION:#######################\n"); 
   printf("VERIFY IS STILL USING THE REQlISTlOG EXAMPLE, \n");
   printf("PLEASE COMMENT IT OUT IN THE CASE THAT YOU WANT TO DO REAL DEBUGGING\n"); 
   printf("############################################################\n"); 
   printf("############################################################\n"); 

   for (int i =0; i<args.num_procs; i++) {
       printf("----PROC---- %d \n", i); 
       for (int j =0; j<test.listSize[i]; j++) {
           reqListLog[i][j].cmd =  test.reqList[i][j].cmd;
           reqListLog[i][j].addr =  test.reqList[i][j].addr;
           reqListLog[i][j].data =  test.reqList[i][j].data;
           reqListLog[i][j].cycle =  test.reqList[i][j].cycle;
           reqListLog[i][j].delay =  test.reqList[i][j].delay;
       }
   } 
}

void initBehavModel() {
    //behavCaches = new behav_cache_t*[args.num_procs];
    behav_mem = (data_t*)malloc(sizeof(data_t)*MEM_SIZE); 
    behavCaches = (behav_cache_t**)malloc(sizeof(behav_cache_t*)*args.num_procs);
    for (int p = 0; p < args.num_procs; ++p) {
        behavCaches[p] = new behav_cache_t(p, 0, 0, LG_CACHE_LINE_SIZE);
            //(behav_cache*)malloc(sizeof(bahav_cache));
    }
    behavDir = new directory(-3, 2, 3, LG_CACHE_LINE_SIZE,args.num_procs, 1);
}
void runBehavioralModel(req_coordinate_t* sortedListCoordinate, int totalNumReq) {
  int node_to_update; 
   
  for (int i =0; i<totalNumReq; i++) {
      int node = sortedListCoordinate[i].node;
      int cmdNumber = sortedListCoordinate[i].req_num;
      address_t addr =  reqListLog[node][cmdNumber].addr;
      int data =  reqListLog[node][cmdNumber].data;
      dir_response_t dir_resp = (behavDir->lookup(addr));
      if (reqListLog[node][cmdNumber].cmd == 0) {
          fprintf(fp, "attempting to do a load to addr:%d  node:%d\n", addr, node);
      }else{
          fprintf(fp, "attempting to do a str to addr:%d  node:%d\n", addr, node);
      }
      switch(reqListLog[node][cmdNumber].cmd) {
          case 0:
              switch(dir_resp.state) {
                  fprintf(fp, "dir_resp.state %d\n", dir_resp.state); 
                  case ME_M:
                    assert(2 ==3); 
                    break;
                  case ME_E:
                    assert(2 ==3); 
                    break;
                  case ALL_I:
                    behavCaches[node]->directory_enforced_load(addr, EXCLUSIVE);
                      behavCaches[node]->modify_cache_line_state(addr, EXCLUSIVE, node, 0);
                      behavDir->update(node, addr, EXCLUSIVE);
                      break;
                  case ONE_M:
                      //printf("here is the m %d\n", dir_resp.m_dst);  
                      //printf("node %d\n", node);  
                      if (node != dir_resp.m_dst) {
                          behavCaches[dir_resp.m_dst]->modify_cache_line_state(addr, SHARED, dir_resp.m_dst, 1);
                          behavDir->update(dir_resp.m_dst, addr, SHARED);
                          behavCaches[node]->directory_enforced_load(addr, SHARED);
                          behavCaches[node]->modify_cache_line_state(addr, SHARED,node,  0);
                          behavDir->update(node, addr, SHARED);
                      } else{
                          behavCaches[node]->directory_enforced_load(addr, SHARED);
                      }
                      break;
                  case ONE_E:
                      //fprintf(fp, "got here 3 is the e %d\n", dir_resp.e_dst);  
                      if (node != dir_resp.e_dst) {
                          behavCaches[dir_resp.e_dst]->modify_cache_line_state(addr, SHARED, dir_resp.e_dst, 1);
                          behavDir->update(dir_resp.e_dst, addr, SHARED);
                          behavCaches[node]->directory_enforced_load(addr, SHARED);
                          behavCaches[node]->modify_cache_line_state(addr, SHARED, node, 0);
                          behavDir->update(node, addr, SHARED);
                      } else{
                          behavCaches[node]->directory_enforced_load(addr, SHARED);
                      }
                      break; 
                  case SOME_S:
                      //fprintf(fp, "got here 4 is the e %d\n", dir_resp.e_dst);  
                      behavCaches[node]->directory_enforced_load(addr, SHARED);
                      behavCaches[node]->modify_cache_line_state(addr, SHARED, node, 0);
                      behavDir->update(node, addr, SHARED);
                      break;
                  default:
                      printf("this state in directory is not reachable\n"); 
                      exit(1);
                      break;
              } 
              break;
          case 1:
              switch(dir_resp.state) {
                  case ME_M:
                      assert(1==2); 
                      behavCaches[node]->directory_enforced_store(addr, MODIFIED, data);
                      //copy_cache_line_behav(mem[addr], data); 
                      break; 
                  case ME_E:
                      assert(1==2); 
                      behavCaches[node]->directory_enforced_store(addr, MODIFIED, data);
                      behavCaches[node]->modify_cache_line_state(addr, MODIFIED, node, 0);
                      //copy_cache_line_behav(mem[addr], data); 
                      break;    
                  case ALL_I:
                      behavCaches[node]->directory_enforced_store(addr, MODIFIED, data);
                      behavCaches[node]->modify_cache_line_state(addr, MODIFIED, node, 0);
                      printf("------ %d\n", behavCaches[0]->tags[0][0].permit_tag);
                      //copy_cache_line_behav(mem[addr], data); 
                      behavDir->update(node, addr, MODIFIED);
                      break;
                      break;
                  case ONE_M:
                      if (node != dir_resp.m_dst) {
                          behavCaches[node]->directory_enforced_store(addr, MODIFIED, data);
                          behavDir->update(dir_resp.m_dst, addr, INVALID);
                          behavCaches[dir_resp.m_dst]->modify_cache_line_state(addr, INVALID, dir_resp.m_dst, 1);
                          behavCaches[node]->modify_cache_line_state(addr, MODIFIED, node, 0);
                          //copy_cache_line_behav(mem[addr], data); 
                          behavDir->update(node, addr, MODIFIED);
                      }else{
                          behavCaches[node]->directory_enforced_store(addr, MODIFIED, data);
                      }
                      break;
                  case ONE_E:
                      if (node != dir_resp.e_dst) {
                          behavCaches[dir_resp.e_dst]->modify_cache_line_state(addr, INVALID, dir_resp.e_dst, 1);
                          behavDir->update(dir_resp.e_dst, addr, INVALID);
                          behavCaches[node]->directory_enforced_store(addr, MODIFIED, data);
                          behavCaches[node]->modify_cache_line_state(addr, MODIFIED, node, 0);
                          //copy_cache_line_behav(mem[addr], data); 
                          behavDir->update(node, addr, MODIFIED);
                      } else{
                          behavCaches[node]->directory_enforced_store(addr, MODIFIED, data);
                          behavCaches[node]->modify_cache_line_state(addr, MODIFIED, node, 0);
                      }
                      break; 
                  case SOME_S:
                      while(!dir_resp.s_dst_list.empty()) { 
                          node_to_update = dir_resp.s_dst_list.front();
                          dir_resp.s_dst_list.pop_front();
                          behavCaches[node_to_update]->modify_cache_line_state(addr, INVALID, node_to_update, 1);
                          behavDir->update(dir_resp.m_dst, addr, INVALID);
                      } 
                      behavCaches[node]->directory_enforced_store(addr, MODIFIED, data);
                      behavCaches[node]->modify_cache_line_state(addr, MODIFIED, node, 0);
                      //copy_cache_line_behav(mem[addr], data); 
                      behavDir->update(dir_resp.m_dst, addr, MODIFIED);
                      break;
                  default:
                      printf("[Error] No matching with the state of directory lookup response.\n");
                      exit(1);
                      break;
              }
              break;
          default:
              ERROR("this type is not defined\n");
      }
  }
}

void get_permit_tag_behav(cache_access_response_t *car, behav_cache_t *caches, int addr) {
    caches->cache_access(addr, INVALID, car);
}

void get_permit_tag(cache_access_response_t *car, cache_t *caches, int addr) {
    caches->cache_access(addr, INVALID, car);
}


void lookForPermitTagInconsistancy(permit_tag_t structCachePermitTag, permit_tag_t behavCachePermitTag, int addr, int node) {
    if (structCachePermitTag != behavCachePermitTag) {
        printf("????? ERROR: CACHE TAG INCONSISTANT FOR NODE:%d  ADDR:%d\n", node, addr);
        printf("structure Cache Tag: %s     behavioral Cache TAG: %s, \n", PERMIT_TAG_STR[structCachePermitTag], PERMIT_TAG_STR[behavCachePermitTag]);
        fprintf(fp, "????? ERROR: CACHE TAG INCONSISTANT FOR NODE:%d  ADDR %d\n", node, addr);
        fprintf(fp, "structure Cache Tag: %s     behavioral Cache TAG: %s \n", PERMIT_TAG_STR[structCachePermitTag], PERMIT_TAG_STR[behavCachePermitTag]);
     

        exit(1); 
    }
//    else {
//        printf("addr: %d    structure Cache Tag: %d     behavioral Cache TAG: %d \n", addr, structCachePermitTag, behavCachePermitTag);
//    }
}
void lookForDataInconsistancy(int data1, int data2, int addr, int node) {
    if (data1 != data2) {
        
        printf("????? ERROR: DATA INCONSISTANT FOR NODE:%d  ADDR:%d\n", node, addr);
        printf("structure Cache Data: %d     behavioral Cache data: %d \n", data1, data2);
        
        fprintf(fp, "????? ERROR: DATA INCONSISTANT FOR NODE:%d  ADDR %d\n", node, addr);
        fprintf(fp, "structure Cache Data: %d     behavioral Cache data: %d \n", data1, data2);
        exit(1);
    }
//    else{
//        printf("addr%d    structure Cache Data: %d     behavioral Cache data: %d \n", addr, data1, data2);
//    }
}

void compareModels(int inputAddr){
    int addr = inputAddr; 
    int structCacheData;
    int behavCacheData;
    cache_access_response_t structCacheCar;
    cache_access_response_t behavCacheCar;
    int local_mem_offset = 1 <<  LG_INTERLEAVE_SIZE;
    
    #ifdef VERIFY_WITH_BEHAV_MODEL_PER_INSTRUCTION 
    for( int i =0; i< args.num_procs; i++) {
        //for (int j =0; j<MEM_SIZE*CACHE_LINE_SIZE*args.num_procs; j++) { //go through every line of cache
            //addr = j; 
            get_permit_tag(&structCacheCar, caches[i], addr); 
            get_permit_tag_behav(&behavCacheCar, behavCaches[i], addr); 
            lookForPermitTagInconsistancy(structCacheCar.permit_tag, behavCacheCar.permit_tag,addr, i); 
             
            //assert(behavCacheCar.permit_tag == structCacheCar.permit_tag); 
            if ( behavCacheCar.permit_tag >= 1) {
                for (int k = 0; k < 1; k++) {
                    structCacheData= caches[i]->read_data(addr+k, structCacheCar);
                    behavCacheData= behavCaches[i]->read_data(addr+k, behavCacheCar);
                    lookForDataInconsistancy(structCacheData, behavCacheData, addr, i); 
                    //assert(behavCacheData == structCacheData); 
                }
            }
     //} 
     }
    //printf("the two models matched\n");
#endif 
#ifdef VERIFY_WITH_BEHAV_MODEL_FULLY
    for( int i =0; i< args.num_procs; i++) {
        for (int j =0; j<MEM_SIZE*CACHE_LINE_SIZE*args.num_procs; j++) { //go through every line of cache
            addr = j; 
            get_permit_tag(&structCacheCar, caches[i], addr); 
            get_permit_tag_behav(&behavCacheCar, behavCaches[i], addr); 
            lookForPermitTagInconsistancy(structCacheCar.permit_tag, behavCacheCar.permit_tag,addr, i); 
             
            //assert(behavCacheCar.permit_tag == structCacheCar.permit_tag); 
            if ( behavCacheCar.permit_tag >= 1) {
                for (int k = 0; k < CACHE_LINE_SIZE; k++) {
                    structCacheData= caches[i]->read_data(addr+k, structCacheCar);
                    behavCacheData= behavCaches[i]->read_data(addr+k, behavCacheCar);
                    lookForDataInconsistancy(structCacheData, behavCacheData, addr, i); 
                    //assert(behavCacheData == structCacheData); 
                }
            }
     //} 
     }
    } 
    //printf("passed\n");

#endif
}
void printReqListLog(int proc_id, int node, int mode) {
    if (mode == 1) { //print the current commited instructions
        int reqNumber =  test.reqCounterList[node];
        printf("cmd: %d ",reqListLog[node][reqNumber].cmd);
        printf("addr: %d ",reqListLog[node][reqNumber].addr);
        printf("data: %d ",reqListLog[node][reqNumber].data);
        printf("delay: %d ",reqListLog[node][reqNumber].delay);
        printf("cyle: %d \n",reqListLog[node][reqNumber].cycle);
    }else{
        for (int i =0; i<proc_id; i++) {
            printf("----PROC---- %d \n", i); 
            for (int j =0; j<test.listSize[i]; j++) {
                printf("cmd: %d ",reqListLog[i][j].cmd);
                printf("addr: %d ",reqListLog[i][j].addr);
                printf("data: %d ",reqListLog[i][j].data);
                printf("delay: %d ",reqListLog[i][j].delay);
                printf("cyle: %d \n",reqListLog[i][j].cycle);
            }
        } 
    }
}


void get_data(cache_t *caches, int addr,int *data) {
      caches->load(addr, 0, data, false);
}


void verifyBehavTest(int testNumber) {
      int addr; 
      int data;
     int local_mem_size = 256;
      cache_access_response_t car;
      switch(testNumber) { 
          case 1:
            for( int i =0; i< args.num_procs; i++) {
              addr = i*local_mem_size + 10;
              get_permit_tag_behav(&car, behavCaches[i], addr); 
              printf(" i is %d\n", i); 
              //printf("permit is%d\n", car.permit_tag); 
              assert(car.permit_tag == MODIFIED);
              behavCaches[i]->cache_access(addr, INVALID, &car);
              data = behavCaches[i]->read_data(addr, car);
              //printf("data is %d\n", data); 
              assert(data == 200);
          }
          break; 
          case 2: 
          for( int i =0; i< args.num_procs; i++) {
              addr = 0;
              get_permit_tag_behav(&car, behavCaches[i], addr); 
              printf(" i is %d\n", i); 
              //printf("permit is%d\n", car.permit_tag); 
              assert(car.permit_tag == SHARED);
              behavCaches[i]->cache_access(addr, INVALID, &car);
              data = behavCaches[i]->read_data(addr, car);
              printf("data is %d\n", data); 
              assert(data == 200);
          }
          break;
          default:
          printf("this test not defined\n"); 
          exit(1);
          break;
          case 6:
          for( int i =0; i< args.num_procs; i++) {
              if (i %2 == 0) {
                  addr = i<<LG_CACHE_LINE_SIZE;
              }else{
                  addr = (i-1)<<LG_CACHE_LINE_SIZE;
              }
              get_permit_tag_behav(&car, behavCaches[i], addr); 
              printf(" i is %d\n", i); 
              //printf("permit is%d\n", car.permit_tag); 
              //printf("addres is %d\n", addr); 
              assert(car.permit_tag == SHARED);
              behavCaches[i]->cache_access(addr, INVALID, &car);
              data = behavCaches[i]->read_data(addr, car);
              //printf("data is %d\n", data); 
              if (i %2 == 0) {
                    assert(data == 7*i);
              }else{
                    assert(data == 7*(i-1));
              }
          }
          break; 
      case 7: //every processor ends up being exclusive
          for (int i = 0; i < args.num_procs ; ++i ) {
              addr = i<<LG_CACHE_LINE_SIZE;
              get_permit_tag_behav(&car, behavCaches[i], addr); 
              printf(" i is %d\n", i); 
              //printf("permit is%d\n", car.permit_tag); 
              printf("addres is %d\n", addr); 
              assert(car.permit_tag == EXCLUSIVE);
          }
          break;
      case 8: 
          for (int i = 0; i < args.num_procs ; ++i ) {
              addr = i<<LG_CACHE_LINE_SIZE;
              get_permit_tag_behav(&car, behavCaches[i], addr); 
              printf(" i is %d\n", i); 
              //printf("permit is%d\n", car.permit_tag); 
              printf("addres is %d\n", addr); 
              assert(car.permit_tag == MODIFIED);
              printf("data is %d\n", 7*i); 
          }
          break;
       case 9: 
          for (int i = 0; i < args.num_procs ; ++i ) {
              addr = i<<LG_CACHE_LINE_SIZE;
              get_permit_tag_behav(&car, behavCaches[i], addr); 
              printf(" i is %d\n", i); 
              //printf("permit is%d\n", car.permit_tag); 
              printf("addres is %d\n", addr); 
              data = behavCaches[i]->read_data(addr, car);
              assert(car.permit_tag == MODIFIED);
              assert(data == i+3);
          }
          break;
      case 10: 
          for (int i = 0; i < args.num_procs ; ++i ) {
              if (i  %2 ==0 ) {
                addr = (i)<<LG_CACHE_LINE_SIZE;
              }else{
                  addr = (i-1)<<LG_CACHE_LINE_SIZE;
              }
              get_permit_tag_behav(&car, behavCaches[i], addr); 
              printf(" i is %d\n", i); 
              //printf("permit is%d\n", car.permit_tag); 
              printf("addres is %d\n", addr); 
              data = behavCaches[i]->read_data(addr, car);
              if (i %2 ==0) {
                  assert(car.permit_tag == INVALID);
              }else{
                  assert(car.permit_tag == MODIFIED);
                  assert(data == 7*i);
              }
          }
          break;
      case 11: 
          for (int i = 0; i < args.num_procs ; ++i ) {
              if (i  %4 ==0 ) {
                addr = (i)<<LG_CACHE_LINE_SIZE;
              }
              if (i  %4 ==1 ) {
                addr = (i-1)<<LG_CACHE_LINE_SIZE;
              }
               if (i  %4 ==2 ) {
                addr = (i-2)<<LG_CACHE_LINE_SIZE;
              }
               if (i  %4 ==3 ) {
                addr = (i-3)<<LG_CACHE_LINE_SIZE;
              }
              
              get_permit_tag_behav(&car, behavCaches[i], addr); 
              printf(" i is %d\n", i); 
              printf("permit is%d\n", car.permit_tag); 
              printf("addres is %d\n", addr); 
              data = behavCaches[i]->read_data(addr, car);
              if (i %4 == 0) {
                  assert(car.permit_tag == INVALID);
              }
              if (i %4 == 1) {
                  assert(car.permit_tag == INVALID);
              }
              if (i %4 == 2) {
                  assert(car.permit_tag == INVALID);
              }
              if (i %4 == 3) {
                  assert(car.permit_tag == MODIFIED);
                  assert(data == 7*i);
              }
              
          }
         break;
      case 12:
         for (int i = 0; i < args.num_procs ; ++i ) {
             if (i  %2==0 ) {
                addr = (i)<<LG_CACHE_LINE_SIZE;
              }
              if(i %2 ==1 ) {
                addr = (i-1)<<LG_CACHE_LINE_SIZE;
             } 
             get_permit_tag_behav(&car, behavCaches[i], addr); 
             data = behavCaches[i]->read_data(addr, car);
             printf("permit is%d\n", car.permit_tag); 
             printf("addres is %d\n", addr); 
             if (i %2 == 0) {
                 assert(car.permit_tag == INVALID);
             }
             if (i %2 == 1) {
                 assert(car.permit_tag == MODIFIED);
             } 
         }
         break;
    case 13:
         for (int i = 0; i < args.num_procs ; ++i ) {
             addr = i<<LG_CACHE_LINE_SIZE;
             get_permit_tag_behav(&car, behavCaches[i], addr); 
             data = behavCaches[i]->read_data(addr, car);
             printf(" i is %d\n", i); 
             printf("permit is%d\n", car.permit_tag); 
             printf("addres is %d\n", addr); 
             assert(car.permit_tag == MODIFIED);
             assert(data == i);
         }
         break;
    case 14:
         for (int i = 0; i < args.num_procs ; ++i ) {
             if (i  %2==0 ) {
                addr = (i)<<LG_CACHE_LINE_SIZE;
              }
              if(i %2 ==1 ) {
                addr = (i-1)<<LG_CACHE_LINE_SIZE;
             } 
             get_permit_tag_behav(&car, behavCaches[i], addr); 
             data = behavCaches[i]->read_data(addr, car);
             printf(" i is %d\n", i); 
             printf("permit is%d\n", car.permit_tag); 
             printf("addres is %d\n", addr); 
            assert(car.permit_tag == SHARED);
         }
         break;
      }
}
//using behavioral models to verify the test
void verifyTestFully(void) {
  int data; 
  int addr; 
  cache_access_response_t car;
  //fillUpReqListLogAsAnExample(); //as an example we fill up the reqList log
                                 // not necessary usually
//  printf("----------------------------printing the reqList ------------------------\n"); 
//  test.print(args.num_procs);
  //printf("----------------------------printing the reqListLog ------------------------\n"); 
  //printReqListLog(args.num_procs, -3, 0);
  //printf("*************************************************************************\n"); 
  //printf("*************************************************************************\n"); 
  //printf("----------starting the verification process\n");
  //verification processes
  //get the total num of requests 
  int totalNumReq = 0; 
  for (int i =0; i < args.num_procs; i++) {//get the totalNumReq
      totalNumReq += test.listSize[i];  
  } 
  
  assert(totalNumReq > 0);  //sanity check
  //printf("--------sorting the requests based on cycles-----------------------------\n"); 
  req_coordinate_t* sortedListCoordinate;
  sortedListCoordinate = (req_coordinate_t*) malloc(sizeof(req_coordinate_t)*totalNumReq);
  sort_logged_requests(sortedListCoordinate);
  //print_sorted_result(sortedListCoordinate);
  //printf("--------applying the behavioral cache model-------------------------------\n"); 
 runBehavioralModel(sortedListCoordinate, totalNumReq); 
  
  //verifyBehavTest(args.test);
  //printf("--------compare the two models--------------------------------------------\n");
  compareModels(-1);
}


void verifyTestPerInstruction(int node) {
  //fillUpReqListLogAsAnExample(); //as an example we fill up the reqList log
//  printf("----------------------------printing the reqList ------------------------\n"); 
//  test.print(args.num_procs);
  //printf("----------------------------printing the reqListLog ------------------------\n"); 
  //printReqListLog(args.num_procs, node, 1);
  //printf("*************************************************************************\n"); 
  //printf("*************************************************************************\n"); 
  //verification processes
  //get the total num of requests 
  int totalNumReq = 1; 
//  for (int i =0; i < args.num_procs; i++) {//get the totalNumReq
//      totalNumReq += test.listSize[i];  
//  } 
  
  //assert(totalNumReq > 0);  //sanity check
//  printf("--------sorting the requests based on cycles-----------------------------\n"); 
//  req_coordinate_t* sortedListCoordinate;
//  sortedListCoordinate = (req_coordinate_t*) malloc(sizeof(req_coordinate_t)*totalNumReq);
//  sort_logged_requests(sortedListCoordinate);
//  print_sorted_result(sortedListCoordinate);
  //printf("--------applying the behavioral cache model-------------------------------\n"); 
  req_coordinate_t* sortedListCoordinate;
  sortedListCoordinate = (req_coordinate_t*) malloc(sizeof(req_coordinate_t)*totalNumReq);
  sortedListCoordinate[0].node = node;
  sortedListCoordinate[0].req_num=  test.reqCounterList[node];
  runBehavioralModel(sortedListCoordinate, totalNumReq); 
  int addr =  reqListLog[node][test.reqCounterList[node]].addr;
  //verifyBehavTest(args.test);
  //printf("--------compare the two models--------------------------------------------\n");
  compareModels(addr);
}



//void logReq(int reqCounter, int node, int cycle){
//    reqListLog[node][reqCounter].addr = test.reqList[node][reqCounter].addr;
//    reqListLog[node][reqCounter].data = test.reqList[node][reqCounter].data;
//    reqListLog[node][reqCounter].cmd = test.reqList[node][reqCounter].cmd;   
//    reqListLog[node][reqCounter].cycle = cycle;
//}
//
