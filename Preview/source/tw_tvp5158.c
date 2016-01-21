#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "hi_comm_vi.h"
#include "hi_comm_vb.h"
#include "hi_comm_aio.h"
#include "mpi_ai.h"
#include "mpi_ao.h"
#if defined(Hi3535)
#include "acodec.h"
#endif

#include "prv_comm.h"
#include "prv_err.h"
#include "disp_api.h"
#include "tw2865.h"

static int tw2865_fd = -1;

#if defined(SN2004) || defined(SN2008)
#define TW2865_0_WRITE 0x50
#define TW2865_0_READ 0x51
#define TW2865_1_WRITE 0x52
#define TW2865_1_READ 0x53

static int WriteAsicByte(unsigned char IIC_Add, unsigned char reg, unsigned char data)
{
	tw2865_params tw_param = {0};
	int fd = tw2865_fd;
	char ret = 0;
	tw_param.bank = 0;
	tw_param.reg = reg;
	tw_param.num = 1;
	tw_param.buf = &data;
	tw_param.chip = IIC_Add;
	ret = ioctl(fd, tw2865_ioctl_write, &tw_param);

	return ret;
}
static int ReadAsicByte(unsigned char IIC_Add, unsigned char reg, unsigned char *data)
{
	tw2865_params tw_param = {0};
	int fd = tw2865_fd;
	char ret = 0;
	
	tw_param.bank = 0;
	tw_param.reg = reg;
	tw_param.num = 1;
	tw_param.buf = data;
	tw_param.chip = IIC_Add;
	ret = ioctl(fd, tw2865_ioctl_read, &tw_param);
	return ret;
}

int WriteAsicTable(unsigned char IIC_Add, unsigned char reg,unsigned char* data,unsigned char num)
{
	tw2865_params tw_param = {0};
	int fd = tw2865_fd;
	char ret = 0;
	
	tw_param.bank = 0;
	tw_param.reg = reg;
	tw_param.num = num;
	tw_param.buf = data;
	tw_param.chip = IIC_Add;
	ret = ioctl(fd, tw2865_ioctl_write, &tw_param);
	return ret;
}
int ReadAsicTable(unsigned char IIC_Add, unsigned char reg,unsigned char* data,unsigned char num)
{
	tw2865_params tw_param = {0};
	int fd = tw2865_fd;
	char ret = 0;
	
	tw_param.bank = 0;
	tw_param.reg = reg;
	tw_param.num = num;
	tw_param.buf = data;
	tw_param.chip = IIC_Add;
	ret = ioctl(fd, tw2865_ioctl_read, &tw_param);
	return ret;
}

#elif defined(SN2116LE) || defined(SN2116LS) || defined(SN2116LP) || defined(SN2116HE) || defined(SN2116HS) || defined(SN2116HP) //|| defined(SN6116HE)
#define MOD_DRV MOD_PRV  //输出消息转为MOD_PRV模块的

#define TW2865_0_WRITE 0
#define TW2865_0_READ 0
#define TW2865_0 0

#define TW2865_1_WRITE 1
#define TW2865_1_READ 1
#define TW2865_1 1

#define TW2865_2_WRITE 2
#define TW2865_2_READ 2
#define TW2865_2 2

#define TW2865_3_WRITE 3
#define TW2865_3_READ 3
#define TW2865_3 3

int tw2865_read_reg(unsigned int chip, unsigned char addr)
{
	int ret = 0;
	tw2865_video_reg video_reg;
	
	if (tw2865_fd < 0)
	{
		tw2865_fd = open(TW2865_DEVICE, O_RDWR);
		if (tw2865_fd < 0)
		{
			return -1;
		}
	}
	video_reg.chip = chip;
	video_reg.addr = addr;
	ret = ioctl(tw2865_fd, TW2865CMD_READ_REG, &video_reg);
	if (ret < 0)
	{
		TRACE(SCI_TRACE_HIGH, MOD_PRV, "read tw2865 reg failed!");
		return -1;
	}
	return video_reg.value;
}

int tw2865_write_reg(unsigned int chip, unsigned char addr, unsigned char value)
{
	int ret = 0;
	tw2865_video_reg video_reg;
	
	if (tw2865_fd < 0)
	{
		tw2865_fd = open(TW2865_DEVICE, O_RDWR);
		if (tw2865_fd < 0)
		{
			return -1;
		}
	}
	video_reg.chip = chip;
	video_reg.addr = addr;
	video_reg.value = value;
	ret = ioctl(tw2865_fd, TW2865CMD_WRITE_REG, &video_reg);
	if (ret < 0)
	{	
		TRACE(SCI_TRACE_HIGH, MOD_PRV, "write tw2865 reg failed!");
		return -1;
	}
	return 0;
}

int tw2865_read_reg2(unsigned int chip, unsigned char addr, unsigned char *data)
{
	int ret;

	if (NULL==data)
	{
		return -1;
	}
	ret = tw2865_read_reg(chip, addr);
	if (-1 == ret)
	{
		return -1;
	}
	*data = (unsigned char)ret;
	return 0;
}

int tw2865_write_table(unsigned int chip, unsigned char addr, unsigned char *data, unsigned char num)
{
	unsigned char i = 0;

	if (NULL == data)
	{
		return -1;
	}
	for (i=0; i<num; i++, addr++)
	{
		if(0 != tw2865_write_reg(chip, addr, data[i]))
		{
			return -1;
		}
	}
	return 0;
}
int tw2865_read_table(unsigned int chip, unsigned char addr, unsigned char *data, unsigned char num)
{
	unsigned char i = 0;

	for (i=0; i<num; i++, addr++)
	{
		if(0 != tw2865_read_reg2(chip, addr, data+i))
		{
			return -1;
		}
	}
	return 0;
}

#define WriteAsicByte	tw2865_write_reg
#define ReadAsicByte	tw2865_read_reg2
#define WriteAsicTable	tw2865_write_table
#define ReadAsicTable	tw2865_read_table
int tw2865_improve(unsigned char IIC)
{
	/*for sharpness*/
	WriteAsicByte(IIC, 0x03, 0x3f);
	WriteAsicByte(IIC, 0x13, 0x3f);
	WriteAsicByte(IIC, 0x23, 0x3f);
	WriteAsicByte(IIC, 0x33, 0x3f);

	/*for hue*/
	WriteAsicByte(IIC, 0x06, 0xfb);
	WriteAsicByte(IIC, 0x16, 0xfb);
	WriteAsicByte(IIC, 0x26, 0xfb);
	WriteAsicByte(IIC, 0x36, 0xfb);

	/*for contrast*/
	WriteAsicByte(IIC, 0x02, 0x7a);
	WriteAsicByte(IIC, 0x12, 0x7a);
	WriteAsicByte(IIC, 0x22, 0x7a);
	WriteAsicByte(IIC, 0x32, 0x7a);

	/*for vertical peaking level*/
	WriteAsicByte(IIC, 0xAF, 0x33);
	WriteAsicByte(IIC, 0xB0, 0x33);

	return 0;
}

