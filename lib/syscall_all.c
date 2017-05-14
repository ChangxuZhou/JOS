#include "../drivers/gxconsole/dev_cons.h"
#include <mmu.h>
#include <env.h>
#include <printf.h>
#include <pmap.h>
#include <sched.h>
#include <kerelf.h>

extern char *KERNEL_SP;
extern struct Env *curenv;

/* Overview:
 * 	This function is used to print a character on screen.
 * 
 * Pre-Condition:
 * 	`c` is the character you want to print.
 */
void sys_putchar(int sysno, int c, int a2, int a3, int a4, int a5)
{
	printcharc((char) c);
	return ;
}

/* Overview:
 * 	This function enables you to copy content of `srcaddr` to `destaddr`.
 *
 * Pre-Condition:
 * 	`destaddr` and `srcaddr` can't be NULL. Also, the `srcaddr` area 
 * 	shouldn't overlap the `destaddr`, otherwise the behavior of this 
 * 	function is undefined.
 *
 * Post-Condition:
 * 	the content of `destaddr` area(from `destaddr` to `destaddr`+`len`) will
 * be same as that of `srcaddr` area.
 */
void *memcpy(void *destaddr, void const *srcaddr, u_int len)
{
	char *dest = destaddr;
	char const *src = srcaddr;

	while (len-- > 0) {
		*dest++ = *src++;
	}

	return destaddr;
}

/* Overview:
 *	This function provides the environment id of current process.
 *
 * Post-Condition:
 * 	return the current environment id
 */
u_int sys_getenvid(void)
{
	return curenv->env_id;
}

/* Overview:
 *	This function enables the current process to give up CPU.
 *
 * Post-Condition:
 * 	Deschedule current environment. This function will never return.
 */
void sys_yield(void)
{
    // Store current trap frame
    bcopy((u_int) KERNEL_SP - sizeof(struct Trapframe),
          TIMESTACK - sizeof(struct Trapframe),
          sizeof(struct Trapframe));

    sched_yield();
}

/* Overview:
 * 	This function is used to destroy the current environment.
 *
 * Pre-Condition:
 * 	The parameter `envid` must be the environment id of a 
 * process, which is either a child of the caller of this function 
 * or the caller itself.
 *
 * Post-Condition:
 * 	Return 0 on success, < 0 when error occurs.
 */
int sys_env_destroy(int sysno, u_int envid)
{
	/*
		printf("[%08x] exiting gracefully\n", curenv->env_id);
		env_destroy(curenv);
	*/
	int r;
	struct Env *e;

	if ((r = envid2env(envid, &e, 1)) < 0) {
		return r;
	}

	printf("[%08x] destroying %08x\n", curenv->env_id, e->env_id);
	env_destroy(e);
	return 0;
}

/* Overview:
 * 	Set envid's pagefault handler entry point and exception stack.
 * 
 * Pre-Condition:
 * 	xstacktop points one byte past exception stack.
 *
 * Post-Condition:
 * 	The envid's pagefault handler will be set to `func` and its
 * 	exception stack will be set to `xstacktop`.
 * 	Returns 0 on success, < 0 on error.
 */
int sys_set_pgfault_handler(int sysno, u_int envid, u_int func, u_int xstacktop)
{
	struct Env *env;
	int ret;
    ret = envid2env(envid, &env, 0);
    if (ret < 0)
        return ret;
    env->env_pgfault_handler = func;

    //printf("PID %d handler entry [%8x]\n", ENVX(envid), env->env_pgfault_handler);
    xstacktop = TRUP(xstacktop);
    env->env_xstacktop = xstacktop;

	return 0;
}

/* Overview:
 * 	Allocate a page of memory and map it at 'va' with permission
 * 'perm' in the address space of 'envid'.
 *
 * 	If a page is already mapped at 'va', that page is unmapped as a
 * side-effect.
 * 
 * Pre-Condition:
 * perm -- PTE_V is required,
 *         PTE_COW is not allowed(return -E_INVAL),
 *         other bits are optional.
 *
 * Post-Condition:
 * Return 0 on success, < 0 on error
 *	- va must be < UTOP
 *	- env may modify its own address space or the address space of its children
 */
