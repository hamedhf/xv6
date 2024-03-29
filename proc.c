#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"
#include "scheduler.h"

struct {
  struct spinlock lock;
  struct proc proc[NPROC];
  int priorityChanged[NCPU];      // 0 means false, 1 means ture. we should inform every cpu that priorities have changed.
  int queue[3];
} ptable;

static struct proc *initproc;

int nextpid = 1;
extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);

void
pinit(void)
{
  initlock(&ptable.lock, "ptable");
  // ncpu has a correct value when we enter the scheduler(for each cpu)
  acquire(&ptable.lock);
  for(int i = 0; i < NCPU; i++)
  {
    ptable.priorityChanged[i] = 0;
  }
  for (int i = 0; i < 3; i++)
  {
    ptable.queue[i] = 0;
  }
  release(&ptable.lock);
}

// Must be called with interrupts disabled
int
cpuid() {
  return mycpu()-cpus;
}

// Must be called with interrupts disabled to avoid the caller being
// rescheduled between reading lapicid and running through the loop.
struct cpu*
mycpu(void)
{
  int apicid, i;
  
  if(readeflags()&FL_IF)
    panic("mycpu called with interrupts enabled\n");
  
  apicid = lapicid();
  // APIC IDs are not guaranteed to be contiguous. Maybe we should have
  // a reverse map, or reserve a register to store &cpus[i].
  for (i = 0; i < ncpu; ++i) {
    if (cpus[i].apicid == apicid)
      return &cpus[i];
  }
  panic("unknown apicid\n");
}

// Disable interrupts so that we are not rescheduled
// while reading proc from the cpu structure
struct proc*
myproc(void) {
  struct cpu *c;
  struct proc *p;
  pushcli();
  c = mycpu();
  p = c->proc;
  popcli();
  return p;
}

//PAGEBREAK: 32
// Look in the process table for an UNUSED proc.
// If found, change state to EMBRYO and initialize
// state required to run in the kernel.
// Otherwise return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;
  char *sp;

  acquire(&ptable.lock);

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == UNUSED)
      goto found;

  release(&ptable.lock);
  return 0;

found:
  p->state = EMBRYO;
  p->pid = nextpid++;

  p->stime = ticks;       // set start time
  p->etime = 0;           // set end time to 0, means it’s not valid
  p->rtime = 0;           // set run time to 0
  p->iotime = 0;          // set i/o time to 0

  switch (SCHEDULER)
  {
    case MAIN_SCHEDULER:
      p->priority = 0;
      break;

    case TEST_SCHEDULER:
      p->priority = 10;
      break;

    case PRIORITY_SCHEDULER:
      p->priority = 60;
      break;

    case MLQ_SCHEDULER:
      // in this scheduler, priority shows the number of the queue.
      // 1(highest priority) -> 2(medium priority) -> 3(lowest priority)
      p->priority = 1;
      ptable.queue[0]++;
      break;

    default:
      break;
  }

  release(&ptable.lock);

  // Allocate kernel stack.
  if((p->kstack = kalloc()) == 0){
    p->state = UNUSED;
    return 0;
  }
  sp = p->kstack + KSTACKSIZE;

  // Leave room for trap frame.
  sp -= sizeof *p->tf;
  p->tf = (struct trapframe*)sp;

  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint*)sp = (uint)trapret;

  sp -= sizeof *p->context;
  p->context = (struct context*)sp;
  memset(p->context, 0, sizeof *p->context);
  p->context->eip = (uint)forkret;

  return p;
}

