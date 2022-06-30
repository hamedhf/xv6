/*
    an user program for testing proc_dump system call
*/

#include "types.h"
#include "stat.h"
#include "user.h"

int main(int argc, char const *argv[])
{
    printf(1, "user program for testing proc_dump with pid = %d\n", getpid());

    if(argc != 2)
    {
        printf(2, "proc_dump only accepts one argument!\n");
        exit();
    }

    int n = atoi(argv[1]);
    proc_info proc_infos[n];
    for (int i = 0; i < n; i++)
    {
        proc_infos[i].pid = 0;
        proc_infos[i].memsize = 0;
    }
    int cpids[2];

    if(!(cpids[0] = fork()))
    {
        //child 1
        while(1);
        exit();
    }

    if(!(cpids[1] = fork()))
    {
        //child 2
        while(1);
        exit();
    }

    printf(1, "child 1 with pid %d and child 2 with pid %d\n", cpids[0], cpids[1]);

    malloc(2000);
    proc_dump(proc_infos, n);

    printf(1, "output of proc_dump(sorted by memsize):\n");
    for (int i = 0; i < n; i++)
    {
        printf(1, "p[%d].pid = %d, p[%d].memsize = %d\n", i, proc_infos[i].pid, i, proc_infos[i].memsize);
    }

    exit();
}
