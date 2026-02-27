#
# Makefile for TMS devices
#

ccflags-y += -DTMS_DEBUGER_LOGGER=1 \
             -DTMS_DEBUGER_CHECKER=1 \
             -DCONFIG_TMS_NFC_DEVICE=1 \
             -DCONFIG_TMS_ESE_DEVICE=1

KBUILD_EXTRA_SYMBOLS := $(M)/../oplus_nfc/Module.symvers

obj-m                    += tms_device_modules.o
tms_device_modules-objs  := tms_common.o

# TMS NFC driver
tms_device_modules-objs += debuger/debuger.o \
                           debuger/logger.o \
                           debuger/checker.o

tms_device_modules-objs += nfc/nfc_common.o \
                           nfc/nfc_driver.o
# Guidev driver suport
#ifdef CONFIG_TMS_GUIDE_DEVICE
#tms_device_modules-objs += guidev/guide_driver.o
#endif

# TMS eSE driver
tms_device_modules-objs += ese/ese_common.o \
                           ese/ese_driver.o
