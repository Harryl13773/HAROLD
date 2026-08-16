// gui.elf — a text-mode desktop: box-drawing chrome, a live clock, a real directory listing as
// clickable labels, and a taskbar. Every driver it needs already exists: rtc_read() for the
// clock, mouse_read() for cursor/clicks, listdir()/run() to browse and launch real programs.
// Pure text-mode VGA (80x25), no pixel graphics, no window manager — just one more userspace ELF
// like any other, launched from and returning to the ordinary shell.

#include "libc.h"

#define VGA_WIDTH 80
#define VGA_HEIGHT 25

#define ATTR_NORMAL 0x0F
#define ATTR_INVERT 0xF0

// CP437 box-drawing glyphs — VGA text mode's built-in font, not Unicode, so these are raw byte
// values rather than literal box-drawing characters in the source
#define CH_TL ((char)0xDA) // top-left corner
#define CH_TR ((char)0xBF) // top-right corner
#define CH_BL ((char)0xC0) // bottom-left corner
#define CH_BR ((char)0xD9) // bottom-right corner
#define CH_H ((char)0xC4)  // horizontal
#define CH_V ((char)0xB3)  // vertical
#define CH_ML ((char)0xC3) // divider left junction
#define CH_MR ((char)0xB4) // divider right junction

#define TITLE_ROW 1
#define CONTENT_TOP 3
#define CONTENT_BOTTOM 20 // inclusive — 18 visible entries, no scrolling in this version
#define DIVIDER2_ROW 21
#define TASKBAR_ROW 22
#define HINT_ROW 23

#define MAX_ENTRIES (CONTENT_BOTTOM - CONTENT_TOP + 1)

static const char *HOME_LABEL = " [Home]";
static const char *TERMINAL_LABEL = "Terminal";
static const char *REFRESH_LABEL = "Refresh";

struct entry
{
    char name[64];
    int is_dir;
};

static struct entry entries[MAX_ENTRIES];
static int entry_count;
static char current_dir[64] = ""; // "" means root; one level deep only in this version

static int cursor_row = 12;
static int cursor_col = 40;
static int prev_left = 0;

// Taskbar button column ranges (inclusive), computed once by draw_taskbar
static int home_start, home_end;
static int terminal_start, terminal_end;
static int refresh_start, refresh_end;

// Appends s to buf at pos, returns the new position
static unsigned int append_str(char *buf, unsigned int pos, const char *s)
{
    while (*s != '\0')
    {
        buf[pos++] = *s++;
    }
    return pos;
}