#elif defined(SN2016) || defined(SN2016HS) || defined(SN2016HE)|| defined(SN2116V2) || defined(SN2016V2) || defined(SN6000) || defined(SN2008LE) || defined(SN8600)

#define MOD_DRV MOD_PRV  //输出消息转为MOD_PRV模块的

#define TW2865_0_WRITE 0
#define TW2865_0_READ 0
#define TW2865_0 0

#define TW2865_1_WRITE 1
#define TW2865_1_READ 1
#define TW2865_1 1

#define TW2865_2_WRITE 2
#define TW2865_2_READ 2
#define TW2865_2 2

#define TW2865_3_WRITE 3
#define TW2865_3_READ 3
#define TW2865_3 3
#if defined(SN6104) || defined(SN8604D) || defined(SN8604M) || defined(SN2008LE)
#define TLV320AIC3104 2
#define CODEC_MUTE_ADDR 04	//使用HP_OUT,LEFT作为音频输出，rigth作为GSM输出
#define REG_OFFSET	0x8080
#elif defined(SN6108) || defined(SN8608D) || defined(SN8608M)
#define TLV320AIC3104 4
#define CODEC_MUTE_ADDR 43
#define REG_OFFSET	0x80

#elif defined(Hi3531)||defined(Hi3535)
#define TLV320AIC3104 4
#define CODEC_MUTE_ADDR 7
#define REG_OFFSET	0x08

#else 
#define TLV320AIC3104 4
#define CODEC_MUTE_ADDR 43
#define REG_OFFSET	0x80
#endif
static int __tw2865_read_reg(unsigned int chip, unsigned char chn, unsigned char addr)
{
	int ret = 0;
	tw2865_video_reg video_reg;
	
	if (tw2865_fd < 0)
	{
		tw2865_fd = open(TW2865_DEVICE, O_RDWR);
		if (tw2865_fd < 0)
		{
			return -1;
		}
	}
	video_reg.chip = chip;
	video_reg.addr = addr;
	video_reg.chn = chn;
	ret = ioctl(tw2865_fd, TW2865CMD_READ_REG, &video_reg);
	if (ret < 0)
	{
		TRACE(SCI_TRACE_HIGH, MOD_PRV, "read tw2865 reg failed!");
		return -1;
	}
	return video_reg.value;
}

static int __tw2865_write_reg(unsigned int chip, unsigned char chn, unsigned char addr, unsigned char value)
{
	int ret = 0;
	tw2865_video_reg video_reg;
	
	if (tw2865_fd < 0)
	{
		tw2865_fd = open(TW2865_DEVICE, O_RDWR);
		if (tw2865_fd < 0)
		{
			return -1;
		}
	}
	video_reg.chip = chip;
	video_reg.addr = addr;
	video_reg.value = value;
	video_reg.chn = chn;
	ret = ioctl(tw2865_fd, TW2865CMD_WRITE_REG, &video_reg);
	if (ret < 0)
	{	
		TRACE(SCI_TRACE_HIGH, MOD_PRV, "write tw2865 reg failed!");
		return -1;
	}
	return 0;
}

static int __tw2865_read_reg2(unsigned int chip, unsigned char chn, unsigned char addr, unsigned char *data)
{
	int ret;

	if (NULL==data)
	{
		return -1;
	}
	ret = __tw2865_read_reg(chip, chn, addr);
	if (-1 == ret)
	{
		return -1;
	}
	*data = (unsigned char)ret;
	return 0;
}

static int __tw2865_write_reg2(unsigned int chip, unsigned char chn, unsigned char addr, unsigned int value)
{
	int ret = 0;
	tw2865_video_reg video_reg;
	
	if (tw2865_fd < 0)
	{
		tw2865_fd = open(TW2865_DEVICE, O_RDWR);
		if (tw2865_fd < 0)
		{
			return -1;
		}
	}
	video_reg.chip = chip;
	video_reg.addr = addr;
	video_reg.reg_val = value;
	video_reg.chn = chn;
	ret = ioctl(tw2865_fd, TW2865CMD_WRITE_REG, &video_reg);
	if (ret < 0)
	{	
		TRACE(SCI_TRACE_HIGH, MOD_PRV, "write tw2865 reg failed!");
		return -1;
	}
	return 0;
}

static int __tw2865_read_reg3(unsigned int chip, unsigned char chn, unsigned char addr, unsigned int *data)
{
	int ret = 0;
	tw2865_video_reg video_reg;
	
	if (tw2865_fd < 0)
	{
		tw2865_fd = open(TW2865_DEVICE, O_RDWR);
		if (tw2865_fd < 0)
		{
			return -1;
		}
	}
	video_reg.chip = chip;
	video_reg.addr = addr;
	video_reg.chn = chn;
	ret = ioctl(tw2865_fd, TW2865CMD_READ_REG, &video_reg);
	if (ret < 0)
	{
		TRACE(SCI_TRACE_HIGH, MOD_PRV, "read tw2865 reg failed!");
		return -1;
	}
	*data = video_reg.reg_val;
	return 0;
}

int tw2865_write_table(unsigned int chip, unsigned char addr, unsigned char *data, unsigned char num)
{
	unsigned char i = 0;

	if (NULL == data)
	{
		return -1;
	}
	for (i=0; i<num; i++, addr++)
	{
		if(0 != __tw2865_write_reg(chip, 0, addr, data[i]))
		{
			return -1;
		}
	}
	return 0;
}

int tw2865_read_table(unsigned int chip, unsigned char addr, unsigned char *data, unsigned char num)
{
	unsigned char i = 0;

	for (i=0; i<num; i++, addr++)
	{
		if(0 != __tw2865_read_reg2(chip, 0, addr, data+i))
		{
			return -1;
		}
	}
	return 0;
}

int tw2865_write_table2(unsigned int chip, unsigned char addr, unsigned int *data, unsigned char num)
{
	unsigned char i = 0;

	if (NULL == data)
	{
		return -1;
	}
	for (i=0; i<num; i++, addr++)
	{
		if(0 != __tw2865_write_reg2(chip, 0, addr, data[i]))
		{
			return -1;
		}
	}
	return 0;
}

int tw2865_read_table2(unsigned int chip, unsigned char addr, unsigned int *data, unsigned char num)
{
	unsigned char i = 0;

	for (i=0; i<num; i++, addr++)
	{
		if(0 != __tw2865_read_reg3(chip, 0, addr, data+i))
		{
			return -1;
		}
	}
	return 0;
}

