#include "sys_calls.h"

static uint32_t process_count = 0;

// System calls for checkpoint 3.
int32_t halt (uint8_t status){
    return SYSCALL_SUCCESS;
}

int32_t execute (const uint8_t* command){
    /* Parsing */
    // initialize filename buffer
    uint8_t filename[ECE391FS_MAX_FILENAME_LEN];
    memset(filename, 0, ECE391FS_MAX_FILENAME_LEN);
    // get filename
    _execute_parse(command, filename);
    /* Executable check */
    // check if the file exist
    int32_t fd;
    if(ECE391FS_CALL_FAIL == file_open(&fd, (char*)filename)) return SYSCALL_FAIL;
    if (SYSCALL_SUCCESS != _execute_exe_check(&fd))
    {
        return SYSCALL_FAIL;
    }
    /* Pgaing */
    _execute_paging();
    /* User-level program loader */
    if (SYSCALL_SUCCESS != _execute_pgm_loader(&fd)) return SYSCALL_FAIL;

    /* Create PCB */

    /* Context Switch */

    return SYSCALL_SUCCESS;
}

int32_t read (int32_t fd, void* buf, int32_t nbytes){

    return SYSCALL_SUCCESS;
}

int32_t write (int32_t fd, const void* buf, int32_t nbytes){

    return SYSCALL_SUCCESS;
}

int32_t open (const uint8_t* filename){

    return SYSCALL_SUCCESS;
}

int32_t close (int32_t fd){

    return SYSCALL_SUCCESS;
}


// System calls for checkpoint 4.
int32_t getargs (uint8_t* buf, int32_t nbytes){

    return SYSCALL_SUCCESS;
}

int32_t vidmap (uint8_t** screen_start){

    return SYSCALL_SUCCESS;
}


//Extra credit system calls.
int32_t set_handler (int32_t signum, void* handler_address){

    return SYSCALL_SUCCESS;
}

int32_t sigreturn (void){

    return SYSCALL_SUCCESS;
}


/* Helper functions for sytstem calls */
/*
 * _execute_parse(const uint8_t* command, uint8_t* filename)
 *    @description: helper function called by sytstem call execute()
 *                  to parse the commands
 *    @input: command -  a space-separated sequence of words.
 *                       The first word is the file name of the program to be
 *                       executed, and the rest of the command—stripped of
 *                       leading spaces—should be provided to the new
 *                       program on request via the getargs system call
 *            filename - empry buffer used to hold the filename as a return
 *                       value. Its size should be no smaller than ECE391FS_MAX_FILENAME_LEN
 *    @output: filename - as described above
 *    @note: need to add support for arguments parsing
 */
void _execute_parse (const uint8_t* command, uint8_t* filename)
{
    // loop variable
    int index;
    // parsing
    for (index = 0; index < ECE391FS_MAX_FILENAME_LEN; index++)
    {
        // end of filename
        if (command[index] == SPACE) break;
        // read filename to the filename buffer
        else filename[index] = command[index];
    }
    return;
}

/*
 * int32_t _execute_exe_check (int32_t* fd)
 *    @description: helper function called by sytstem call execute()
 *                  to check if the file is executable or not
 *    @input: fd - the input file descriptor
 *    @output: SYSCALL_FAIL if fail, and SYSCALL_SUCCESS if succeed
 */
int32_t _execute_exe_check (int32_t* fd)
{
    // read the header of the file
	char buf[FILE_HEADER_LEN];
    memset(buf, 0, FILE_HEADER_LEN);
    uint32_t offset = 0;
	if(ECE391FS_CALL_FAIL == file_read(fd, &offset, buf, FILE_HEADER_LEN)) return SYSCALL_FAIL;
    if(offset != FILE_HEADER_LEN) return SYSCALL_FAIL;
    // check if the file is executable
    if ((buf[0] != FILE_EXE_HEADER_0) |
        (buf[1] != FILE_EXE_HEADER_1) |
        (buf[2] != FILE_EXE_HEADER_2) |
        (buf[3] != FILE_EXE_HEADER_3))
        {
            return SYSCALL_FAIL;
        }
    return SYSCALL_SUCCESS;
}

/*
 * void _execute_paging (const uint8_t* filename)
 *    @description: helper function called by sytstem call execute()
 *                  to set up a single 4 MB page directory that maps
 *                  virtual address 0x08000000 (128 MB) to the right physical
 *                  memory address (either 8 MB or 12 MB)
 *    @input: none
 *    @output: none
 *    @side effect: flush the TLB
 */
