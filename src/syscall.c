// int 0x80 syscall dispatcher: file I/O, sockets, terminal write, and task exit for ring 3 programs

#include <stdint.h>
#include "idt.h"
#include "terminal.h"
#include "task.h"
#include "keyboard.h"
#include "mouse.h"
#include "fat.h"
#include "tcp.h"
#include "dns.h"
#include "rtc.h"
#include "syscall.h"

extern void syscall_stub(void);

// Same layout isr.c/irq.c use — matches what syscall_stub actually pushes
struct registers
{
    uint32_t ds;
    uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax;
    uint32_t int_no, err_code;
    uint32_t eip, cs, eflags, useresp, ss;
};

#define SYSCALL_WRITE 0
#define SYSCALL_EXIT 1
#define SYSCALL_READ 2
#define SYSCALL_OPEN 3
#define SYSCALL_CLOSE 4
#define SYSCALL_LIST 5
#define SYSCALL_SOCKET_ACCEPT 6
#define SYSCALL_SOCKET_RECV 7
#define SYSCALL_SOCKET_SEND 8
#define SYSCALL_SOCKET_CLOSE 9
#define SYSCALL_OPEN_WRITE 10
#define SYSCALL_FWRITE 11
#define SYSCALL_MKDIR 12
#define SYSCALL_SEEK 13
#define SYSCALL_DNS_RESOLVE 14
#define SYSCALL_RTC_READ 15
#define SYSCALL_MOUSE_READ 16

void syscall_handler(struct registers *regs)
{
    switch (regs->eax)
    {
    case SYSCALL_WRITE:
    {
        const char *buf = (const char *)regs->ebx;
        uint32_t len = regs->ecx;

        for (uint32_t i = 0; i < len; i++)
        {
            terminal_putchar(buf[i]);
        }

        regs->eax = len;
        break;
    }

    case SYSCALL_READ:
    {
        int fd = (int)regs->ebx;
        char *buf = (char *)regs->ecx;
        uint32_t max_len = regs->edx;

        if (fd == 0)
        {
            // stdin — blocks on the keyboard, unchanged from before
            uint32_t count = 0;
            while (count < max_len)
            {
                char c = keyboard_read_char();
                buf[count++] = c;
                if (c == '\n')
                {
                    break;
                }
            }
            regs->eax = count;
        }
        else
        {
            regs->eax = (uint32_t)fat_read(fd, (uint8_t *)buf, max_len);
        }
        break;
    }

    case SYSCALL_OPEN:
    {
        const char *filename = (const char *)regs->ebx;
        regs->eax = (uint32_t)fat_open(filename);
        break;
    }

    case SYSCALL_CLOSE:
    {
        int fd = (int)regs->ebx;
        regs->eax = (uint32_t)fat_close(fd);
        break;
    }

    case SYSCALL_LIST:
    {
        const char *dir_path = (const char *)regs->esi;
        int index = (int)regs->ebx;
        char *name_buf = (char *)regs->ecx;
        uint32_t buf_size = regs->edx;
        uint32_t file_size;

        if (fat_list_entry(dir_path, index, name_buf, buf_size, &file_size) == 0)
        {
            regs->eax = file_size;
        }
        else
        {
            regs->eax = (uint32_t)-1;
        }
        break;
    }

    case SYSCALL_SOCKET_ACCEPT:
        regs->eax = (uint32_t)tcp_socket_accept();
        break;

    case SYSCALL_SOCKET_RECV:
    {
        int sockfd = (int)regs->ebx;
        uint8_t *buf = (uint8_t *)regs->ecx;
        uint32_t max_len = regs->edx;
        regs->eax = (uint32_t)tcp_socket_recv(sockfd, buf, max_len);
        break;
    }

    case SYSCALL_SOCKET_SEND:
    {
        int sockfd = (int)regs->ebx;
        const uint8_t *buf = (const uint8_t *)regs->ecx;
        uint32_t len = regs->edx;
        regs->eax = (uint32_t)tcp_socket_send(sockfd, buf, len);
        break;
    }

    case SYSCALL_SOCKET_CLOSE:
    {
        int sockfd = (int)regs->ebx;
        regs->eax = (uint32_t)tcp_socket_close(sockfd);
        break;
    }

    case SYSCALL_OPEN_WRITE:
    {
        const char *filename = (const char *)regs->ebx;
        int mode = (int)regs->ecx;
        regs->eax = (uint32_t)fat_open_write(filename, mode);
        break;
    }

    case SYSCALL_FWRITE:
    {
        int fd = (int)regs->ebx;
        const uint8_t *buf = (const uint8_t *)regs->ecx;
        uint32_t len = regs->edx;
        regs->eax = (uint32_t)fat_write(fd, buf, len);
        break;
    }

    case SYSCALL_MKDIR:
    {
        const char *path = (const char *)regs->ebx;
        regs->eax = (uint32_t)fat_mkdir(path);
        break;
    }

    case SYSCALL_SEEK:
    {
        int fd = (int)regs->ebx;
        uint32_t offset = regs->ecx;
        regs->eax = (uint32_t)fat_seek(fd, offset);
        break;
    }

    case SYSCALL_DNS_RESOLVE:
    {
        const char *hostname = (const char *)regs->ebx;
        uint8_t *out_ip = (uint8_t *)regs->ecx;
        regs->eax = (uint32_t)dns_resolve(hostname, out_ip);
        break;
    }

    case SYSCALL_RTC_READ:
    {
        struct rtc_time *out = (struct rtc_time *)regs->ebx;
        rtc_read(out);
        regs->eax = 0;
        break;
    }

    case SYSCALL_MOUSE_READ:
    {
        struct mouse_packet *out = (struct mouse_packet *)regs->ebx;
        *out = mouse_read_packet();
        regs->eax = 0;
        break;
    }

    case SYSCALL_EXIT:
        task_exit(); // never returns — removes this task and switches away
        break;

    default:
        terminal_writestring("syscall: unknown call number\n");
        break;
    }
}

void syscall_install(void)
{
    // 0xEE = present, DPL 3, interrupt gate — DPL 3 lets ring 3 code invoke this
    idt_set_gate(0x80, (uint32_t)syscall_stub, 0x08, 0xEE);
}