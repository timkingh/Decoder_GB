
INCLUDE = -I../../system/Include -I../../../Global_Include   -I./../Include

ifeq ($(PLATFORM),Hi3520)
INCLUDE += -I../../../platform/hi3520/include/
CFLAGSARM = -O2 -Wall -march=armv4 -mtune=arm9tdmi -rdynamic -DENABLE_ARM=1 -DHi3520 -g
CC=arm-hismall-linux-gcc
CPP=arm-hismall-linux-g++
AR=arm-hismall-linux-ar
STRIP=arm-hismall-linux-strip
LIB_SDK = mpi _VoiceEngine _amr_spc _amr_fipop _aec _aacdec _aacenc resampler loadbmp pciv tde parted ext2fs uuid AOnvifClient ssl crypto dl ACgiProtocolLayer_Hi3520 AgbProtocolLayer mxml osip2 osipparser2
SDKLIBDIR=SDKLib3520
ST = -static
endif

ifeq ($(PLATFORM),Hi3531)
INCLUDE += -I../../../platform/hi3531/include/
CFLAGSARM = -O2 -Wall -Wno-strict-aliasing -march=armv7-a -mcpu=cortex-a9 -mfloat-abi=softfp -mfpu=vfpv3-d16 -pthread -D_FILE_OFFSET_BITS=64 -DENABLE_ARM=1 -DHi3531 -g -static
CC=arm-hisiv100nptl-linux-gcc
CPP=arm-hisiv100nptl-linux-g++
AR=arm-hisiv100nptl-linux-ar
STRIP=arm-hisiv100nptl-linux-strip
LIB_SDK = mpi VoiceEngine anr hdmi aec jpeg  resampler pciv tde parted ext2fs uuid AOnvifClient ssl crypto ACgiProtocolLayer_Hi3531 AgbProtocolLayer mxml osip2 osipparser2
SDKLIBDIR=SDKLib3531
ST = -static
endif

ifeq ($(PLATFORM),Hi3535)
INCLUDE += -I../../../platform/hi3535/include/
CFLAGSARM = -O2 -Wall -Wno-strict-aliasing -march=armv7-a -mcpu=cortex-a9  -DENABLE_ARM=1 -DHi3535 -g -ffunction-sections
CC=arm-hisiv100nptl-linux-gcc
CPP=arm-hisiv100nptl-linux-g++
AR=arm-hisiv100nptl-linux-ar
STRIP=arm-hisiv100nptl-linux-strip
LIB_SDK = mpi VoiceEngine ive vqe hdmi  jpeg  mem resampler pciv tde parted ext2fs uuid devmapper AOnvifClient ssl crypto ACgiProtocolLayer_Hi3531 AgbProtocolLayer mxml osip2 osipparser2
SDKLIBDIR=SDKLib3535
ST = -static
endif

CFLAGSI386 = -O2 -Wall -rdynamic -DENABLE_ARM=0
I386_CC = gcc

#CFLAGSARM += -D$(CONFIG_PRODUCT_TYPE)
CFLAGSI386 += -D$(CONFIG_PRODUCT_TYPE)
ARM_LIB_DIR = ../../LIB/ARM/$(CONFIG_PRODUCT_TYPE)
I386_LIB_DIR = ../../LIB/I386/$(CONFIG_PRODUCT_TYPE)
#ARM_TARGET_NAME = dvrapp_$(CONFIG_PRODUCT_TYPE)
ARM_TARGET_NAME = dvrapp

DEFINE_TYPE = $(shell echo $$CONFIG_PRODUCT_TYPE | sed 's/-/_/g')

ifeq ($(VENDOR),SN)
CFLAGSARM += -D$(DEFINE_TYPE)
CFLAGSI386 += -D$(DEFINE_TYPE)
else
   ifeq ($(vendortype),NVR)
       CFLAGSARM += -D$(shell echo $(DEFINE_TYPE) | sed 's/DEC/SN/g') -DBLANKDVR
       CFLAGSI386 += -D$(shell echo $$DEFINE_TYPE | sed 's/DEC/SN/g') -DBLANKDVR
   else
       CFLAGSARM += -D$(shell echo $(DEFINE_TYPE) | sed 's/ZT/SN/g') -DZTDVR
       CFLAGSI386 += -D$(shell echo $(DEFINE_TYPE) | sed 's/ZT/SN/g') -DZTDVR
   endif
endif