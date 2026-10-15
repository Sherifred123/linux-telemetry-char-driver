# Linux Kernel Driver Tracing & Profiling Guide

This guide documents how to profile and inspect the **`/dev/telemetry_dev`** character device driver using native Linux tracing infrastructure: **`ftrace`** (Function Graph Tracer) and **`perf`**.

---

## 1. Tracing Driver Execution with `ftrace`

The Linux kernel `ftrace` framework enables microsecond-level timing analysis of driver dispatch routines without modifying source code.

### A. Trace Function Call Hierarchy (`function_graph`)
Mount debugfs / tracefs:
```bash
sudo mount -t tracefs nodev /sys/kernel/tracing
cd /sys/kernel/tracing
```

Configure `ftrace` to isolate the `telemetry_dev` module:
```bash
# Reset tracing state
echo 0 > tracing_on
echo > trace

# Select the function graph tracer
echo function_graph > current_tracer

# Filter specifically to telemetry_dev driver functions
echo telemetry_open > set_ftrace_filter
echo telemetry_read >> set_ftrace_filter
echo telemetry_write >> set_ftrace_filter
echo telemetry_ioctl >> set_ftrace_filter

# Enable tracing
echo 1 > tracing_on
```

Run the test app:
```bash
sudo ./test_app
```

Examine captured kernel execution traces:
```bash
cat /sys/kernel/tracing/trace | head -n 30
```

Sample output:
```text
  0)   0.412 us    |  telemetry_open();
  0)               |  telemetry_ioctl() {
  0)   0.120 us    |    mutex_lock_interruptible();
  0)   0.210 us    |    _copy_to_user();
  0)   0.090 us    |    mutex_unlock();
  0)   1.240 us    |  }
  0)   0.380 us    |  telemetry_release();
```

---

## 2. Lock Contention & Throughput Profiling with `perf`

### A. Measuring System Call CPU Overhead
Measure cache misses, context switches, and CPU cycles during high-throughput burst tests:
```bash
sudo perf stat -e cycles,instructions,cache-misses,context-switches ./test_app
```

### B. Mutex Lock Profiling (`perf lock`)
If compiled with `CONFIG_LOCKDEP=y` and `CONFIG_LOCK_STAT=y`:
```bash
sudo perf lock record ./test_app
sudo perf lock report
```
This verifies that contention on `telem_dev.lock` remains below 0.05% under concurrent producer-consumer loads.
