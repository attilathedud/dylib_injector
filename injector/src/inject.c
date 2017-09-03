#include "../include/inject.h"

#include <mach/mach_vm.h>
#include <dlfcn.h>
#include <pthread.h>

// The stack will be used by any function calls we make in our code cave.
#define STACK_SIZE 0x1000

// Offsets for the placeholders in our code cave.
#define i386_pthread_offset 4
#define i386_dylib_offset 12
#define i386_dlopen_offset 28
#define i386_mach_thread_self_offset 35
#define i386_thread_suspend_offset 45

#define x86_64_pthread_offset 6
#define x86_64_dylib_offset 19
#define x86_64_dlopen_offset 39
#define x86_64_mach_thread_self_offset 51
#define x86_64_thread_suspend_offset 66

/*!
*   Given a pid, return the task mach_port for that process and fill in kern_return with the detailed
*   return code.
*/
mach_port_t get_task_for_pid( int pid, kern_return_t *kern_return )
{
    mach_port_t task = RETURN_INVALID_PID;

    if( pid <= 0 )
        return RETURN_INVALID_PID;

    // mach_task_self() is the calling task.
    *kern_return = task_for_pid( mach_task_self(), pid, &task );
    if( *kern_return != KERN_SUCCESS ) 
    {
        return RETURN_INVALID_PID;
    }

    return task;
}

