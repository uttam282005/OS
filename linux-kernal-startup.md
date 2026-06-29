# Linux Kernel Startup

## What happens between pressing the power button and seeing your login screen?

Think of your computer as an empty planet. The Linux kernel is an advance team dropped in to build a whole colony from scratch — and they have to do it in seconds.

---

## Phase 1: The Bare Minimum (Assembly)

The kernel file on disk is compressed to save space. The very first thing that runs is a tiny program that unpacks the real kernel into memory.

Once unpacked, the kernel:

- **Checks the CPU** — Can this chip even run 64-bit code? If not, everything stops immediately.
- **Fixes addresses** — The kernel was compiled expecting one memory location but got loaded at another, so it adjusts its own address maps so everything points to the right place.

At this point the kernel can finally run its first C code. It's still extremely limited — no memory manager, no console, no way to print anything.

---

## Phase 2: Early C — Safety Nets and Saving the Bootloader's Notes

Before doing anything interesting, the kernel sets up basic safety:

- **Zeros out uninitialized data** — In normal programs, the language runtime does this. Here, the kernel *is* the runtime, so it has to do it itself.
- **Installs emergency handlers** — Without these, a crash causes a silent reboot with zero explanation (a "triple fault").
- **Saves the bootloader's information** — The bootloader gave the kernel important notes (where memory is, what the command-line arguments were, where the temporary filesystem lives). The kernel copies them into its own memory before that scratch space gets overwritten.
- **Updates the CPU's internal firmware** — Modern chips have bugs (Spectre, Meltdown), and the kernel applies the fixes right now, before anything else runs.

---

## Phase 3: Understanding the Hardware

Now the kernel looks around to understand what machine it's actually running on:

- **Asks the CPU what features it has** — Does it have hardware acceleration for encryption? Fast vector math? The kernel catalogs everything.
- **Reads the memory map** — The firmware says where RAM is and which areas are reserved. The kernel cleans up this map (firmware is notoriously sloppy) and builds a reliable version.
- **Identifies the machine** — Is this a real computer or a virtual machine? Which hypervisor (KVM, VMware, Hyper-V)? Which motherboard vendor? (This matters for applying hardware-specific workarounds.)
- **Maps all of RAM** — The kernel builds a giant address table so it can reach any byte of memory.
- **Ropes off important areas** — The temporary filesystem, crash-dump regions, and firmware-reserved memory all get marked as "do not touch."

---

## Phase 4: Core Systems Come Online

This is the longest phase — a carefully ordered chain where each system unlocks the next.

### Memory

Until now, the kernel had only a primitive way to hand out memory. In this phase, three layers of memory management come online:

1. **The page allocator** — Divides RAM into fixed-size blocks that can be combined or split as needed. This is the foundation.
2. **The slab allocator** — Specializes in small, frequently-used objects (process structures, file handles). It's like having pre-cut lumber rather than cutting each piece from raw trees.
3. **The vmalloc allocator** — Handles cases where you need a big chunk of virtual space but the physical pages are scattered.

After this, the kernel can easily allocate memory for anything.

### The Scheduler

Every CPU gets a list of tasks ready to run and the rules for picking who goes next. There's only one task right now (the boot thread itself), but the machinery is ready.

> **Interesting detail:** The boot thread gets quietly renamed as the **idle task** (PID 0). It will eventually become the "do nothing when there's nothing to do" loop.

### Synchronization (RCU)

The kernel sets up a clever system called RCU (Read-Copy-Update) for data that is read constantly but updated rarely. Instead of making every reader wait for a lock, readers just read the current version. Writers build a new copy, swap a pointer atomically, and wait for all current readers to finish before freeing the old version. Readers never wait.

### Interrupts and Time

- The **interrupt controller** is wired up — hardware can now demand the kernel's attention.
- Two timer systems come online: regular timers (for things like network timeouts) and high-resolution timers (for precise sleeps).
- The **real-time clock** is read — the kernel finally knows what time it is.
- The **random number generator** starts collecting randomness from hardware sources and boot timing.

Then interrupts are turned on. From here on, the kernel can be interrupted by hardware events like keyboard presses, network packets, or disk completions.

### The Console

All the boot messages that have been accumulating in a buffer suddenly appear on screen at once — that's why your boot log bursts onto the screen rather than trickling in steadily.

### CPU Self-Patching

The kernel finishes detecting exactly which CPU you have and which security vulnerabilities apply (Spectre, Meltdown, etc.). It then rewrites parts of its own code to use the best instructions your CPU supports and to activate the right security mitigations. The same kernel file optimizes itself for your exact processor.

### Stocking Warehouses

Every major subsystem (processes, files, networking, security, containers) creates its own private memory pools. Each department is now ready to work.

---

## Phase 5: The Big Handoff

So far, everything has been running as a single thread. The kernel now creates two new threads:

- **PID 1** — Will become the userspace init program (systemd, OpenRC, etc.), the ancestor of every process on your system.
- **PID 2** — The kernel thread dispatcher. From now on, whenever any code needs a new kernel worker, this thread handles the request.

The original boot thread then yields the CPU to one of the new threads and **permanently becomes the idle task**. The advance team has clocked off and become the standby maintenance crew.

---

## Phase 6: Final Steps and Userspace

PID 1 (still running inside the kernel) finishes the last tasks:

- **Worker threads come online** — The deferred-work system finally hires its employees (kworker threads) and starts processing the backlog.
- **Other CPU cores wake up** — If your computer has multiple cores (almost all do), the kernel sends interrupts to wake them up. Each core goes through its own mini-startup before joining the idle loop.
- **All drivers and filesystems load** — Thousands of initialization functions run. PCI, SATA, NVMe, USB, ext4, XFS, network protocols — every piece of hardware support and every filesystem registers itself. This is where most boot log messages come from.
- **The root filesystem is mounted** — On modern systems, a tiny temporary filesystem (initramfs) loads the necessary drivers, finds the real root filesystem, mounts it, and hands off.
- **Setup code is thrown away** — All the initialization code from earlier phases is now useless. The kernel reclaims those memory pages (several megabytes worth).
- **Security is locked down** — Kernel read-only data is made truly read-only at the hardware level, and kernel memory is separated from userspace memory (the Meltdown fix).

Finally, PID 1 transforms itself. It calls `execve` — the same program-loader that runs any application — and replaces itself with the userspace init program (like systemd).

**The system is fully booted.** The kernel becomes pure infrastructure — managing memory, scheduling processes, handling hardware — while userspace (your desktop, your apps, your services) runs on top.

---

## Summary

| Phase | What Happens | Simple Analogy |
|---|---|---|
| 1. Assembly | Unpack kernel, check CPU, fix addresses | Dropship lands, team checks gear |
| 2. Early C | Set safety nets, save boot info, patch CPU bugs | First aid kit, secure the cargo |
| 3. Hardware Discovery | Map memory, identify CPU and machine | Survey the planet, find resources |
| 4. Core Systems | Memory allocator, scheduler, interrupts, console, self-patching | Build power grid, roads, comms tower |
| 5. Handoff | Create PID 1 and PID 2, boot thread becomes idle task | Advance team becomes maintenance crew |
| 6. Userspace Launch | Wake other cores, load drivers, mount root, exec init | Thaw colonists, open all departments |

After all this, the kernel sits quietly underneath everything you do — allocating memory when programs ask, deciding which process runs next on each CPU core, and talking to hardware whenever a program needs to read a file, send a network packet, or display something on screen.
