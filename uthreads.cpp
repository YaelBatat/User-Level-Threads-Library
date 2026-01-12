#include <cstdlib>
#include <csignal>
#include <unistd.h>
#include <sys/time.h>
#include <csetjmp>
#include <vector>
#include <set>
#include <queue>
#include "uthreads.h"
#include "iostream"
#include "algorithm"
#include "unordered_map"
using namespace std;

#ifdef __x86_64__
/* code for 64 bit Intel arch */

typedef unsigned long address_t;
#define JB_SP 6
#define JB_PC 7

/* A translation is required when using an address of a variable.
   Use this as a black box in your code. */
extern address_t translate_address(address_t addr)
{
    address_t ret;
    asm volatile("xor    %%fs:0x30,%0\n"
                 "rol    $0x11,%0\n"
                 : "=g" (ret)
                 : "0" (addr));
    return ret;
}

#else
/* code for 32 bit Intel arch */

typedef unsigned int address_t;
#define JB_SP 4
#define JB_PC 5


/* A translation is required when using an address of a variable.
   Use this as a black box in your code. */
extern address_t translate_address(address_t addr)
{
  address_t ret;
  asm volatile("xor    %%gs:0x18,%0\n"
               "rol    $0x9,%0\n"
      : "=g" (ret)
      : "0" (addr));
  return ret;
}

#endif

// Constants
// Error messages
const char* INPUT_ERROR = "thread library error: ";
const char* SYS_CALL_ERROR = "system error: ";
#define MAIN_THREAD_TID 0
#define VAL 3


// Typedef for thread entry point function
typedef void (*thread_entry_point)(void);
// Signal set for managing SIGVTALRM signals
static sigset_t vtalrm_set;

/**
 * @brief Enum representing the states a thread can be in.
 *
 * This enum defines the possible states for a thread: READY, RUNNING, and BLOCKED.
 */
enum class States {
    READY,
    RUNNING,
    BLOCKED
};

// Thread structure
struct Thread {
    int id;
    States state;
    sigjmp_buf env;
    int run_quantom;
    char* stack;
    bool is_sleeping;
    int sleeping_quantum;
};

// Global variables
static Thread* current_thread;
static int total_quantums = 0 ;
static struct itimerval timer;
static set<Thread*> blocked_ids; //block threads
static std::deque<Thread*> ready_threads;
static std::unordered_map<int,Thread*> threads;
static std::set<int> valid_ids;
static int num_current_threads = 0;


// Function declarations
void block_vtalrm();
void unblock_vtalrm();
void init_main_thread();
int create_new_thread(char *stack, thread_entry_point entry_point);
Thread* next_ready_thread();
void block_thread(int tid);
void run_next_thread();
int resume_thread(int tid);
void terminate_process();
int terminate_thread(int tid);
void update_sleeping_time();
void timer_handler(int sig);
void reset_timer();
bool thread_id_exist(int tid);
void initialize_ids_set();
void init_timer(int quantum_usecs);
void initialize_vtalrm_set();
void set_timer();
void setup_thread(const char *stack, thread_entry_point entry_point,Thread*
new_thread);

/**
 * @brief Function to initialize the set of valid thread IDs.
 *
 * This function initializes the set of valid thread IDs from 1 to MAX_THREAD_NUM.
 */
void initialize_ids_set(){
  for(int i = 1;i<MAX_THREAD_NUM ;i++){
    valid_ids.insert (i);
  }
}

/**
 * @brief Function to block SIGVTALRM using the pre-created signal set.
 *
 * This function blocks the SIGVTALRM signal using the pre-created signal set vtalrm_set.
 */
void block_vtalrm() {
  if (sigprocmask(SIG_BLOCK, &vtalrm_set, nullptr) < 0) {
    std::cerr << SYS_CALL_ERROR << "Failed to block SIGVTALRM."<< std::endl;
    terminate_process();
    exit(1);
  }
}

/**
 * @brief Function to unblock SIGVTALRM using the pre-created signal set.
 *
 * This function unblocks the SIGVTALRM signal using the pre-created signal set vtalrm_set.
 */
void unblock_vtalrm() {
  if (sigprocmask(SIG_UNBLOCK, &vtalrm_set, nullptr) < 0) {
    std::cerr << SYS_CALL_ERROR << "Failed to unblock SIGVTALRM." << std::endl;
    terminate_process();
    exit(1);
  }
}

