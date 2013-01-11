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
USING_PROFILING ?= 1
USING_INTERNAL_PROFILING ?= 0

OSKOS :=linux
FILES_PREFIX=
MALI_FILE_PREFIX := $(srctree)/drivers/gpu/arm/mali
KBUILDROOT =

# Add platform configuration file for Mali
#ifeq ($(CONFIG_ARCH_EXYNOS4),y)
MACHDIR  := $(srctree)/arch/arm/mach-exynos
MALICONFIGDIR :=$(MACHDIR)/include/mach/mali
#endif

ifeq ($(CONFIG_USING_MMU),y)
USING_MMU=1
endif

ifeq ($(CONFIG_MALI400_UMP),y)
USING_UMP=1
endif

ifeq ($(CONFIG_USING_MMU),y)
USING_MMU=1
endif

ifeq ($(CONFIG_USING_OS_MEMORY),y)
USING_OS_MEMORY=1
endif

ifeq ($(CONFIG_MALI400_INTERNAL_PROFILING),y)
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
OS_MEMORY_KERNEL_BUFFER_SIZE_IN_MB ?= 16
USING_PROFILING ?= 0
USING_INTERNAL_PROFILING ?= 0
TIMESTAMP ?= default
TARGET_PLATFORM ?= orion-m400
MALI_PP_SCHEDULER_FORCE_NO_JOB_OVERLAP ?= 0
MALI_PP_SCHEDULER_KEEP_SUB_JOB_STARTS_ALIGNED ?= 0
MALI_PP_SCHEDULER_FORCE_NO_JOB_OVERLAP_BETWEEN_APPS ?= 0
PROFILING_SKIP_PP_JOBS ?= 0
PROFILING_SKIP_PP_AND_GP_JOBS ?= 0
MALI_UPPER_HALF_SCHEDULING ?= 1


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

# Linux has its own mmap cleanup handlers (see mali_kernel_mem_mmu.c)
DEFINES += -DMALI_UKK_HAS_IMPLICIT_MMAP_CLEANUP

ifeq ($(USING_UMP),1)
	DEFINES += -DMALI_USE_UNIFIED_MEMORY_PROVIDER=1
	EXTRA_CFLAGS += -I$(MALI_FILE_PREFIX)/../ump/include
else
	DEFINES += -DMALI_USE_UNIFIED_MEMORY_PROVIDER=0
endif

# Target build file
obj-$(CONFIG_MALI400)		+= mali.o

# Use our defines when compiling
EXTRA_CFLAGS += $(DEFINES) -I$(MALI_FILE_PREFIX) -I$(MALI_FILE_PREFIX)/common -I$(MALI_FILE_PREFIX)/linux -I$(MALI_FILE_PREFIX)/platform -I$(MALI_FILE_PREFIX)/include

OSKFILES = \
	$(OSKOS)/mali_osk_irq.o \
	$(OSKOS)/mali_osk_wq.o \
	$(OSKOS)/mali_osk_low_level_mem.o \
	$(OSKOS)/mali_osk_wait_queue.o \
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
OSKFILES+=\
	$(FILES_PREFIX)$(OSKOS)/mali_osk_profiling.o
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
	common/mali_group.o \
	common/mali_dlbu.o \
	common/mali_broadcast.o \
	common/mali_pm.o \
	common/mali_pmu.o \
	common/mali_user_settings_db.o \
        common/mali_kernel_utilization.o \
	common/mali_l2_cache.o \
	linux/mali_osk_pm.o \
	linux/mali_pmu_power_up_down.o \
	__malidrv_build_info.o \
	$(MALI_PLATFORM_FILE) \
	$(OSKFILES) \
	$(UKKFILES)


ifeq ($(USING_INTERNAL_PROFILING),1)
mali-y += \
	linux/mali_osk_profiling_internal.o \
	timestamp-$(TIMESTAMP)/mali_timestamp.o
EXTRA_CFLAGS += -I$(MALI_FILE_PREFIX)/timestamp-$(TIMESTAMP)
endif

mali-$(CONFIG_DMA_SHARED_BUFFER) += \
	linux/mali_dma_buf.o

mali-$(CONFIG_SYNC) += \
	linux/mali_sync.o \
	linux/mali_sync_user.o


# Get subversion revision number, fall back to only ${MALI_RELEASE_NAME} if no svn info is available
MALI_RELEASE_NAME=$(shell cat $(MALI_FILE_PREFIX)/.version 2> /dev/null)

SVN_INFO = (cd $(MALI_FILE_PREFIX); (svn info || git svn info || \
        echo -e "\nURL: $(MALI_RELEASE_NAME)\n" \
        "Last Changed Rev: $(MALI_RELEASE_NAME)\n" \
        "Last Changed Date: $(MALI_RELEASE_NAME)") 2>/dev/null)

SVN_REV := $(shell (cd $(MALI_FILE_PREFIX); echo "$(SVN_INFO)" | grep '^Revision: '| sed -e 's/^Revision: //' ) 2>/dev/null )
ifeq ($(SVN_REV),)
SVN_REV := $(MALI_RELEASE_NAME)
else
SVN_REV := $(MALI_RELEASE_NAME)-r$(SVN_REV)
endif

DEFINES += -DSVN_REV=$(SVN_REV)
DEFINES += -DSVN_REV_STRING=\"$(SVN_REV)\"

VERSION_STRINGS :=
VERSION_STRINGS += API_VERSION=$(shell cd $(MALI_FILE_PREFIX); grep "\#define _MALI_API_VERSION" $(FILES_PREFIX)include/linux/mali/mali_utgard_uk_types.h | cut -d' ' -f 3 )
VERSION_STRINGS += REPO_URL=$(shell $(SVN_INFO) | grep '^URL: ' | cut -d: -f2- | cut -b2-)
VERSION_STRINGS += REVISION=$(SVN_REV)
VERSION_STRINGS += CHANGED_REVISION=$(shell $(SVN_INFO) | grep '^Last Changed Rev: ' | cut -d: -f2- | cut -b2-)
VERSION_STRINGS += CHANGE_DATE=$(shell $(SVN_INFO) | grep '^Last Changed Date: ' | cut -d: -f2- | cut -b2-)
VERSION_STRINGS += BUILD_DATE=$(shell date)
ifdef CONFIG_MALI400_DEBUG
VERSION_STRINGS += BUILD=debug
else
VERSION_STRINGS += BUILD=release
endif
VERSION_STRINGS += TARGET_PLATFORM=$(TARGET_PLATFORM)
VERSION_STRINGS += MALI_PLATFORM=$(MALI_PLATFORM)
VERSION_STRINGS += OS_MEMORY_KERNEL_BUFFER_SIZE_IN_MB=$(OS_MEMORY_KERNEL_BUFFER_SIZE_IN_MB)
VERSION_STRINGS += USING_UMP=$(CONFIG_MALI400_UMP)
VERSION_STRINGS += USING_PROFILING=$(CONFIG_MALI400_PROFILING)
VERSION_STRINGS += USING_INTERNAL_PROFILING=$(CONFIG_MALI400_INTERNAL_PROFILING)
VERSION_STRINGS += USING_GPU_UTILIZATION=$(USING_GPU_UTILIZATION)
VERSION_STRINGS += MALI_UPPER_HALF_SCHEDULING=$(MALI_UPPER_HALF_SCHEDULING)

# Create file with Mali driver configuration
$(MALI_FILE_PREFIX)/__malidrv_build_info.c:
	@echo 'const char *__malidrv_build_info(void) { return "malidrv: $(VERSION_STRINGS)";}' > $(MALI_FILE_PREFIX)/__malidrv_build_info.c
