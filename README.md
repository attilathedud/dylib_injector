# dylib_injector
![Injection screenshot](/promos/promo.gif?raw=true "Injecting into process")

### About
dylib_injector is an injector that allows you to load dynamic libraries into other processes' memory and execute them. This allows you to do things like directly modify memory and execute remote code within the memory and permissions of the process you inject into it.

Injections works via creating a remote thread inside the app that calls dlopen. The more detailed breakdown:

1. Use get_task_for_pid to get a task port for the application.
2. Allocate space and write in the full path of our dynamic library.
3. Allocate space for our thread's stack.
4. Allocate and write our thread's code into the application. The code:
```c
    _pthread_set_self();                    // so dlopen works
    dlopen( dylib_name, 2 );                // 2 is global
    thread_suspend( mach_thread_self() )    // to prevent crashes
```      

The opcodes for 86 and 64 bit are different so we need to code two different codecaves depending on the architecture.

5. Patch the code cave so it has the correct addresses of _pthread_set_self, the dylib_name address inside the address, dlopen, mach_thread_self, and thread_suspend.
6. Use thread_create_running to create our remote thread. Set the base pointer, stack pointer, and destination index registers to the stack address (setting edi is for _pthread_set_self). Set the instruction pointer to our code cave.
7. Detach our task port.

### Example
A target and an example dylib (in both 32 and 64 bit) are given for testing. 

They can be executed by starting `./target_app`, finding its pid with `ps`, and then running 
`sudo ./dylib_injector [pid] test.dylib`

### Building
Run `make` to build for both sets of architecture or make i386/x86_64 for a specific one. The injector
must be build with the same architecture as the target.
