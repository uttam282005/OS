# Linux Kernel Startup

## What happens between pressing the power button and seeing your login screen?

Think of your computer as an empty planet. The Linux kernel is an advance team dropped in to build a whole colony from scratch — and they have to do it in seconds.

---

## Phase 1: The Bare Minimum (Assembly)

The kernel file on disk is compressed to save space. The very first thing that runs is a tiny decompressor that unpacks the real kernel into memory. As a security measure, the kernel is loaded at a random memory address so attackers can't predict where its code lives.

Once unpacked, the kernel does the bare essentials that can only be done in assembly language:

- **Sets up a stack** — You can't call any functions without a stack, so a small region of memory is reserved for this.
- **Installs the GDT and IDT** — These are two lookup tables the CPU requires. The GDT (Global Descriptor Table) defines how memory segments are organized. The IDT (Interrupt Descriptor Table) tells the CPU where to jump when something goes wrong — a divide-by-zero, a page fault, or a hardware interrupt. Without the IDT, any crash causes a "triple fault" — the CPU tries three times to find a handler, fails, and silently reboots with no error message.
- **Checks the CPU** — The kernel runs a verification to confirm the chip supports 64-bit mode and the SSE2 instruction set (required by modern code). If either is missing, the machine halts immediately.
- **Fixes the address mismatch** — The kernel was compiled expecting to live at one specific memory address, but the random load (KASLR) put it somewhere else. Every internal address is now wrong. The kernel adjusts its page tables — the CPU's virtual-to-physical address translation maps — so that all addresses point to the correct physical locations.

With the stack ready, the exception table installed, the CPU verified, and addresses fixed, the kernel can finally leave assembly and run its first C code. But it's still extremely limited — no memory manager, no console, no way to print anything.

---

## Phase 2: Early C — Safety Nets and Saving the Bootloader's Notes

We're running C now, but with almost no infrastructure. The kernel sets up basic safety:

- **Zeros out uninitialized data** — In normal programs, the language runtime does this automatically before your code runs. Here, the kernel *is* the runtime, so it has to wipe this memory region itself.
- **Installs a minimal IDT** — Remember that crash handler table from Phase 1? Now we fill it with actual handlers for basic faults like page faults and general protection faults. This way, if something crashes from this point on, the kernel can at least print an error instead of silently rebooting.
- **Saves the bootloader's information** — The bootloader gave the kernel important notes: a map of where RAM is, the command-line arguments, where the temporary filesystem (initramfs) lives. All of this is sitting in scratch memory that will soon be overwritten, so the kernel copies it into its own protected structures.
- **Applies CPU microcode patches** — Modern processors have bugs. Intel and AMD release microcode updates — firmware patches that run inside the CPU itself — to fix issues like Spectre, Meltdown, and other speculative-execution vulnerabilities. The kernel loads and applies these patches right now, before anything else runs. It's like updating your BIOS, but done by the kernel at every boot.

---

## Phase 3: Understanding the Hardware

Now the kernel looks around to understand what machine it's actually running on. This is called `setup_arch` — architecture-specific setup — and it's where the advance team surveys the planet:

- **Asks the CPU what features it has** — The kernel sends special CPU identification instructions and the chip responds with a detailed feature list: Does it support hardware-accelerated encryption? Advanced vector math for fast memory copies? Which speculative-execution bugs is it vulnerable to? Every capability is cataloged into a structure that the rest of the kernel will consult whenever it needs to decide which code path to use.
- **Reads the memory map** — The firmware provides an E820 map describing where RAM is, which regions are reserved for hardware, and which are available. This map is famously unreliable — regions overlap, ranges are off by one, special areas aren't marked. The kernel sanitizes it into a trustworthy version.
- **Sets up the early allocator** — A primitive memory allocator called `memblock` comes online. It does nothing fancy — just tracks "this range of physical memory is free, this range is reserved." It's the only allocator the kernel will have for a while.
- **Identifies the machine** — Is this real hardware or a virtual machine? If virtual, which hypervisor (KVM, Xen, Hyper-V, VMware)? Which motherboard vendor and BIOS version? This information lets the kernel apply per-vendor quirks later ("oh, this is a 2019 Lenovo laptop with the buggy touchpad firmware, apply workaround").
- **Sets up early printing** — A minimal serial-port driver can be activated if the user passed special boot flags. This is the first way the kernel can communicate with the outside world — a low-power radio, not a real comms tower.
- **Maps all of RAM** — The kernel builds the "direct map": a giant range of virtual addresses where every physical page of RAM gets a corresponding virtual address. After this, the kernel can reach any byte of memory just by computing an offset.
- **Ropes off important areas** — The initramfs (temporary filesystem), crash-dump regions, and ACPI-reserved memory all get marked as reserved. The early allocator won't hand them out.
- **Finalizes the safety inspector** — A memory error detector (KASAN) that was running with placeholder stubs now gets real shadow memory and starts actively catching use-after-free bugs and buffer overflows.

