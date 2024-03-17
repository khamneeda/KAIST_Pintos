KAIST Pintos project for operating system course: Designing mini OS

Thread
1. Handing over CPU occupancy between threads by sleep and wake up threads
2. Add priority between threads
3. Use semaphore, lock, condition to synchronize threads

User program : System call
1. Restrict user program not to use kernel area
2. Implement system call : fork, exit, wait, create, open ...
3. Implement system call in linux

Virtual memory : Memory management
1. Manage virtual memory by linking page and frame
2. Use lazy-load
3. Grow stack
4. Manage memory mapped file
5. Implement swap in/out

The manual is available at https://casys-kaist.github.io/pintos-kaist/.


_____________________________________________________________________________

Design project

Build a Hanuri rocket OS that can manage randomly disturbed memory from time to time.
1. Ensure the design safe: Not using the disturbed memory address.
2. Prevent the memory loss
3. Improve the os performance
4. Present a persuasive and effective design

Please refer to the project guide
