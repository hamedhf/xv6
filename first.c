//A Simple C program
#include "types.h"
#include "stat.h"
#include "user.h"

//passing command line arguments

int main(int argc, char *argv[])
{
  printf(1, "My first xv6 program\n");
  printf(1, "Note: Unix V6 was released in year %d\n", getyear());
  exit();
}
