# 🧵 User-Level Threads Library

> **Course:** Operating Systems (67808) - Exercise 2
> **Authors:** Yael Batat & Batya Mayer

A static library implementing a **User-Level Thread (ULT)** system in C++.
The library provides a complete environment for creating, scheduling, and managing threads entirely in user space, using **Round-Robin** scheduling algorithm.

## 📌 Overview
This project simulates the functionality of an Operating System's thread scheduler but runs strictly in user mode. It manages multiple threads within a single process, handling context switching, scheduling, and state transitions without kernel intervention.

**Key capabilities:**
* Creating and terminating threads.
* Context switching using `sigsetjmp` and `siglongjmp`.
* Preemptive scheduling based on a virtual timer (`SIGVTALRM`).
* Thread synchronization (Blocking, Resuming, Sleeping).

## ⚙️ How It Works
The library manages the CPU execution time for multiple threads using a **Round-Robin** scheduler.

### 1. The Scheduler
* Each thread is allocated a fixed time slice called a **Quantum**.
* The library uses `setitimer` to trigger a `SIGVTALRM` signal every quantum.
* When the signal is received (or when a thread yields/blocks), the scheduler preempts the current thread and switches to the next one in the **Ready Queue**.

### 2. Thread States
Every thread moves between three possible states:
* **READY:** The thread is waiting for CPU time (stored in a `std::deque`).
* **RUNNING:** The thread is currently executing.
* **BLOCKED:** The thread is waiting for an event (explicit block or sleep) and will not be scheduled until resumed/woken up.

### 3. Context Switching
We use the `sigsetjmp` and `siglongjmp` functions to save and restore the execution environment (registers, stack pointer, program counter) of each thread.

## 🛠️ API Reference (`uthreads.h`)

| Function | Description |
|----------|-------------|
| `uthread_init(quantum_usecs)` | Initializes the library and creates the main thread. Sets the quantum duration. |
| `uthread_spawn(entry_point)` | Creates a new thread that starts executing the given function. |
| `uthread_terminate(tid)` | Terminates a thread. If the main thread is terminated, the whole process ends. |
| `uthread_block(tid)` | Blocks a thread (moves it to BLOCKED state). |
| `uthread_resume(tid)` | Resumes a blocked thread (moves it to READY state). |
| `uthread_sleep(num_quantums)` | Puts the current thread to sleep for a specified number of quantums. |
| `uthread_get_tid()` | Returns the ID of the calling thread. |
| `uthread_get_total_quantums()` | Returns the total number of quantums passed since initialization. |
| `uthread_get_quantums(tid)` | Returns the number of quantums a specific thread has run. |

## 📂 File Structure

* `uthreads.cpp` - The core implementation of the library (scheduler logic, signal handling, internal data structures).
* `uthreads.h` - The public header file defining the library's API.
* `Thread.cpp` / `Thread.h` - Internal encapsulation of a single Thread object (ID, stack, state, environment).
* `Makefile` - Compiles the source code into a static library `libuthreads.a`.

## 🚀 Usage

### Prerequisites
* Linux Environment (x86/x64).
* G++ Compiler.

### Compilation
To build the static library `libuthreads.a`, run:

### Linking
To use the library in your own project, include the header and link the static library:

```bash
g++ -Wall -std=c++11 main.cpp -L. -luthreads -o program
```
## ⚠️ Implementation Details
* [cite_start]**Signal Masking:** Critical sections (like queue modifications) are protected by masking `SIGVTALRM` to prevent race conditions[cite: 502].
* [cite_start]**Memory Management:** Each thread has its own stack (4KB)[cite: 502]. [cite_start]The library ensures proper allocation and deallocation of stack memory upon thread termination[cite: 502].
* [cite_start]**Black Box Translation:** Address translation for the stack pointer (SP) and program counter (PC) is handled for both 32-bit and 64-bit architectures using the provided `translate_address` function[cite: 502].

## ⚠️ Disclaimer
This project was created for educational purposes as part of the Hebrew University Operating Systems course. It is intended to demonstrate the implementation of user-level threads and scheduling algorithms.
