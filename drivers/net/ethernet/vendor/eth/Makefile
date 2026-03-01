#
# Makefile for the huanglong hleth device drivers.
#

ccflags-y += -I$(srctree)/include/linux/
KBUILD_CFLAGS   += -Werror
ifeq ($(CONFIG_SOCT_DRV_BUILD_KO),y)
obj-m += hl_eth.o
else
obj-y += hl_eth.o
endif
hl_eth-objs := mdio.o hleth.o phy.o autoeee.o
