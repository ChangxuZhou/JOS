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

    Elf32_Ehdr ehdr;
    readn(fd, &ehdr, sizeof(Elf32_Ehdr));
    int ph_entry_count = ehdr.e_phnum;
    seek(fd, ehdr.e_phoff);

    while (ph_entry_count--) {
        Elf32_Phdr phdr;
        readn(fd, &phdr, ehdr.e_phentsize);
        if (phdr.p_type != PT_LOAD)
            continue;
        u_int i;
        for (i = 0; i < phdr.p_filesz; i += BY2PG) {
            u_int va;
            read_map(fd, phdr.p_offset + i, &va);
            if (phdr.p_filesz - i >= BY2PG) {
                syscall_mem_map(0, va, envid, phdr.p_vaddr + i, PTE_V | PTE_R);
            } else {
                seek(fd, phdr.p_offset + i);
                syscall_mem_alloc(0, TMPPAGE, PTE_V | PTE_R);
                readn(fd, TMPPAGE, phdr.p_filesz - i);
                syscall_mem_map(0, TMPPAGE, envid, phdr.p_vaddr + i, PTE_V | PTE_R);
                syscall_mem_unmap(0, TMPPAGE);
            }
        }
        while (i < phdr.p_memsz) {
            syscall_mem_alloc(envid, phdr.p_vaddr + i, PTE_V | PTE_R);
            i += BY2PG;
        }
    }

    r = init_stack(envid, argv, &esp);
    if (r < 0) {
        close(fd);
        writef("[ERR] spawn.c : spawn : init_stack\n");
        return r;
    }

    struct Trapframe *tf = &(envs[ENVX(envid)].env_tf);
    tf->regs[29] = esp;
    tf->pc = UTEXT;

    int pn;
    for (pn = 0; pn < (USTACKTOP / BY2PG) - 2; pn++) {
        if (((*vpd)[pn / PTE2PT]) != 0 && ((*vpt)[pn] & PTE_LIBRARY) != 0) {
            syscall_mem_map(0, pn * BY2PG, envid, pn * BY2PG, PTE_V | PTE_R | PTE_LIBRARY);
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