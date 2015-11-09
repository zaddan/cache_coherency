#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include "types.h"
#include "cache.h"
#include <assert.h>
#include <list>
#ifndef DIRECTORY_H
#define DIRECTORY_H

enum dir_state_t {
  ME_M, // I am in M state 
  ME_E, // I am in E state
  ALL_I, // All nodes are in I state
  ONE_M, // Another node is in M state
  ONE_E, // Another node is in E state
  SOME_S // Some of nodes are in S state
};

const char* const DIR_STATE_STR[] = {
  "ME_M", // I am in M state 
  "ME_E", // I am in E state
  "ALL_I", // All nodes are in I state
  "ONE_M", // Another node is in M state
  "ONE_E", // Another node is in E state
  "SOME_S" // Some of nodes are in S state
};

struct dir_response_t {
  dir_state_t state;
  int e_dst;
  int m_dst;
  unsigned s_count;
  std::list<int> s_dst_list;
};

typedef struct {
    int dirty; 
    int *proc_cache_state;
}dir_entry_t;




//Note by setting the mode to 1, we switch to behavioral mode
//since the behavioral and "structural" model are extramely similar, we didn't feel a need
//to redfine for a behavioral model
class directory {
    int assoc;
    int num_sets;
    int cache_line_size;

    int lg_assoc;
    int lg_num_sets;
    int lg_cache_line_size;

    int set_shift;
    uint set_mask;

    uint cache_line_mask;

    int address_tag_shift;
    //int node_who_has_it;
      
    // stats
    int full_hits;
    int partial_hits;
    int misses;
    int node; //processor associated with the directory
    dir_entry_t *directoryTable;
    int num_procs; 
    //initialization example 
    //behavDir = new directory(1, 2, 3, LG_CACHE_LINE_SIZE,args.num_procs, 1);
    public:  
    directory(int __node, int __lg_assoc, int __lg_num_sets, int __lg_cache_line_size, int total_num_procs, int mode) { //0 structural, 1 behavioral
    //void init(int __node, int __lg_assoc, int __lg_num_sets, int __lg_cache_line_size, int total_num_procs) {
        node = __node;
        num_procs = total_num_procs; 
        lg_assoc = __lg_assoc;
        lg_num_sets = __lg_num_sets;
        lg_cache_line_size = __lg_cache_line_size;

        num_sets = (1 << lg_num_sets);
        assoc = (1 << lg_assoc);

        set_shift = lg_cache_line_size;
        set_mask = (1 << lg_num_sets) - 1;

        address_tag_shift = lg_cache_line_size + lg_num_sets;


        int dir_num_rows;
        if (mode == 0) {
            dir_num_rows = MEM_SIZE;
        }else {
            dir_num_rows = MEM_SIZE*num_procs;
        }
        cache_line_mask = (1 << lg_cache_line_size) - 1;
        if ((cache_line_mask + 1) != CACHE_LINE_SIZE) {
            ERROR("inconsistent cache_line_size");
        }

        // define and intitialize the table
        directoryTable = (dir_entry_t*) malloc(sizeof(dir_entry_t)*dir_num_rows);
        for (int i = 0; i<dir_num_rows; i++) {
            directoryTable[i].dirty = 0;
            directoryTable[i].proc_cache_state = (int*)malloc(sizeof(int)*num_procs);
            for (int j = 0; j<num_procs; j++) {
                directoryTable[i].proc_cache_state[j] = 0;    //set evryone to 0
            }
        }
    } 
    // the following 3 functions are borrowed from the cache.cpp
    int gen_address_tag(address_t addr) {
        return(addr >> address_tag_shift);
    }

    int gen_offset(address_t addr) {
        return(addr & cache_line_mask);
    }

    int gen_set(address_t addr) {
        int set = (addr >> set_shift) & set_mask;
        NOTE_ARGS(("addr = %x, set_shift %d, set_mask %x, set %d\n", addr, set_shift, set_mask, set));
        return(set);
    }     

