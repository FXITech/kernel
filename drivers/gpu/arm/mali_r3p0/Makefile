#
# Copyright (C) 2010-2012 ARM Limited. All rights reserved.
# 
# This program is free software and is provided to you under the terms of the GNU General Public License version 2
# as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
# 
# A copy of the licence is included with the program, and can also be obtained from Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
#

USE_UMPV2=0

OSKOS :=linux
FILES_PREFIX=
MALI_FILE_PREFIX := $(srctree)/drivers/gpu/arm/mali_r3p0
KBUILDROOT =

# Add platform configuration file for Mali
#ifeq ($(CONFIG_ARCH_EXYNOS4),y)
MACHDIR  := $(srctree)/arch/arm/mach-exynos
MALICONFIGDIR :=$(MACHDIR)/include/mach/mali
#endif

ifeq ($(CONFIG_USING_MMU),y)
USING_MMU=1
endif

ifeq ($(CONFIG_USING_UMP),y)
USING_UMP=1
endif

ifeq ($(CONFIG_USING_MMU),y)
USING_MMU=1
endif

ifeq ($(CONFIG_USING_OS_MEMORY),y)
USING_OS_MEMORY=1
endif

ifeq ($(CONFIG_MALI400_GPU_UTILIZATION),y)
USING_GPU_UTILIZATION =1
endif

ifeq ($(CONFIG_USING_MALI_RUN_TIME_PM),y)
USING_MALI_RUN_TIME_PM =1
endif

ifeq ($(CONFIG_USING_MALI_PMM_TESTSUITE),y)
USING_MALI_PMM_TESTSUITE=1
endif

ifeq ($(CONFIG_MALI400_PROFILING),y)
USING_PROFILING =1
endif

# set up defaults if not defined by the user
ARCH ?= arm
USING_MMU ?= 1
USING_UMP ?= 0
USING_OS_MEMORY ?= 0
USING_GPU_UTILIZATION ?= 0
USING_MALI_RUN_TIME_PM ?= 0
USING_MALI_PMM_TESTSUITE ?= 0
OS_MEMORY_KERNEL_BUFFER_SIZE_IN_MB ?= 6
USING_PROFILING ?= 0
USING_INTERNAL_PROFILING ?= 0
TIMESTAMP ?= default
TARGET_PLATFORM ?= orion-m400
MALI_PP_SCHEDULER_FORCE_NO_JOB_OVERLAP ?= 0
MALI_PP_SCHEDULER_KEEP_SUB_JOB_STARTS_ALIGNED ?= 0
MALI_PP_SCHEDULER_FORCE_NO_JOB_OVERLAP_BETWEEN_APPS ?= 0

ifeq ($(USING_UMP),1)
ifeq ($(USE_UMPV2),1)
	UMP_SYMVERS_FILE ?= ../umpv2/Module.symvers
else
	UMP_SYMVERS_FILE ?= ../ump/Module.symvers
endif
	KBUILD_EXTRA_SYMBOLS = $(KBUILD_EXTMOD)/$(UMP_SYMVERS_FILE)
endif

# Check if a Mali Core sub module should be enabled, true or false returned
submodule_enabled = $(shell gcc $(DEFINES) -E $(MALICONFIGDIR)/config.h | grep type | grep -c $(2))

# Set up our defines, which will be passed to gcc
DEFINES += -DUSING_OS_MEMORY=$(USING_OS_MEMORY)
DEFINES += -DUSING_MMU=$(USING_MMU)
DEFINES += -DUSING_UMP=$(USING_UMP)
DEFINES += -D_MALI_OSK_SPECIFIC_INDIRECT_MMAP
DEFINES += -DMALI_TIMELINE_PROFILING_ENABLED=$(USING_PROFILING)
DEFINES += -DMALI_INTERNAL_TIMELINE_PROFILING_ENABLED=$(USING_INTERNAL_PROFILING)
DEFINES += -DMALI_POWER_MGMT_TEST_SUITE=$(USING_MALI_PMM_TESTSUITE)
DEFINES += -DMALI_PMM_RUNTIME_JOB_CONTROL_ON=$(USING_MALI_RUN_TIME_PM)
DEFINES += -DMALI_PP_SCHEDULER_FORCE_NO_JOB_OVERLAP=$(MALI_PP_SCHEDULER_FORCE_NO_JOB_OVERLAP)
DEFINES += -DMALI_PP_SCHEDULER_KEEP_SUB_JOB_STARTS_ALIGNED=$(MALI_PP_SCHEDULER_KEEP_SUB_JOB_STARTS_ALIGNED)
DEFINES += -DMALI_PP_SCHEDULER_FORCE_NO_JOB_OVERLAP_BETWEEN_APPS=$(MALI_PP_SCHEDULER_FORCE_NO_JOB_OVERLAP_BETWEEN_APPS)

# MALI_STATE_TRACKING is only supported on Linux kernels from version 2.6.32.
DEFINES += -DMALI_STATE_TRACKING=1
DEFINES += -DMALI_STATE_TRACKING_USING_PROC=1
DEFINES += -DMALI_OS_MEMORY_KERNEL_BUFFER_SIZE_IN_MB=$(OS_MEMORY_KERNEL_BUFFER_SIZE_IN_MB)

ifneq ($(call submodule_enabled, $M, PMU),0)
        MALI_PLATFORM_FILE = platform/mali400-pmu/mali_platform.o
else
        MALI_PLATFORM_FILE = platform/$(TARGET_PLATFORM)/mali_platform.o
endif

DEFINES += -DMALI_GPU_UTILIZATION=$(USING_GPU_UTILIZATION)

