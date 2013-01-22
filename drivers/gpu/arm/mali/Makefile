#
# Copyright (C) 2010-2012 ARM Limited. All rights reserved.
# 
# This program is free software and is provided to you under the terms of the GNU General Public License version 2
# as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
# 
# A copy of the licence is included with the program, and can also be obtained from Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
#

ifeq ($(CONFIG_MALI400_UMP),y)
USING_UMP=1
endif

ifeq ($(CONFIG_MALI400_INTERNAL_PROFILING),y)
USING_INTERNAL_PROFILING = 1
endif

ifeq ($(CONFIG_MALI400_PROFILING),y)
USING_PROFILING =1
endif

ifeq ($(CONFIG_MALI400_USING_GPU_UTILIZATION),y)
USING_GPU_UTILIZATION =1
endif

# set up defaults if not defined by the user
USING_UMP ?= 0
USING_GPU_UTILIZATION ?= 0
OS_MEMORY_KERNEL_BUFFER_SIZE_IN_MB ?= 16
USING_PROFILING ?= 0
USING_INTERNAL_PROFILING ?= 0
TIMESTAMP ?= default
MALI_PP_SCHEDULER_FORCE_NO_JOB_OVERLAP ?= 0
MALI_PP_SCHEDULER_KEEP_SUB_JOB_STARTS_ALIGNED ?= 0
MALI_PP_SCHEDULER_FORCE_NO_JOB_OVERLAP_BETWEEN_APPS ?= 0
PROFILING_SKIP_PP_JOBS ?= 0
PROFILING_SKIP_PP_AND_GP_JOBS ?= 0
MALI_UPPER_HALF_SCHEDULING ?= 1


# MALI_STATE_TRACKING is only supported on Linux kernels from version 2.6.32.
DEFINES += -DMALI_STATE_TRACKING=1
DEFINES += -DMALI_OS_MEMORY_KERNEL_BUFFER_SIZE_IN_MB=$(OS_MEMORY_KERNEL_BUFFER_SIZE_IN_MB)

DEFINES += -DUSING_GPU_UTILIZATION=$(USING_GPU_UTILIZATION)
DEFINES += -DPROFILING_SKIP_PP_JOBS=$(PROFILING_SKIP_PP_JOBS)
DEFINES += -DPROFILING_SKIP_PP_AND_GP_JOBS=$(PROFILING_SKIP_PP_AND_GP_JOBS)

ifeq ($(MALI_UPPER_HALF_SCHEDULING),1)
	DEFINES += -DMALI_UPPER_HALF_SCHEDULING
endif

ifeq ($(CONFIG_MALI400_DEBUG),y)
DEFINES += -DDEBUG
endif

ifeq ($(USING_UMP),1)
	DEFINES += -DMALI_USE_UNIFIED_MEMORY_PROVIDER=1 -I$(src)/../ump/include
else
	DEFINES += -DMALI_USE_UNIFIED_MEMORY_PROVIDER=0
endif

# Target build file
obj-$(CONFIG_MALI400)		+= mali.o

MALI_RELEASE_NAME=$(shell cat $(src)/.version 2> /dev/null)

DEFINES += -DSVN_REV=$(MALI_RELEASE_NAME)
DEFINES += -DSVN_REV_STRING=\"$(MALI_RELEASE_NAME)\"

# Use our defines when compiling
EXTRA_CFLAGS += $(DEFINES) -I$(src) -I$(src)/common -I$(src)/linux -I$(src)/include

OSKFILES = \
	linux/mali_osk_irq.o \
	linux/mali_osk_wq.o \
	linux/mali_osk_low_level_mem.o \
	linux/mali_osk_wait_queue.o \
	linux/mali_osk_mali.o \
	linux/mali_osk_notification.o \
	linux/mali_osk_time.o \
	linux/mali_osk_timers.o \
	linux/mali_osk_atomics.o \
	linux/mali_osk_locks.o \
	linux/mali_osk_math.o \
	linux/mali_osk_memory.o \
	linux/mali_osk_misc.o

UKKFILES=\
        linux/mali_ukk_mem.o \
        linux/mali_ukk_gp.o \
        linux/mali_ukk_pp.o \
        linux/mali_ukk_core.o

ifeq ($(USING_PROFILING),1)
UKKFILES+=\
        linux/mali_ukk_profiling.o
OSKFILES+=\
	linux/mali_osk_profiling.o
endif

EXTRA_CFLAGS += -I$(src)/linux/license/gpl

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
	$(OSKFILES) \
	$(UKKFILES)


ifeq ($(USING_INTERNAL_PROFILING),1)
mali-y += \
	linux/mali_profiling_internal.o \
	timestamp-$(TIMESTAMP)/mali_timestamp.o
EXTRA_CFLAGS += -I$(src)/timestamp-$(TIMESTAMP)
endif

mali-$(CONFIG_DMA_SHARED_BUFFER) += \
	linux/mali_dma_buf.o

mali-$(CONFIG_SYNC) += \
	linux/mali_sync.o \
	linux/mali_sync_user.o


VERSION_STRINGS :=
VERSION_STRINGS += API_VERSION=$(shell cd $(src); grep "\#define _MALI_API_VERSION" include/linux/mali/mali_utgard_uk_types.h | cut -d' ' -f 3 )
VERSION_STRINGS += REVISION=$(SVN_REV)
VERSION_STRINGS += BUILD_DATE=$(shell date)
ifdef CONFIG_MALI400_DEBUG
VERSION_STRINGS += BUILD=debug
else
VERSION_STRINGS += BUILD=release
endif
VERSION_STRINGS += OS_MEMORY_KERNEL_BUFFER_SIZE_IN_MB=$(OS_MEMORY_KERNEL_BUFFER_SIZE_IN_MB)
VERSION_STRINGS += USING_UMP=$(CONFIG_MALI400_UMP)
VERSION_STRINGS += USING_PROFILING=$(CONFIG_MALI400_PROFILING)
VERSION_STRINGS += USING_INTERNAL_PROFILING=$(CONFIG_MALI400_INTERNAL_PROFILING)
VERSION_STRINGS += USING_GPU_UTILIZATION=$(USING_GPU_UTILIZATION)
VERSION_STRINGS += MALI_UPPER_HALF_SCHEDULING=$(MALI_UPPER_HALF_SCHEDULING)

# Create file with Mali driver configuration
$(src)/__malidrv_build_info.c:
	@echo 'const char *__malidrv_build_info(void) { return "malidrv: $(VERSION_STRINGS)";}' > $(src)/__malidrv_build_info.c
