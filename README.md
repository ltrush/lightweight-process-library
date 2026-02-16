# Lightweight Process (LWP) Library

A small user‑level threading library written in C with a round‑robin scheduler. This was a class project for CPE 453, built and tested on my school’s Unix servers (x86‑64 Linux). It relies on x86‑64 low‑level context switching, so it will not run on macOS without significant porting.

## What It Does
- Creates user‑level threads with their own stacks.
- Saves/restores CPU state to switch between threads.
- Provides a simple round‑robin scheduler implementation.

## Built/Run Environment
- **Target:** x86‑64 Linux
- **Tested on:** school Unix servers (x86‑64)
- **Not supported:** macOS 

## Build
From `src/`:

```bash
make
```

This produces `liblwp.so` and `liblwp.a`.

## My Work
Primary files I implemented:
- `src/lwp.c`
- `src/rr.c`
- `src/rr.h`

## What I Learned
- Manual stack allocation and alignment using `mmap`.
- Saving/restoring register state for context switching.

## How It Works
I build user‑level threads by manually creating a stack and setting up its initial state so that a context switch will “return” into a wrapper that invokes the target function. In `lwp_create`, I allocate a stack with `mmap`, push the wrapper address as the return address, and initialize the saved register context (`rsp`, `rbp`, and argument registers). When the scheduler selects a brand‑new thread, restoring this context effectively starts execution at the wrapper, which then calls the thread’s function with its argument.

Thread lifecycle is managed with simple lists. A main list tracks all threads, while separate FIFO lists track terminated threads and threads that are waiting. The round‑robin scheduler maintains its own queue of runnable threads. On yield or exit, threads are removed from the scheduler and placed into the appropriate list (terminated or waiting), and the next runnable thread is selected. 

## Provided by Professor Nico
The following were provided as course materials and/or starter code (and may be unmodified or lightly modified by me):
- Assignment spec `asgn2.pdf`
- `src/magic64.S`
- `src/fp.h`
- Headers in `include/` (e.g., `include/lwp.h`, `include/fp.h`)
- `src/lwp.h`

## Attribution
This README was written with the assistance of AI. The code was not.