int tw2865_write_reg(unsigned int chip, unsigned char addr, unsigned char value)
{
	return __tw2865_write_reg(chip, 0, addr, value);
}
int tw2865_read_reg(unsigned int chip, unsigned char addr)
{
	return __tw2865_read_reg(chip, 0, addr);
}
int tw2865_read_reg2(unsigned int chip, unsigned char addr, unsigned char *data)
{
	return __tw2865_read_reg2(chip, 0, addr, data);
}

int tw2865_write_reg3(unsigned int chip, unsigned char addr, unsigned int value)
{
	return __tw2865_write_reg2(chip, 0, addr, value);
}
int tw2865_read_reg3(unsigned int chip, unsigned char addr, unsigned int *data)
{
	return __tw2865_read_reg3(chip, 0, addr, data);
}

#define __WriteAsicByte	__tw2865_write_reg
#define __ReadAsicByte	__tw2865_read_reg2

#define WriteAsicByte	tw2865_write_reg
#define ReadAsicByte	tw2865_read_reg2
#define WriteAsicTable	tw2865_write_table
#define ReadAsicTable	tw2865_read_table

#endif


#if defined(SN2116LE) || defined(SN2116LS) || defined(SN2116LP) || defined(SN2116HE) || defined(SN2116HS) || defined(SN2116HP) //|| defined(SN6116HE)

/************************************************************************/
/* TW2865音频预览通道控制.
reg 0xdc [0~3]-ain1~4,[4]-pb:0-on,1-off    
mask[0~3]: 0-off, 1-on
*/
/************************************************************************/
int tw2865_ain_cfg(unsigned int chip, unsigned char mask)
{
	int ret;
	unsigned char reg;
	
	mask = ~mask;
	
	ret = ReadAsicByte(chip,0xdc,&reg);
	ret = WriteAsicByte(chip,0xdc,(mask&0x0f) | (reg&0xf0) );
	
	return ret;
}

/************************************************************************/
/* 主TW2865 第5路音频控制。
 第5路音频控制 reg 0x7e [5]:0-on,1-off    
       flag: 0-off, none0-on
                                                                */
/************************************************************************/
int tw2865_master_ain5_cfg(char flag)
{
	int ret;
	unsigned char reg;
 	
	ret = ReadAsicByte(TW2865_0,0x7e,&reg);
	if (0 == flag)	//off
	{
		ret = WriteAsicByte(TW2865_0,0x7e,reg|0x20);//set reg[5] to 1: off
	}
	else			//on
	{
		ret = WriteAsicByte(TW2865_0,0x7e,reg&0xdf);//set reg[5] to 0: on
	}
	return ret;
}
/************************************************************************/
/* 主TW2865 音频回放开关控制.
reg 0xdc [4]-pb:0-on,1-off    
       flag: 0-off, none0-on
                                                                     */
/************************************************************************/
int tw2865_master_pb_cfg(char flag)
{
	int ret;
	unsigned char reg;

	ret = ReadAsicByte(TW2865_0,0xdc,&reg);
	if (0 == flag)	//off
	{
		ret = WriteAsicByte(TW2865_0,0xdc,reg|0x10);//set reg[4] to 1: off
	}
	else			//on
	{
		ret = WriteAsicByte(TW2865_0,0xdc,reg&0xef);//set reg[4] to 0: on
	}
	return ret;
}
/************************************************************************/
/* 对讲输出与回放输出之间进行切换。
  flag: 0-对讲
		1-回放
 reg 0xdb [4] 0: 1st Left channel audio data(default),1: 1st Right channel audio data.
                                                                   */
/************************************************************************/
int tw2865_master_tk_pb_switch(char flag)
{
	int ret;
	unsigned char reg;

	ret = ReadAsicByte(TW2865_0,0xdb,&reg);
	if (0 == flag)
	{
		ret = WriteAsicByte(TW2865_0,0xdb,reg&0xef);
	}
	else if (1 == flag)
	{
		ret = WriteAsicByte(TW2865_0,0xdb,reg|0x10);
	}
	else
	{
		TRACE(SCI_TRACE_NORMAL, MOD_DRV, "tw2865_master_tk_pb_switch(char flag): bad flag:%d!!\n", flag);
		return -1;
	}
	return 0;
}

#elif defined(SN2016) || defined(SN2016HS) || defined(SN2016HE)	|| defined(SN2116V2) || defined(SN2016V2) || defined(SN6000) || defined(SN2008LE) || defined(SN8600)

/*音频预览通道控制.reg 0xc5 [0~3]-ain1~4,[4~7]-reserved  
mask[0~3]: 0-on, 1-mute*/
int tw2865_ain_cfg(unsigned int chip, unsigned char mask)
{
	unsigned char addr = 0xc5;
	return WriteAsicByte(chip, addr, 0x0f & ~mask);
}


/* flag: 0-off, none0-on */
int tw2865_master_ain5_cfg(char flag)
{
	return 0;
}

/*音频回放开关控制.*/
int tw2865_master_pb_cfg(char flag)
{
#if defined(SN6104) || defined(SN8604D) || defined(SN8604M) ||defined(SN2008LE)
	int ret;
	unsigned int reg;
	unsigned char chip = TLV320AIC3104;
	unsigned char addr = CODEC_MUTE_ADDR;/*Page 0/Register 43: Left-DAC Digital Volume Control Register*/
	TRACE(SCI_TRACE_NORMAL, MOD_DRV, "tw2865_master_pb_cfg ALC5627: chip = %d\n",chip);

	ret = tw2865_read_reg3(chip,addr,&reg);
	if (0 == flag)	//off
	{
		ret = tw2865_write_reg3(chip,addr,reg|REG_OFFSET);//set reg[7] to 1: off
	}
	else			//on
	{
		ret = tw2865_write_reg3(chip,addr,reg&(~REG_OFFSET));//set reg[7] to 0: on
	}
#elif defined(Hi3520)
	int ret;
	unsigned char reg;
	unsigned char chip = TLV320AIC3104;
	unsigned char addr = CODEC_MUTE_ADDR;/*Page 0/Register 43: Left-DAC Digital Volume Control Register*/
	TRACE(SCI_TRACE_NORMAL, MOD_DRV, "tw2865_master_pb_cfg TLV320AIC3104: chip = %d\n",chip);

	ret = ReadAsicByte(chip,addr,&reg);
	if (0 == flag)	//off
	{
		ret = WriteAsicByte(chip,addr,reg|REG_OFFSET);//set reg[7] to 1: off
	}
	else			//on
	{
		ret = WriteAsicByte(chip,addr,reg&(~REG_OFFSET));//set reg[7] to 0: on
	}

#elif defined(Hi3531)
	int ret;
	unsigned char reg;
	unsigned char chip = TLV320AIC3104;
	unsigned char addr = CODEC_MUTE_ADDR;/*Page 0/Register 43: Left-DAC Digital Volume Control Register*/
	TRACE(SCI_TRACE_NORMAL, MOD_DRV, "tw2865_master_pb_cfg TLV320AIC3104: chip = %d\n",chip);

	ret = ReadAsicByte(chip,addr,&reg);
	if (0 == flag)	//off
	{
	
		ret = WriteAsicByte(chip,addr,reg&(~REG_OFFSET));//set reg[3] to 0: off
	}
	else			//on
	{
		
		ret = WriteAsicByte(chip,addr,reg|REG_OFFSET);//set reg[7] to 1: on
	}
#elif defined(Hi3535)
	int ret;
	unsigned int value = 0;
	if(flag == 0)
	{
		ret = testmod_reg_rw(0, 0x20680000, 0x2000, &value);
		if(ret == 0)
		{
			value |= 0x3<<16;
			ret = testmod_reg_rw(1, 0x20680000, 0x2000, &value);
		}
		ret = testmod_reg_rw(0, 0x20680000, 0x2100, &value);
		if(ret == 0)
		{
			value |= 0x3<<16;
			ret = testmod_reg_rw(1, 0x20680000, 0x2100, &value);
		}
	}
	else
	{
		
		ret = testmod_reg_rw(0, 0x20680000, 0x2000, &value);
		if(ret == 0)
		{
			value &= ~(0x3<<16);
			ret = testmod_reg_rw(1, 0x20680000, 0x2000, &value);
		}
		ret = testmod_reg_rw(0, 0x20680000, 0x2100, &value);
		if(ret == 0)
		{
			value &= ~(0x3<<16);
			ret = testmod_reg_rw(1, 0x20680000, 0x2100, &value);
		}
	}
#endif
	return 0;
}