---

## Phase 4: Core Systems Come Online

This is the longest phase — a carefully ordered chain of initialization where each subsystem unlocks the next. The function that drives this is `start_kernel`, and almost the entire sequence runs with interrupts disabled. The kernel is laser-focused on bringing systems up in the right order.

### Boot CPU Housekeeping

- A **stack overflow detector** is armed — a magic value is written at the bottom of the kernel's stack so that if a function call goes too deep and overwrites it, the kernel can detect the corruption and panic with a meaningful error.
- The **boot CPU is officially marked online** — until this point, any code iterating over CPUs would see zero of them.
- The **first boot message** is printed into a memory buffer ("Linux version 6.x..."). It doesn't appear on screen yet — no console exists — but it's sitting there waiting.

### Code Self-Patching

The kernel uses a technique called **static keys** and **alternatives** to rewrite its own code at boot. Feature checks that are always false get patched into a single no-op instruction (zero cost). Indirect function calls get converted into direct calls. The same kernel binary optimizes itself for your exact CPU model and configuration.

### Security Framework

A pluggable security layer (LSM) powers up just enough to install the most fundamental hooks — basic permission checks that SELinux or AppArmor will later extend.

### Memory Management Comes Alive

Until now, the kernel has been surviving on a primitive allocator that just hands out raw chunks of physical memory. Three layered allocators now come online:

1. **The page allocator (buddy system)** — Divides physical RAM into power-of-two blocks (1 page, 2 pages, 4 pages, 8 pages, etc.). When memory is requested, it finds the smallest block that fits. When memory is freed, it merges adjacent blocks back into larger ones. This prevents memory from fragmenting into unusably small pieces over time.
2. **The slab allocator** — Sits on top of the page allocator and specializes in *objects* rather than raw pages. Structures like `task_struct` (representing a process) or `inode` (representing a file) are allocated and freed constantly. Instead of getting a whole page each time, the slab allocator carves pages into fixed-size slots and reuses them quickly. It's like having pre-cut lumber rather than cutting each piece from raw trees.
3. **The vmalloc allocator** — Handles the case where you need a large region contiguous in *virtual* address space but can't find that many contiguous physical pages. It stitches together scattered physical pages via the page tables so they appear continuous to the caller.

After these come online, the standard allocation functions work. The kernel is no longer memory-constrained.

### The Scheduler

Every CPU gets a **runqueue** — a list of tasks ready to run on that CPU — and the rules for picking who goes next are established. The scheduler supports multiple policies (deadline-based, real-time, fair-sharing, and idle) and arranges them in priority order. There's still only one task in the system (the boot thread), but the machinery is in place.

> **Key detail:** The boot thread is quietly relabeled as the **idle task** (PID 0). It will eventually become the "do nothing when there's nothing to do" loop — the placeholder that runs on a CPU when no other task wants it.

### Synchronization: RCU

The kernel brings up **RCU (Read-Copy-Update)**, a synchronization mechanism designed for data that is read constantly but updated rarely (like the list of network devices or the routing table). With traditional locks, every reader would have to acquire and release a lock — a cost even when no writer is active. RCU lets readers proceed completely lock-free: they just read the current version. Writers build a new copy in memory, atomically swap a single pointer to publish it, and then wait until every in-progress reader has finished before freeing the old version. Readers always see either the old or the new data, never a partially-updated state, and they pay zero cost on the fast path.

### Interrupts and Time

The kernel now builds the infrastructure that lets it respond to events and measure time:

