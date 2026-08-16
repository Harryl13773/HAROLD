/*
CMOS Real-Time Clock driver: reads the current wall-clock date/time from the MC146818-compatible
RTC via ports 0x70 (index) / 0x71 (data) — no interrupts, polled on demand, same as this
project's other simple hardware reads (ATA sector reads, PCI config space).
*/

#include <stdint.h>
#include "io.h"
#include "rtc.h"

#define CMOS_INDEX 0x70
#define CMOS_DATA 0x71

#define CMOS_REG_SECONDS 0x00
#define CMOS_REG_MINUTES 0x02
#define CMOS_REG_HOURS 0x04
#define CMOS_REG_DAY 0x07
#define CMOS_REG_MONTH 0x08
#define CMOS_REG_YEAR 0x09
#define CMOS_REG_STATUS_A 0x0A
#define CMOS_REG_STATUS_B 0x0B

// Reads one CMOS register by index
static uint8_t cmos_read(uint8_t reg)
{
    outb(CMOS_INDEX, reg);
    return inb(CMOS_DATA);
}

// RTC Status A bit 7 indicates an update in progress, so time registers may be inconsistent
static int cmos_update_in_progress(void)
{
    return cmos_read(CMOS_REG_STATUS_A) & 0x80;
}

// Converts a BCD byte (each nibble a decimal digit) to plain binary
static uint8_t bcd_to_bin(uint8_t bcd)
{
    return (uint8_t)((bcd & 0x0F) + ((bcd >> 4) * 10));
}

// Raw register values, before BCD/12-hour correction
struct cmos_snapshot
{
    uint8_t second, minute, hour, day, month, year;
};

// Reads every time field in one pass, for comparison against a second read
static void cmos_read_snapshot(struct cmos_snapshot *s)
{
    s->second = cmos_read(CMOS_REG_SECONDS);
    s->minute = cmos_read(CMOS_REG_MINUTES);
    s->hour = cmos_read(CMOS_REG_HOURS);
    s->day = cmos_read(CMOS_REG_DAY);
    s->month = cmos_read(CMOS_REG_MONTH);
    s->year = cmos_read(CMOS_REG_YEAR);
}

// True if two snapshots agree on every field, meaning the RTC wasn't ticking mid-read
static int snapshots_equal(const struct cmos_snapshot *a, const struct cmos_snapshot *b)
{
    return a->second == b->second && a->minute == b->minute && a->hour == b->hour &&
           a->day == b->day && a->month == b->month && a->year == b->year;
}

// Reads the RTC date/time, handling BCD, 12-hour mode, and update races
void rtc_read(struct rtc_time *out)
{
    // Read repeatedly until two RTC snapshots match to avoid torn timestamps
    struct cmos_snapshot current;

    while (cmos_update_in_progress())
    {
    }
    cmos_read_snapshot(&current);

    for (;;)
    {
        struct cmos_snapshot next;

        while (cmos_update_in_progress())
        {
        }
        cmos_read_snapshot(&next);

        if (snapshots_equal(&current, &next))
        {
            break;
        }
        current = next;
    }

    uint8_t status_b = cmos_read(CMOS_REG_STATUS_B);

    if (!(status_b & 0x04)) // bit 2 clear means BCD mode — the common default, so convert every field
    {
        current.second = bcd_to_bin(current.second);
        current.minute = bcd_to_bin(current.minute);
        current.hour = (uint8_t)(bcd_to_bin(current.hour & 0x7F) | (current.hour & 0x80)); // keep the PM bit intact
        current.day = bcd_to_bin(current.day);
        current.month = bcd_to_bin(current.month);
        current.year = bcd_to_bin(current.year);
    }

    if (!(status_b & 0x02) && (current.hour & 0x80)) // bit 1 clear means 12-hour mode, and PM bit set
    {
        current.hour = (uint8_t)(((current.hour & 0x7F) + 12) % 24);
    }
    else
    {
        current.hour &= 0x7F;
    }

    out->second = current.second;
    out->minute = current.minute;
    out->hour = current.hour;
    out->day = current.day;
    out->month = current.month;

    // CMOS stores a 2-digit year; assume 2000+ for this project
    out->year = (uint16_t)(2000 + current.year);
}