/* flag: 0-tk, 1-pb/prv */
int tw2865_master_tk_pb_switch(char flag)
{
	return 0;
}

#endif

/************************************************************************/
/* 初始化各TW2865芯片。参数sysmode未使用。
*/
/************************************************************************/
int tw2865Init(unsigned char sysmode)
{
	if (tw2865_fd < 0)
	{
		tw2865_fd = open(TW2865_DEVICE, O_RDWR);
		if (tw2865_fd < 0)
		{
			perror("open " TW2865_DEVICE " FAIL: ");
			return -1;
		}
	}


#if defined(SN2116LE) || defined(SN2116LS) || defined(SN2116LP) || defined(SN2116HE) || defined(SN2116HS) || defined(SN2116HP) //|| defined(SN6116HE)
	
	WriteAsicByte(TW2865_0_WRITE,0xe0,0x14);//音频输出: Audio Out: Mixed
	
	tw2865_master_ain5_cfg(0);//关闭第5路对讲音频
	tw2865_master_pb_cfg(0); //关闭回放音频
	
	tw2865_ain_cfg(TW2865_0, 0x00);//关闭第一片TW2865音频预览
	tw2865_ain_cfg(TW2865_1, 0x00);//关闭第二片TW2865音频预览
	tw2865_ain_cfg(TW2865_2, 0x00);//关闭第三片TW2865音频预览
	tw2865_ain_cfg(TW2865_3, 0x00);//关闭第四片TW2865音频预览
	
	tw2865_improve(TW2865_0);
	tw2865_improve(TW2865_1);
	tw2865_improve(TW2865_2);
	tw2865_improve(TW2865_3);
#elif defined(SN2016) || defined(SN2016HS) || defined(SN2016HE)	|| defined(SN2116V2) || defined(SN2016V2) || defined(SN6000) || defined(SN2008LE) || defined(SN8600)

	int i;
    unsigned char sharp = 0x0a;
    unsigned char opmode = 0x38;
	unsigned char Dec_count = 0x03;
	unsigned char Vpll_data = 0x06;
	unsigned char Max_fieled = 0x60;
	unsigned char Min_dur = 0x08;
	/* 0x60:  Operation Mode Control - Force TVP5157 TV mode 
			New V-Bit control algorithm (number of active lines per frame is constant as total LPF varies
	*/
	/* 0x19:  Luminance Processing Control 2 - picture sharpness */
	/* 0x88: F and V bits decoded from line count */
	/* 0x89: V-PLL fast-lock disabled and windowed VSYNC pipe disabled */
	/* 0x8B: Maximum field duration set to 788 for NTSC and 938 for PAL */
	/* 0xD5: Minimum duration from F-bit falling edge to active video */
#if (DEV_TYPE == DEV_SN_9234_H4_1)
	{
		//int ret;
		unsigned char chip = TLV320AIC3104;
		//unsigned char reg;
		TRACE(SCI_TRACE_NORMAL, MOD_DRV, " TLV320AIC3104 CLCK init\n");
		
       /*soft reset*/ 
        WriteAsicByte(chip,0x1,0x80);
        usleep(50);
        /*CLKDIV_IN uses MCLK*/
    	WriteAsicByte(chip, 102, 0xc2);//
#if 0    	
        /*PLL enable  MCLK = 54MHZ*/
        WriteAsicByte(chip, 3, 0x85);/* new P=5    old P=2*/
        WriteAsicByte(chip, 4, 0x24);/* old J=2 new J=9    第一第二位没用为0*/
        WriteAsicByte(chip, 5, 0x0f);/*old 0x2c  new 0x1f*/
        WriteAsicByte(chip, 6, 0xf8);/* reg 5 and 6 set D=2818          newf8  D=1022 */
        WriteAsicByte(chip, 11, 0x1);/* R=1 */

#else
        /*PLL enable  MCLK = 66MHZ*/
		WriteAsicByte(chip,102,0xC2);//PLLCLK_IN uses MCLK
        WriteAsicByte(chip, 3, 0x85);/*  P=5  */
        WriteAsicByte(chip, 4, 0x1C);/*  J=7    */
        //WriteAsicByte(chip, 5, 0x2A);/*  D=2727=0xAA7*/
        //WriteAsicByte(chip, 6, 0x9C);/*  */
		
        WriteAsicByte(chip, 5, 0x45);/*  D=4473=0x1179*/
        WriteAsicByte(chip, 6, 0xE4);/*  */		
        WriteAsicByte(chip, 11, 0x1);/* R=1 */	
#endif
        /*left and right DAC open*/	
        WriteAsicByte(chip, 7,  0xa);/* FSref = 48 kHz */

        /*sample*/
    	WriteAsicByte(chip, 2,  0xaa);/* FS = FSref/6 */
                
        /*ctrl mode*/
        WriteAsicByte(chip, 8,  0xf0);/* master mode */
                
        /*Audio Serial Data Interface Control*/	
        WriteAsicByte(chip, 9,  0xf);/* old I2S mode,16bit      new 0xf  */

        /*Audio Codec Digital Filter Control Register*/	
        WriteAsicByte(chip, 12,  0x50);

        WriteAsicByte(chip, 25,  0x0);
        WriteAsicByte(chip, 17,  0xf);
        WriteAsicByte(chip, 18,  0xf0);

        WriteAsicByte(chip, 15,  0x0);
        WriteAsicByte(chip, 16,  0x0);
        WriteAsicByte(chip, 19,  0x7c);
        WriteAsicByte(chip, 22,  0x7c);
        WriteAsicByte(chip, 28,  0x0);
        WriteAsicByte(chip, 31,  0x0);
            	
        /*out ac-coupled*/	
        WriteAsicByte(chip, 14, 0x80);
        
        /*left and right DAC power on*/	
        WriteAsicByte(chip, 37, 0x80);  //new 0x80 old 0xc0  /*old:0xc0 new:0x80*/

        /*out common-mode voltage*/	
        WriteAsicByte(chip, 40, 0x80);
        
        /*out path select*/	
     //   WriteAsicByte(chip, 41, 0x1);    
        WriteAsicByte(chip, 41, 0x40);  /*old:0x1 new:0x40, Left-DAC output selects DAC_L3 path to left line output driver*/  

        /*out path select*/	
        WriteAsicByte(chip, 42, 0xa8);  
        
        /*left DAC not muted*/	
        WriteAsicByte(chip, 43, 0x0);    

        /*right DAC not muted*/	
        WriteAsicByte(chip, 44, 0x0); 

        WriteAsicByte(chip, 47, 0x80); 
            	
        /*HPLOUT is not muted*/	
        WriteAsicByte(chip, 51, 0x9f); 

        WriteAsicByte(chip, 64, 0x80); 
        /*HPROUT is not muted*/	
        WriteAsicByte(chip, 65, 0x9f); 
                
        /*out short circuit protection*/	
    //    WriteAsicByte(chip, 38, 0x3e);  
    	WriteAsicByte(chip, 86, 0x09);//LEFT_LOP/M Output Level Control Register			
	}	
#endif	
	for (i=0;i<4;i++)
	{
		__tw2865_write_reg(TW2865_0, i, 0x60, opmode);
		__tw2865_write_reg(TW2865_0, i, 0x19, sharp);
		__tw2865_write_reg(TW2865_0, i, 0x88, Dec_count);
		__tw2865_write_reg(TW2865_0, i, 0x89, Vpll_data);
		__tw2865_write_reg(TW2865_0, i, 0x8b, Max_fieled);
		__tw2865_write_reg(TW2865_0, i, 0xd5, Min_dur);
#ifdef SN2008LE
		__tw2865_read_reg(TW2865_0, i, 0xC4);
		__tw2865_write_reg(TW2865_0, i, 0xC4, 0x02);
#endif
	}
	for (i=0;i<4;i++)
	{
		__tw2865_write_reg(TW2865_1, i, 0x60, opmode);
		__tw2865_write_reg(TW2865_1, i, 0x19, sharp);
		__tw2865_write_reg(TW2865_1, i, 0x88, Dec_count);
		__tw2865_write_reg(TW2865_1, i, 0x89, Vpll_data);
		__tw2865_write_reg(TW2865_1, i, 0x8b, Max_fieled);
		__tw2865_write_reg(TW2865_1, i, 0xd5, Min_dur);
#ifdef SN2008LE
		__tw2865_write_reg(TW2865_1, i, 0xC8, 0x01);
		__tw2865_write_reg(TW2865_1, i, 0xc5, 0x0f);
		__tw2865_read_reg(TW2865_1, i, 0xC8);
		__tw2865_read_reg(TW2865_1, i, 0xC5);
		__tw2865_read_reg(TW2865_1, i, 0xC3);
		__tw2865_read_reg(TW2865_1, i, 0xC3);
		__tw2865_read_reg(TW2865_1, i, 0xC4);
		__tw2865_write_reg(TW2865_1, i, 0xC4, 0x02);
#endif
	}
	for (i=0;i<4;i++)
	{
		__tw2865_write_reg(TW2865_2, i, 0x60, opmode);
		__tw2865_write_reg(TW2865_2, i, 0x19, sharp);
		__tw2865_write_reg(TW2865_2, i, 0x88, Dec_count);
		__tw2865_write_reg(TW2865_2, i, 0x89, Vpll_data);
		__tw2865_write_reg(TW2865_2, i, 0x8b, Max_fieled);
		__tw2865_write_reg(TW2865_2, i, 0xd5, Min_dur);
	}
	for (i=0;i<4;i++)
	{
		__tw2865_write_reg(TW2865_3, i, 0x60, opmode);
		__tw2865_write_reg(TW2865_3, i, 0x19, sharp);
		__tw2865_write_reg(TW2865_3, i, 0x88, Dec_count);
		__tw2865_write_reg(TW2865_3, i, 0x89, Vpll_data);
		__tw2865_write_reg(TW2865_3, i, 0x8b, Max_fieled);
		__tw2865_write_reg(TW2865_3, i, 0xd5, Min_dur);
	}

#else
	
#error "Unknown DEV_TYPE!"
	
#endif
	
	return 0;
}