/*!
*   Given a pid and a path to a dynamic library, inject the library inside the passed application.
*
*   Injections works via creating a remote thread inside the app that calls dlopen. The more detailed 
*   breakdown:
*       1. Use get_task_for_pid to get a task port for the application.
*       2. Allocate space and write in the full path of our dynamic library.
*       3. Allocate space for our thread's stack.
*       4. Allocate and write our thread's code into the application. The code:
*           _pthread_set_self();                    // so dlopen works
*           dlopen( dylib_name, 2 );                // 2 is global
*           thread_suspend( mach_thread_self() )    // to prevent crashes
*       
*       The opcodes for 86 and 64 bit are different so we need to code two different codecaves
*       depending on the architecture.
*
*       5. Patch the code cave so it has the correct addresses of _pthread_set_self, the dylib_name
*           address inside the address, dlopen, mach_thread_self, and thread_suspend.
*       6. Use thread_create_running to create our remote thread. Set the base pointer, stack pointer,
*           and destination index registers to the stack address (setting edi is for 
*           _pthread_set_self). Set the instruction pointer to our code cave.
*       7. Detach our task port.
*/
mem_return_t inject_dylib( int pid, char *dylib_name, allocated_memory_t *mem_locations, 
    kern_return_t *kern_return )
{
    if( pid <= 0 || dylib_name == NULL || mem_locations == NULL || kern_return == NULL )
        return RETURN_GERROR;

    // Get the task 
    mach_port_t task = get_task_for_pid( pid, kern_return );
    if( task == RETURN_INVALID_PID )
        return RETURN_INVALID_PID;

    // Allocate space in the task for our library's name
    *kern_return = mach_vm_allocate( task, &mem_locations->dylib_address, strlen( dylib_name ) + 1, 1 );
    if( *kern_return != KERN_SUCCESS ) 
        return RETURN_DYLIB_ALLOCATE_FAILED;

    // Write our library's name into the task.
    *kern_return = mach_vm_write( task, mem_locations->dylib_address, ( vm_offset_t )dylib_name, strlen( dylib_name ) + 1 );
    if( *kern_return != KERN_SUCCESS )
        return RETURN_DYLIB_WRITE_FAILED;

    // Allocate space for our stack
    *kern_return = mach_vm_allocate( task, &mem_locations->stack_address, STACK_SIZE, 1 );
    if( *kern_return != KERN_SUCCESS )
        return RETURN_STACK_ALLOCATE_FAILED;

    // Set the protection for our stack to allow writes
    mach_vm_protect( task, mem_locations->stack_address, STACK_SIZE, 0, VM_PROT_WRITE | VM_PROT_READ );

    // Create our code cave. Depending on the architecture, we need to have different opcodes.
    // 0x00000000's are used as placeholders for our dynamic addresses.
    // We need to save and restore the stack for _pthread_set_self due to it destroying all the info.
    #if defined (__i386__)
        unsigned char code[ 100 ] = 
            "\x55"                                          // push %ebp
            "\x89\xe5"                                      // mov %ebp, %esp
            "\xb8\x00\x00\x00\x00"                          // mov %eax, _pthread_set_self
            "\xff\xd0"                                      // call %eax
            "\x5d"                                          // pop %ebp
            "\xBf\x00\x00\x00\x00"                          // mov %edi, dylib_address
            "\x89\x3c\x24"                                  // mov dword ptr [ %esp ], %edi
            "\xc7\x44\x24\x04\x02\x00\x00\x00"              // mov dword ptr [ %esp+4 ], 2
            "\xb8\x00\x00\x00\x00"                          // mov %eax, dlopen
            "\xff\xd0"                                      // call %eax
            "\xb8\x00\x00\x00\x00"                          // mov %eax, mach_thread_self
            "\xff\xd0"                                      // call %eax
            "\x89\x04\x24"                                  // mov dword ptr [ %esp ], eax
            "\xb8\x00\x00\x00\x00"                          // mov %eax, thread_suspend
            "\xff\xd0"                                      // call %eax
        ;
    #elif defined(__x86_64__)
        unsigned char code[ 100 ] = 
            "\x55"                                          // push %rbp
            "\x48\x89\xe5"                                  // mov %rbp, %rsp
            "\x48\xb8\x00\x00\x00\x00\x00\x00\x00\x00"      // mov %rax, _pthread_set_self
            "\xff\xd0"                                      // call %rax
            "\x5d"                                          // pop %rbp
            "\x48\xbf\x00\x00\x00\x00\x00\x00\x00\x00"      // mov %rdi, dylib_address
            "\x48\xbe\x02\x00\x00\x00\x00\x00\x00\x00"      // mov %rsi, 2
            "\x48\xb8\x00\x00\x00\x00\x00\x00\x00\x00"      // mov %rax, dlopen
            "\xff\xd0"                                      // call %rax
            "\x48\xb8\x00\x00\x00\x00\x00\x00\x00\x00"      // mov %rax, mach_thread_self
            "\xff\xd0"                                      // call %rax
            "\x48\x89\xc7"                                  // mov %rdi, %rax
            "\x48\xb8\x00\x00\x00\x00\x00\x00\x00\x00"      // mov %rax, thread_suspend
            "\xff\xd0"                                      // call %rax
        ;
    #endif
    
    // Allocate space for our code
    *kern_return = mach_vm_allocate( task, &mem_locations->code_address, sizeof( code ), 1 );
    if( *kern_return != KERN_SUCCESS )
        return RETURN_CODE_ALLOCATE_FAILED;
    
    // Fill in the placeholders with our dynamic addresses
    // In order to not overwrite opcode, we need to declare different size variables for each
    // architecture.
    #if defined (__i386__)
        uint32_t _pthread_set_self_address = (uint32_t)dlsym ( RTLD_DEFAULT, "_pthread_set_self" );
        uint32_t mach_thread_self_address = (uint32_t)mach_thread_self;
        uint32_t thread_suspend_address = (uint32_t)thread_suspend;
        uint32_t dlopen_address = (uint32_t)dlopen;
    
        memcpy( &code[ i386_pthread_offset ], &_pthread_set_self_address, sizeof( uint32_t ) );
        memcpy( &code[ i386_dylib_offset ], &mem_locations->dylib_address, sizeof( uint32_t ) );
        memcpy( &code[ i386_dlopen_offset ], &dlopen_address, sizeof( uint32_t ) );
        memcpy( &code[ i386_mach_thread_self_offset ], &mach_thread_self_address, sizeof( uint32_t ) );
        memcpy( &code[ i386_thread_suspend_offset ], &thread_suspend_address, sizeof( uint32_t ) );
    #elif defined(__x86_64__)
        mach_vm_address_t _pthread_set_self_address = (mach_vm_address_t)dlsym ( RTLD_DEFAULT, "_pthread_set_self" );
        mach_vm_address_t mach_thread_self_address = (mach_vm_address_t)mach_thread_self;
        mach_vm_address_t thread_suspend_address = (mach_vm_address_t)thread_suspend;
        mach_vm_address_t dlopen_address = (mach_vm_address_t)dlopen;
    
        memcpy( &code[ x86_64_pthread_offset ], &_pthread_set_self_address, sizeof( mach_vm_address_t ) );
        memcpy( &code[ x86_64_dylib_offset ], &mem_locations->dylib_address, sizeof( mach_vm_address_t ) );
        memcpy( &code[ x86_64_dlopen_offset ], &dlopen_address, sizeof( mach_vm_address_t ) );
        memcpy( &code[ x86_64_mach_thread_self_offset ], &mach_thread_self_address, sizeof( mach_vm_address_t ) );
        memcpy( &code[ x86_64_thread_suspend_offset ], &thread_suspend_address, sizeof( mach_vm_address_t ) );
    #endif
    
    // Write our codecave into the task
    *kern_return = mach_vm_write( task, mem_locations->code_address, ( vm_offset_t )code, sizeof( code ) );
    if( *kern_return != KERN_SUCCESS )
        return RETURN_CODE_WRITE_FAILED;

    // Set the protect level to execute for our code
    mach_vm_protect( task, mem_locations->code_address, sizeof( code ), 0, VM_PROT_EXECUTE | VM_PROT_READ );
    
    // Create our thread, set the registers, and start the thread
    thread_t thread = { 0 };

    #if defined (__i386__)
        i386_thread_state_t thread_state = { 0 };
    
        thread_state.__eip = mem_locations->code_address;
        thread_state.__edi = mem_locations->stack_address;
        thread_state.__esp = mem_locations->stack_address;
        thread_state.__ebp = mem_locations->stack_address;
    
        *kern_return = thread_create_running( task, i386_THREAD_STATE, (thread_state_t)&thread_state, i386_THREAD_STATE_COUNT, &thread );
        if( *kern_return != KERN_SUCCESS )
            return RETURN_THREAD_CREATE_FAILED;
    #elif defined(__x86_64__)
        x86_thread_state64_t thread_state = { 0 };
    
        thread_state.__rip = mem_locations->code_address;
        thread_state.__rdi = mem_locations->stack_address;
        thread_state.__rsp = mem_locations->stack_address;
        thread_state.__rbp = mem_locations->stack_address;
    
        *kern_return = thread_create_running( task, x86_THREAD_STATE64, (thread_state_t)&thread_state, x86_THREAD_STATE64_COUNT, &thread );
        if( *kern_return != KERN_SUCCESS )
            return RETURN_THREAD_CREATE_FAILED;    
    #endif
    
    // Detach from the task
    mach_port_deallocate( mach_task_self(), task );

    return RETURN_SUCCESS;
}
