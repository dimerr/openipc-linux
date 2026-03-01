vendor_soc_family_name := $(subst $\",,$(CONFIG_SOC_FAMILY))
obj-y += mach_$(vendor_soc_family_name).o
obj-$(CONFIG_SMP)	+= platsmp.o
obj-$(CONFIG_PM)	+= pm.o
