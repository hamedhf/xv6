#include "types.h"
#include "x86.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"

int
sys_fork(void)
{
  return fork();
}

int
sys_exit(void)
{
  exit();
  return 0;  // not reached
}

int
sys_wait(void)
{
  return wait();
}

int
sys_kill(void)
{
  int pid;

  if(argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

int
sys_getpid(void)
{
  return myproc()->pid;
}

int
sys_sbrk(void)
{
  int addr;
  int n;

  if(argint(0, &n) < 0)
    return -1;
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

int
sys_sleep(void)
{
  int n;
  uint ticks0;

  if(argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(myproc()->killed){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

// return how many clock tick interrupts have occurred
// since start.
int
sys_uptime(void)
{ 
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

//return the year of which
//Unix version 6 was released

int 
sys_getyear(void) 
{
  return 1975;
}

// proc_dump system call definition
void
sys_proc_dump()
{
  // extract arguments and send them to proc_dump
  proc_info *ptr_proc_infos;
  argptr(0, (char **)&ptr_proc_infos, sizeof(ptr_proc_infos));

  int n;
  argint(1, &n);

  if(n <= 0)
  {
    return cprintf("proc_dump system call only accepts positive arg!\n");
  }
  else if (n > NPROC)
  {
    return cprintf("in proc_dump system call, n must be less than or equal to %d\n", NPROC);
  }

  // call corresponding function
  return kproc_dump(ptr_proc_infos, n);
}

void
sys_cps(void)
{
  return kcps();
}

int
sys_chpr(void)
{
  int pid, priority;
  if(argint(0, &pid) < 0)
    return -1;
  if(argint(1, &priority) < 0)
    return -1;

  return kchpr(pid, priority);
}