#if defined(SN2116LE) || defined(SN2116LS) || defined(SN2116LP) || defined(SN2116HE) || defined(SN2116HS) || defined(SN2116HP) //|| defined(SN6116HE)
//获取中断状态值
int Preview_GetAVstate(unsigned char ch)
{
	unsigned char tmp=0;
	int ret=-1;
	tw2865_video_loss video_loss;
	
	video_loss.chip = ch/4;
    video_loss.ch   = ch%4;
	if (tw2865_fd < 0)
	{
		tw2865_fd = open(TW2865_DEVICE, O_RDWR);
		if (tw2865_fd < 0)
		{
			return -1;
		}
	}

    ret = ioctl(tw2865_fd, TW2865_GET_VIDEO_LOSS, &video_loss);
	if(ret)
		return -1;
	tmp = video_loss.is_lost;
	return tmp;
}
//横向位移
int Preview_SetVideo_x(unsigned char ch,unsigned char x_data)
{
	unsigned char sadd=0;
	unsigned char	video_x_reg[4] ={0x0a,0x1a,0x2a,0x3a};
	int ret=0;
	signed char valid_value_range_min = 1;
	signed char valid_value_range_max = 40;
	x_data = x_data*(valid_value_range_max-valid_value_range_min)/(PRV_X_OFFSET_MAX-PRV_X_OFFSET_MIN) + valid_value_range_min;

	if(ch<4)
	{
		sadd = TW2865_0_WRITE;
	}
	else if(ch<8)
	{
		ch = ch%4;
		sadd = TW2865_1_WRITE;
	}
	else if(ch<12)
	{
		ch = ch%4;
		sadd = TW2865_2_WRITE;
	}
	else
	{
		ch = ch%4;
		sadd = TW2865_3_WRITE;
	}
	ret = WriteAsicByte(sadd,video_x_reg[ch],x_data);
	return ret;
}