void _execute_paging ()
{
    // get the entry in page directory for the input process
    uint32_t PD_index = (uint32_t) USER_PROCESS_ADDR >> PD_ADDR_OFFSET;
    // present
    page_directory[PD_index].pde_MB.present = 1;
    // read only
    page_directory[PD_index].pde_MB.read_write = 0;
    // user level
    page_directory[PD_index].pde_MB.user_supervisor = 1;
    page_directory[PD_index].pde_MB.write_through = 0;
    page_directory[PD_index].pde_MB.cache_disabled = 0;
    page_directory[PD_index].pde_MB.accessed = 0;
    page_directory[PD_index].pde_MB.dirty = 0;
    // 4MB page
    page_directory[PD_index].pde_MB.page_size = 1;
    page_directory[PD_index].pde_MB.global = 0;
    page_directory[PD_index].pde_MB.avail = 0;
    page_directory[PD_index].pde_MB.pat = 0;
    page_directory[PD_index].pde_MB.reserved = 0;
    // physical address = PROCESS_PYSC_BASE_ADDR + process_count
    page_directory[PD_index].pde_MB.PB_addr = PROCESS_PYSC_BASE_ADDR + process_count;
    // increment the process_count
    process_count++;
    // flush the TLB by writing to the page directory base register (CR3)
    // reference: https://wiki.osdev.org/TLB
    asm volatile ("              \n\
  	        movl %%cr3, %%eax    \n\
  	        movl %%eax, %%cr3    \n\
            "
            : : : "eax", "cc"
    );
    return;
}

/*
 * int32_t _execute_pgm_loader (int32_t* fd)
 *    @description: helper function called by sytstem call execute()
 *                  to copy the program image to the user process memory
 *    @input: fd - the input file descriptor
 *    @output: SYSCALL_FAIL if fail, and SYSCALL_SUCCESS if succeed
 *    @side effect: modify the user memory
 */
int32_t _execute_pgm_loader (int32_t* fd)
{
    // loop variable
    int32_t index;
    // read the content of the file
	char buf[ECE391FS_MAX_FILENAME_LEN];
    memset(buf, 0, ECE391FS_MAX_FILENAME_LEN);
    uint32_t offset = 0;
	if(ECE391FS_CALL_FAIL == file_read(fd, &offset, buf, ECE391FS_MAX_FILENAME_LEN))
        return SYSCALL_FAIL;
    // get the user memory address (logical)
    char* user_mem = (char *)USER_PROCESS_ADDR;
    // copy the program file data into the memory for that program
    for (index = 0; index < ECE391FS_MAX_FILENAME_LEN; index++)
    {
        if (buf[index] == 0) break;
        else *(uint8_t *)(user_mem + index) = buf[index];
    }
    return SYSCALL_SUCCESS;
}

void _execute_context_switch (int32_t* fd)
{
    // read the header of the file
	char buf[FILE_HEADER_LEN];
    memset(buf, 0, FILE_HEADER_LEN);
    uint32_t offset = 0;
	file_read(fd, &offset, buf, FILE_HEADER_LEN);
    // get the entry point from bytes 24-27 of the executable
    uint32_t entry_point = (buf[24] << 24) | (buf[25] << 16) | (buf[26] << 8) | buf[27];
    uint32_t user_stack = USER_PROCESS_ADDR + USER_PAGE_SIZE;
    // IRET pops the return instruction pointer, return code segment selector,
    // and EFLAGS image from the stack to the EIP, CS,
    // and EFLAGS registers, respectively, and then resumes execution of the
    // interrupted program or procedure.
    // reference: http://faydoc.tripod.com/cpu/iret.htm
    // set up correct values for user-level CS, SS, EIP, ESP, DS, EFLAGS

    // not ready now
    /*
    asm volatile ("              \n\
            movl %%edx, %%esp    \n\
            movw $USER_DS, %%ds  \n\
            movw $USER_DS, %%ss  \n\
            pushfl               \n\
            pushl $USER_CS       \n\
            pushl %%eax          \n\
            iret                 \n\
            "
            :
            : "a"(entry_point), "D"(user_stack)
            : "cc"
    );
    */
    // modify TSS

    return;
}
