..
   Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
   AVSystem Anjay LwM2M SDK
   All rights reserved.

   Licensed under the AVSystem-5-clause License.
   See the attached LICENSE file for details.

Migrating from Anjay 3.3
========================

.. contents:: :local:

.. highlight:: c

Introduction
------------

Since Anjay 3.4.0 and avs_commons 5.4.0, time handling in avs_sched and avs_coap
has been refactored, which slightly redefined existing APIs.

These changes should not require any changes to user code in typical usage, but
may break some edge cases, especially on platforms where the system clock has a
low resolution and you are scheduling custom jobs through the avs_sched module.

Refactor of time handling in avs_sched and avs_coap
---------------------------------------------------

It is now enforced more strictly that time-based events shall happen when the
clock reaches *at least* the expected value. Previously, the tasks scheduled via
avs_sched were executed only when the clock reached a value *later* than the
scheduled job execution time.

This change will have no impact on your code if your platform has enough clock
resolution so that two subsequent calls to ``avs_time_real_now()`` or
``avs_time_monotonic_now()`` will *always* return different values. As a rule of
thumb, this should be the case if your clock has a resolution no worse than
about 1-2 orders of magnitude smaller than the CPU clock. For example, for a
100 MHz CPU, a clock resolution of around 100-1000 ns (i.e., 1-10 MHz) should be
sufficient, depending on the specific architecture.

If your clock has a lower resolution, you may observe the following changes:

* ``anjay_sched_run()`` is now properly guaranteed to execute at least one job
  if the time reported by ``anjay_sched_time_to_next()`` passed. Previously this
  could require waiting for another change of the numerical value of the clock,
  which could cause undesirable active waiting in the event loop. This is the
  motivating factor in introducing these changes.
* Jobs scheduled using ``AVS_SCHED_NOW()`` during an execution of
  ``anjay_sched_run()`` before the numerical value of the clock changes, *will*
  be executed during the same run. The previous behavior more strictly enforced
  the policy to not execute such jobs in the same run.

If you are scheduling custom jobs through the avs_sched module, you may want or
need to modify their logic accordingly to accommodate for these changes. In most
typical use cases, no changes are expected to be necessary.

