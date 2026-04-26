#ifndef SHELL_H
#define SHELL_H

#include "types.h"

/* ── Shell config ────────────────────────────────────────────────────────── */
#define SHELL_MAX_INPUT   128
#define SHELL_MAX_ARGS    16
#define SHELL_MAX_CMDS    32
#define SHELL_PROMPT      "axiomX> "

/* ── Command handler ─────────────────────────────────────────────────────── */
typedef void (*shell_cmd_fn)(int argc, char **argv);

typedef struct {
    const char   *name;
    const char   *desc;
    shell_cmd_fn  fn;
} shell_cmd_t;

/* ── Public API ──────────────────────────────────────────────────────────── */
void shell_init(void);
void shell_run(void);
void shell_putchar(char c);       /* printable char from keyboard IRQ  */
void shell_set_shift(int held);   /* shift press/release from IRQ      */
void shell_special(uint8_t sc);   /* arrow keys from IRQ               */
int  shell_register(const char *name, const char *desc, shell_cmd_fn fn);
int shell_trace_active(void);

#endif