int sys_mem_alloc(int sysno, u_int envid, u_int va, u_int perm)
{

	struct Env *env;
	struct Page *ppage;
	int ret;

    if (perm & PTE_V == 0 || perm & PTE_COW != 0) {
        printf("[ERR] perm\n");
        return -E_INVAL;
    }

    if (va >= UTOP) {
        printf("[ERR] va\n");
        return -1;
    }

    // Get env
    ret = envid2env(envid, &env, 0);
    if (ret < 0) {
        printf("[ERR] envid2env\n");
        return ret;
    }

    // Allocate page
	ret = page_alloc(&ppage);
    if (ret < 0) {
        printf("[ERR] page_alloc\n");
        return ret;
    }

    // Clean page
    bzero(page2kva(ppage), BY2PG);

    // Insert page to env
    ret = page_insert(env->env_pgdir, ppage, va, perm | PTE_R | PTE_V);
    if (ret < 0) {
        printf("[ERR] page_insert\n");
        return ret;
    }
    return 0;
}

/* Overview:
 * 	Map the page of memory at 'srcva' in srcid's address space
 * at 'dstva' in dstid's address space with permission 'perm'.
 * Perm has the same restrictions as in sys_mem_alloc.
 * (Probably we should add a restriction that you can't go from
 * non-writable to writable?)
 *
 * Post-Condition:
 * 	Return 0 on success, < 0 on error.
 *
 * Note:
 * 	Cannot access pages above UTOP.
 */
int sys_mem_map(int sysno, u_int srcid, u_int srcva, u_int dstid, u_int dstva,
				u_int perm)
{
	int ret;
	u_int round_srcva, round_dstva;
	struct Env *srcenv;
	struct Env *dstenv;
	struct Page *ppage;
	Pte *ppte;

	ppage = NULL;
	ret = 0;
	round_srcva = ROUNDDOWN(srcva, BY2PG);
	round_dstva = ROUNDDOWN(dstva, BY2PG);

    // va restriction
    if (srcva >= UTOP || dstva >= UTOP || srcva != round_srcva || dstva != round_dstva) {
        printf("[ERR] va\n");
        return -1;
    }

    // Get env
    ret = envid2env(srcid, &srcenv, 1);
    if (ret < 0) {
        printf("[ERR] envid2env\n");
        return ret;
    }

    ret = envid2env(dstid, &dstenv, 1);
    if (ret < 0) {
        printf("[ERR] envid2env\n");
        return ret;
    }

    // Lookup the page in the source env
    ppage = page_lookup(srcenv->env_pgdir, srcva, &ppte);
    if (ppage == NULL) {
        printf("[ERR] sys_mem_map : page_lookup : [%8x] : [%8x] : [%8x]\n", srcenv->env_pgdir, srcva, &ppte);
        return -1;
    }

    //printf("sys_mem_map page insert [%8x] \n", dstva);

    // Insert the page to the destination env
    ret = page_insert(dstenv->env_pgdir, ppage, dstva, perm);
    if (ret < 0) {
        printf("[ERR] page_insert\n");
        return ret;
    }

	return 0;
}

/* Overview:
 * 	Unmap the page of memory at 'va' in the address space of 'envid'
 * (if no page is mapped, the function silently succeeds)
 *
 * Post-Condition:
 * 	Return 0 on success, < 0 on error.
 *
 * Cannot unmap pages above UTOP.
 */
int sys_mem_unmap(int sysno, u_int envid, u_int va)
{
	int ret;
	struct Env *env;

    ret = envid2env(envid, &env, 1);
    if (ret < 0) {
        printf("[ERR] sys_mem_unmap : envid2env\n");
        return ret;
    }

    page_remove(env->env_pgdir, va);

	return ret;
}