- The **interrupt controller** is wired up — the hardware can now deliver interrupts to the CPU. Each interrupt line gets bookkeeping structures allocated for it.
- Two **timer systems** come online: regular timers (for millisecond-scale events like network timeouts) and high-resolution timers (for nanosecond-precision sleeps).
- The **software interrupt system** (softirq) is initialized — a lightweight mechanism for deferring work that was triggered by a hardware interrupt. This is used by networking, block devices, and tasklets.
- The **real-time clock** (the battery-backed clock on the motherboard) is read. For the first time, the kernel knows what time it is. Both the wall-clock time and the monotonic clock (which always moves forward, unaffected by system time changes) are anchored to this reading.
- The **random number generator** starts collecting entropy — randomness gathered from hardware sources like CPU instructions, bootloader-provided seeds, and the precise timing of early boot events.

Then interrupts are turned on. Up to this point, every subsystem was set up with interrupts disabled because a stray interrupt hitting partially-initialized code would be catastrophic. Now that the controller is wired, timers can fire, RCU is alive, and the timekeeper has a clock, the kernel can finally accept interrupts. From here on, hardware events like keyboard presses, network packets, and disk completions can interrupt the kernel at any time.

### The Real Console

All the boot messages that have been accumulating in the printk buffer since the very first announcement suddenly appear on screen at once. The console subsystem walks each registered display driver (VGA text mode, framebuffer, serial port) and drains the buffer to them. This is why your boot log appears in a sudden burst rather than a steady trickle.

### CPU Finalization and Self-Patching

The kernel finishes its deep CPU inspection and makes two important moves:

1. **Security mitigation selection** — Based on the CPU's exact model and microcode version, the kernel chooses which speculative-execution mitigations to activate (Spectre variant 1, variant 2, Meltdown, MDS, etc.). Different CPUs need different combinations.
2. **Instruction patching** — The kernel walks every site in its own code that was marked with an `ALTERNATIVE` macro and patches in the best instructions for this CPU. Got AVX-512? Memory copy paths get rewritten to use it. Don't have it? They get rewritten to a generic fallback. The same kernel binary optimizes itself for your exact processor generation.

### Stocking Warehouses: Slab Caches

Every major subsystem now creates its own private slab caches — the pre-sized object pools provided by the slab allocator:

- **Process management** — Creates caches for the process structure (`task_struct`), credentials, signal handlers, and kernel stacks. The kernel can finally create new processes.
- **Virtual filesystem (VFS)** — Comes online so the kernel can mount filesystems. Even before any disk driver exists, a tiny in-memory filesystem is available. The `/proc` filesystem also starts up — the pseudo-filesystem that lets userspace peek at every running process.
- **Cgroups** — Finalized so processes can be grouped and limited (CPU shares, memory limits, etc.).
- **Namespaces** — The plumbing for containers (process ID namespaces, network namespaces, mount namespaces) is wired up.
- **Security modules** — Full bring-up of the LSM framework so SELinux or AppArmor can enforce policies.
- **Page cache and block layer** — The caching layer that keeps recently-read file data in memory is initialized.

Every department now has its own stocked warehouse and is ready to work.

---

## Phase 5: The Big Handoff

So far, everything has been running as a single thread — the boot thread we've been following since the first assembly instruction. The kernel now transforms from a single-threaded setup program into a multitasking operating system. It creates two new threads:

- **PID 1 (kernel_init)** — A kernel thread now, but destined to transform into the userspace init program (systemd, OpenRC, etc.). It will become the ancestor of every process on your system.
- **PID 2 (kthreadd)** — The kernel thread dispatcher. From now on, whenever any kernel code needs a new background thread, the request is routed here. This is the permanent hiring office for kernel workers.

PID 1 is created first so it gets PID 1, then PID 2. But PID 1 can't actually start working until PID 2 is alive (since almost everything it does will need to create kernel threads). So PID 1 immediately goes to sleep waiting for a signal. Once PID 2 is alive, the signal arrives and PID 1 proceeds.

The original boot thread then yields the CPU to one of the new threads. **This is the magic moment.** The thread that has been running since decompression, through CPU verification, memory mapping, subsystem initialization, and every other step, now permanently becomes the **idle task** (PID 0). It will only run again when every other task on that CPU is blocked or sleeping.

The advance team has clocked off and become the standby maintenance crew. The civilian government takes over.

---