//PAGEBREAK: 32
// Set up first user process.
void
userinit(void)
{
  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];

  p = allocproc();
  
  initproc = p;
  if((p->pgdir = setupkvm()) == 0)
    panic("userinit: out of memory?");
  inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);
  p->sz = PGSIZE;
  memset(p->tf, 0, sizeof(*p->tf));
  p->tf->cs = (SEG_UCODE << 3) | DPL_USER;
  p->tf->ds = (SEG_UDATA << 3) | DPL_USER;
  p->tf->es = p->tf->ds;
  p->tf->ss = p->tf->ds;
  p->tf->eflags = FL_IF;
  p->tf->esp = PGSIZE;
  p->tf->eip = 0;  // beginning of initcode.S

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  // this assignment to p->state lets other cores
  // run this process. the acquire forces the above
  // writes to be visible, and the lock is also needed
  // because the assignment might not be atomic.
  acquire(&ptable.lock);

  p->state = RUNNABLE;

  release(&ptable.lock);
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;
  struct proc *curproc = myproc();

  sz = curproc->sz;
  if(n > 0){
    if((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  } else if(n < 0){
    if((sz = deallocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  curproc->sz = sz;
  switchuvm(curproc);
  return 0;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *curproc = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy process state from proc.
  if((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0){
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
    return -1;
  }
  np->sz = curproc->sz;
  np->parent = curproc;
  *np->tf = *curproc->tf;

  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;

  for(i = 0; i < NOFILE; i++)
    if(curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  pid = np->pid;

  acquire(&ptable.lock);

  np->state = RUNNABLE;

  release(&ptable.lock);

  return pid;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void
exit(void)
{
  struct proc *curproc = myproc();
  struct proc *p;
  int fd;

  if(curproc == initproc)
    panic("init exiting");

  // Close all open files.
  for(fd = 0; fd < NOFILE; fd++){
    if(curproc->ofile[fd]){
      fileclose(curproc->ofile[fd]);
      curproc->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(curproc->cwd);
  end_op();
  curproc->cwd = 0;

  acquire(&ptable.lock);

  // Parent might be sleeping in wait().
  wakeup1(curproc->parent);

  // Pass abandoned children to init.
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->parent == curproc){
      p->parent = initproc;
      if(p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }

  curproc->etime = ticks;   // set exit time when process terminates

  // Jump into the scheduler, never to return.
  curproc->state = ZOMBIE;
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(void)
{
  struct proc *p;
  int havekids, pid;
  struct proc *curproc = myproc();
  
  acquire(&ptable.lock);
  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->parent != curproc)
        continue;
      havekids = 1;
      if(p->state == ZOMBIE){
        // Found one.
        pid = p->pid;
        kfree(p->kstack);
        p->kstack = 0;
        freevm(p->pgdir);
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->state = UNUSED;
        release(&ptable.lock);
        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || curproc->killed){
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock);  //DOC: wait-sleep
  }
}

//PAGEBREAK: 42
// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.
void
main_scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();
  c->proc = 0;
  
  for(;;){
    // Enable interrupts on this processor.
    sti();

    // Loop over process table looking for process to run.
    acquire(&ptable.lock);
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->state != RUNNABLE)
        continue;

      // Switch to chosen process.  It is the process's job
      // to release ptable.lock and then reacquire it
      // before jumping back to us.
      c->proc = p;
      switchuvm(p);
      p->state = RUNNING;

      swtch(&(c->scheduler), p->context);   // resumes the for loop
      switchkvm();

      // Process is done running for now.
      // It should have changed its p->state before coming back.
      c->proc = 0;
    }
    release(&ptable.lock);

  }
}

// don’t use this scheduler.
// it’s not compatibale with proc changes that we have made
void
test_scheduler(void)
{
  struct proc *p, *p1;
  struct cpu *c = mycpu();
  c->proc = 0;
  
  for(;;){
    // Enable interrupts on this processor.
    sti();

    struct proc *highP;

    // Loop over process table looking for process to run.
    acquire(&ptable.lock);
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->state != RUNNABLE)
        continue;

      // Switch to chosen process.  It is the process's job
      // to release ptable.lock and then reacquire it
      // before jumping back to us.

      highP = p;
      //choose one with highest priority
      for(p1 = ptable.proc; p1 < &ptable.proc[NPROC]; p1++){
        if(p1->state != RUNNABLE)
          continue;
          
        if(highP->priority > p1->priority)   //larger value, lower priority
          highP = p1;
      }
      p = highP;
      c->proc = p;
      switchuvm(p);
      p->state = RUNNING;

      swtch(&(c->scheduler), p->context);
      switchkvm();

      // Process is done running for now.
      // It should have changed its p->state before coming back.
      c->proc = 0;
    }
    release(&ptable.lock);

  }
}

void
priority_scheduler(void)
{
  struct proc *p;  
  struct cpu *c = mycpu();
  c->proc = 0;

  int highestPriority;

  for(;;)
  {
    // Enable interrupts on this processor.
    sti();

    highestPriority = 101;

    // Loop over process table looking for process to run.
    acquire(&ptable.lock);
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    {
      // here we find the highest priority(which means the lowest value)
      if(p->state != RUNNABLE)
        continue;
      
      if (p->priority < highestPriority)      
        highestPriority = p->priority;    
    }

    if(highestPriority == 101)
    {
      release(&ptable.lock);
      continue;
    }

    // round robin for the processes that have same priority
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    {
      if(p->state != RUNNABLE || p->priority != highestPriority)
        continue;

      // Switch to chosen process.  It is the process's job
      // to release ptable.lock and then reacquire it
      // before jumping back to us.
      c->proc = p;
      switchuvm(p);
      p->state = RUNNING;

      // resumes the inner for loop(the second one)
      // which means we have round robin for the processes that have same priority
      swtch(&(c->scheduler), p->context);
      switchkvm();

      // Process is done running for now.
      // It should have changed its p->state before coming back.
      c->proc = 0;

      int reschedule = 0;
      for(int i = 0; i < ncpu; i++)
      {
        if(ptable.priorityChanged[i] == 1)
        {
          reschedule = 1;
          ptable.priorityChanged[i] = 0;
          break;
        }
      }
      
      if(reschedule == 1)
      {
        // we should find the highest priority again
        break;
      }
      else
      {
        // if not changed we do round robin for the same priorities
        continue;
      }
    }
    release(&ptable.lock);

  }
}

void
mlq_scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();
  c->proc = 0;
  
  for(;;){
    // Enable interrupts on this processor.
    sti();

    acquire(&ptable.lock);

    if(ptable.queue[0] > 0)
    {
      // service the first queue
      // guaranteed scheduling policy

      struct proc *p1 = 0;
      float minRatio, ratio;
      int entitled;

      for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
      {
        if(p->state != RUNNABLE || p->priority != 1)
          continue;

        // we found a process which is runnable and belongs to the first queue(priority = 1)
        if(p1 == 0)
        {
          // first one
          p1 = p;
          entitled = (float)(ticks - p1->stime) / ptable.queue[0]; // according to the tanenbaum book
          minRatio = (float)(p1->rtime) / entitled;
        }
        else
        {
          entitled = (float)(ticks - p->stime) / ptable.queue[0]; // according to the tanenbaum book
          ratio = (float)(p->rtime) / entitled;
          if(minRatio > ratio)
          {
            p1 = p;
            minRatio = ratio;
          }
        }
      }

      // we found the minRatio to service
      c->proc = p1;
      switchuvm(p1);
      p1->state = RUNNING;

      swtch(&(c->scheduler), p1->context);   // resumes the for loop - we come here from the exit or the yield by means of the sched function.
      switchkvm();

      ptable.queue[0]--;
      if(p1->state != ZOMBIE)
      {
        // go to the second queue
        p1->priority = 2;
        ptable.queue[1]++;
      }

      c->proc = 0;
    }
    else if(ptable.queue[1] > 0)
    {
      // service the second queue
      // FIFO and RR

      struct proc *p1 = 0;
      uint minStartTime;

      for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
      {
        if(p->state != RUNNABLE || p->priority != 2)
          continue;

        if(p1 == 0)
        {
          // first one
          p1 = p;
          minStartTime = p1->stime;
        }
        else if(minStartTime > p->stime)
        {
          p1 = p;
          minStartTime = p->stime;
        }
      }

      c->proc = p1;
      switchuvm(p1);
      p1->state = RUNNING;

      swtch(&(c->scheduler), p1->context);   // resumes the for loop
      switchkvm();

      ptable.queue[1]--;
      if(p1->state != ZOMBIE)
      {
        // go to the third queue
        p1->priority = 3;
        ptable.queue[2]++;
      }

      c->proc = 0;
    }
    else if(ptable.queue[2] > 0)
    {
      // service the third queue
      // Round Robin

      for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
      {
        if(p->state != RUNNABLE || p->priority != 3)
          continue;

        c->proc = p;
        switchuvm(p);
        p->state = RUNNING;

        swtch(&(c->scheduler), p->context);   // resumes the for loop
        switchkvm();

        if(p->state == ZOMBIE)
        {
          ptable.queue[2]--;
        }

        c->proc = 0;
        break;  // we should check from the first queue
      }
    }
    
    release(&ptable.lock);

  }
}

// Enter scheduler.  Must hold only ptable.lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->ncli, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&ptable.lock))
    panic("sched ptable.lock");
  if(mycpu()->ncli != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched running");
  if(readeflags()&FL_IF)
    panic("sched interruptible");
  intena = mycpu()->intena;
  swtch(&p->context, mycpu()->scheduler);   // resumes the for loop in scheduler
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  acquire(&ptable.lock);  //DOC: yieldlock
  myproc()->state = RUNNABLE;
  sched();
  release(&ptable.lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void
forkret(void)
{
  static int first = 1;
  // Still holding ptable.lock from scheduler.
  release(&ptable.lock);

  if (first) {
    // Some initialization functions must be run in the context
    // of a regular process (e.g., they call sleep), and thus cannot
    // be run from main().
    first = 0;
    iinit(ROOTDEV);
    initlog(ROOTDEV);
  }

  // Return to "caller", actually trapret (see allocproc).
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  
  if(p == 0)
    panic("sleep");

  if(lk == 0)
    panic("sleep without lk");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if(lk != &ptable.lock){  //DOC: sleeplock0
    acquire(&ptable.lock);  //DOC: sleeplock1
    release(lk);
  }
  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  if(lk != &ptable.lock){  //DOC: sleeplock2
    release(&ptable.lock);
    acquire(lk);
  }
}

//PAGEBREAK!
// Wake up all processes sleeping on chan.
// The ptable lock must be held.
static void
wakeup1(void *chan)
{
  struct proc *p;

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == SLEEPING && p->chan == chan)
      p->state = RUNNABLE;
}

// Wake up all processes sleeping on chan.
void
wakeup(void *chan)
{
  acquire(&ptable.lock);
  wakeup1(chan);
  release(&ptable.lock);
}

// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
int
kill(int pid)
{
  struct proc *p;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      p->killed = 1;
      // Wake process from sleep if necessary.
      if(p->state == SLEEPING)
        p->state = RUNNABLE;
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}

//PAGEBREAK: 36
// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [EMBRYO]    "embryo",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  int i;
  struct proc *p;
  char *state;
  uint pc[10];

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    cprintf("%d %s %s", p->pid, state, p->name);
    if(p->state == SLEEPING){
      getcallerpcs((uint*)p->context->ebp+2, pc);
      for(i=0; i<10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    cprintf("\n");
  }
}

void
kproc_dump(proc_info proc_infos[], int n)
{
  struct proc *p;
  int i = 0;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == RUNNING || p->state == RUNNABLE){
      proc_infos[i].pid = p->pid;
      proc_infos[i].memsize = p->sz;
      i++;
      if(n < i)
      {
        cprintf("Not enough space in the given array!\n");
      }
    }
  }
  n = (n > i) ? i : n;
  release(&ptable.lock);

  //start sorting in place
  int j = 0;
  i = 0;

  // i in [n, n-1, ... , 1]
  for (i = n; i >= 1; i--)
  {
    for (j = 0; j <= i - 2; j++)
    {
      if(proc_infos[j].memsize > proc_infos[j + 1].memsize)
      {
        int tmp;

        tmp = proc_infos[j].pid;
        proc_infos[j].pid = proc_infos[j + 1].pid;
        proc_infos[j + 1].pid = tmp;

        tmp = proc_infos[j].memsize;
        proc_infos[j].memsize = proc_infos[j + 1].memsize;
        proc_infos[j + 1].memsize = tmp;
      }
    }
  }
  
}

void
kcps()
{
  struct proc *p;

  //Enables interrupts on this processor.
  sti();

  //Loop over process table looking for process with pid.
  acquire(&ptable.lock);

  // add what is the scheduler
  switch (SCHEDULER)
  {
    case MAIN_SCHEDULER:
      cprintf("Here we are using MAIN_SCHEDULER\n");
      break;
    
    case TEST_SCHEDULER:
      cprintf("Here we are using TEST_SCHEDULER\n");
      break;

    case PRIORITY_SCHEDULER:
      cprintf("Here we are using PRIORITY_SCHEDULER\n");
      break;
    
    case MLQ_SCHEDULER:
      cprintf("Here we are using MLQ_SCHEDULER\n");
      break;
    
    default:
      cprintf("Probably wrong number has been selected for SCHEDULER\n");
      break;
  }

  cprintf("name \t pid \t state \t\t priority \n");
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == SLEEPING)
      cprintf("%s \t %d \t SLEEPING \t %d \n", p->name,p->pid,p->priority);
    else if(p->state == RUNNING)
      cprintf("%s \t %d \t RUNNING \t %d \n", p->name,p->pid,p->priority);
    else if(p->state == RUNNABLE)
      cprintf("%s \t %d \t RUNNABLE \t %d \n", p->name,p->pid,p->priority);
  }
  release(&ptable.lock);
}

