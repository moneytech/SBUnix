#include <sys/kmalloc.h>
#include <sys/proc_mngr.h>
#include <stdio.h>

// The process lists. The task at the head of the READY_LIST should always be executed next.
task_struct* READY_LIST = NULL;
task_struct* CURRENT_TASK = NULL;
task_struct* prev = NULL;
task_struct* next = NULL;

// Whether scheduling has been initiated
uint8_t IsInitScheduler;

void read_ip()
{
}

void add_to_ready_list(task_struct* new_task)
{
    task_struct* ready_list_ptr = READY_LIST;
    // kprintf("\nThe READY_LIST: %p", READY_LIST);
    if (ready_list_ptr == NULL) {
        READY_LIST = new_task;
    } else {
        // kprintf("\nThe READY_LIST->next= %p", ready_list_ptr->next);
        while(ready_list_ptr->next != NULL) {
            ready_list_ptr = ready_list_ptr->next;
        }
        // kprintf("\nGoing in to add with ready_list_ptr=%p", ready_list_ptr);
        ready_list_ptr->next = new_task;
        new_task->next = NULL;
    }
    // kprintf("\nLeaving add_to_ready_list with READY_LIST=%p", READY_LIST);
}

#if PREMPTIVE_OS

// Push all Registers into Stack
#define PUSHA \
    __asm__ __volatile__(\
        "pushq %rax;"\
        "pushq %rbx;"\
        "pushq %rcx;"\
        "pushq %rdx;")

// Pop all Registers from Stack
#define POPA \
    __asm__ __volatile__(\
        "popq %rdx;"\
        "popq %rcx;"\
        "popq %rbx;"\
        "popq %rax;")

void timer_handler()
{
    uint64_t cur_rsp = NULL;

    cli;
    PUSHA;
    __asm__ __volatile__("movq %%rsp, %[cur_rsp]": [cur_rsp] "=r"(cur_rsp));

    if (READY_LIST != NULL) {
        if (!IsInitScheduler) {
            // The first time schedule gets invoked - additional processing needs done
            prev = READY_LIST;
            READY_LIST = READY_LIST->next;
            CURRENT_TASK = prev;
            IsInitScheduler = TRUE;

#if DEBUG_SCHEDULING
            kprintf("\nScheduler Initiated with PID: %d", prev->proc_id);
#endif

            LOAD_CR3(prev->mm->pml4_t);

            // set_tss_rsp0(prev->rsp_register);
            
            // Need to switch the kernel stack to that of the first process
            // IRETQ pops in the order of rip, cs, rflags, rsp and ss
            __asm__ __volatile__("movq %[prev_rsp], %%rsp" : : [prev_rsp] "m" (prev->rsp_register));

            __asm__ __volatile__("mov $0x20, %al;" "out %al, $0x20");
            POPA;
            sti;
            __asm__ __volatile__("iretq");

        } else {
            prev = CURRENT_TASK;
            next = READY_LIST;

            prev->rsp_register = cur_rsp;

            LOAD_CR3(next->mm->pml4_t);

            // set_tss_rsp0(next->rsp_register);
            __asm__ __volatile__("movq %[next_rsp], %%rsp" : : [next_rsp] "m" (next->rsp_register));

            CURRENT_TASK = READY_LIST;
            // Add prev to the end of the READY_LIST
            add_to_ready_list(prev);

            READY_LIST = READY_LIST->next;

#if DEBUG_SCHEDULING
            //kprintf("\nPID:%d", next->proc_id);
#endif
            __asm__ __volatile__("mov $0x20, %al;" "out %al, $0x20");
            POPA;
            sti;
            __asm__ __volatile__("iretq");
        }
    } else {
        // READY_LIST is empty
#if DEBUG_SCHEDULING
        //__asm__ __volatile__("popq %ax");
        //kprintf("\nList Empty");
#endif
        __asm__ __volatile__("mov $0x20, %al;" "out %al, $0x20");
        POPA;
        sti;
        __asm__ __volatile__("iretq");
    }
}

#else

void schedule()
{
    if (IsInitScheduler == FALSE) {
        if (READY_LIST == NULL) {
            kprintf("\nReady List Empty");
            return;
        }
        prev = READY_LIST;
        READY_LIST = READY_LIST->next;
        CURRENT_TASK = prev;
    } else {
        prev = CURRENT_TASK;
        CURRENT_TASK = READY_LIST;
        next = READY_LIST;
        // Add prev to the end of the READY_LIST
        // kprintf("\nGoing in to add");
        add_to_ready_list(prev);
        // kprintf("\nThe task to be scheduled next is %p", READY_LIST);
        READY_LIST = READY_LIST->next;
        // kprintf("\nThe task to be scheduled after that is %p", READY_LIST);

        LOAD_CR3(next->mm->pml4_t);
        switch_to(prev, next);
    }
}

void init_schedule()
{
    IsInitScheduler = FALSE;
    schedule();

    // Need to switch the kernel stack to that of the first process
    __asm__ __volatile("movq %[prev_sp], %%rsp"::[prev_sp] "m" (prev->rsp_register));
    __asm__ __volatile("movq %[prev_bp], %%rbp"::[prev_bp] "m" (prev->rsp_register));

    IsInitScheduler = TRUE;
}

#endif

void schedule_process(task_struct* new_task, uint64_t func_addr)
{
#if PREMPTIVE_OS
    // Set up kernel stack => ss, rsp, rflags, cs, rip
    int ss_ind     = KERNEL_STACK_SIZE-1;
    int rsp_ind    = KERNEL_STACK_SIZE-2;
    int rflags_ind = KERNEL_STACK_SIZE-3;
    int cs_ind     = KERNEL_STACK_SIZE-4;
    int rip_ind    = KERNEL_STACK_SIZE-5;

    new_task->kernel_stack[ss_ind]     = 0x23;
    new_task->kernel_stack[rsp_ind]    = (uint64_t)&new_task->kernel_stack[ss_ind];
    new_task->kernel_stack[rflags_ind] = 0x200202UL;
    new_task->kernel_stack[cs_ind]     = 0x1B;
    new_task->kernel_stack[rip_ind]    = func_addr;
    new_task->rsp_register = (uint64_t)&new_task->kernel_stack[rip_ind-4]; // For AX/BX/CX/DX
#else
    new_task->rsp_register = (uint64_t)&new_task->kernel_stack[KERNEL_STACK_SIZE-1];
#endif

    new_task->rip_register = func_addr;
    new_task->next = NULL;
    new_task->last = NULL;

#if DEBUG_SCHEDULING
    kprintf("\tEntry Point:%p", func_addr);
#endif

    // Add to the ready list 
    add_to_ready_list(new_task);
}

