#include "Thread.h"
#include <stdlib.h>

Thread::Thread() {
  id = -1;
  state = States::READY;
  duplicate = 0;
  run_quantum = 0;
  stack = nullptr;
  sleeping = false;
  sleep_quantum = 0;
}

Thread::~Thread() {
  free(stack);
}
