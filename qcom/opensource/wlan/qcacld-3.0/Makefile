KERNEL_SRC ?= /lib/modules/$(shell uname -r)/build

# The Make variable $(M) must point to the directory that contains the module
# source code (which includes this Makefile). It can either be an absolute or a
# relative path. If it is a relative path, then it must be relative to the
# kernel source directory (KERNEL_SRC). An absolute path can be obtained very
# easily through $(shell pwd). Generating a path relative to KERNEL_SRC is
# difficult and we accept some outside help by letting the caller override the
# variable $(M). Allowing a relative path for $(M) enables us to have the build
# system put output/object files (.o, .ko.) into a directory different from the
# module source directory.
M ?= $(shell pwd)

ifeq ($(WLAN_ROOT),)

ifeq ($(WLAN_ROOT_MODULE_SOURCE_PATH),)
# WLAN_ROOT must contain an absolute path (i.e. not a relative path)
KBUILD_OPTIONS := WLAN_ROOT=$(shell cd $(KERNEL_SRC); readlink -e $(M))
else
# Honor what is passed from Make variable $(M), which could be a relative path.
KBUILD_OPTIONS := WLAN_ROOT=$(M)
endif

# MODNAME should be qca_cld3_wlan for helium based wear target
ifeq (qca_cld3, $(WLAN_WEAR_CHIPSET))
KBUILD_OPTIONS += MODNAME?=$(WLAN_WEAR_CHIPSET)_wlan
else
KBUILD_OPTIONS += MODNAME?=wlan
endif

WLAN_CHIPSET := qca_cld3
LOCAL_DEV_NAME := $(patsubst .%,%,\
	$(lastword $(strip $(subst /, ,$(M)))))

ifneq ($(filter qcacld-3.0,$(LOCAL_DEV_NAME)),)
WLAN_VARIANT := peach_v2
WLAN_MODULE_ROOT := $(M)
else
WLAN_VARIANT := $(LOCAL_DEV_NAME)
WLAN_MODULE_ROOT := $(patsubst %/,%,$(dir $(M)))
endif

WLAN_CONFIG_PROFILE := $(subst _,-,$(WLAN_VARIANT))

KBUILD_EXTRA := \
    CONFIG_IPA_OUT_OF_TREE=y \
    CONFIG_QCA_CLD_WLAN_PROFILE=sun_gki_$(WLAN_CONFIG_PROFILE) \
    WLAN_PROFILE=$(WLAN_VARIANT) \
    DYNAMIC_SINGLE_CHIP=$(WLAN_VARIANT) \
    MODNAME=$(WLAN_CHIPSET)_$(WLAN_VARIANT) \
    DEVNAME=$(WLAN_VARIANT)
KBUILD_OPTIONS += \
    KBUILD_EXTRA_SYMBOLS=$(WLAN_MODULE_ROOT)/../../dataipa/drivers/platform/msm/Module.symvers \
    KBUILD_EXTRA_SYMBOLS+=$(WLAN_MODULE_ROOT)/../platform/Module.symvers

#By default build for CLD
WLAN_SELECT := CONFIG_QCA_CLD_WLAN=m
KBUILD_OPTIONS += CONFIG_QCA_WIFI_ISOC=0
KBUILD_OPTIONS += CONFIG_QCA_WIFI_2_0=1
KBUILD_OPTIONS += $(WLAN_SELECT)
KBUILD_OPTIONS += $(KBUILD_EXTRA) # Extra config if any
endif

all:
	$(MAKE) -C $(KERNEL_SRC) M=$(M) modules $(KBUILD_OPTIONS)

modules_install:
	$(MAKE) INSTALL_MOD_STRIP=1 M=$(M) -C $(KERNEL_SRC) modules_install

clean:
	$(MAKE) -C $(KERNEL_SRC) M=$(M) clean $(KBUILD_OPTIONS)