int 
kchpr(int pid, int priority)
{
	struct proc *p;
  int oldPriority = -1;

	acquire(&ptable.lock);
	for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
	  if(p->pid == pid){
      oldPriority = p->priority;
			p->priority = priority;
			break;
		}
	}

  for(int i = 0; i < ncpu; i++)
  {
    // we should inform every cpu to reschedule.
    ptable.priorityChanged[i] = 1;
  }
	release(&ptable.lock);

  yield();

	return oldPriority;
}

int
kwaitx(int *wtime, int *rtime)
{
  struct proc *p;
  int havekids, pid;
  struct proc *curproc = myproc();
  
  acquire(&ptable.lock);
  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->parent != curproc)
        continue;
      havekids = 1;
      if(p->state == ZOMBIE){
        // Found one.
        pid = p->pid;
        kfree(p->kstack);
        p->kstack = 0;
        freevm(p->pgdir);
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->state = UNUSED;

        *wtime = (p->etime - p->stime) - p->rtime;  // set the waiting time of the child
        *rtime = p->rtime;    // set the run time of child

        release(&ptable.lock);
        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || curproc->killed){
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock);  //DOC: wait-sleep
  }
}

int
kset_priority(int priority)
{
  struct proc *curproc = myproc();

  acquire(&ptable.lock);
  int oldPriority = curproc->priority;
  curproc->priority = priority;
  for(int i = 0; i < ncpu; i++)
  {
    // we should inform every cpu to reschedule.
    ptable.priorityChanged[i] = 1;
  }
  release(&ptable.lock);

  yield();

  return oldPriority;
}

// This method will run every clock tick and update the statistic fields for each proc
void
updateStatistics() 
{
  struct proc *p;

  acquire(&ptable.lock);

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    switch(p->state)
    {
      case SLEEPING:
        p->iotime++;
        break;
      case RUNNABLE:
        break;
      case RUNNING:
        p->rtime++;
        break;
      default:
        break;
    }
  }

  release(&ptable.lock);
}