//纵向位移
int Preview_SetVideo_y(unsigned char ch,unsigned char y_data)
{
	unsigned char sadd=0;
	unsigned char	video_y_reg[4] ={0x08,0x18,0x28,0x38};
	int ret=0;
	signed char valid_value_range_min = 3;
	signed char valid_value_range_max = 25;
	y_data = y_data*(valid_value_range_max-valid_value_range_min)/(PRV_Y_OFFSET_MAX-PRV_Y_OFFSET_MIN) + valid_value_range_min;

	if(ch<4)
	{
		sadd = TW2865_0_WRITE;
	}
	else if(ch<8)
	{
		ch = ch%4;
		sadd = TW2865_1_WRITE;
	}
	else if(ch<12)
	{
		ch = ch%4;
		sadd = TW2865_2_WRITE;
	}
	else
	{
		ch = ch%4;
		sadd = TW2865_3_WRITE;
	}
	ret = WriteAsicByte(sadd,video_y_reg[ch],y_data);
	return ret;
}

//设置图像色度
int Preview_SetVideo_Hue(unsigned char ch,unsigned char hue_data)
{
	unsigned char sadd=0;
	unsigned char	video_hue_reg[4] ={0x06,0x16,0x26,0x36};
	int ret=0;

	if(ch<4)
	{
		sadd = TW2865_0_WRITE;
	}
	else if(ch<8)
	{
		ch = ch%4;
		sadd = TW2865_1_WRITE;
	}
	else if(ch<12)
	{
		ch = ch%4;
		sadd = TW2865_2_WRITE;
	}
	else
	{
		ch = ch%4;
		sadd = TW2865_3_WRITE;
	}
	if(hue_data < 2) hue_data = 2;
	hue_data = 0x80+hue_data;
	ret = WriteAsicByte(sadd,video_hue_reg[ch],hue_data);
	return ret;
}
//设置图像对比度
int Preview_SetVideo_Cont(unsigned char ch,unsigned char cont_data)
{
	unsigned char	 sadd=0;
	unsigned char	video_cont_reg[4] ={0x02,0x12,0x22,0x32};
	int ret=0;
	if(ch<4)
	{
		sadd = TW2865_0_WRITE;
	}
	else if(ch<8)
	{
		ch = ch%4;
		sadd = TW2865_1_WRITE;
	}
	else if(ch<12)
	{
		ch = ch%4;
		sadd = TW2865_2_WRITE;
	}
	else
	{
		ch = ch%4;
		sadd = TW2865_3_WRITE;
	}
	if(cont_data < 2) cont_data = 2;
	ret = WriteAsicByte(sadd,video_cont_reg[ch],cont_data);
	return ret;
}
//设置图像亮度
int Preview_SetVideo_Brt(unsigned char ch,unsigned char brt_data)
{
	unsigned char	 sadd=0;
	unsigned char	video_brt_reg[4] ={0x01,0x11,0x21,0x31}; 
	int ret=0;
	
	if(ch<4)
	{
		sadd = TW2865_0_WRITE;
	}
	else if(ch<8)
	{
		ch = ch%4;
		sadd = TW2865_1_WRITE;
	}
	else if(ch<12)
	{
		ch = ch%4;
		sadd = TW2865_2_WRITE;
	}
	else
	{
		ch = ch%4;
		sadd = TW2865_3_WRITE;
	}
	brt_data = 0x80+brt_data;
	ret = WriteAsicByte(sadd,video_brt_reg[ch],brt_data);
	
	return ret;
}
//设置图像饱和度
int Preview_SetVideo_Sat(unsigned char ch,unsigned char sat_data)
{
	unsigned char	 sadd=0;
	unsigned char	video_satU_reg[4] ={0x04,0x14,0x24,0x34};
	unsigned char	video_satV_reg[4] ={0x05,0x15,0x25,0x35};
	int ret=0;

	if(ch<4)
	{
		sadd = TW2865_0_WRITE;
	}
	else if(ch<8)
	{
		ch = ch%4;
		sadd = TW2865_1_WRITE;
	}
	else if(ch<12)
	{
		ch = ch%4;
		sadd = TW2865_2_WRITE;
	}
	else
	{
		ch = ch%4;
		sadd = TW2865_3_WRITE;
	}

	ret = WriteAsicByte(sadd,video_satU_reg[ch],sat_data);
	if(ret == -1)	return ret;
	ret = WriteAsicByte(sadd,video_satV_reg[ch],sat_data);
	return ret;
}
int Preview_SetVideoParam(unsigned char ch,const PRM_DISPLAY_CFG_CHAN *pInfo)
{
	int ret=0;
	if(!pInfo)	
	{
		return -1;
	}
	
	ret=Preview_SetVideo_Hue(ch,pInfo->Hue);
	if(ret == -1)	goto out;
	
	ret=Preview_SetVideo_Sat(ch,pInfo->Saturation);
	if(ret == -1)	goto out;
	
	ret=Preview_SetVideo_Cont(ch,pInfo->ColorContrast);
	if(ret == -1)	goto out;
	
	ret=Preview_SetVideo_Brt(ch,pInfo->Brightness);
	if(ret == -1)	goto out;
	
	ret=Preview_SetVideo_x(ch,pInfo->xAdjust);
	if(ret == -1)	goto out;
	
	ret=Preview_SetVideo_y(ch,pInfo->yAdjust);
	if(ret == -1)	goto out;
	
	return ret;
out:
	TRACE(SCI_TRACE_HIGH, MOD_DRV, "tw2865  VideoParam set fail!\n");
	return ret;
}
//输入制式检测
//pInput_mode 0-NTSC,1-PAL
int	GetVideoInputInfo(unsigned char ch,unsigned char *pInput_mode)
{
	int sadd=0,ret =-1;
	unsigned char tmp=-1;
	unsigned char	reg[4] ={0x00,0x10,0x20,0x30};
	if (NULL == pInput_mode)
	{
		return -1;
	}
	if(ch<4)
	{
		sadd = TW2865_0_WRITE;
	}
	else if(ch<8)
	{
		ch = ch%4;
		sadd = TW2865_1_WRITE;
	}
	else if(ch<12)
	{
		ch = ch%4;
		sadd = TW2865_2_WRITE;
	}
	else
	{
		ch = ch%4;
		sadd = TW2865_3_WRITE;
	}
	ret = ReadAsicByte(sadd,reg[ch],&tmp);
	if(ret  == -1)
	{
		return ret ;
	}
	tmp = tmp &0x01;/*0 = 60Hz source detected (NTSC), 1 = 50Hz source detected (PAL)*/
	*pInput_mode = tmp;
	return 0;
}
//生产测试接口
int TW_GetAVstate(void)
{
	unsigned char tmp=0,i=0;
	int r=0;
	int ret=-1;
	for(i=0;i< CHANNEL_NUM/4;i++)
	{
		ret = ReadAsicByte(i,0xfd,&tmp);			//IRQ state
		if(ret == -1)
			return ret;	
		r |= (tmp>>4) << (i*4);
	}
	ret = ReadAsicByte(0,0x74,&tmp);			//IRQ state
	if(ret == -1)
		return ret;
	r |= (tmp&0x01)<< CHANNEL_NUM;
	return r;
}


