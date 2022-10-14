obj-$(CONFIG_CRYPTO_DEV_DWC) := crypto-dwc.o

ccflags-y += -I$(src)

crypto-dwc-objs := pdu.o pdu_get_version.o

ifneq ($(CONFIG_CRYPTO_DEV_DWC_PKA), )
crypto-dwc-objs += class.o pka.o elppka.o firmware.o
endif

ifneq ($(CONFIG_HW_RANDOM_DWC_CLP890), )
crypto-dwc-objs += clp890.o elpclp890.o
endif

