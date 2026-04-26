#include "vga.h"

#define VGA_BUF        ((volatile uint16_t *)0xB8000)
#define VGA_COLS       80
#define VGA_ROWS       25
#define VGA_SCROLL_MAX 23
#define CELL(c, col)   ((uint16_t)(((col) << 8) | (uint8_t)(c)))

/* scroll history */
#define SCROLL_HISTORY  150

static uint16_t scroll_buf[SCROLL_HISTORY][VGA_COLS];
static int      scroll_buf_count  = 0;
static int      scroll_buf_head   = 0;
static int      scroll_view_off   = 0;

/* live screen snapshot taken when we start scrolling */
static uint16_t live_snap[VGA_SCROLL_MAX][VGA_COLS];
static int      live_snap_valid   = 0;

static int     cur_row       = 0;
static int     cur_col       = 0;
static uint8_t default_color = 0;

/* ── helpers ─────────────────────────────────────────────────────────────── */

static void save_live_snap(void) {
    for (int r = 0; r < VGA_SCROLL_MAX; r++)
        for (int c = 0; c < VGA_COLS; c++)
            live_snap[r][c] = VGA_BUF[r * VGA_COLS + c];
    live_snap_valid = 1;
}

static void restore_live_snap(void) {
    if (!live_snap_valid) return;
    for (int r = 0; r < VGA_SCROLL_MAX; r++)
        for (int c = 0; c < VGA_COLS; c++)
            VGA_BUF[r * VGA_COLS + c] = live_snap[r][c];
}

static void redraw_view(void) {
    /*
     * Virtual line space:
     *   [0 .. scroll_buf_count-1]          = history (oldest first)
     *   [scroll_buf_count .. total-1]       = live screen rows
     *
     * scroll_view_off = 0 → show live screen
     * scroll_view_off = N → show window N lines above live
     */
    int total      = scroll_buf_count + VGA_SCROLL_MAX;
    int view_start = total - VGA_SCROLL_MAX - scroll_view_off;

    /* oldest history entry in ring */
    int oldest = (scroll_buf_head - scroll_buf_count + SCROLL_HISTORY)
                 % SCROLL_HISTORY;

    for (int r = 0; r < VGA_SCROLL_MAX; r++) {
        int vl = view_start + r;

        if (vl < 0) {
            /* before history — blank */
            for (int c = 0; c < VGA_COLS; c++)
                VGA_BUF[r * VGA_COLS + c] = CELL(' ', default_color);

        } else if (vl < scroll_buf_count) {
            /* from history ring buffer */
            int idx = (oldest + vl) % SCROLL_HISTORY;
            for (int c = 0; c < VGA_COLS; c++)
                VGA_BUF[r * VGA_COLS + c] = scroll_buf[idx][c];

        } else {
            /* from live screen snapshot */
            int sr = vl - scroll_buf_count;
            if (sr < VGA_SCROLL_MAX && live_snap_valid) {
                for (int c = 0; c < VGA_COLS; c++)
                    VGA_BUF[r * VGA_COLS + c] = live_snap[sr][c];
            } else {
                for (int c = 0; c < VGA_COLS; c++)
                    VGA_BUF[r * VGA_COLS + c] = CELL(' ', default_color);
            }
        }
    }
}

/* ── scroll indicator on status row 23 ──────────────────────────────────── */

static void draw_scroll_indicator(void) {
    uint8_t ind = VGA_COLOR(VGA_YELLOW, VGA_DARK_GREY);
    const char *msg = "  [SCROLLED] SHIFT+DOWN to return  ";
    int col = 0;
    for (int i = 0; msg[i] && col < VGA_COLS; i++)
        VGA_BUF[23 * VGA_COLS + col++] = (uint16_t)(ind << 8 | (uint8_t)msg[i]);
}

/* ── public scroll API ───────────────────────────────────────────────────── */

void vga_scroll_view_up(int lines) {
    if (scroll_buf_count == 0) return;

    /* take snapshot of live screen before first scroll */
    if (scroll_view_off == 0)
        save_live_snap();

    scroll_view_off += lines;
    if (scroll_view_off > scroll_buf_count)
        scroll_view_off = scroll_buf_count;

    redraw_view();
    draw_scroll_indicator();
}

void vga_scroll_view_down(int lines) {
    if (scroll_view_off == 0) return;

    scroll_view_off -= lines;
    if (scroll_view_off < 0) scroll_view_off = 0;

    if (scroll_view_off == 0) {
        /* back to live — restore snapshot */
        restore_live_snap();
    } else {
        redraw_view();
        draw_scroll_indicator();
    }
}

int vga_is_scrolled(void) {
    return scroll_view_off > 0;
}

/* ── core VGA functions ──────────────────────────────────────────────────── */