#elif defined(SN2016) || defined(SN2016HS) || defined(SN2016HE)	|| defined(SN2116V2) || defined(SN2016V2) || defined(SN6000) || defined(SN2008LE) || defined(SN8600)


//获取中断状态值
int Preview_GetAVstate(unsigned char ch)
{
	unsigned char tmp=0;
	int ret=-1;
	tw2865_video_loss video_loss;
	
	video_loss.chip = ch/4;
    video_loss.ch   = ch%4;
	if (tw2865_fd < 0)
	{
		tw2865_fd = open(TW2865_DEVICE, O_RDWR);
		if (tw2865_fd < 0)
		{
			return -1;
		}
	}

    ret = ioctl(tw2865_fd, TW2865_GET_VIDEO_LOSS, &video_loss);
	if(ret)
		return -1;
	tmp = video_loss.is_lost;
	return tmp;
}
//横向位移
int Preview_SetVideo_x(unsigned char ch,unsigned char x_data)
{
	unsigned char sadd=0;
	unsigned char addr = 0x8c;
	int ret=0;
	signed char valid_value_range_min = 0xc0;
	signed char valid_value_range_max = 0x40;
	x_data = x_data;
	x_data = x_data*(valid_value_range_max-valid_value_range_min)/(PRV_X_OFFSET_MAX-PRV_X_OFFSET_MIN) + valid_value_range_min;

	sadd = ch/4;
	ret = __WriteAsicByte(sadd, ch%4, addr, x_data);
	return ret;
}

//纵向位移
int Preview_SetVideo_y(unsigned char ch,unsigned char y_data)
{
	unsigned char sadd=0;
	unsigned char addr = 0xae;
	int ret=0;
	signed char valid_value_range_min = 0xea;//0x80;
	signed char valid_value_range_max = 0x04;
	y_data = y_data*(valid_value_range_max-valid_value_range_min)/(PRV_Y_OFFSET_MAX-PRV_Y_OFFSET_MIN) + valid_value_range_min;

	sadd = ch/4;
	ret = __WriteAsicByte(sadd, ch%4, addr, y_data);
	return ret;
}

//设置图像色度
int Preview_SetVideo_Hue(unsigned char ch,unsigned char hue_data)
{
	tw2865_image_adjust data = {0};
	data.hue = hue_data-0x80;
	data.chip = ch/4;
	data.chn = ch%4;
	data.item_sel = TW2865_SET_HUE;
	
	if (ioctl(tw2865_fd, TW2865_SET_IMAGE_ADJUST, &data) != 0)
	{
		return -1;
	}

	return 0;
}
//设置图像对比度
int Preview_SetVideo_Cont(unsigned char ch,unsigned char cont_data)
{
	tw2865_image_adjust data = {0};
	data.contrast = cont_data;
	data.chip = ch/4;
	data.chn = ch%4;
	data.item_sel = TW2865_SET_CONTRAST;
	
	if (ioctl(tw2865_fd, TW2865_SET_IMAGE_ADJUST, &data) != 0)
	{
		return -1;
	}
	
	return 0;
}
//设置图像亮度
int Preview_SetVideo_Brt(unsigned char ch,unsigned char brt_data)
{
	tw2865_image_adjust data = {0};
	data.brightness = brt_data;
	data.chip = ch/4;
	data.chn = ch%4;
	data.item_sel = TW2865_SET_BRIGHT;
	
	if (ioctl(tw2865_fd, TW2865_SET_IMAGE_ADJUST, &data) != 0)
	{
		return -1;
	}
	
	return 0;
}
//设置图像饱和度
int Preview_SetVideo_Sat(unsigned char ch,unsigned char sat_data)
{
	tw2865_image_adjust data = {0};
	data.saturation = sat_data;
	data.chip = ch/4;
	data.chn = ch%4;
	data.item_sel = TW2865_SET_SATURATION;
	
	if (ioctl(tw2865_fd, TW2865_SET_IMAGE_ADJUST, &data) != 0)
	{
		return -1;
	}
	
	return 0;
}

int Preview_SetVideoParam(unsigned char ch,const PRM_DISPLAY_CFG_CHAN *pInfo)
{
	tw2865_image_adjust data = {0};
	if(!pInfo)	
	{
		
		printf("erro !pInfo\n");
		return -1;
	}
	
	data.hue = pInfo->Hue - 0x80;
	data.brightness = pInfo->Brightness;
	data.contrast = pInfo->ColorContrast;
	data.saturation = pInfo->Saturation;
	data.chip = ch/4;
	data.chn = ch%4;
	data.item_sel = TW2865_SET_HUE | TW2865_SET_BRIGHT | TW2865_SET_CONTRAST | TW2865_SET_SATURATION;
	
	if (ioctl(tw2865_fd, TW2865_SET_IMAGE_ADJUST, &data) != 0)
	{
		
		printf("erro !ioctl TW2865_SET_IMAGE_ADJUST\n");
		return -1;
	}
	
	Preview_SetVideo_x(ch, pInfo->xAdjust);
	Preview_SetVideo_y(ch, pInfo->yAdjust);

	return 0;
}

//输入制式检测
//pInput_mode 0-NTSC,1-PAL
int	GetVideoInputInfo(unsigned char ch,unsigned char *pInput_mode)
{
	int sadd=0,ret =-1;
	unsigned char tmp=-1;
	unsigned char addr=0x0c;

	if (NULL == pInput_mode)
	{
		return -1;
	}
	sadd = ch/4;
	ret = __ReadAsicByte(sadd, ch%4, addr,&tmp);
	if(ret == -1)
	{
		return ret ;
	}

/*
	if ((tmp&0x80) == 0)
	{
		return -1;/ *tvp5157 NOT in auto detect mode* /
	}
*/

	switch (tmp&0x03)
	{
	case 0x01:
	case 0x05:
		*pInput_mode = 0;/*NTSC*/
		break;
	case 0x02:
	case 0x03:
	case 0x04:
	case 0x07:
		*pInput_mode = 1;/*PAL*/
		break;
	default :
		return -1;
	}
	return 0;
}

//生产测试接口
int TW_GetAVstate(void)
{
	return PRV_TestAi();
}
#endif

