#ifndef PRINTF_OUTPUT_H
#define PRINTF_OUTPUT_H

#ifdef __cplusplus
extern "C" {
#endif

void my_printf_output(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

#ifdef __cplusplus
}
#endif

#endif /* PRINTF_OUTPUT_H */
