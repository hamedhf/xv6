#include "types.h"
#include "stat.h"
#include "user.h"

int main(int argc, char *argv[])
{
    int priority, pid;
    if(argc != 3){
        printf(2, "Usage: nice pid priority\n");
        exit();
    }
    pid = atoi(argv[1]);
    priority = atoi(argv[2]);
    chpr(pid, priority);
    exit();
}