#ifndef THREAD_H
#define THREAD_H

#include <signal.h>
#include <setjmp.h>

// Constants

#define MAIN_THREAD_TID 0
#define RETURN_FROM_STARTER 5

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
// Typedef for thread entry point function
typedef void (*thread_entry_point)(void);

class Thread {
 private:

  int id;
  States state;
  sigjmp_buf env;
  int duplicate;
  int run_quantom;
  char* stack;
  bool is_sleeping;
  int sleeping_quantum;

 public:
  Thread();
  ~Thread();
  int get_id() const;




};

#endif // THREAD_H
