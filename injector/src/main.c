/*!
*   dylib_injector is an injector that allows you to load dynamic libraries into other processes'
*   memory and execute them. This allows you to do things like directly modify memory and execute remote 
*   code within the memory and permissions of the process you inject into it.
*
*   Description of the technique can be found in inject.c.
*
*   A target and an example dylib (in both 32 and 64 bit) are given for testing. 
*   They can be executed by starting `./target_app`, finding its pid with `ps`, and then running 
*   `sudo ./dylib_injector [pid] test.dylib.
*
*   Run `make` to build for both sets of architecture or make i386/x86_64 for a specific one. The injector
*   must be build with the same architecture as the target.
*/

#include <stdio.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>

#include "../include/inject.h"

char *help_text = "usage:\nsudo ./dylib_injector [pid] [dylib_path]";

/*! 
*	Helper function to safety extract a numerical value from a passed character array.
*
*	Sets errno with the result of strtol.
*
*	Returns the value on success, -1 on failure.
*/
unsigned long get_long_value_from_optarg( char* optarg, int base )
{
	char *end_ptr = NULL;

	errno = 0;

	unsigned long temp_value = strtol( optarg, &end_ptr, base );
	if( end_ptr != optarg && errno != ERANGE && (temp_value >= LONG_MIN || temp_value <= LONG_MAX))
	{
		return temp_value;
	}

	return -1;
}

int main( int argc, char** argv )
{
    if( argc < 3 )
    {   
        printf( "%s\n", help_text );
        return RETURN_GERROR;
    }

    unsigned long pid = get_long_value_from_optarg( argv[ 1 ], 10 );
    char *dylib_name = argv[ 2 ];

    allocated_memory_t mem_locations = { 0 };
    kern_return_t kern_return = { 0 };

    mem_return_t injection_return_type;

    injection_return_type = inject_dylib( pid, dylib_name, &mem_locations, &kern_return );
    if( injection_return_type != RETURN_SUCCESS )
    {
        switch( injection_return_type )
        {
            case RETURN_GERROR:
                printf( "Bad parameters: " );
                break;
            case RETURN_INVALID_PID: 
                printf( "Invalid pid: " );
                break;
            case RETURN_DYLIB_ALLOCATE_FAILED: 
                printf( "Couldn't allocate space for dylib: " );
                break;
            case RETURN_DYLIB_WRITE_FAILED: 
                printf( "Couldn't write dylib path into memory: " );
                break;
            case RETURN_STACK_ALLOCATE_FAILED:
                printf( "Couldn't allocate space for the stack: " );
                break;
            case RETURN_CODE_ALLOCATE_FAILED:
                printf( "Couldn't allocate space for the code: " );
                break;
            case RETURN_CODE_WRITE_FAILED:
                printf( "Couldn't write code into memory: " );
                break; 
            case RETURN_THREAD_CREATE_FAILED:
                printf( "Couldn't create remote thread: " );
                break;
            default:
                break;
        }

        if( injection_return_type != RETURN_GERROR )
        {
            printf( "%s\n", mach_error_string( kern_return ) );
        }
    }
    else
    {
        printf( "Injection successful.\n" );
        printf( "dylib_address:\t\t 0x%llx\n", mem_locations.dylib_address );
        printf( "stack_address:\t\t 0x%llx\n", mem_locations.stack_address );
        printf( "code_address:\t\t 0x%llx\n", mem_locations.code_address );
    }

    return RETURN_SUCCESS;
}