/**
 * @brief Function to reset the timer.
 *
 * This function resets the timer with the previously set interval.
 */
void reset_timer(){
  //block_vtalrm();
  if (setitimer(ITIMER_VIRTUAL, &timer, nullptr) == -1) {
    std::cerr << SYS_CALL_ERROR << "failed setitimer system call." << std::endl;
    terminate_process();
    exit (1);
  }

}
/**
 * @brief Function to initialize the timer with the given time quantum.
 *
 * This function initializes the timer with the specified time quantum in microseconds.
 * It sets the timer using the setitimer system call.
 *
 * @param quantum_usecs The time quantum in microseconds.
 */
void init_timer(int quantum_usecs){

  timer.it_value.tv_sec = quantum_usecs / 1000000;  // Seconds part
  timer.it_value.tv_usec = quantum_usecs % 1000000; // Microseconds part

  timer.it_interval.tv_sec = quantum_usecs / 1000000;  // Seconds part
  timer.it_interval.tv_usec = quantum_usecs % 1000000; // Microseconds part

  // Timer setting
  reset_timer();

}
/**
 * @brief Function to initialize the signal set with SIGVTALRM.
 *
 * This function initializes the signal set vtalrm_set with SIGVTALRM.
 * It should be called once during initialization.
 */
void initialize_vtalrm_set() {
  if (sigemptyset(&vtalrm_set) < 0 ||
      sigaddset(&vtalrm_set, SIGVTALRM) < 0) {
    std::cerr << SYS_CALL_ERROR << "Failed to set up signal set." << std::endl;
    terminate_process();
    exit(1);
  }
}


/**
 * @brief Function to set up the signal handler for SIGVTALRM signals.
 *
 * This function sets up the signal handler for SIGVTALRM signals.
 * It associates the timer_handler function as the signal handler for SIGVTALRM.
 */
void set_timer(){
  struct sigaction sa = {nullptr};
  sa.sa_handler = &timer_handler; // Handler function to call when signal is received
  // Apply the signal handler settings for SIGVTALRM
  if (sigaction(SIGVTALRM, &sa, NULL) < 0) {
    std::cerr << SYS_CALL_ERROR << "failed sigaction system call." << std::endl;
    terminate_process();
    exit (1);
  }
}

/**
 * @brief Function to handle the timer signal (SIGVTALRM).
 *
 * This function is called when the SIGVTALRM signal is received.
 * It performs the necessary actions for context switching between threads.
 *
 * @param signum The signal number.
 */
void timer_handler(int sig) {
  block_vtalrm();
  int ret_val = sigsetjmp(current_thread->env, 1);
  if (ret_val == VAL) {
    unblock_vtalrm();
    return;
  }
  if (current_thread->state == States::RUNNING &&
      (!current_thread->is_sleeping)) {
    current_thread->state = States::READY;
    ready_threads.push_back(current_thread);
  }
  unblock_vtalrm();
  run_next_thread();
}

/**
 * @brief Selects the next thread to run.
 *
 * This function selects the next thread to run from the ready queue.
 * It sets the selected thread as the current running thread and updates its state to RUNNING.
 * The function also increases the run quantum count for the selected thread.
 */
void run_next_thread(){
  Thread *nextThread = next_ready_thread();
  nextThread->state = States::RUNNING;
  current_thread = nextThread;
  total_quantums++;
  update_sleeping_time();
  current_thread->run_quantom++;
  siglongjmp(nextThread->env, VAL);
}

/**
 * @brief Function to initialize the main thread.
 *
 * This function initializes the main thread, sets its properties, and adds it to the threads map.
 */
void init_main_thread() {
  auto *mainThread = new Thread();
  //mainThread->stack = new char[STACK_SIZE]; /////////////
  mainThread->id = MAIN_THREAD_TID;
  mainThread->state = States::RUNNING;
  mainThread->run_quantom = 1;
  mainThread->sleeping_quantum = -1;
  mainThread->is_sleeping = false;
  //sigemptyset(&(mainThread->env)->__saved_mask); /////////
  threads[mainThread->id] = mainThread;
  current_thread = mainThread;
  //sigsetjmp(mainThread->env, 1); ////////////
}

