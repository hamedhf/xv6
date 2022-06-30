/*
    an user program for testing proc_dump system call
*/

#include "types.h"
#include "stat.h"
#include "user.h"

int main(int argc, char const *argv[])
{
    /* code */
    // proc_dump()
    printf(1, "output of proc_dump = %d\n", proc_dump());
    exit();
}
