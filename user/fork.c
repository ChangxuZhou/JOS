// implement fork from user space

#include "lib.h"
#include <mmu.h>
#include <env.h>

#define PAGE_FAULT_TEMP (USTACKTOP - 2 * BY2PG)

/* ----------------- help functions ---------------- */

/* Overview:
 * 	Copy `len` bytes from `src` to `dst`.
 *
 * Pre-Condition:
 * 	`src` and `dst` can't be NULL. Also, the `src` area 
 * 	 shouldn't overlap the `dest`, otherwise the behavior of this 
 * 	 function is undefined.
 */
void user_bcopy(const void *src, void *dst, size_t len)
{
	void *max;

	//	writef("~~~~~~~~~~~~~~~~ src:%x dst:%x len:%x\n",(int)src,(int)dst,len);
	max = dst + len;

	// copy machine words while possible
	if (((int)src % 4 == 0) && ((int)dst % 4 == 0)) {
		while (dst + 3 < max) {
			*(int *)dst = *(int *)src;
			dst += 4;
			src += 4;
		}
	}

	// finish remaining 0-3 bytes
	while (dst < max) {
		*(char *)dst = *(char *)src;
		dst += 1;
		src += 1;
	}

	//for(;;);
}

/* Overview:
 * 	Sets the first n bytes of the block of memory 
 * pointed by `v` to zero.
 * 
 * Pre-Condition:
 * 	`v` must be valid.
 *
 * Post-Condition:
 * 	the content of the space(from `v` to `v`+ n) 
 * will be set to zero.
 */
void user_bzero(void *v, u_int n)
{
	char *p;
	int m;

	p = v;
	m = n;

	while (--m >= 0) {
		*p++ = 0;
	}
}
/*--------------------------------------------------------------*/

/* Overview:
 * 	Custom page fault handler - if faulting page is copy-on-write,
 * map in our own private writable copy.
 * 
 * Pre-Condition:
 * 	`va` is the address which leads to a TLBS exception.
 *
 * Post-Condition:
 *  Launch a user_panic if `va` is not a copy-on-write page.
 * Otherwise, this handler should map a private writable copy of 
 * the faulting page at correct address.
 */
static void
pgfault(u_int va)
{
    int r;
    va = ROUNDDOWN(va, BY2PG);

    if (!((*vpt)[VPN(va)] & PTE_COW)) {
        user_panic("pgfault : not copy on write");
    }
    //map the new page at a temporary place
    r = syscall_mem_alloc(0, PAGE_FAULT_TEMP, PTE_V | PTE_R);
    if (r < 0) {
        user_panic("pgfault : syscall_mem_alloc");
    }

    //copy the content
    user_bcopy(va, PAGE_FAULT_TEMP, BY2PG);

    //map the page on the appropriate place
    r = syscall_mem_map(0, PAGE_FAULT_TEMP, 0, va, PTE_V | PTE_R);
    if (r < 0) {
        user_panic("pgfault : syscall_mem_map");
    }

    //unmap the temporary place
    r = syscall_mem_unmap(0, PAGE_FAULT_TEMP);
    if (r < 0) {
        user_panic("pgfault : syscall_mem_unmap");
    }
}

/* Overview:
 * 	Map our virtual page `pn` (address pn*BY2PG) into the target `envid`
 * at the same virtual address. 
 *
 * Post-Condition:
 *  if the page is writable or copy-on-write, the new mapping must be 
 * created copy on write and then our mapping must be marked 
 * copy on write as well. In another word, both of the new mapping and
 * our mapping should be copy-on-write if the page is writable or 
 * copy-on-write.
 * 
 * Hint:
 * 	PTE_LIBRARY indicates that the page is shared between processes.
 * A page with PTE_LIBRARY may have PTE_R at the same time. You
 * should process it correctly.
 */
static void
duppage(u_int envid, u_int pn)
{
	int r;
    u_int addr = pn * BY2PG;
    u_int perm = ((*vpt)[pn]) & 0xfff;

    if (perm & PTE_LIBRARY) {
        //writef("[LOG] duppage : PTE_LIBRARY [pn] [%08x]\n", pn);
        r = syscall_mem_map(0, addr, envid, addr, PTE_R);
        if (r < 0) {
            writef("[ERR] duppage : syscall_mem_map #-1\n");
        }
    } else if ((perm & PTE_R) != 0 || (perm & PTE_COW) != 0) {
        r = syscall_mem_map(0, addr, envid, addr, PTE_V | PTE_R | PTE_COW);
        if (r < 0) {
            writef("[ERR] duppage : syscall_mem_map #0\n");
        }
        r = syscall_mem_map(0, addr, 0,     addr, PTE_V | PTE_R | PTE_COW);
        if (r < 0) {
            writef("[ERR] duppage : syscall_mem_map #1\n");
        }
    } else {
        r = syscall_mem_map(0, addr, envid, addr, PTE_V);
        if (r < 0) {
            writef("[ERR] duppage : syscall_mem_map #3\n");
        }
    }

    //writef("*********A duppage end\n");

}

/* Overview:
 * 	User-level fork. Create a child and then copy our address space
 * and page fault handler setup to the child.
 *
 * Hint: use vpd, vpt, and duppage.
 * Hint: remember to fix "env" in the child process!
 * Note: `set_pgfault_handler`(user/pgfault.c) is different from 
 *       `syscall_set_pgfault_handler`. 
 */
extern void __asm_pgfault_handler(void);
int
fork(void)
{
	u_int envid;
	extern struct Env *envs;
	extern struct Env *env;
    int ret;


	//The parent installs pgfault using set_pgfault_handler
    set_pgfault_handler(pgfault);

	//alloc a new alloc
    envid = syscall_env_alloc();
    if (envid < 0) {
        user_panic("[ERR] fork %d : syscall_env_alloc failed", ret);
        return -1;
    }

    // Child (look from child)
    if (envid == 0) {
        env = &envs[ENVX(syscall_getenvid())];
        return 0;
    }

    // Done by parent
    int pn;
    for (pn = 0; pn < (USTACKTOP / BY2PG) - 2; pn++) {
        if (((*vpd)[pn / PTE2PT]) != 0 && ((*vpt)[pn]) != 0) {
            duppage(envid, pn);
        }
    }

    ret = syscall_mem_alloc(envid, UXSTACKTOP - BY2PG, PTE_V | PTE_R);
    if (ret < 0) {
        user_panic("[ERR] fork %d : syscall_mem_alloc failed", envid);
        return ret;
    }

    ret = syscall_set_pgfault_handler(envid, __asm_pgfault_handler, UXSTACKTOP);
    if (ret < 0) {
        user_panic("[ERR] fork %d : syscall_set_pgfault_handler failed", envid);
        return ret;
    }

    ret = syscall_set_env_status(envid, ENV_RUNNABLE);
    if (ret < 0) {
        user_panic("[ERR] fork %d : syscall_set_env_status failed", envid);
        return ret;
    }

    //writef("********Fork end new pid %d\n", ENVX(envid));
	return envid;
}

// Challenge!
int
sfork(void)
{
	user_panic("sfork not implemented");
	return -E_INVAL;
}