int uthread_init(int quantum_usecs) {
  if (quantum_usecs <= 0) {
    std::cerr << INPUT_ERROR << "quantum_usecs must be a non-negative number." << std::endl;
    return -1;
  }
  initialize_ids_set();
  initialize_vtalrm_set();
  set_timer();
  init_timer(quantum_usecs);
  init_main_thread();
  //unblock_vtalrm();
  total_quantums++;
  num_current_threads++;
  return 0;
}

int uthread_spawn(thread_entry_point entry_point) {
  if (entry_point == nullptr) {
    std::cerr << INPUT_ERROR << "entry point is NULL." << std::endl;
    return -1;
  }
  if (num_current_threads >= MAX_THREAD_NUM) {
    std::cerr << INPUT_ERROR<< "Maximum number of threads reached." << std::endl;
    return -1;
  }

  char *stack = new char[STACK_SIZE];
  block_vtalrm();
  int new_thread_id = create_new_thread(stack, entry_point);
  unblock_vtalrm();
  return new_thread_id;
}



int uthread_terminate(int tid) {
  block_vtalrm();
  if (!thread_id_exist (tid)) {
    return -1;
  }
  if (tid == MAIN_THREAD_TID) { //main thread terminated
    terminate_process();
    exit(0);
  }
  if (current_thread->id == tid) { //terminate himself
    if (terminate_thread(tid)){
      return -1;
    }
    reset_timer();
    timer_handler(0);
  }
  if (terminate_thread(tid)) {
    return -1;
  }
  unblock_vtalrm();
  return 0;
}

/**
 * @brief Function to translate the stack and entry point addresses.
 *
 * This function translates the stack and entry point addresses for a new thread.
 *
 * @param stack The stack address.
 * @param entry_point The entry point function address.
 * @param new_thread Pointer to the new thread.
 */
void setup_thread(const char *stack, thread_entry_point entry_point,Thread*
new_thread){
  address_t sp = (address_t)stack + STACK_SIZE - sizeof(address_t);
  auto pc = (address_t)entry_point;
  sigsetjmp(new_thread->env, 1);
  ((new_thread->env)->__jmpbuf)[JB_SP] = (long)translate_address(sp);
  ((new_thread->env)->__jmpbuf)[JB_PC] = (long)translate_address(pc);
  sigemptyset(&(new_thread->env)->__saved_mask);
}

/**
 * @brief Function to create a new thread.
 *
 * This function creates a new thread with the specified stack and entry point function.
 *
 * @param stack The stack for the new thread.
 * @param entry_point The entry point function for the new thread.
 * @return The new thread ID.
 */
int create_new_thread(char *stack, thread_entry_point entry_point) {
  auto *new_thread = new Thread();
  new_thread->stack = stack;
  new_thread->id = *valid_ids.begin();
  valid_ids.erase (new_thread->id);
  new_thread->state = States::READY;
  new_thread->sleeping_quantum = -1;
  new_thread->is_sleeping = false;
  setup_thread(stack,entry_point,new_thread);
  threads[new_thread->id] = new_thread;
  ready_threads.push_back(new_thread);
  num_current_threads++;
  return new_thread->id;
}

/**
 * @brief Function to get the next ready thread.
 *
 * This function returns the next thread from the ready queue.
 *
 * @return Pointer to the next ready thread.
 */
Thread* next_ready_thread() {
  if (ready_threads.empty()) {
    return threads[0];
  }
  Thread *next_thread = ready_threads.front();
  ready_threads.pop_front();
  while ((next_thread->state != States::READY)) { //////
    if (ready_threads.empty()) {
      return current_thread;
    }
    next_thread = ready_threads.front();
    ready_threads.pop_front();
  }
  return next_thread;
}

/**
 * @brief Function to block a thread.
 *
 * This function blocks the thread with the specified thread ID.
 *
 * @param tid The thread ID to block.
 */
void block_thread(int tid) {
  Thread* thread = threads[tid];
  thread->state = States::BLOCKED;
  blocked_ids.insert(thread);
}


/**
 * @brief Function to resume a thread.
 *
 * This function resumes the thread with the specified thread ID.
 *
 * @param tid The thread ID to resume.
 * @return 0 on success, -1 on failure.
 */