// Converts one uppercase letter to lowercase, leaves everything else unchanged
static char to_lower_char(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

// True if name ends in ".elf", case-insensitively
static int is_elf_name(const char *name)
{
    unsigned int len = strlen(name);
    if (len < 4)
    {
        return 0;
    }
    return to_lower_char(name[len - 4]) == '.' && to_lower_char(name[len - 3]) == 'e' &&
           to_lower_char(name[len - 2]) == 'l' && to_lower_char(name[len - 1]) == 'f';
}

// Draws one full-width border/divider row with corner/junction glyphs
static void draw_border_row(int row, char left, char fill, char right)
{
    char buf[VGA_WIDTH];
    buf[0] = left;
    for (int i = 1; i < VGA_WIDTH - 1; i++)
    {
        buf[i] = fill;
    }
    buf[VGA_WIDTH - 1] = right;
    draw_text(row, 0, buf, VGA_WIDTH, ATTR_NORMAL);
}

// Draws text between two vertical bars, left-aligned, space-padded to fill the row
static void draw_content_row(int row, const char *text, unsigned char attr)
{
    char buf[VGA_WIDTH];
    buf[0] = CH_V;

    unsigned int len = strlen(text);
    if (len > VGA_WIDTH - 2)
    {
        len = VGA_WIDTH - 2;
    }

    unsigned int i = 0;
    for (; i < len; i++)
    {
        buf[1 + i] = text[i];
    }
    for (; i < VGA_WIDTH - 2; i++)
    {
        buf[1 + i] = ' ';
    }
    buf[VGA_WIDTH - 1] = CH_V;

    draw_text(row, 0, buf, VGA_WIDTH, attr);
}

// Reloads entries[] from the current directory via listdir(), up to MAX_ENTRIES
static void refresh_entries(void)
{
    entry_count = 0;
    int index = 0;
    while (entry_count < MAX_ENTRIES)
    {
        int is_dir;
        char name[64];
        int size = listdir(current_dir, index, name, sizeof(name), &is_dir);
        if (size < 0)
        {
            break;
        }
        index++;

        unsigned int len = strlen(name);
        if (len >= sizeof(entries[0].name))
        {
            len = sizeof(entries[0].name) - 1;
        }
        memcpy(entries[entry_count].name, name, len);
        entries[entry_count].name[len] = '\0';
        entries[entry_count].is_dir = is_dir;
        entry_count++;
    }
}

// Formats one entry's display line (a [DIR] prefix plus its name) into line
static void entry_line(int idx, char *line)
{
    unsigned int pos = 0;
    const char *prefix = entries[idx].is_dir ? "[DIR] " : "      ";
    pos = append_str(line, pos, prefix);
    pos = append_str(line, pos, entries[idx].name);
    line[pos] = '\0';
}

// Draws the app name and a live HH:MM clock, right-aligned, read via rtc_read()
static void draw_title_bar(void)
{
    struct rtc_time t;
    rtc_read(&t);

    char line[VGA_WIDTH];
    unsigned int pos = append_str(line, 0, " HAROLD");

    char clock_str[8];
    unsigned int clen = 0;
    char num[4];
    int n = itoa((int)t.hour, num);
    if (n < 2)
    {
        clock_str[clen++] = '0';
    }
    for (int i = 0; i < n; i++)
    {
        clock_str[clen++] = num[i];
    }
    clock_str[clen++] = ':';
    n = itoa((int)t.minute, num);
    if (n < 2)
    {
        clock_str[clen++] = '0';
    }
    for (int i = 0; i < n; i++)
    {
        clock_str[clen++] = num[i];
    }
    clock_str[clen] = '\0';

    unsigned int content_width = VGA_WIDTH - 2; // between the two vertical bars
    unsigned int pad_target = content_width - clen - 1; // leave a space before the right border
    while (pos < pad_target)
    {
        line[pos++] = ' ';
    }
    pos = append_str(line, pos, clock_str);
    line[pos] = '\0';

    draw_content_row(TITLE_ROW, line, ATTR_NORMAL);
}

// Draws the Home/Terminal/Refresh buttons and records each one's column range for hit-testing
static void draw_taskbar(void)
{
    char line[VGA_WIDTH];
    unsigned int pos = 0;

    home_start = 1 + (int)pos; // +1: column 0 is the left border, text starts at column 1
    pos = append_str(line, pos, HOME_LABEL);
    home_end = home_start + (int)strlen(HOME_LABEL) - 1;

    pos = append_str(line, pos, "   ");

    terminal_start = 1 + (int)pos;
    pos = append_str(line, pos, TERMINAL_LABEL);
    terminal_end = terminal_start + (int)strlen(TERMINAL_LABEL) - 1;

    pos = append_str(line, pos, "   ");

    refresh_start = 1 + (int)pos;
    pos = append_str(line, pos, REFRESH_LABEL);
    refresh_end = refresh_start + (int)strlen(REFRESH_LABEL) - 1;

    line[pos] = '\0';
    draw_content_row(TASKBAR_ROW, line, ATTR_NORMAL);
}

// Redraws the whole screen: chrome, entry list, taskbar, and contextual hint
static void redraw_desktop(void)
{
    draw_border_row(0, CH_TL, CH_H, CH_TR);
    draw_title_bar();
    draw_border_row(2, CH_ML, CH_H, CH_MR);

    for (int row = CONTENT_TOP; row <= CONTENT_BOTTOM; row++)
    {
        int idx = row - CONTENT_TOP;
        if (idx < entry_count)
        {
            char line[VGA_WIDTH];
            entry_line(idx, line);
            draw_content_row(row, line, ATTR_NORMAL);
        }
        else
        {
            draw_content_row(row, "", ATTR_NORMAL);
        }
    }

    draw_border_row(DIVIDER2_ROW, CH_ML, CH_H, CH_MR);
    draw_taskbar();

    const char *hint = (current_dir[0] == '\0')
                            ? " Click a folder to open it, a .elf to run it, or a file to edit it."
                            : " Inside a folder -- click Home to go back to the root.";
    draw_content_row(HINT_ROW, hint, ATTR_NORMAL);

    draw_border_row(24, CH_BL, CH_H, CH_BR);
}

// Returns the entry index the given row falls on, or -1 if it's not over a real entry
static int hit_test_content_row(int row)
{
    if (row < CONTENT_TOP || row > CONTENT_BOTTOM)
    {
        return -1;
    }
    int idx = row - CONTENT_TOP;
    if (idx >= entry_count)
    {
        return -1;
    }
    return idx;
}

// Highlights whatever the cursor is currently over, then draws the cursor glyph on top — called
// after redraw_desktop() so the highlight/cursor are never clobbered by the plain redraw
static void apply_hover_and_cursor(void)
{
    int hovered_entry = hit_test_content_row(cursor_row);
    if (hovered_entry >= 0)
    {
        char line[VGA_WIDTH];
        entry_line(hovered_entry, line);
        draw_content_row(cursor_row, line, ATTR_INVERT);
    }
    else if (cursor_row == TASKBAR_ROW)
    {
        if (cursor_col >= home_start && cursor_col <= home_end)
        {
            draw_text(TASKBAR_ROW, home_start, HOME_LABEL, strlen(HOME_LABEL), ATTR_INVERT);
        }
        else if (cursor_col >= terminal_start && cursor_col <= terminal_end)
        {
            draw_text(TASKBAR_ROW, terminal_start, TERMINAL_LABEL, strlen(TERMINAL_LABEL), ATTR_INVERT);
        }
        else if (cursor_col >= refresh_start && cursor_col <= refresh_end)
        {
            draw_text(TASKBAR_ROW, refresh_start, REFRESH_LABEL, strlen(REFRESH_LABEL), ATTR_INVERT);
        }
    }

    draw_text(cursor_row, cursor_col, "*", 1, ATTR_INVERT);
}

// Resets the sequential terminal cursor/scroll state via the normal write() syscall (positioned
// draw_text() calls never touch it) so the shell resumes on a clean blank screen instead of
// continuing from wherever the cursor was left before this program ever started drawing
static void clean_exit(void)
{
    for (int i = 0; i < VGA_HEIGHT; i++)
    {
        write("\n", 1);
    }
    exit();
}

// Handles a left click: navigates into a directory, runs a .elf, opens anything else in the
// editor, or activates whichever taskbar button was clicked
static void handle_click(void)
{
    int hovered_entry = hit_test_content_row(cursor_row);
    if (hovered_entry >= 0)
    {
        struct entry *e = &entries[hovered_entry];

        char full_path[128];
        unsigned int pos = 0;
        if (current_dir[0] != '\0')
        {
            pos = append_str(full_path, pos, current_dir);
            pos = append_str(full_path, pos, "/");
        }
        pos = append_str(full_path, pos, e->name);
        full_path[pos] = '\0';

        if (e->is_dir)
        {
            unsigned int len = pos;
            if (len >= sizeof(current_dir))
            {
                len = sizeof(current_dir) - 1;
            }
            memcpy(current_dir, full_path, len);
            current_dir[len] = '\0';
            refresh_entries();
        }
        else if (is_elf_name(e->name))
        {
            char *argv[1];
            argv[0] = full_path;
            run(1, argv);
            refresh_entries();
        }
        else
        {
            char *argv[2];
            argv[0] = "edit.elf";
            argv[1] = full_path;
            run(2, argv);
            refresh_entries();
        }
        return;
    }

    if (cursor_row == TASKBAR_ROW)
    {
        if (cursor_col >= home_start && cursor_col <= home_end)
        {
            current_dir[0] = '\0';
            refresh_entries();
        }
        else if (cursor_col >= terminal_start && cursor_col <= terminal_end)
        {
            clean_exit(); // never returns
        }
        else if (cursor_col >= refresh_start && cursor_col <= refresh_end)
        {
            refresh_entries();
        }
    }
}

void _start(void)
{
    refresh_entries();
    redraw_desktop();
    apply_hover_and_cursor();

    for (;;)
    {
        struct mouse_packet p;
        mouse_read(&p);

        cursor_col += p.dx;
        cursor_row -= p.dy; // raw PS/2 convention: positive dy means the mouse moved up

        if (cursor_col < 0)
        {
            cursor_col = 0;
        }
        if (cursor_col >= VGA_WIDTH)
        {
            cursor_col = VGA_WIDTH - 1;
        }
        if (cursor_row < 0)
        {
            cursor_row = 0;
        }
        if (cursor_row >= VGA_HEIGHT)
        {
            cursor_row = VGA_HEIGHT - 1;
        }

        int clicked = p.left_button && !prev_left; // rising edge only, not held
        prev_left = p.left_button;

        if (clicked)
        {
            handle_click(); // may block running another program, or exit and never return
        }

        redraw_desktop(); // reclaims the screen, whether or not a launched program used it
        apply_hover_and_cursor();
    }
}
