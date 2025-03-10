/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#define _GNU_SOURCE // for RTLD_NEXT
#include <anjay_init.h>

#include <dlfcn.h>
#include <time.h>

#include <avsystem/commons/avs_list.h>
#include <avsystem/commons/avs_unit_test.h>

#include <anjay_modules/anjay_time_defs.h>

#include "tests/utils/mock_clock.h"

static avs_time_monotonic_t MOCK_CLOCK = { { 0, -1 } };

void _anjay_mock_clock_start(const avs_time_monotonic_t t) {
    const avs_time_monotonic_t last_value = MOCK_CLOCK;
    MOCK_CLOCK = AVS_TIME_MONOTONIC_INVALID;
    AVS_UNIT_ASSERT_FALSE(avs_time_monotonic_valid(last_value));
    AVS_UNIT_ASSERT_TRUE(avs_time_monotonic_valid(t));
    MOCK_CLOCK = t;
}

void _anjay_mock_clock_reset(const avs_time_monotonic_t t) {
    _anjay_mock_clock_finish();
    _anjay_mock_clock_start(t);
}

void _anjay_mock_clock_advance(const avs_time_duration_t t) {
    AVS_UNIT_ASSERT_TRUE(avs_time_monotonic_valid(MOCK_CLOCK));
    AVS_UNIT_ASSERT_TRUE(avs_time_duration_valid(t));
    MOCK_CLOCK = avs_time_monotonic_add(MOCK_CLOCK, t);
}

void _anjay_mock_clock_finish(void) {
    AVS_UNIT_ASSERT_TRUE(avs_time_monotonic_valid(MOCK_CLOCK));
    MOCK_CLOCK = AVS_TIME_MONOTONIC_INVALID;
}

static int (*orig_clock_gettime)(clockid_t, struct timespec *);

AVS_UNIT_GLOBAL_INIT(verbose) {
    (void) verbose;
    typedef int (*clock_gettime_t)(clockid_t, struct timespec *);
    orig_clock_gettime =
            (clock_gettime_t) (intptr_t) dlsym(RTLD_NEXT, "clock_gettime");
}

int clock_gettime(clockid_t clock, struct timespec *t);
int clock_gettime(clockid_t clock, struct timespec *t) {
    if (avs_time_monotonic_valid(MOCK_CLOCK)) {
        // all clocks are equivalent for our purposes, so ignore clock
        t->tv_sec = (time_t) MOCK_CLOCK.since_monotonic_epoch.seconds;
        t->tv_nsec = MOCK_CLOCK.since_monotonic_epoch.nanoseconds;
        return 0;
    } else {
        return orig_clock_gettime(clock, t);
    }
}