/* Overview:
 * 	Allocate a new environment.
 *
 * Pre-Condition:
 * The new child is left as env_alloc created it, except that
 * status is set to ENV_NOT_RUNNABLE and the register set is copied
 * from the current environment.
 *
 * Post-Condition:
 * 	In the child, the register set is tweaked so sys_env_alloc returns 0.
 * 	Returns envid of new environment, or < 0 on error.
 */
int sys_env_alloc(void)
{
	int r;
	struct Env *e;

    r = env_alloc(&e, curenv->env_id);
    if (r < 0) {
        return r;
    }

    bcopy(KERNEL_SP - sizeof(struct Trapframe), &e->env_tf, sizeof(struct Trapframe));
    Pte *ppte = NULL;
    pgdir_walk(curenv->env_pgdir, USTACKTOP - BY2PG, 0, &ppte);
    if (ppte != NULL) {
        struct Page *ppc, *ppp;
        ppp = pa2page(PTE_ADDR(*ppte));
        page_alloc(&ppc);
        bcopy(page2kva(ppp), page2kva(ppc), BY2PG);
        page_insert(e->env_pgdir, ppc, USTACKTOP - BY2PG, PTE_R | PTE_V);
    }

    // Child directly return to where his parent finish the call of sys_env_alloc
    e->env_tf.pc = e->env_tf.cp0_epc;
/*
    printf("e pc [%8x]\n", e->env_tf.pc);
    printf("e ra [%8x]\n", e->env_tf.regs[31]);
    printf("e sp [%8x]\n", e->env_tf.regs[29]);*/

    e->env_status = ENV_NOT_RUNNABLE;

    // Child process return a zero (no explicit return)
    e->env_tf.regs[2] = 0;

    // Return for parent process
	return e->env_id;
}

static int load_icode_mapper(u_long va, u_int32_t sgsize,
                             u_char *bin, u_int32_t bin_size, void *user_data) {
    struct Env *env = (struct Env *) user_data;
    struct Page *p = NULL;
    u_long i;
    int r;
    u_long offset = va - ROUNDDOWN(va, BY2PG);
    for (i = 0; i < bin_size; i += BY2PG) {
        r = page_alloc(&p);
        if (r < 0) {
            panic("Allocate page failed.");
        }
        p->pp_ref++;
        r = page_insert(env->env_pgdir, p, va - offset + i, PTE_V | PTE_R);
        if (r < 0) {
            panic("Insert page failed.");
        }
        bcopy(bin + i, (void *) page2kva(p) + offset, MIN(BY2PG, bin_size - i));
    }
    while (i < sgsize) {
        r = page_alloc(&p);
        if (r < 0) {
            panic("Allocate page failed.");
        }
        p->pp_ref++;

        r = page_insert(env->env_pgdir, p, va - offset + i, PTE_V | PTE_R);
        if (r < 0) {
            panic("Insert page failed.");
        }
        i += BY2PG;
    }
    return 0;
}

int sys_env_spawn(int sysno, u_int envid, char *binary, u_int size, u_int esp) {

    int r;
    struct Env *e;
    u_int entry_point;

    r = env_alloc(&e, curenv->env_id);
    if (r < 0) {
        return r;
    }

    r = load_elf(binary, size, &entry_point, e, load_icode_mapper);
    if (r < 0) {
        panic("Load elf failed.");
    }
    e->env_tf.pc = entry_point;
    e->env_tf.regs[29] = esp;

    printf("e pc [%8x]\n", e->env_tf.pc);
    printf("e ra [%8x]\n", e->env_tf.regs[31]);
    printf("e sp [%8x]\n", e->env_tf.regs[29]);

    e->env_status = ENV_NOT_RUNNABLE;
    //e->env_tf.regs[2] = 0;
    return e->env_id;
}

