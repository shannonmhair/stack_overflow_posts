
#include <liburing.h>
#include <iostream>
#include <assert.h>
#include <cstring>


constexpr unsigned QUEUE_DEPTH = 64;
constexpr unsigned IO_BLOCK_SIZE = 8192;
constexpr unsigned WRITE_SIZE = (IO_BLOCK_SIZE * 1024);
constexpr char* FILE_PATH =  "test.txt";
constexpr unsigned MAX_IOVEC_DEPTH = 512;


#define MEMORY_ALIGNED_BUFFER //Comment this line out to reproduce error.


// Verify app is run as root, exit if not.
void rootPrivelegeCheck()
{
    if (geteuid()) 
    {
        fprintf(stderr, "You need root privileges to run this program.\n");
        exit(1);
    }
}

/* Print error code and corresponding error message to console. 
Exit on error if specified.*/
void errorCheck(int status, const char* msg, bool exitOnError)
{
    if (status < 0)
    {
        std::cout << msg << std::strerror(-status) << std::endl;
        if (exitOnError) exit(status);
    }
}

// Populate a buffer with a repeating ascii alphabet.
void populateBuffer(char *buf, size_t len)
{
    for (int i = 0; i < len; i++)
    {
        if (i % 27 < 26) buf[i] = 'a' + (i % 26);
        if (i % 27 == 26) buf[i] = '\n';
    }
}

// Helper function to pack an iovec aray with data from buffer.
unsigned populateIovecArray(char* buffer, unsigned bytes, iovec* result)
{
    unsigned bytesAssigned = 0;
    unsigned bytesToAssign = 0;
    unsigned blocksAssigned = 0;
    unsigned bytesRemaining = bytes;

    if (bytes > IO_BLOCK_SIZE*MAX_IOVEC_DEPTH)
    {
        std::cout << "FATAL: too many bytes assigned to single iovec array" << std::endl;
        std::cout << bytes << " bytes requested, " << IO_BLOCK_SIZE*MAX_IOVEC_DEPTH << " max." << std::endl;
        exit(1);
    }

    while(bytesAssigned < bytes)
    {
        bytesToAssign = (bytesRemaining > IO_BLOCK_SIZE) ? IO_BLOCK_SIZE : bytesRemaining;
        result[blocksAssigned].iov_base = buffer + bytesAssigned;
        result[blocksAssigned].iov_len = bytesToAssign;

        bytesAssigned += bytesToAssign;
        bytesRemaining -= bytesToAssign;
        blocksAssigned++;
    }

    return blocksAssigned;
}

// Write contents of a buffer to file using io_uring
void writeToFile(int fd, io_uring* ring, char* buffer, int size, int fileOffset)
{
    io_uring_cqe *cqe;
    io_uring_sqe *sqe;
    iovec* iov_array;

    unsigned bytesRemaining = size;
    unsigned bytesToWrite = 0;
    unsigned bytesWritten = 0;
    unsigned writesPending = 0;
    unsigned currentVectorSize = 0;
    unsigned iov_array_len = 0;
    int status;

    while (bytesRemaining || writesPending)
    {
        // Write up to QUEUE_DEPTH blocks to the submission queue
        while(writesPending < QUEUE_DEPTH && bytesRemaining)
        {
            // Get SQE
            sqe = io_uring_get_sqe(ring);
            assert(sqe);

            // Determine how much data to write with next request
            bytesToWrite = (bytesRemaining > IO_BLOCK_SIZE * MAX_IOVEC_DEPTH) ? IO_BLOCK_SIZE * MAX_IOVEC_DEPTH : bytesRemaining;

            // Allocate and populate the iovec array (the iovec array is freed when handling completions)
            iov_array = new iovec[MAX_IOVEC_DEPTH];
            iov_array_len = populateIovecArray(buffer+bytesWritten, bytesToWrite, iov_array);

            // Add request to queue
            io_uring_prep_writev(sqe, 0, iov_array, iov_array_len, fileOffset + bytesWritten);
            io_uring_sqe_set_data(sqe, iov_array);
            sqe->flags |= IOSQE_FIXED_FILE;
            
            // Update counters
            writesPending++;
            bytesWritten += bytesToWrite;
            bytesRemaining -= bytesToWrite;
            
            // If no more data to write, break
            if (bytesRemaining == 0) break;
        }
        
        io_uring_submit(ring);

        // Handle Completed IO
        while(writesPending)
        {
            // Look to see if any requests have completed
            status = io_uring_peek_cqe(ring, &cqe);
            if (status == -EAGAIN) break;
            // errorCheck(status, "Bad CQE ", true);

            // If so, handle them
            iov_array = (iovec*) (cqe->user_data);
            delete[] iov_array;
            iov_array = nullptr;
            
            errorCheck(status, "Error peeking for completion: ", true);
            errorCheck(cqe->res, "Error in async operation: ", false);
            io_uring_cqe_seen(ring, cqe);

            writesPending--;
        }
    }
}


// Globals
char* simulatedNetworkBuffer = nullptr;


int main() 
{
    int status;
    int fds[2]; // registered file descriptors array
    
    // Make sure we have root privileges
    rootPrivelegeCheck();

    // Set up data to write, memory alligned
    #ifdef MEMORY_ALIGNED_BUFFER
        void* temp;
        posix_memalign(&temp, 4096, WRITE_SIZE);
        simulatedNetworkBuffer = (char*) temp;
    #endif

    #ifndef MEMORY_ALIGNED_BUFFER
        simulatedNetworkBuffer = new char[WRITE_SIZE];
    #endif

    populateBuffer(simulatedNetworkBuffer, WRITE_SIZE);

    // Open the file
    fds[0] = open(FILE_PATH, O_WRONLY | O_TRUNC | O_CREAT | O_DIRECT);


    // initialize IO Ring
    io_uring_params params;
    io_uring* ring = new io_uring;

    memset(&params, 0, sizeof(params));
    params.flags |= IORING_SETUP_SQPOLL | IORING_SETUP_IOPOLL;
    params.sq_thread_idle = 5000;

    status = io_uring_queue_init_params(QUEUE_DEPTH, ring, &params);
    errorCheck(status, "Error initializing io_uring: ", true);

    status = io_uring_register_files(ring, fds, 1);
    errorCheck(status, "Error registering files: ", true);

    // Write the data to file
    writeToFile(0, ring, simulatedNetworkBuffer, WRITE_SIZE, 0);
    
    
    // Cleanup
    io_uring_queue_exit(ring);
    delete ring;
    
    close(fds[0]);

    delete simulatedNetworkBuffer;

    return 0;
}