int resume_thread(int tid) {
  Thread* thread = threads[tid];
  if (thread->state == States::BLOCKED) {
    blocked_ids.erase(thread);
    thread->state = States::READY;
    if (!thread->is_sleeping) {
      ready_threads.push_back(thread);

    }
    return 0;
  }
  return -1;
}


/**
 * @brief Function to terminate the process.
 *
 * This function terminates the entire process and deletes all threads.
 */
void terminate_process() {
  for (auto &thread : threads) {
    if (thread.second != nullptr) {
      delete[] thread.second->stack;
      delete thread.second;
    }
  }
  threads.clear();
}

/**
 * @brief Function to terminate a specific thread.
 *
 * This function terminates the thread with the specified thread ID.
 *
 * @param tid The thread ID to terminate.
 * @return 0 on success, -1 on failure.
 */
int terminate_thread(int tid) {
  Thread* thread = threads[tid];
  if (thread == nullptr) {
    return -1;
  }
  // Use std::remove_if with a lambda function
  ready_threads.erase(std::remove_if(ready_threads.begin(),
                                     ready_threads.end(),
                                     [thread](Thread* t) { return t == thread; }),
                      ready_threads.end());
  threads[tid] = nullptr;
  threads.erase(tid);
  valid_ids.insert (tid);
  delete[] thread->stack;
  delete thread;
  num_current_threads--;
  return 0;
}


/**
 * @brief Function to update the sleeping time for all threads.
 *
 * This function decrements the sleeping time for all sleeping threads and wakes them up if necessary.
 */
void update_sleeping_time() {
  for (auto &thread : threads) {
    if (thread.second != nullptr) {
      if (thread.second->is_sleeping) {
        thread.second->sleeping_quantum--;
      }
      if (thread.second->sleeping_quantum == 0) {
        thread.second->is_sleeping = false;
        if (thread.second->state == States::READY) {
          ready_threads.push_back(thread.second);
        }
        thread.second->sleeping_quantum = -1;
      }
    }
  }
}



/**
 * @brief Checks if a thread with the given ID exists.
 *
 * This function checks if a thread with the specified ID exists in the threads map.
 *
 * @param tid The ID of the thread to check.
 * @return True if the thread exists, false otherwise.
 */
bool thread_id_exist(int tid) {
  // Checking if the id exist
  if (threads.find(tid) == threads.end() || threads[tid] == nullptr) {
    std::cerr << INPUT_ERROR << "Thread ID does not exist." << std::endl;
    return false;
  }
  return true;
}



int uthread_block(int tid) {
  block_vtalrm();
  if (!thread_id_exist (tid)) {
    unblock_vtalrm();
    return -1;
  }
  if (tid == MAIN_THREAD_TID) {
    std::cerr<<INPUT_ERROR << "Trying to block the main thread."<<std::endl;
    unblock_vtalrm();
    return -1;
  }
  block_thread(tid);
  unblock_vtalrm();
  if (current_thread->id == tid) {
    //block himself
    reset_timer();
    timer_handler(0);
  }
  return 0;
}


int uthread_resume(int tid) {
  block_vtalrm();
  if (!thread_id_exist (tid)) {
    unblock_vtalrm();
    return -1;
  }
  resume_thread(tid);

  unblock_vtalrm();
  return 0;
}


int uthread_sleep(int num_quantums) {
  block_vtalrm();
  if (num_quantums <= 0) {
    std::cerr << INPUT_ERROR << "quantum_usecs must be a non-negative number."
              << std::endl;
    unblock_vtalrm();
    return -1;
  }
  if (current_thread->id == MAIN_THREAD_TID) {
    std::cerr<<INPUT_ERROR << "main thread can't sleep"
             << std::endl;
    unblock_vtalrm();
    return -1;
  }
  current_thread->is_sleeping = true;
  current_thread->sleeping_quantum = num_quantums;
  current_thread->state = States::READY;
  reset_timer();
  timer_handler(0);
  return 0;
}


int uthread_get_tid() {
  return current_thread->id;
}

int uthread_get_total_quantums() {
  return total_quantums;
}

int uthread_get_quantums(int tid) {
  if (!thread_id_exist (tid)) {
    return -1;
  }
  return threads[tid]->run_quantom;
}