## Phase 6: Final Steps and Userspace

PID 1 (still running inside the kernel, in kernel mode) has a final checklist before it can hand the keys to userspace:

- **Worker threads come online** — The workqueue subsystem finally hires its background worker threads. These kernel threads handle deferred work that drivers and subsystems have been queuing up. The help desk that's been collecting tickets finally opens for business.
- **Other CPU cores wake up** — If your computer has multiple cores (almost all do), the kernel sends special interrupts to wake them up. Each secondary core goes through its own abbreviated startup sequence: it sets up page tables, connects to the interrupt controller, and enters the idle loop. The scheduler then builds **scheduling domains** — a hierarchy describing which CPUs share caches, which are on the same NUMA node, and which are hyperthreading siblings. This lets the load balancer intelligently decide where each task should run. Real multitasking truly begins here.
- **All drivers and filesystems load** — Thousands of driver and subsystem initialization functions run in ordered groups: core infrastructure first, then subsystems, then filesystems, then device drivers, then late init. PCI, SATA, NVMe, USB, ext4, XFS, Btrfs, network protocols — every piece of hardware support and every filesystem registers itself. This is where most boot log messages come from.
- **The root filesystem is mounted** — On modern systems, an initramfs (initial RAM filesystem) was loaded into memory early in boot. The kernel unpacks it into a RAM-based filesystem and looks for an `/init` script. That script loads the storage drivers needed to access the real root filesystem (e.g., an ext4 partition on an NVMe drive), mounts it, and pivots the system over to it. Without an initramfs, the kernel falls back to mounting whatever device was specified directly.
- **Setup code is thrown away** — All the initialization code from Phase 2 through early Phase 6 — hundreds of functions tagged as init-only — is now dead weight. The kernel reclaims those memory pages (typically a few megabytes). The advance team's scaffolding, construction tools, and temporary signage are packed up and the space becomes usable real estate.
- **Security is locked down** — Kernel data that should never change (read-only data) is made truly read-only at the page-table level. Then the kernel finalizes Page Table Isolation (PTI), the mitigation for the Meltdown vulnerability. During boot, the kernel was patching its own code and writing tables that had to stay writable until finished. Now that construction is done, the doors are locked: kernel page tables are separated from userspace page tables so that user programs can't leak kernel memory through speculative-execution side channels.

Finally, PID 1 transforms itself. It searches for a userspace init program by trying several paths in order:
1. A custom path from the boot command line (often `/init` inside the initramfs)
2. The compiled-in default init path
3. `/sbin/init`, `/etc/init`, `/bin/init`
4. `/bin/sh` as a last resort

When it finds one, it calls the program loader — the same mechanism that launches any application — and replaces itself entirely. The kernel-mode PID 1 thread vanishes, and in its place runs the userspace init program (systemd, OpenRC, etc.). Same process ID, same task, but now running unprivileged userspace code.

**The system is fully booted.** The kernel becomes pure infrastructure — managing memory, scheduling processes, handling hardware interrupts, enforcing security policies — while userspace (your desktop, your applications, your services) runs on top.

---

## Summary

| Phase | What Happens | Simple Analogy |
|---|---|---|
| 1. Assembly | Decompress kernel, set up stack and CPU tables, verify CPU, fix page tables | Dropship lands, team unloads gear and checks their equipment |
| 2. Early C | Zero data, install exception handlers, save boot params, patch CPU microcode | First aid kit, secure the cargo, update the lander's firmware |
| 3. Hardware Discovery | Query CPU features, read memory map, identify machine, build direct memory map | Survey the planet, map terrain, identify resources |
| 4. Core Systems | Memory allocators, scheduler, RCU, interrupt controller, timers, console, self-patching | Build power grid, roads, comms tower, government buildings |
| 5. Handoff | Create PID 1 (future init) and PID 2 (kernel thread dispatcher), boot thread becomes idle | Advance team becomes maintenance crew, civilian government forms |
| 6. Userspace Launch | Wake other CPUs, load drivers, mount root filesystem, free init memory, lock down security, launch init | Thaw colonists, open all departments, governor takes office |

After all this, the kernel sits quietly underneath everything you do — allocating memory when programs ask, deciding which process runs next on each CPU core, and talking to hardware whenever a program needs to read a file, send a network packet, or display something on screen.
