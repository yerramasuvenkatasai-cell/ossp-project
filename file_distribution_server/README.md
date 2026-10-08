# High-Concurrency File Distribution Server

OS & Systems Programming (25CS2104E) — Section 5, Team 8
C implementation matching the project scenario: multi-client TCP server,
worker thread pool, synchronized request queue, `mmap()`-based file
transfer, logging, monitoring, and signal-driven shutdown.

## 1. Files

```
file_distribution_server/
├── main.c     -- the server (all 8 modules, see comment at top of file)
├── client.c     -- test client (LIST / GET)
├── Makefile
└── README.md    -- this file
```

## 2. One-time setup on Ubuntu

```bash
sudo apt update
sudo apt install build-essential
```

## 3. Build

```bash
cd file_distribution_server
make
```

This produces two executables: `server` and `client`.
(`make debug` rebuilds both with AddressSanitizer + UndefinedBehaviorSanitizer
enabled — use this while testing to catch memory bugs and races early.)

## 4. Prepare files to share

The server serves files from a `shared/` folder next to the executable.
It is created automatically the first time you run the server, but you
need to put some files in it:

```bash
mkdir -p shared
cp ~/Videos/lecture1.mp4 shared/
cp ~/Documents/lab_manual.pdf shared/
echo "hello world" > shared/readme.txt
```

## 5. Run the server

```bash
./server [port] [workers]
```

Examples:

```bash
./server                 # port 8080, 8 worker threads (defaults)
./server 9000 16         # port 9000, 16 worker threads
```

The server prints its PID and keeps running until you stop it:

```
Server pid 12345 listening on port 8080 with 8 workers, sharing ./shared/
Ctrl+C = graceful stop | kill -USR1 12345 = stats
```

- **Ctrl+C** (or `kill -TERM <pid>`) → graceful shutdown: the acceptor
  stops taking new connections, all workers finish whatever transfer they
  are doing, then the process exits cleanly.
- **`kill -USR1 <pid>`** → prints a live stats line (active connections,
  queue length, total downloads, memory RSS, page faults) without
  stopping the server.

Two log files are created next to the executable:

- `activity.log` — connect/disconnect/request events, one line per event.
- `downloads.log` — one line per completed/failed transfer with size,
  duration, and status. Good raw data for your report/graphs.

## 6. Run the client (from another terminal, or another machine)

List what the server is sharing:

```bash
./client 127.0.0.1 8080 LIST
```

Download a file:

```bash
./client 127.0.0.1 8080 GET lecture1.mp4 my_copy.mp4
```

(If you omit the last argument, it saves using the remote name in the
current directory: `./client 127.0.0.1 8080 GET lecture1.mp4`.)

Replace `127.0.0.1` with the server machine's IP address when the client
runs elsewhere on the network.

## 7. Test concurrency (the whole point of the project)

Launch many clients against the same file at once and confirm every copy
is byte-identical to the original:

```bash
for i in $(seq 1 20); do
  ./client 127.0.0.1 8080 GET lecture1.mp4 /tmp/copy_$i.mp4 &
done
wait

for i in $(seq 1 20); do
  cmp -s /tmp/copy_$i.mp4 shared/lecture1.mp4 && echo "$i OK" || echo "$i MISMATCH"
done
```

Watch the server's stats while this runs (from a third terminal):

```bash
watch -n1 "kill -USR1 \$(pgrep -f './server 8080')"
```

(Check the server's own terminal for the `[STATS]` lines it prints.)

## 8. Load-test with many concurrent clients

A simple way to push more load than manual terminals allow, useful for
your JMeter-style performance-testing section:

```bash
seq 1 100 | xargs -P 50 -I{} ./client 127.0.0.1 8080 GET lecture1.mp4 /tmp/load_{}.mp4
```

`-P 50` runs 50 clients in parallel; increase it to push the server
harder and watch `downloads.log` / `[STATS]` for throughput and failures.

## 9. Useful debugging commands (also good for your report)

| Command | What it shows |
|---|---|
| `ps -T -p <pid>` | All threads of the running server |
| `cat /proc/<pid>/status \| grep VmRSS` | Current memory usage |
| `ls -l /proc/<pid>/fd` | Open file descriptors (sockets, log files, mmap'd file fds are closed after mapping) |
| `ipcs` | (not used here — no SysV shared memory, only mmap) |
| `strace -f -e trace=network ./server 8080 4` | Trace socket system calls |
| `valgrind --tool=helgrind ./server` | Check for race conditions on the queue/file-table locks |
| `make debug && ./server` | Rebuild with sanitizers, then run the same tests above |

## 10. How this maps to the syllabus / your PPT

| Module in code | Syllabus concept | Where |
|---|---|---|
| `acceptor_main()` | System calls, sockets as file descriptors (CO-1) | `socket()`, `bind()`, `listen()`, `accept()` |
| `queue_push()`/`queue_pop()` | Synchronization, race conditions (CO-6) | mutex + two counting semaphores (classic bounded-buffer producer–consumer) |
| `worker_main()` | Threads, thread lifecycle (CO-2) | `pthread_create()`/`pthread_join()`, one pool serving all clients |
| `file_acquire()`/`file_release()` | Virtual memory, demand paging (CO-4) | `mmap()` with a reference count so N clients can share one mapping of the same file |
| `handle_get()` | File I/O, file abstraction (CO-5) | `open()`, `fstat()`, sending straight from the mapped pages |
| `log_activity()`/`log_download()` | File systems, buffered I/O (CO-5) | append-only log files, one mutex so lines never interleave |
| `monitor_main()`/`print_stats()` | OS-level introspection (CO-4) | `getrusage()`, `/proc/self/status` (VmRSS, page faults) |
| `on_signal()` + `sigaction()` in `main()` | Signals, process control (CO-3) | `SIGINT`/`SIGTERM` for graceful stop, `SIGUSR1` for on-demand stats, `SIGPIPE` ignored so a dropped client can't kill the server |

## 11. Known limits (mention these as "future work" if asked)

- No resume/partial-download support yet (the scenario given to you as a
  C project doesn't require it — that feature is in your separate
  Python/FastAPI project's abstract).
- No TLS/authentication — the C scenario is about OS mechanisms, not
  web security.
- `MAX_FILES` (128) and `QUEUE_SIZE` (64) are compile-time constants in
  `main.c` — raise them there and rebuild if you need more headroom.
