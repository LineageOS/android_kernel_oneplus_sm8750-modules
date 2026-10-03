#
# Makefile for TMS devices
#
# Out-of-tree kbuild port of the vendor thn31 module.
#
# The in-tree driver source references get_nfc_id() from oplus_nfc.ko,
# so modpost needs the sibling oplus_nfc Module.symvers.
#

$(info   CONFIG_TMS_NFC_DEVICE = $(CONFIG_TMS_NFC_DEVICE))
$(info   CONFIG_TMS_ESE_DEVICE = $(CONFIG_TMS_ESE_DEVICE))
$(info   CONFIG_TMS_GUIDE_DEVICE = $(CONFIG_TMS_GUIDE_DEVICE))
TMS_DEVICES = $(findstring y, $(CONFIG_TMS_NFC_DEVICE) $(CONFIG_TMS_ESE_DEVICE))
ifneq ($(TMS_DEVICES), y)
TMS_DEVICES = $(findstring m, $(CONFIG_TMS_NFC_DEVICE) $(CONFIG_TMS_ESE_DEVICE))
endif
$(info   TMS_DEVICES = $(TMS_DEVICES))
ccflags-y += -DTMS_DEBUGER_LOGGER \
             -DTMS_DEBUGER_CHECKER

obj-m += tms_device_modules.o
tms_device_modules-objs  := tms_common.o

# thn31/nfc/nfc_driver.c calls get_nfc_id(), which oplus_nfc.ko exports.
# Use the same relative path convention as the other :kbuild modules in this tree.
KBUILD_EXTRA_SYMBOLS := $(M)/../oplus_nfc/Module.symvers

# TMS NFC driver

tms_device_modules-objs += debuger/debuger.o \
                           debuger/logger.o \
                           debuger/checker.o

ifdef CONFIG_TMS_NFC_DEVICE
tms_device_modules-objs += nfc/nfc_common.o \
                           nfc/nfc_driver.o
# Guidev driver suport
ifdef CONFIG_TMS_GUIDE_DEVICE
tms_device_modules-objs += guidev/guide_driver.o
endif
endif

# TMS eSE driver

ifdef CONFIG_TMS_ESE_DEVICE
tms_device_modules-objs += ese/ese_common.o \
                           ese/ese_driver.o
endif
