oplus_bsp_fpga_monitor-y := fpga_mid.o
oplus_bsp_fpga_monitor-y += fpga_monitor.o
oplus_bsp_fpga_monitor-y += fpga_exception.o
oplus_bsp_fpga_monitor-y += fpga_proc.o
obj-m	+= oplus_bsp_fpga_monitor.o

ccflags-y += -DFPGA_TRACE_INCLUDE_PATH=$(srctree)/$(src)
