# Linux Telemetry Device Driver: Technical Architecture & Core Principles

This document details the core operating system concepts, kernel memory safety guarantees, and architectural decisions implemented in the `/dev/telemetry_dev` character device driver.

---

### 1. Architectural Summary
The `/dev/telemetry_dev` driver is an out-of-tree Linux kernel module built using the `miscdevice` framework. It provides asynchronous data streaming via `struct kfifo`, thread-safe multi-process synchronization with `struct mutex`, and interruptible sleep scheduling with wait queues (`wait_queue_head_t`). In addition, it exposes a custom `ioctl()` ABI for runtime configuration, strictly isolating user-kernel memory boundaries with `copy_to_user()` and `-EFAULT` validation.

---

### 2. Core Operating System Concepts

#### A. User Space vs. Kernel Space Isolation
- **User Space:** Executes in unprivileged CPU Ring 3 with virtual address spaces isolated per process. Faults terminate the calling process only.
- **Kernel Space:** Executes in privileged Ring 0 with unrestricted hardware and shared memory access. Unhandled faults cause a fatal Kernel Panic.
- **Boundary Crossing:** Transitions occur strictly through POSIX system calls (`open`, `read`, `write`, `ioctl`, `close`), triggering CPU privilege mode switches and address translation checks.

#### B. User Pointer Validation & Memory Safety
Direct dereferencing of user-space pointers (`*ptr`) inside the kernel is strictly avoided:
1. User pointers reside in process-specific virtual memory, which may be unmapped or swapped out.
2. Direct access poses security risks (e.g., passing kernel addresses to overwrite arbitrary memory).
3. The driver routes all transfers through `copy_to_user()`, `copy_from_user()`, and `kfifo_*_user()`, which validate page table permissions via `access_ok()` and use the kernel Exception Table (`extable`) to catch faults cleanly.

#### C. Kernel FIFO Buffer Semantics (`struct kfifo`)
- Pre-allocated power-of-two sizing ($1024\,\text{bytes}$) eliminates expensive modulo arithmetic, wrapping indices using fast bitwise masking (`& (size - 1)`).
- Synchronized by `struct mutex` to ensure deterministic ordering and data integrity across multi-threaded applications.

#### D. Signal-Aware Mutex Locking (`mutex_lock_interruptible`)
- Traditional `mutex_lock()` places tasks in uninterruptible sleep (`TASK_UNINTERRUPTIBLE`), leaving them unresponsive to OS signals (`SIGINT` / `SIGKILL`).
- Using `mutex_lock_interruptible()` places waiting tasks in `TASK_INTERRUPTIBLE`. Incoming signals immediately abort the lock acquisition with `-ERESTARTSYS`, preventing hung processes.

#### E. Blocking & Non-Blocking Read Scheduling
- **Blocking Read:** If the FIFO is empty and `O_NONBLOCK` is clear, the driver suspends the calling task on `wait_queue_head_t` using `wait_event_interruptible()`, yielding CPU cycles until `write()` issues `wake_up_interruptible()`.
- **Non-Blocking Read:** If opened with `O_NONBLOCK`, an empty FIFO immediately returns `-EAGAIN`.
- **Re-entrant Safety:** Re-evaluating the FIFO state under the mutex ensures that multi-threaded wakeups never return premature `0` (EOF).
