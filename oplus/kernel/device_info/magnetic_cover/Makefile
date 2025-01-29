#
# Makefile for the magnetic cover drivers.
#
oplus_magnetic_cover-y += magcvr_src/abstract/magnetic_cover_core.o
obj-m += oplus_magnetic_cover.o

oplus_magcvr_ak09973-y += magcvr_src/hardware/magcvr_ak09973.o
obj-m += oplus_magcvr_ak09973.o

oplus_magcvr_mxm1120-y += magcvr_src/hardware/magcvr_mxm1120.o
obj-m += oplus_magcvr_mxm1120.o

ccflags-y := -I$(srctree)/$(src)
