#include <mach/mach.h>

typedef enum mem_return {
    RETURN_SUCCESS = 0, 
    RETURN_GERROR = -1, 
    RETURN_INVALID_PID = -2, 
    RETURN_DYLIB_ALLOCATE_FAILED = -3, 
    RETURN_DYLIB_WRITE_FAILED = -4, 
    RETURN_STACK_ALLOCATE_FAILED = -5,
    RETURN_CODE_ALLOCATE_FAILED = -6,
    RETURN_CODE_WRITE_FAILED = -7,
    RETURN_THREAD_CREATE_FAILED = -8
} mem_return_t;

typedef struct allocated_memory
{
    mach_vm_address_t dylib_address;
    mach_vm_address_t stack_address;
    mach_vm_address_t code_address;
} allocated_memory_t;

/*!
*   Given a pid and a path to a dynamic library, inject the library inside the passed application.
*
*   Full description inside inject.c.
*/
mem_return_t inject_dylib( int pid, char *dylib_name, allocated_memory_t *mem_locations, 
    kern_return_t *kern_return );
