
/***
 *
 * include user defined headers
 *
 ***/
#include "hutil.h"
using namespace std;
/***
 *
 * Exit and send a mesage to stderr
 *
 ***/
void h_exit(char *errmesg){
  perror(errmesg);
  exit(1);
}
