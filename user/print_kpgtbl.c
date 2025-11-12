#include "kernel/types.h"
#include "user/user.h"

int
main(void)
{
  print_kpgtbl();  // calls the new syscall you implemented in kernel
  exit(0);
}
