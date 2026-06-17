#include "printf_output.h"

#include <stdarg.h>
#include <stdio.h>

void my_printf_output(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
}
