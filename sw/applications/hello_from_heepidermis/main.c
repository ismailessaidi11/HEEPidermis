// Remember to build this app with make app COMPILER_FLAGS=-Os
#include "syscalls.h"
int main(){
    _writestr("hello from HEEPidermis");
    return 0;
}