/* Overview:
 * 	Set envid's env_status to status.
 *
 * Pre-Condition:
 * 	status should be one of `ENV_RUNNABLE`, `ENV_NOT_RUNNABLE` and
 * `ENV_FREE`. Otherwise return -E_INVAL.
 * 
 * Post-Condition:
 * 	Returns 0 on success, < 0 on error.
 * 	Return -E_INVAL if status is not a valid status for an environment.
 * 	The status of environment will be set to `status` on success.
 */
int sys_set_env_status(int sysno, u_int envid, u_int status)
{
	struct Env *env;
	int ret;

    if (status != ENV_RUNNABLE && status != ENV_NOT_RUNNABLE && status != ENV_FREE) {
        return -E_INVAL;
    }

    ret = envid2env(envid, &env, 0);
    if(ret < 0)
        return ret;
    env->env_status = status;
    return 0;
}

/* Overview:
 * 	Set envid's trap frame to tf.
 *
 * Pre-Condition:
 * 	`tf` should be valid.
 *
 * Post-Condition:
 * 	Returns 0 on success, < 0 on error.
 * 	Return -E_INVAL if the environment cannot be manipulated.
 *
 * Note: This hasn't be used now?
 */
int sys_set_trapframe(int sysno, u_int envid, struct Trapframe *tf)
{

	return 0;
}

/* Overview:
 * 	Kernel panic with message `msg`. 
 *
 * Pre-Condition:
 * 	msg can't be NULL
 *
 * Post-Condition:
 * 	This function will make the whole system stop.
 */
void sys_panic(int sysno, char *msg)
{
	// no page_fault_mode -- we are trying to panic!
	panic("%s", TRUP(msg));
}

/* Overview:
 * 	This function enables caller to receive message from 
 * other process. To be more specific, it will flag 
 * the current process so that other process could send 
 * message to it.
 *
 * Pre-Condition:
 * 	`dstva` is valid (Note: NULL is also a valid value for `dstva`).
 * 
 * Post-Condition:
 * 	This syscall will set the current process's status to 
 * ENV_NOT_RUNNABLE, giving up cpu. 
 */
void sys_ipc_recv(int sysno, u_int dstva)
{
    //if (dstva > UTOP) {
    //    return;
    //}
    curenv->env_ipc_dstva = dstva;
    curenv->env_ipc_recving = 1;
    curenv->env_status = ENV_NOT_RUNNABLE;
    //printf("[IPC] yield @ [%08x]\n", curenv->env_tf.pc);
    sys_yield();
}

/* Overview:
 * 	Try to send 'value' to the target env 'envid'.
 *
 * 	The send fails with a return value of -E_IPC_NOT_RECV if the
 * target has not requested IPC with sys_ipc_recv.
 * 	Otherwise, the send succeeds, and the target's ipc fields are
 * updated as follows:
 *    env_ipc_recving is set to 0 to block future sends
 *    env_ipc_from is set to the sending envid
 *    env_ipc_value is set to the 'value' parameter
 * 	The target environment is marked runnable again.
 *
 * Post-Condition:
 * 	Return 0 on success, < 0 on error.
 *
 * Hint: the only function you need to call is envid2env.
 */
int sys_ipc_can_send(int sysno, u_int envid, u_int value, u_int srcva,
					 u_int perm)
{

	int r;
	struct Env *e;
	struct Page *p;

    if (srcva > UTOP) {
        return -1;
    }

    r = envid2env(envid, &e, 0);
    if (r < 0) {
        return -E_BAD_ENV;
    }

    if (!e->env_ipc_recving) {
        return -E_IPC_NOT_RECV;
    }

    if (srcva != 0 && e->env_ipc_dstva != 0) {
        //printf("[IPC] map [%08x] to [%08x]\n", srcva, e->env_ipc_dstva);
        p = page_lookup(curenv->env_pgdir, srcva, 0);
        page_insert(e->env_pgdir, p, e->env_ipc_dstva, perm);
        e->env_ipc_perm = perm;
    }

    e->env_ipc_recving = 0;
    e->env_ipc_from = curenv->env_id;
    e->env_ipc_value = value;
    e->env_status = ENV_RUNNABLE;

	return 0;
}