static void scroll(void) {
    /* save row 0 into history before discarding it */
    for (int c = 0; c < VGA_COLS; c++)
        scroll_buf[scroll_buf_head][c] = VGA_BUF[c];
    scroll_buf_head = (scroll_buf_head + 1) % SCROLL_HISTORY;
    if (scroll_buf_count < SCROLL_HISTORY) scroll_buf_count++;

    /* if user is scrolled, keep their view stable */
    if (scroll_view_off > 0) {
        /* update live snap but don't touch visible area */
        for (int r = 0; r < VGA_SCROLL_MAX - 1; r++)
            for (int c = 0; c < VGA_COLS; c++)
                live_snap[r][c] = live_snap[r + 1][c];
        for (int c = 0; c < VGA_COLS; c++)
            live_snap[VGA_SCROLL_MAX - 1][c] = CELL(' ', default_color);
    } else {
        /* normal scroll */
        for (int r = 0; r < VGA_SCROLL_MAX - 1; r++)
            for (int c = 0; c < VGA_COLS; c++)
                VGA_BUF[r * VGA_COLS + c] = VGA_BUF[(r + 1) * VGA_COLS + c];
        for (int c = 0; c < VGA_COLS; c++)
            VGA_BUF[(VGA_SCROLL_MAX - 1) * VGA_COLS + c] = CELL(' ', default_color);
    }

    cur_row = VGA_SCROLL_MAX - 1;
    cur_col = 0;
}

void vga_init(void) {
    default_color = VGA_COLOR(VGA_WHITE, VGA_BLUE);
    cur_row = 0;
    cur_col = 0;
    scroll_buf_count = 0;
    scroll_buf_head  = 0;
    scroll_view_off  = 0;
    live_snap_valid  = 0;
}

void vga_clear(uint8_t color) {
    default_color   = color;
    scroll_view_off = 0;
    live_snap_valid = 0;
    for (int r = 0; r < VGA_SCROLL_MAX; r++)
        for (int c = 0; c < VGA_COLS; c++)
            VGA_BUF[r * VGA_COLS + c] = CELL(' ', color);
    cur_row = 0;
    cur_col = 0;
}

void vga_putc(char c, uint8_t color) {
    /* if scrolled, update live snapshot instead of VGA directly */
    if (scroll_view_off > 0) {
        if (c == '\n') {
            /* scroll live snap */
            for (int r = 0; r < VGA_SCROLL_MAX - 1; r++)
                for (int cc = 0; cc < VGA_COLS; cc++)
                    live_snap[r][cc] = live_snap[r + 1][cc];
            for (int cc = 0; cc < VGA_COLS; cc++)
                live_snap[VGA_SCROLL_MAX - 1][cc] = CELL(' ', color);
        } else if (c != '\r' && c != '\b' && c != '\t') {
            if (cur_row < VGA_SCROLL_MAX && cur_col < VGA_COLS)
                live_snap[cur_row][cur_col] = CELL(c, color);
        }
        /* fall through to update cur_row/cur_col */
    }

    if (c == '\n') {
        cur_col = 0;
        cur_row++;
        if (cur_row >= VGA_SCROLL_MAX) scroll();
        return;
    }
    if (c == '\r') { cur_col = 0; return; }
    if (c == '\b') {
        if (cur_col > 0) {
            cur_col--;
            if (scroll_view_off == 0)
                VGA_BUF[cur_row * VGA_COLS + cur_col] = CELL(' ', color);
            else if (cur_row < VGA_SCROLL_MAX)
                live_snap[cur_row][cur_col] = CELL(' ', color);
        }
        return;
    }
    if (c == '\t') {
        cur_col = (cur_col + 8) & ~7;
        if (cur_col >= VGA_COLS) {
            cur_col = 0;
            cur_row++;
            if (cur_row >= VGA_SCROLL_MAX) scroll();
        }
        return;
    }
    if (cur_row >= VGA_SCROLL_MAX) scroll();
    if (scroll_view_off == 0)
        VGA_BUF[cur_row * VGA_COLS + cur_col] = CELL(c, color);
    else if (cur_row < VGA_SCROLL_MAX)
        live_snap[cur_row][cur_col] = CELL(c, color);
    cur_col++;
    if (cur_col >= VGA_COLS) {
        cur_col = 0;
        cur_row++;
        if (cur_row >= VGA_SCROLL_MAX) scroll();
    }
}

void vga_puts(const char *s, uint8_t color) {
    while (*s) vga_putc(*s++, color);
}

void vga_puts_at(int row, int col, uint8_t color, const char *s) {
    cur_row = row;
    cur_col = col;
    vga_puts(s, color);
}

void vga_puthex(uint64_t val, uint8_t color) {
    const char *d = "0123456789ABCDEF";
    vga_puts("0x", color);
    for (int i = 60; i >= 0; i -= 4)
        vga_putc(d[(val >> i) & 0xF], color);
}

void vga_set_cursor(int row, int col) {
    cur_row = row;
    cur_col = col;
    if (scroll_view_off > 0) return;   /* don't move hw cursor while scrolled */
    uint16_t pos = (uint16_t)(row * VGA_COLS + col);
    __asm__ volatile("outb %0, %1" :: "a"((uint8_t)0x0F), "Nd"((uint16_t)0x3D4));
    __asm__ volatile("outb %0, %1" :: "a"((uint8_t)(pos & 0xFF)), "Nd"((uint16_t)0x3D5));
    __asm__ volatile("outb %0, %1" :: "a"((uint8_t)0x0E), "Nd"((uint16_t)0x3D4));
    __asm__ volatile("outb %0, %1" :: "a"((uint8_t)(pos >> 8)), "Nd"((uint16_t)0x3D5));
}

void vga_get_cursor(int *row, int *col) {
    *row = cur_row;
    *col = cur_col;
}