ifeq ($(CONFIG_MALI400_DEBUG),y)
DEFINES += -DDEBUG
endif
DEFINES += -DSVN_REV=$(SVN_REV)
DEFINES += -DSVN_REV_STRING=\"$(SVN_REV)\"

# Linux has its own mmap cleanup handlers (see mali_kernel_mem_mmu.c)
DEFINES += -DMALI_UKK_HAS_IMPLICIT_MMAP_CLEANUP

ifeq ($(USING_UMP),1)
	DEFINES += -DMALI_USE_UNIFIED_MEMORY_PROVIDER=1
	EXTRA_CFLAGS += -I$(MALI_FILE_PREFIX)/../ump/include
else
	DEFINES += -DMALI_USE_UNIFIED_MEMORY_PROVIDER=0
endif

# Target build file
obj-$(CONFIG_MALI400MP)		+= mali.o

# Use our defines when compiling
EXTRA_CFLAGS += $(DEFINES) -I$(MALI_FILE_PREFIX) -I$(MALI_FILE_PREFIX)/common -I$(MALI_FILE_PREFIX)/linux -I$(MALI_FILE_PREFIX)/platform -I$(MALI_FILE_PREFIX)/include


OSKFILES = \
	$(OSKOS)/mali_osk_irq.o \
	$(OSKOS)/mali_osk_wait_queue.o \
	$(OSKOS)/mali_osk_low_level_mem.o \
	$(OSKOS)/mali_osk_mali.o \
	$(OSKOS)/mali_osk_notification.o \
	$(OSKOS)/mali_osk_time.o \
	$(OSKOS)/mali_osk_timers.o

ifneq ($(CONFIG_UMP),y)
OSKFILES += $(OSKOS)/mali_osk_atomics.o \
            $(OSKOS)/mali_osk_locks.o \
            $(OSKOS)/mali_osk_math.o \
            $(OSKOS)/mali_osk_memory.o \
            $(OSKOS)/mali_osk_misc.o
endif

UKKFILES=\
        $(FILES_PREFIX)$(OSKOS)/mali_ukk_mem.o \
        $(FILES_PREFIX)$(OSKOS)/mali_ukk_gp.o \
        $(FILES_PREFIX)$(OSKOS)/mali_ukk_pp.o \
        $(FILES_PREFIX)$(OSKOS)/mali_ukk_core.o

ifeq ($(USING_PROFILING),1)
UKKFILES+=\
        $(FILES_PREFIX)$(OSKOS)/mali_ukk_profiling.o
endif

# For customer releases the Linux Device Drivers will be provided as ARM proprietary and GPL releases:
# The ARM proprietary product will only include the license/proprietary directory
# The GPL product will only include the license/gpl directory

ifeq ($(wildcard $(MALI_FILE_PREFIX)/linux/license/gpl/*),)
EXTRA_CFLAGS += -I$(MALI_FILE_PREFIX)/linux/license/proprietary
else
EXTRA_CFLAGS += -I$(MALI_FILE_PREFIX)/linux/license/gpl
endif

# Source files which always are included in a build
mali-y := \
	common/mali_kernel_core.o \
	linux/mali_kernel_linux.o \
	$(OSKOS)/mali_osk_indir_mmap.o \
	common/mali_kernel_descriptor_mapping.o \
	common/mali_session.o \
	common/mali_device_pause_resume.o \
	common/mali_kernel_vsync.o \
	linux/mali_ukk_vsync.o \
	linux/mali_kernel_sysfs.o \
	common/mali_mmu.o \
	common/mali_mmu_page_directory.o \
	common/mali_memory.o \
	common/mali_kernel_memory_engine.o \
	common/mali_block_allocator.o \
	common/mali_kernel_mem_os.o \
	common/mali_mem_validation.o \
	common/mali_hw_core.o \
	common/mali_gp.o \
	common/mali_pp.o \
	common/mali_pp_job.o \
	common/mali_gp_job.o \
	common/mali_scheduler.o \
	common/mali_gp_scheduler.o \
	common/mali_pp_scheduler.o \
	common/mali_cluster.o \
	common/mali_group.o \
	common/mali_dlbu.o \
	common/mali_pm.o \
	common/mali_pmu.o \
	common/mali_user_settings_db.o \
	common/mali_l2_cache.o \
	$(OSKOS)/mali_osk_pm.o \
	linux/mali_kernel_pm.o \
	linux/mali_pmu_power_up_down.o \
	$(MALI_PLATFORM_FILE) \
	$(OSKFILES) \
	$(UKKFILES)

ifeq ($(USING_PROFILING),1)
mali-y += \
	linux/mali_osk_profiling_gator.o \
	timestamp-$(TIMESTAMP)/mali_timestamp.o
EXTRA_CFLAGS += -I$(MALI_FILE_PREFIX)/timestamp-$(TIMESTAMP)
endif

ifeq ($(USING_INTERNAL_PROFILING),1)
mali-y += \
	common/mali_osk_profiling_internal.o \
	timestamp-$(TIMESTAMP)/mali_timestamp.o
EXTRA_CFLAGS += -I$(MALI_FILE_PREFIX)/timestamp-$(TIMESTAMP)
endif


# Selecting files to compile by parsing the config file

ifeq ($(USING_GPU_UTILIZATION),1)
mali-y += \
	common/mali_kernel_utilization.o
endif

ifneq ($(call submodule_enabled, $M, MALI400PP),0)
        # Mali-400 PP in use
	EXTRA_CFLAGS += -DUSING_MALI400
endif

ifneq ($(call submodule_enabled, $M, MALI300PP),0)
        # Mali-400 PP in use
	EXTRA_CFLAGS += -DUSING_MALI400
endif

ifneq ($(call submodule_enabled, $M, MALI200),0)
        # Mali200 in use
	EXTRA_CFLAGS += -DUSING_MALI200
endif
