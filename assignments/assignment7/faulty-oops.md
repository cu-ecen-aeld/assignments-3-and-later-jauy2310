# ECEN5713 Assignment 7 Analysis - Faulty Device Driver

## Output

```sh
Unable to handle kernel NULL pointer dereference at virtual address 0000000000000000
Mem abort info:
  ESR = 0x0000000096000045
  EC = 0x25: DABT (current EL), IL = 32 bits
  SET = 0, FnV = 0
  EA = 0, S1PTW = 0
  FSC = 0x05: level 1 translation fault
Data abort info:
  ISV = 0, ISS = 0x00000045
  CM = 0, WnR = 1
user pgtable: 4k pages, 39-bit VAs, pgdp=0000000041bd1000
[0000000000000000] pgd=0000000000000000, p4d=0000000000000000, pud=0000000000000000
Internal error: Oops: 0000000096000045 [#1] SMP
Modules linked in: scull(O) faulty(O) hello(O)
CPU: 0 PID: 151 Comm: sh Tainted: G           O       6.1.44 #1
Hardware name: linux,dummy-virt (DT)
pstate: 80000005 (Nzcv daif -PAN -UAO -TCO -DIT -SSBS BTYPE=--)
pc : faulty_write+0x10/0x20 [faulty]
lr : vfs_write+0xc8/0x390
sp : ffffffc008dd3d20
x29: ffffffc008dd3d80 x28: ffffff8001b35cc0 x27: 0000000000000000
x26: 0000000000000000 x25: 0000000000000000 x24: 0000000000000000
x23: 000000000000000c x22: 000000000000000c x21: ffffffc008dd3dc0
x20: 0000005574b79440 x19: ffffff8001bf0d00 x18: 0000000000000000
x17: 0000000000000000 x16: 0000000000000000 x15: 0000000000000000
x14: 0000000000000000 x13: 0000000000000000 x12: 0000000000000000
x11: 0000000000000000 x10: 0000000000000000 x9 : 0000000000000000
x8 : 0000000000000000 x7 : 0000000000000000 x6 : 0000000000000000
x5 : 0000000000000001 x4 : ffffffc000785000 x3 : ffffffc008dd3dc0
x2 : 000000000000000c x1 : 0000000000000000 x0 : 0000000000000000
Call trace:
 faulty_write+0x10/0x20 [faulty]
 ksys_write+0x74/0x110
 __arm64_sys_write+0x1c/0x30
 invoke_syscall+0x54/0x130
 el0_svc_common.constprop.0+0x44/0xf0
 do_el0_svc+0x2c/0xc0
 el0_svc+0x2c/0x90
 el0t_64_sync_handler+0xf4/0x120
 el0t_64_sync+0x18c/0x190
Code: d2800001 d2800000 d503233f d50323bf (b900003f) 
---[ end trace 0000000000000000 ]---
```

## Output Analysis

In the output before the system crash, we get this message that says "Unable to handle kernel NULL pointer dereference at virtual address 0000000000000000". This is an indicator
that we are attempting to access a NULL pointer (hence the message and 0000000000000000). After listing that information, it lists some information about the fault and some system
info (registers, error codes, loaded modules, process IDs, etc.). The next good indicator of the fault's location is the PC register, which references faulty_write+0x10/0x20
(error occurred at 16 bytes into the faulty_write function, out of the function's total size of 32 bytes), as well as the source (in this case, the faulty module loaded into the
kernel). The next few lines are the register states, which could be important if you know which registers are being used by the program. The call trace is the next vital piece of
information, which tells us the function calls that led up to the crash. For a simple case as a faulty write, we know that this state will be deterministic (and therefore always
happen no matter what), so the call trace is less important for this particular case. However, for a less deterministic case where an error doesn't always happen by design, we can
expect to find some good information about what values caused the crash (especially when combined with other values). For example, combining a function call stack with information
on the current stack pointer (SP register), we can identify if there was something wrong with the data in the stack.

## Objdump

```sh
0000000000000000 <faulty_write>:
   0:	d2800001 	mov	x1, #0x0                   	// #0
   4:	d2800000 	mov	x0, #0x0                   	// #0
   8:	d503233f 	paciasp
   c:	d50323bf 	autiasp
  10:	b900003f 	str	wzr, [x1]
  14:	d65f03c0 	ret
  18:	d503201f 	nop
  1c:	d503201f 	nop

```

## Objdump Analysis

Using aarch64-linux-gnu-objdump allows us to view the disassembly of the faulty.ko file. Looking at the function faulty_write (which we got from the output of the kernel oops),
we can see that offset 16/0x10 corresponds to: 

```
  10:	b900003f 	str	wzr, [x1]
```

This line indicates a store instruction involving the zero register and the x1 register. If this were a non-deterministic value, we could inspect the value of register x1 from the
above crash report.

## Conclusion

Using a combination of objdump (for the right architecture of the file you're dumping) and the kernel oops report, we can analyze exactly where the fault occured through the call
stack, program counter, values of the individual registers, and the assembly instructions that led up to the fault.