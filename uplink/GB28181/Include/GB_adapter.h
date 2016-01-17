#ifndef _GB_ADAPTER_H_
#define _GB_ADAPTER_H_


#if (DVR_SUPPORT_GB == 1)	

#define GB_PTZCMD_IS_ZOOM_OUT(cmd) 	 	(((cmd & 0xC0) == 0) && (cmd & (0x01 << 5)))
#define GB_PTZCMD_IS_ZOOM_IN(cmd)  		(((cmd & 0xC0) == 0) && (cmd & (0x01 << 4)))
#define GB_PTZCMD_IS_TILT_UP(cmd)  			(((cmd & 0xC0) == 0) && (cmd & (0x01 << 3)))
#define GB_PTZCMD_IS_TILT_DOWN(cmd)  		(((cmd & 0xC0) == 0) && (cmd & (0x01 << 2)))
#define GB_PTZCMD_IS_PAN_LEFT(cmd)  		(((cmd & 0xC0) == 0) && (cmd & (0x01 << 1)))
#define GB_PTZCMD_IS_PAN_RIGHT(cmd)  		(((cmd & 0xC0) == 0) && (cmd & (0x01 << 0)))
#define GB_PTZCMD_IS_RU(cmd)  				(GB_PTZCMD_IS_PAN_RIGHT(cmd) && GB_PTZCMD_IS_TILT_UP(cmd))
#define GB_PTZCMD_IS_LU(cmd)  				(GB_PTZCMD_IS_PAN_LEFT(cmd) && GB_PTZCMD_IS_TILT_UP(cmd))
#define GB_PTZCMD_IS_RD(cmd)  				(GB_PTZCMD_IS_PAN_RIGHT(cmd) && GB_PTZCMD_IS_TILT_DOWN(cmd))
#define GB_PTZCMD_IS_LD(cmd)  				(GB_PTZCMD_IS_PAN_LEFT(cmd) && GB_PTZCMD_IS_TILT_DOWN(cmd))
#define GB_PTZCMD_IS_IRIS_SMALL(cmd) 	 	((cmd & (0x01 << 6)) && (cmd & (0x01 << 3)))
#define GB_PTZCMD_IS_IRIS_LARGE(cmd) 	 	((cmd & (0x01 << 6)) && (cmd & (0x01 << 2)))
#define GB_PTZCMD_IS_FOCUS_NEAR(cmd) 	 	((cmd & (0x01 << 6)) && (cmd & (0x01 << 1)))
#define GB_PTZCMD_IS_FOCUS_FAR(cmd) 	 	((cmd & (0x01 << 6)) && (cmd & (0x01 << 0)))

// Ô¤ÖÃµã
#define GB_PTZCMD_IS_SET_PRESET(cmd) 	 	((cmd & (0x01 << 7)) && ((cmd & 0x0F) == 0x01))
#define GB_PTZCMD_IS_GOTO_PRESET(cmd) 	 	((cmd & (0x01 << 7)) && ((cmd & 0x0F) == 0x02))
#define GB_PTZCMD_IS_DEL_PRESET(cmd) 	 	((cmd & (0x01 << 7)) && ((cmd & 0x0F) == 0x03))

// Ñ²º½
#define GB_PTZCMD_IS_ADD_CRUISE_PRESET(cmd) 	 	((cmd & (0x01 << 7)) && ((cmd & 0x0F) == 0x04))
#define GB_PTZCMD_IS_DEL_CRUISE_PRESET(cmd) 	 	((cmd & (0x01 << 7)) && ((cmd & 0x0F) == 0x05))
#define GB_PTZCMD_IS_SET_CRUISE_SPEED(cmd) 	 	((cmd & (0x01 << 7)) && ((cmd & 0x0F) == 0x06))
#define GB_PTZCMD_IS_SET_CRUISE_TIME(cmd) 	 		((cmd & (0x01 << 7)) && ((cmd & 0x0F) == 0x07))
#define GB_PTZCMD_IS_START_CRUISE(cmd) 	 		((cmd & (0x01 << 7)) && ((cmd & 0x0F) == 0x08))

typedef struct 
{
	int opt;
	int speed;
	int preset_id;
	int cruise_id;
	int cruise_speed;
	int cruise_time;
}GB_PTZ_CTRL_STRUCT;


int GB_Deal_Query_Req(GB_CONNECT_STATE *gb_cons, osip_event_t * osip_event, gb_Query_Req_Struct *Req);
int GB_Deal_Control_Req(GB_CONNECT_STATE *gb_cons, osip_event_t * osip_event, gb_Control_Req_Struct *Req);
int DeviceID2LocalChn(char *deviceID, int *localchn);

#endif

#endif
