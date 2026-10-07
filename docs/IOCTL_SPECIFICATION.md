# Linux Telemetry Character Driver: IOCTL Interface Specification

## 1. Overview
The **Linux Telemetry Character Driver** (`/dev/telemetry_dev`) provides an out-of-band hardware configuration and diagnostic interface via standard POSIX `ioctl()` system calls.

While `read()` and `write()` handle streaming payload data, `ioctl()` enables user-space supervisory applications to:
1. Reset the underlying kernel circular buffer (`kfifo`).
2. Query real-time buffer utilization and packet loss statistics.
3. Dynamically reconfigure telemetry acquisition modes (Normal, Burst, Diagnostic).

---

## 2. IOCTL Command Encoding

Commands are defined using the standard Linux `<linux/ioctl.h>` macro suite:

```c
#define TELEMETRY_IOC_MAGIC 'T' /* Magic Subsystem Identifier (0x54) */
```

| Command Identifier | Encoding Macro | Direction | Payload Type | Description |
| :--- | :--- | :--- | :--- | :--- |
| `TELEMETRY_IOC_RESET` | `_IO(TELEMETRY_IOC_MAGIC, 0)` | None | None | Atomically flushes `kfifo` and clears statistics counters |
| `TELEMETRY_IOC_GET_STATS` | `_IOR(TELEMETRY_IOC_MAGIC, 1, struct telemetry_stats)` | Kernel $\rightarrow$ User | `struct telemetry_stats` | Copies runtime metrics snapshot to user-space buffer |
| `TELEMETRY_IOC_SET_MODE` | `_IOW(TELEMETRY_IOC_MAGIC, 2, uint32_t)` | User $\rightarrow$ Kernel | `uint32_t` | Updates acquisition mode (`0`=Normal, `1`=Burst, `2`=Diag) |

---

## 3. Data Structures

### `struct telemetry_stats`
```c
struct telemetry_stats {
    uint32_t bytes_written;  /* Total bytes successfully written via write() */
    uint32_t bytes_read;     /* Total bytes extracted via read() */
    uint32_t fifo_used;      /* Number of unread bytes currently inside kfifo */
    uint32_t fifo_total;     /* Total capacity of kfifo (1024 bytes) */
    uint32_t dropped_bytes;  /* Number of bytes discarded due to buffer saturation */
    uint32_t mode;           /* Active operating acquisition mode */
};
```

---

## 4. Kernel Synchronization & Thread Safety

All IOCTL commands and file operations are protected by a dedicated `struct mutex`:

```
User Process A (write) ───────┐
                              ▼
User Process B (ioctl)  ───> [ struct mutex ] ───> [ struct kfifo (1024B) ]
                              ▲
User Process C (read)  ───────┘
```

- **Atomicity:** Resetting the FIFO (`kfifo_reset`) or updating statistics cannot race with ongoing `read()` or `write()` calls.
- **Interruptible Sleep:** Locks use `mutex_lock_interruptible()`, ensuring processes can safely receive `SIGINT` (Ctrl+C) without locking up the kernel thread.

---

## 5. Return Values & Error Codes

| Return Code | Symbolic Constant | Cause |
| :---: | :--- | :--- |
| `0` | Success | Command processed successfully |
| `-ENOTTY` | Inappropriate IOCTL | Unrecognized magic number or command sequence number $> 2$ |
| `-EFAULT` | Bad Memory Address | `copy_to_user()` or `copy_from_user()` encountered an unmapped address |
| `-EINVAL` | Invalid Argument | Value passed to `TELEMETRY_IOC_SET_MODE` exceeds valid range ($> 2$) |
| `-ERESTARTSYS` | Interrupted Call | Kernel lock waiting was interrupted by an incoming OS signal |
