// Public interface for the CMOS Real-Time Clock driver.

#ifndef RTC_H
#define RTC_H

#include <stdint.h>

// Wall-clock time read from the CMOS RTC, converted from BCD/12-hour format
struct rtc_time
{
    uint16_t year;  // full 4-digit year, e.g. 2026
    uint8_t month;  // 1-12
    uint8_t day;    // 1-31
    uint8_t hour;   // 0-23
    uint8_t minute; // 0-59
    uint8_t second; // 0-59
};

// Reads the RTC date/time, handling BCD, 12-hour mode, and update races
void rtc_read(struct rtc_time *out);

#endif
