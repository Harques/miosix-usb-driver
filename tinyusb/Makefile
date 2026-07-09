##
## Makefile for tinyusb Miosix integration
## This makefile builds libtinyusb.a
##

## KPATH and CONFPATH are forwarded by the parent Makefile
MAKEFILE_VERSION := 3.01
include $(KPATH)/Makefile.kcommon

## List of all tinyusb source files (both .c and .cpp)
## These files will end up in libtinyusb.a
SRC := \
    tinyusb/src/host/hub.c \
    tinyusb/src/host/usbh.c \
    tinyusb/src/portable/synopsys/dwc2/dwc2_common.c \
    tinyusb/src/portable/synopsys/dwc2/hcd_dwc2.c \
    tinyusb/src/portable/synopsys/dwc2/dcd_dwc2.c \
    tinyusb/src/common/tusb_fifo.c \
    tinyusb/src/class/cdc/cdc_rndis_host.c \
    tinyusb/src/class/cdc/cdc_host.c \
    tinyusb/src/class/msc/msc_host.c \
    tinyusb/src/class/hid/hid_host.c \
    tinyusb/src/class/vendor/vendor_host.c \
    tinyusb/src/tusb.c \
    tusb_os_custom.cpp

INCLUDE_DIRS := -I. -I./cmsis_stubs -I./tinyusb/src

CFLAGS   += -fno-strict-aliasing -Wno-address-of-packed-member
CXXFLAGS += -fno-strict-aliasing -Wno-address-of-packed-member

all: $(OBJ)
	$(ECHO) "[AR  ] libtinyusb.a"
	$(Q)$(AR) rcs libtinyusb.a $(OBJ)

clean:
	-rm -f $(OBJ) $(OBJ:.o=.d) libtinyusb.a

-include $(OBJ:.o=.d)
