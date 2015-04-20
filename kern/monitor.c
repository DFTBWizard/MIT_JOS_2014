// Simple command-line kernel monitor useful for
// controlling the kernel and exploring the system interactively.

#include <inc/stdio.h>
#include <inc/string.h>
#include <inc/memlayout.h>
#include <inc/assert.h>
#include <inc/x86.h>

#include <kern/console.h>
#include <kern/monitor.h>
#include <kern/kdebug.h>

#define CMDBUF_SIZE	80	// enough for one VGA text line


struct Command {
	const char *name;
	const char *desc;
	// return -1 to force monitor to exit
	int (*func)(int argc, char** argv, struct Trapframe* tf);
};

static struct Command commands[] = {
	{ "help", "Display this list of commands", mon_help },
	{ "kerninfo", "Display information about the kernel", mon_kerninfo },
    { "alloc_page", "Display the address of allocated page", mon_alloc_page},
    { "page_status", "Display the status of the page", mon_page_status},
    { "free_page", "Free the page, successfully or not", mon_free_page},
    { "backtrace", "Backtrace the function call", mon_backtrace},
};
#define NCOMMANDS (sizeof(commands)/sizeof(commands[0]))

/***** Implementations of basic kernel monitor commands *****/

int
mon_help(int argc, char **argv, struct Trapframe *tf)
{
	int i;

	for (i = 0; i < NCOMMANDS; i++)
		cprintf("%s - %s\n", commands[i].name, commands[i].desc);
	return 0;
}

int
mon_kerninfo(int argc, char **argv, struct Trapframe *tf)
{
	extern char _start[], entry[], etext[], edata[], end[];

	cprintf("Special kernel symbols:\n");
	cprintf("  _start                  %08x (phys)\n", _start);
	cprintf("  entry  %08x (virt)  %08x (phys)\n", entry, entry - KERNBASE);
	cprintf("  etext  %08x (virt)  %08x (phys)\n", etext, etext - KERNBASE);
	cprintf("  edata  %08x (virt)  %08x (phys)\n", edata, edata - KERNBASE);
	cprintf("  end    %08x (virt)  %08x (phys)\n", end, end - KERNBASE);
	cprintf("Kernel executable memory footprint: %dKB\n",
		ROUNDUP(end - entry, 1024) / 1024);
	return 0;
}


int mon_alloc_page(int argc, char **argv, struct Trapframe *tf)
{
    struct PageInfo *pp;
    if(page_alloc(&pp) == 0)
    {
        cprintf("    0x%x\n", page2pa(pp));
        pp->pp_ref++;
    }
    else
    {
        cprintf("    Page allocation failed\n");
    }

    return 0;
}

int mon_page_status(int argc, char **argv, struct Trapframe *tf)
{
    if( argc != 2)
    {
        cprintf("Usage: page status [ADDR]\n");
        cprintf("    Address must be aligned in 4KB\n");
        return 0;
    }

    uint32_t pa = strtol(argv[1], 0, 0);

    struct PageInfo *pp = pa2page(pa);

    if(pp->pp_ref > 0)
    {
        cprintf("    Allocated\n");
    }
    else
    {
        cprintf("    free\n");
    }

    return 0;
}

int mon_free_page(int argc, char **argv, struct Trapframe *tf)
{
    if(argc != 2)
    {
        cprintf("Usage: free page [ADDR]\n");
        cprintf("   Address must be aligned in 4KB\n");
        cprintf("    Please make sure that the page is currently mounted 1 time\n");

        return 0;
    }

    uint32_t pa = strtol(argv[1], 0, 0);
    struct PageInfo *pp = pa2page(pa);

    if(pp->pp_ref == 1)
    {
        page_decref(pp);
        cprintf("    Page freed successfully!\n");
    }
    else
    {
        cprintf("   failed\n");
    }

    return 0;
}

unsigned int read_eip()
{
    unsigned int callerpc;
    __asm __volatile("movl 4(%%ebp), %0" : "=r" (callerpc));
    return callerpc;
}

#define J_NEXT_EBP(ebp) (*(unsigned int*)ebp)
#define J_ARG_N(ebp, n) (*(unsigned int*)(ebp + n))

extern unsigned int bootstacktop;
static struct Eipdebuginfo info = {0};
static inline unsigned int*
dump_stack(unsigned int* p)
{
    unsigned int i = 0;

    cprintf("ebp %08x eip %08x args", p, J_ARG_N(p, 1));
    
    for (i = 2; i < 7;i++)
    {
        cprintf(" %08x \n", J_ARG_N(p, i));
    }
    
    return (unsigned int*)J_NEXT_EBP(p);
}

static inline unsigned int*
dump_backstrace_symbols(unsigned int *p)
{

    cprintf("%s %d\n",info.eip_fn_name, info.eip_line);

    debuginfo_eip((uintptr_t)*(p+1), &info);

    return (unsigned int*)J_NEXT_EBP(p);
}


int
mon_backtrace(int argc, char **argv, struct Trapframe *tf)
{
	// Your code here.
    unsigned int *p  = (unsigned int*) read_ebp();
    unsigned int eip = read_eip();

    cprintf("current eip=%08x", eip);
    debuginfo_eip((uintptr_t) eip, &info);
    cprintf("\n");
    do
    {
        p = dump_stack(p);
    }while(p);

    cprintf("\n");
    p = (unsigned int*)read_ebp();
    do
    {
        p = dump_backstrace_symbols(p);
    }while(p);

	return 0;
}



/***** Kernel monitor command interpreter *****/

#define WHITESPACE "\t\r\n "
#define MAXARGS 16

static int
runcmd(char *buf, struct Trapframe *tf)
{
	int argc;
	char *argv[MAXARGS];
	int i;

	// Parse the command buffer into whitespace-separated arguments
	argc = 0;
	argv[argc] = 0;
	while (1) {
		// gobble whitespace
		while (*buf && strchr(WHITESPACE, *buf))
			*buf++ = 0;
		if (*buf == 0)
			break;

		// save and scan past next arg
		if (argc == MAXARGS-1) {
			cprintf("Too many arguments (max %d)\n", MAXARGS);
			return 0;
		}
		argv[argc++] = buf;
		while (*buf && !strchr(WHITESPACE, *buf))
			buf++;
	}
	argv[argc] = 0;

	// Lookup and invoke the command
	if (argc == 0)
		return 0;
	for (i = 0; i < NCOMMANDS; i++) {
		if (strcmp(argv[0], commands[i].name) == 0)
			return commands[i].func(argc, argv, tf);
	}
	cprintf("Unknown command '%s'\n", argv[0]);
	return 0;
}

void
monitor(struct Trapframe *tf)
{
	char *buf;

	cprintf("Welcome to %Cc the JOS kernel monitor!\n", COLOR_GRN, 'H');
	cprintf("Type 'help' for a list of commands.\n");


	while (1) {
		buf = readline("K> ");
		if (buf != NULL)
			if (runcmd(buf, tf) < 0)
				break;
	}
}
