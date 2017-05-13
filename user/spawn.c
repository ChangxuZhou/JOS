#include "lib.h"
#include <mmu.h>
#include <env.h>
#include <kerelf.h>

#define TMPPAGE        (BY2PG)
#define TMPPAGETOP    (TMPPAGE+BY2PG)

int
init_stack(u_int child, char **argv, u_int *init_esp) {
    int argc, i, r, tot;
    char *strings;
    u_int *args;

    // Count the number of arguments (argc)
    // and the total amount of space needed for strings (tot)
    tot = 0;
    for (argc = 0; argv[argc]; argc++)
        tot += strlen(argv[argc]) + 1;

    // Make sure everything will fit in the initial stack page
    if (ROUND(tot, 4) + 4 * (argc + 3) > BY2PG)
        return -E_NO_MEM;

    // Determine where to place the strings and the args array
    strings = (char *) TMPPAGETOP - tot;
    args = (u_int *) (TMPPAGETOP - ROUND(tot, 4) - 4 * (argc + 1));

    if ((r = syscall_mem_alloc(0, TMPPAGE, PTE_V | PTE_R)) < 0)
        return r;
    // Replace this with your code to:
    //
    //	- copy the argument strings into the stack page at 'strings'
    char *ctemp, *argv_temp;
    u_int j;
    ctemp = strings;
    for (i = 0; i < argc; i++) {
        argv_temp = argv[i];
        for (j = 0; j < strlen(argv[i]); j++) {
            *ctemp = *argv_temp;
            ctemp++;
            argv_temp++;
        }
        *ctemp = 0;
        ctemp++;
    }
    //	- initialize args[0..argc-1] to be pointers to these strings
    //	  that will be valid addresses for the child environment
    //	  (for whom this page will be at USTACKTOP-BY2PG!).
    ctemp = (char *) (USTACKTOP - TMPPAGETOP + (u_int) strings);
    for (i = 0; i < argc; i++) {
        args[i] = (u_int) ctemp;
        ctemp += strlen(argv[i]) + 1;
    }
    //	- set args[argc] to 0 to null-terminate the args array.
    ctemp--;
    args[argc] = ctemp;
    //	- push two more words onto the child's stack below 'args',
    //	  containing the argc and argv parameters to be passed
    //	  to the child's umain() function.
    u_int *pargv_ptr;
    pargv_ptr = args - 1;
    *pargv_ptr = USTACKTOP - TMPPAGETOP + (u_int) args;
    pargv_ptr--;
    *pargv_ptr = argc;
    //
    //	- set *init_esp to the initial stack pointer for the child
    //
    *init_esp = USTACKTOP - TMPPAGETOP + (u_int) pargv_ptr;

    if ((r = syscall_mem_map(0, TMPPAGE, child, USTACKTOP - BY2PG, PTE_V | PTE_R)) < 0)
        goto error;
    if ((r = syscall_mem_unmap(0, TMPPAGE)) < 0)
        goto error;

    return 0;

    error:
    syscall_mem_unmap(0, TMPPAGE);
    return r;
}


int spawn(char *prog, char **argv) {
    int fd;
    int r;
    int size;
    u_int esp;

    writef("spawn:open %s my id %d\n", prog, ENVX(env->env_id));

    fd = open(prog, O_RDONLY);
    if (fd < 0) {
        writef("spawn:open %s:%d\n", prog, fd);
        return -1;
    }

    u_int envid = syscall_env_alloc();
    if (envid < 0) {
        writef("[ERR] spawn.c : spawn : syscall_env_alloc\n");
        return envid;
    }

    //syscall_mem_unmap(envid, USTACKTOP - BY2PG);

    r = init_stack(envid, argv, &esp);
    if (r < 0) {
        close(fd);
        writef("[ERR] spawn.c : spawn : init_stack\n");
        return r;
    }

    size = ((struct Filefd *) num2fd(fd))->f_file.f_size;

    char *bin = 0x70000000;
    int bpage = size / BY2PG + 1;
    writef("Got a %d Byte binary, need %d page!\n", size, bpage);
    int i;
    for (i = 0; i <= bpage; i++) {
        syscall_mem_alloc(0, bin + i * BY2PG, PTE_R);
        char buf[BY2PG];
        read(fd, buf, BY2PG);
        user_bcopy(buf, bin + i * BY2PG, BY2PG);
    }

    syscall_env_spawn(envid, bin, size, esp);

    for (i = 0; i <= bpage; i++) {
        syscall_mem_unmap(0, bin + i * BY2PG);
    }


    int pn;
    for (pn = 0; pn < (USTACKTOP / BY2PG) - 2; pn++) {
        if (((*vpd)[pn / PTE2PT]) != 0 && ((*vpt)[pn] & PTE_LIBRARY) != 0) {
            syscall_mem_map(0, pn * BY2PG, envid, pn * BY2PG, PTE_V | PTE_R | PTE_LIBRARY);
            writef("map page va [%08x]\n", pn * BY2PG);
        }
    }


    r = syscall_set_env_status(envid, ENV_RUNNABLE);
    if (r < 0) {
        writef("set child runnable is wrong\n");
        return r;
    }

    writef("Env spawn done!\n");
    return envid;
}

int
spawnl(char *prog, char *args, ...) {
    return spawn(prog, &args);
}