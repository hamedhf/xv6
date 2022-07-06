#include "types.h"
#include "stat.h"
#include "user.h"

int main(int argc, char *argv[]) {
    int pid = 0;

    pid = fork();
    if(pid < 0)
    {
        printf(1, "%d failed in fork!\n", getpid());
    }
    else if (pid > 0)
    {
        printf(1, "Parent %d creating child %d\n",getpid(), pid);
        int wtime, rtime;
        waitx(&wtime, &rtime);
        printf(1, "finished with wtime = %d, rtime = %d\n", wtime, rtime);
    }
    else
    {
        for(int z = 0, x = 0; z < 1000; z++)
            x = x + 3.14 * 2; //Useless calculation to consume CPU Time
    }

    exit();
}