#if defined(Hi3535)
#define ACODEC_FILE     "/dev/acodec"
HI_S32 PRV_TW2865_CfgAudio(AUDIO_SAMPLE_RATE_E enSample)
{
    HI_S32 fdAcodec = -1;
    HI_S32 ret = HI_SUCCESS;
    unsigned int i2s_fs_sel = 0;
    //unsigned int mixer_mic_ctrl = 0;
    //unsigned int gain_mic = 0;

    fdAcodec = open(ACODEC_FILE,O_RDWR);
    if (fdAcodec < 0) 
    {
        printf("%s: can't open Acodec,%s\n", __FUNCTION__, ACODEC_FILE);
        ret = HI_FAILURE;
    }
    if(ioctl(fdAcodec, ACODEC_SOFT_RESET_CTRL))
    {
    	printf("Reset audio codec error\n");
    }

    if ((AUDIO_SAMPLE_RATE_8000 == enSample)
        || (AUDIO_SAMPLE_RATE_11025 == enSample)
        || (AUDIO_SAMPLE_RATE_12000 == enSample)) 
    {
        i2s_fs_sel = 0x18;
    } 
    else if ((AUDIO_SAMPLE_RATE_16000 == enSample)
        || (AUDIO_SAMPLE_RATE_22050 == enSample)
        || (AUDIO_SAMPLE_RATE_24000 == enSample)) 
    {
        i2s_fs_sel = 0x19;
    } 
    else if ((AUDIO_SAMPLE_RATE_32000 == enSample)
        || (AUDIO_SAMPLE_RATE_44100 == enSample)
        || (AUDIO_SAMPLE_RATE_48000 == enSample)) 
    {
        i2s_fs_sel = 0x1a;
    } 
    else 
    {
        printf("%s: not support enSample:%d\n", __FUNCTION__, enSample);
        ret = HI_FAILURE;
    }

    if (ioctl(fdAcodec, ACODEC_SET_I2S1_FS, &i2s_fs_sel)) 
    {
        printf("%s: set acodec sample rate failed\n", __FUNCTION__);
        ret = HI_FAILURE;
    }

#if 0	
    if (HI_TRUE)
    {

        mixer_mic_ctrl = ACODEC_MIXER_MICIN;
        if (ioctl(fdAcodec, ACODEC_SET_MIXER_MIC, &mixer_mic_ctrl))
        {
            printf("%s: set acodec micin failed\n", __FUNCTION__);
            return HI_FAILURE;
        }


        /* set volume plus (0~0x1f,default 0) */
        gain_mic = 0xc;
        if (ioctl(fdAcodec, ACODEC_SET_GAIN_MICL, &gain_mic))
        {
            printf("%s: set acodec micin volume failed\n", __FUNCTION__);
            return HI_FAILURE;
        }
        if (ioctl(fdAcodec, ACODEC_SET_GAIN_MICR, &gain_mic))
        {
            printf("%s: set acodec micin volume failed\n", __FUNCTION__);
            return HI_FAILURE;
        }
        
    }
#endif  
    close(fdAcodec);
    return ret;
}
#else
HI_S32 PRV_TW2865_CfgAudio(AUDIO_SAMPLE_RATE_E enSample)
{
    int fd;
    tw2865_audio_samplerate samplerate;
	
	if (tw2865_fd < 0)
	{
		tw2865_fd = open(TW2865_DEVICE, O_RDWR);
		if (tw2865_fd < 0)
			return -1;
	}
	fd = tw2865_fd;
    
    if (AUDIO_SAMPLE_RATE_8000 == enSample)
    {
        samplerate = TW2865_SAMPLE_RATE_8000;
    }
    else if (AUDIO_SAMPLE_RATE_16000 == enSample)
    {
        samplerate = TW2865_SAMPLE_RATE_16000;
    }
    else if (AUDIO_SAMPLE_RATE_32000 == enSample)
    {
        samplerate = TW2865_SAMPLE_RATE_32000;
    }
    else if (AUDIO_SAMPLE_RATE_44100 == enSample)
    {
        samplerate = TW2865_SAMPLE_RATE_44100;
    }
    else if (AUDIO_SAMPLE_RATE_48000 == enSample)
    {
        samplerate = TW2865_SAMPLE_RATE_48000;
    }
    else 
    {
        TRACE(SCI_TRACE_NORMAL, MOD_PRV, "not support enSample:%d\n",enSample);
        return -1;
    }
	
    if (ioctl(fd, TW2865_SET_SAMPLE_RATE, &samplerate))
    {
        TRACE(SCI_TRACE_NORMAL, MOD_PRV, "ioctl TW2865_SET_SAMPLE_RATE err !!! \n");
        //close(fd);
        return -1;
    }
    
    //close(fd);
    return 0;
}
#endif

HI_S32 PRV_TW2865_CfgV(VIDEO_NORM_E enVideoMode,VI_WORK_MODE_E enWorkMode)
{
    int fd, i;
    int video_mode;
    tw2865_video_norm stVideoMode;
    tw2865_work_mode work_mode;
#ifdef hi3515
    int chip_cnt = 2;
#else
    int chip_cnt = 4;
#endif
	if (tw2865_fd < 0)
	{
		tw2865_fd = open(TW2865_DEVICE, O_RDWR);
		if (tw2865_fd < 0)
			return -1;
	}
	fd = tw2865_fd;
#if defined(SN2016) || defined(SN2016HS) || defined(SN2016HE) || defined(SN2116V2) || defined(SN2016V2) || defined(SN6000) || defined(SN2008LE) || defined(SN8600)
	//video_mode = TW2865_AUTO;
    video_mode = (VIDEO_ENCODING_MODE_PAL == enVideoMode) ? TW2865_PAL : TW2865_NTSC ;
#else
    video_mode = (VIDEO_ENCODING_MODE_PAL == enVideoMode) ? TW2865_PAL : TW2865_NTSC ;
#endif
    for (i=0; i<chip_cnt; i++)
    {
        stVideoMode.chip    = i;
        stVideoMode.mode    = video_mode;
        if (ioctl(fd, TW2865_SET_VIDEO_NORM, &stVideoMode))
        {
            TRACE(SCI_TRACE_NORMAL, MOD_PRV, "set tw2865(%d) video mode fail\n", i);
            return -1;
        }
    }
	
    for (i=0; i<chip_cnt; i++)
    {
        work_mode.chip = i;
#if defined(SN9234H1)
		if (VI_WORK_MODE_4D1 == enWorkMode)
        {
            work_mode.mode = TW2865_4D1_MODE;
        }
        else if (VI_WORK_MODE_2D1 == enWorkMode)
        {
            work_mode.mode = TW2865_2D1_MODE;
        }
        else
        {
            TRACE(SCI_TRACE_NORMAL, MOD_PRV, "work mode not support\n");
            return -1;
        }
#else
        if (VI_WORK_MODE_4Multiplex == enWorkMode)
        {
            work_mode.mode = TW2865_4D1_MODE;
        }
        else if (VI_WORK_MODE_2Multiplex == enWorkMode)
        {
            work_mode.mode = TW2865_2D1_MODE;
        }
        else
        {
            TRACE(SCI_TRACE_NORMAL, MOD_PRV, "work mode not support\n");
            return -1;
        }
#endif		
        ioctl(fd, TW2865_SET_WORK_MODE, &work_mode);
    }
	
    return 0;
}