    dir_response_t lookup(address_t addr) {
        
        int node_who_has_it = -1; 
        int offSet = gen_offset(addr);
        address_t addrToLookUp = addr >> LG_CACHE_LINE_SIZE; 
        int dirtyBit = this->directoryTable[addrToLookUp].dirty;
        bool I_have_the_data = false;  //use to figure out if I am the owner for exclusive or modify
        dir_response_t result; 
        result.e_dst = -1;
        result.m_dst = -1;
        
        result.s_count = 0; 
        for (int i = 0; i < num_procs; i++) {
            if (directoryTable[addrToLookUp].proc_cache_state[i] == 1) {
                result.s_count +=1 ;
                node_who_has_it = i; 
                result.s_dst_list.push_front(i);
            }
        }
        I_have_the_data = (node_who_has_it == node);
//        if (dirtyBit && result.s_count > 1)  {
//            ERROR("can not be modified for more than 2 processors\n");
//        }
//        if (dirtyBit==1 && result.s_count == 0)  {
//            ERROR("can not be modified while noone has it\n")
//        }
        if (dirtyBit==1 && result.s_count == 1 && I_have_the_data ==1) { //MODIFIED
            result.state = ME_M;
            result.m_dst = node; 
        }
        if (dirtyBit==1 && result.s_count == 1 && I_have_the_data == 0) { //MODIFED
            result.state = ONE_M;
            result.m_dst = node_who_has_it; 
        }
        if (dirtyBit == 0 && result.s_count ==0) { // ALL INVALID
            result.state = ALL_I;
        }
        if (dirtyBit == 0 && result.s_count == 1 && I_have_the_data == 1) { //I Have it
            result.state = ME_E;
            result.e_dst = node_who_has_it; 
        }
        if (dirtyBit == 0 && result.s_count == 1 && I_have_the_data == 0) { //SOME other has it
            result.state = ONE_E;
            result.e_dst = node_who_has_it; 
        }
        if (dirtyBit == 0 && result.s_count > 1) { //more than one person has it
            result.state = SOME_S;
        }
        return result; 
    }
    
    void print_dir_entry(address_t addr) {
        int node_who_has_it =  -1; 
        address_t addrToLookUp = addr >> LG_CACHE_LINE_SIZE; 
        int dirtyBit = this->directoryTable[addrToLookUp].dirty;
        dir_response_t result; 
        result.s_count = 0; 
        for (int i = 0; i < num_procs; i++) {
            if (directoryTable[addrToLookUp].proc_cache_state[i] == 1) {
                result.s_count +=1 ;
                node_who_has_it = i; 
                result.s_dst_list.push_front(i);
            }
        }
    
        for (int i = 0; i < num_procs; i++) {
            if (directoryTable[addrToLookUp].proc_cache_state[i] == 0) {
                printf("proc %d status is INVALID\n", i);
            }else {
                if (dirtyBit == 1) {
                    printf("proc %d status is MODIFIED\n", i);
                }else{
                    if (result.s_count > 1) {
                        printf("proc %d status is SHARED\n", i);
                    }else{
                        printf("proc %d status is EXCLUSIVE\n", i);
                    }
                }   
            } 
        }
    }
    void update(int node, address_t addr, permit_tag_t p) {
        int offSet = gen_offset(addr);
        address_t addrToLookUp = addr>> (LG_CACHE_LINE_SIZE); 
        int counter = 0; 
        switch(p){ 
            case(MODIFIED):
                directoryTable[addrToLookUp].dirty = 1;
                directoryTable[addrToLookUp].proc_cache_state[node] = 1;
                for (int i = 0; i < num_procs; i++) { // sanity check 
                    if (i != node) {
                        assert(directoryTable[addrToLookUp].proc_cache_state[node] == 1);
                    }
                } 
                break; 
            case(INVALID):
                directoryTable[addrToLookUp].dirty = 0;// incase the person we 
                // invalidate was in modify
                directoryTable[addrToLookUp].proc_cache_state[node] = 0;
                break; 
            case(SHARED):
                directoryTable[addrToLookUp].dirty = 1;
                directoryTable[addrToLookUp].dirty = 0;
                directoryTable[addrToLookUp].proc_cache_state[node] = 1;
                assert(directoryTable[addrToLookUp].proc_cache_state[node] == 1);
                for (int i = 0; i < num_procs; i++) { // sanity check 
                    if(directoryTable[addrToLookUp].proc_cache_state[node] == 1)
                        counter +=1;
                } 
                // assert(counter<=1); 
                break; 
            case(EXCLUSIVE):
                directoryTable[addrToLookUp].dirty = 0;
                directoryTable[addrToLookUp].proc_cache_state[node] = 1;
                assert(directoryTable[addrToLookUp].proc_cache_state[node] == 1);
                for (int i = 0; i < num_procs; i++) { // sanity check 
                    if (i != node) {
                        assert(directoryTable[addrToLookUp].proc_cache_state[node] == 1);
                    }
                }
                break;   
            default:
                ERROR("the directory can not accept such permit tag \n");
        }
    }
};

#endif
