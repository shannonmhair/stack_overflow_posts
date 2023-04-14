## Dependencies

- [liburing](https://github.com/axboe/liburing)

## How to use:

- To run without errors:
    - Compile (with inlcuded CMake or directly with your choice of compiler)
    - execute the program `sudo ./example`
    - notice `test.txt` appears in the same directory as the program and contains the alphabet 

- To reproduce the error
    - Comment out the definition of `MEMORY_ALIGNED_BUFFER` at the top of main.cpp
    - Follow the steps listed in `To run without errors`