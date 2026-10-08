#define _POSIX_C_SOURCE 200809L
#include "qa/platform_services.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#if !defined(_WIN32)
#include <unistd.h>
#endif

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); \
        exit(EXIT_FAILURE); \
    } \
} while (0)

#if !defined(_WIN32)
static bool between(const qa_platform_timespec *value, const struct timespec *first,
                    const struct timespec *last)
{
    return (value->seconds > first->tv_sec ||
        (value->seconds == first->tv_sec && value->nanoseconds >= first->tv_nsec)) &&
        (value->seconds < last->tv_sec ||
        (value->seconds == last->tv_sec && value->nanoseconds <= last->tv_nsec));
}

static void clock_matches(qa_platform_clock_kind kind, clockid_t native)
{
    struct timespec first, last;
    qa_platform_timespec value;
    qa_error error = {0};
    CHECK(!clock_gettime(native, &first));
    CHECK(qa_platform_clock_read(kind, &value, &error));
    CHECK(!clock_gettime(native, &last));
    CHECK(between(&value, &first, &last));
}
#endif

static void clocks(void)
{
    qa_error error = {0};
    qa_platform_timespec realtime;
    uint64_t first = qa_platform_time_ns();
    CHECK(qa_platform_clock_read(QA_PLATFORM_CLOCK_REALTIME, &realtime, &error));
    CHECK(realtime.nanoseconds >= 0 && realtime.nanoseconds < 1000000000);
    CHECK(qa_platform_time_ns() >= first);
    int64_t counter, frequency;
    CHECK(qa_platform_performance_read(&counter, &frequency, &error));
    CHECK(counter >= 0 && frequency > 0);
    CHECK(qa_platform_tick_ms() * 1000000 == 1);
#if !defined(_WIN32)
    clock_matches(QA_PLATFORM_CLOCK_REALTIME, CLOCK_REALTIME);
    clock_matches(QA_PLATFORM_CLOCK_MONOTONIC, CLOCK_MONOTONIC);
#if defined(CLOCK_PROCESS_CPUTIME_ID)
    clock_matches(QA_PLATFORM_CLOCK_PROCESS_CPU, CLOCK_PROCESS_CPUTIME_ID);
#endif
#if defined(CLOCK_THREAD_CPUTIME_ID)
    clock_matches(QA_PLATFORM_CLOCK_THREAD_CPU, CLOCK_THREAD_CPUTIME_ID);
#endif
#if defined(CLOCK_MONOTONIC_RAW)
    clock_matches(QA_PLATFORM_CLOCK_MONOTONIC_RAW, CLOCK_MONOTONIC_RAW);
#endif
    CHECK(frequency == 1000000000);
    CHECK(qa_platform_clock_ticks_per_second() == sysconf(_SC_CLK_TCK));
#endif
}

static void calendars(void)
{
    qa_platform_calendar_fields date;
    qa_error error = {0};
    CHECK(qa_platform_calendar(0, false, &date, &error));
    CHECK(date.year == 1970 && date.month == 1 && date.day == 1);
    CHECK(date.weekday == 4 && date.yearday == 0 && date.hour == 0);
    CHECK(date.minute == 0 && date.second == 0 && date.millisecond == 0);
    CHECK(date.unix_seconds == 0 && date.timezone_minutes == 0);
#if !defined(_WIN32)
    CHECK(qa_platform_calendar(-1, false, &date, &error));
    CHECK(date.year == 1969 && date.month == 12 && date.day == 31);
    CHECK(date.hour == 23 && date.minute == 59 && date.second == 59);
    CHECK(date.millisecond == 999 && date.unix_seconds == -1);
    CHECK(!setenv("TZ", "EST5EDT,M3.2.0/2,M11.1.0/2", 1));
    tzset();
    CHECK(qa_platform_calendar(INT64_C(1577836800123), true, &date, &error));
    CHECK(date.year == 2019 && date.month == 12 && date.day == 31 && date.hour == 19);
    CHECK(date.timezone_minutes == 300 && date.daylight == 0 && date.millisecond == 123);
    CHECK(qa_platform_calendar(INT64_C(1593561600456), true, &date, &error));
    CHECK(date.month == 6 && date.day == 30 && date.hour == 20);
    CHECK(date.timezone_minutes == 240 && date.daylight == 1 && date.millisecond == 456);
    char text[64];
    CHECK(qa_platform_calendar_format(INT64_C(1593561600456), "%Y-%m-%d %H:%M:%S", text, sizeof(text)));
    CHECK(!strcmp(text, "2020-06-30 20:00:00"));
#endif
}

static void entropy(void)
{
    qa_error error = {0};
    uint8_t bytes[34];
    memset(bytes, 0xa5, sizeof(bytes));
    CHECK(qa_platform_entropy(NULL, 0, &error));
    CHECK(qa_platform_entropy(bytes + 1, sizeof(bytes) - 2, &error));
    CHECK(bytes[0] == 0xa5 && bytes[sizeof(bytes) - 1] == 0xa5);
    uint64_t value;
    CHECK(qa_platform_entropy_u64(&value, &error));
}

int main(void)
{
    clocks();
    calendars();
    entropy();
    return EXIT_SUCCESS;
}
