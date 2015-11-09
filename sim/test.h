// test.h
//   Derek Chiou
//     May 19, 2007

#ifndef TEST_H
#define TEST_H
void init_test();
void finish_test();

typedef struct {
  int addr_range;
  int num_procs;
} test_args_t;

typedef enum {loadCmd, storeCmd} cmd_type;
typedef struct {
    int cmd;
    int addr;
    int data;
    int delay;
    int cycle; //this is mainly used for logging
}req_list_t;

extern test_args_t test_args;
void logReq(int reqCounter, int proc, int cycle);

class test_t {
  private:
  int local_mem_offset;
  int global_addr_space; 
  public:
  int *listSize;
  int *reqCounterList;
  req_list_t **reqList;
  
  void init(int test_num);
  void finish(int test_num);

  void print(int proc_id);
};

extern test_t test;

void printReqListLog(int proc_id);

// functions and class for the behavioral model
void copy_cache_line_behav(data_t dest, data_t src);
class behav_cache_t {
  // cache state
  int node;

  // pointers
  iu_t *iu;

  // management state
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

  // stats
  int full_hits;
  int partial_hits;
  int misses;

 public:
  cache_line_t **tags;
  behav_cache_t(int __n, int lg_assoc, int lg_num_sets, int lg_cache_line_size);
  void bind(iu_t *i);
  void init(int __n, int lg_assoc, int lg_num_sets, int lg_cache_line_size);

  // stats
  void print_stats();
  double hit_rate();

  // helpers
  address_tag_t gen_address_tag(address_t addr);
  int gen_set(address_t addr);
  int gen_offset(address_t addr);

  // manipulate cache
  bool cache_access(address_t addr, permit_tag_t permit, cache_access_response_t *car);
  void touch_replacement(cache_access_response_t car);
  void modify_permit_tag(cache_access_response_t car, permit_tag_t permit, address_t addr, int node);
  void modify_address_permit_tags(cache_access_response_t car);
  void cache_fill(cache_access_response_t car, data_t data);
  void cache_fill_one_word(cache_access_response_t car, int data, address_t addr);
  int read_data(address_t addr, cache_access_response_t car);
  void write_data(address_t addr, cache_access_response_t car, int data);

  // replacement
  cache_access_response_t lru_replacement(address_t addr, address_tag_t *tagToReplace, int *replacedPermitTag);

  // processor side
  response_t load(address_t addr, bus_tag_t tag, int *data, bool retried_p);
  response_t store(address_t addr, bus_tag_t tag, int data, bool retried_p);

  // response side
  void reply(proc_cmd_t proc_cmd);
  // specifically behavioral stuff
  void directory_enforced_load(address_t addr, permit_tag_t permit_tag);
  address_t gen_address(address_tag_t tag, int set, int offset);
  void directory_enforced_store(address_t addr, permit_tag_t permit_tag, int data);
  void modify_cache_line_state(address_t addr, permit_tag_t permit_tag, int node, int mode);
  
  // snoop side
  response_t snoop(net_cmd_t net_cmd);

};

typedef struct {
    int node;
    int req_num;
}req_coordinate_t;

void logReq(int reqCounter, int proc, int cycle);
void init_test();
void finish_test();
void verifyTestFully(void);
void compareModels(int);
void runBehavioralModel(void);
void initBehavModel();
void fillUpReqListLogAsAnExample(void);
void print_sorted_result(req_coordinate_t *coordinate);
void sort_logged_requests(req_coordinate_t *reqCordinateArray);
bool doneWithAll(int *req_num_so_far);
void verifyTestPerInstruction(int node);
void lookForPermitTagInconsistancy(permit_tag_t structCachePermitTag, permit_tag_t behavCachePermitTag, int addr, int i);
void lookForDataInconsistancy(int data1, int data2, int addr, int node);
//#define VERIFY_WITH_BEHAV_MODEL_FULLY 1
#define VERIFY_WITH_BEHAV_MODEL_PER_INSTRUCTION 1
#endif



