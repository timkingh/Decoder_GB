#ifndef _GB_PROTOCOLLAYER_H_
#define _GB_PROTOCOLLAYER_H_

#include <stdio.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <net/if.h>
#include <netinet/in.h> 
#include <arpa/inet.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <limits.h>
#include <sys/socket.h>
#include <ctype.h>
#include <sys/time.h>
#include <unistd.h>
#include <netdb.h>
#include <stdio.h>
#include <ctype.h>
#include <stdarg.h>
#include <poll.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <pthread.h>
#include <netinet/ip.h>
#include <dirent.h>

#include <osipparser2/osip_parser.h>
#include <osipparser2/sdp_message.h>
#include <osip_headers.h>
#include <osip_mt.h>
#include <osip2/osip.h>
#include <osip2/osip_dialog.h>
#include <osip2/osip_mt.h>
#include <osip2/osip_condv.h>
#include <osipparser2/osip_md5.h>

#include <mxml.h>

#include "milenage.h"
#include "rijndael.h"

#define GB_MANSCDP_XML 	("Application/MANSCDP+xml")
#define GB_MANSCDP_SDP	("APPLICATION/SDP")
#define GB_UA_STRING 		("STAR-NET SIP UAS V1.0")



/*************************      GB_CMD_DEF    ************************************************/


typedef enum 
{
	gb_CommandType_KeepAlive = 0,
	gb_CommandType_DeviceStatus,
	gb_CommandType_Catalog,
	gb_CommandType_DeviceInfo,
	gb_CommandType_RecordInfo,
	gb_CommandType_Alarm,
	gb_CommandType_ConfigDownload,
	gb_CommandType_PersetQuery,

	gb_CommandType_DeviceControl,
	
	gb_CommandType_DeviceConfig,
	gb_CommandType_MediaStatus,

	gb_CommandType_NUM,
}gb_CommandType_enum;


/*************************      END GB_CMD_DEF    *******************************************/



#define GB_DEVICEID_LEN				20
#define GB_MAX_PLAYLOAD_BUF		(4*1024)
#define GB_MAX_FILEPATH_LEN			(256)
#define GB_STR_LEN				16
#define GB_DATETIME_STR_LEN				32
#define GB_MAX_STR_LEN			(256)
#define GB_NAME_STR_LEN				64
#define GB_EXTERN_INFO_BUF		(1024)
#define GB_ROI_NUM		(16)
#define GB_PRESET_NUM		(255)


#define GB_SIZEOF_STRUCT(NAME)  ((sizeof(NAME))/(sizeof(NAME[0])))

#define NO_SHOW_ITEM		(-1)


enum
{
	statusType_ON,
	statusType_OFF,
};

enum
{
	resultType_OK,
	resultType_ERROR,
};

enum
{
	ONLINE,
	OFFLINE,
};

enum
{
	DutyStatus_ONDUTY,
	DutyStatus_OFFDUTY,
	DutyStatus_ALARM,
};

enum
{
	Type_time,
	Type_alarm,
	Type_manual,
	Type_all,
};

enum
{
	recordType_Record,
	recordType_StopRecord,
};

enum
{
	guardType_SetGuard,
	guardType_ResetGuard,
};


enum
{
	Msgtype_Control = 1,
	Msgtype_Query,
	Msgtype_Notify,
	Msgtype_Response,
};

enum CodeStruct_enum
{
	Code_Query_Req = 1,
	Code_Control_Req,
	
	Code_MAX_NUM,
};


typedef struct
{
	int Msgtype;
	int Cmdtype;
}BaseInfo;


typedef struct 
{
	char deviceID[GB_DEVICEID_LEN+1];
	
}deviceIDType;


typedef struct 
{
	int SN;
	deviceIDType DeviceID;
	int resultType;  
	int Num;  // 故障设备数量
	deviceIDType *errorDeviceID;
}gb_Keepalive_Struct;


/**************************      Struct_DEF    ************************************/

typedef struct 
{
	int Cmdtype;
	int SN;
	deviceIDType DeviceID;
}gb_BaseInfo_Query;



// 设备状态
typedef struct
{
	deviceIDType DeviceID; 
	int DutyStatus; // 报警设备状态（必选）
}gb_AlarmDeviceInfo;

typedef struct 
{
	gb_BaseInfo_Query BaseInfo;
	int Result;  // 查询结果标志（必选）
	int Online; // 是否在线（必选）
	int Status;  // 是否正常工作（必选）
	char Reason[GB_MAX_STR_LEN+1];  // 不正常工作原因（可选）
	int Encode;  // 是否编码（可选）
	int Decode;  // 是否解码,  参考海康添加该字段，仅解码器使用
	int Record;  // 是否录像（可选）
	char DeviceTime[GB_DATETIME_STR_LEN];  // 设备时间和日期（可选） 
	int Num; // 表示列表项个数（可选）
	gb_AlarmDeviceInfo *AlarmDeviceList; // 报警设备状态列表（可选）
	char *Info; // 扩展信息，可多项
}gb_Query_DeviceStatus_Rsp;




// 目录信息
typedef struct 
{
	gb_BaseInfo_Query Query;
	char StartTime[GB_DATETIME_STR_LEN]; // 增加设备的起始时间（可选）空表示不限
	char EndTime[GB_DATETIME_STR_LEN]; // 增加设备的终止时间（可选）空表示到当前时间
}gb_Catalog_Query;

typedef struct
{
	deviceIDType DeviceID;
	char Name[GB_NAME_STR_LEN+1]; // 设备/区域/系统名称（必选）
	char Manufacturer[GB_NAME_STR_LEN+1]; // 当为设备时，设备厂商（必选）
	char Model[GB_NAME_STR_LEN+1]; // 当为设备时，设备型号（必选）
	char Owner[GB_NAME_STR_LEN+1]; // 当为设备时，设备归属（必选）
	char CivilCode[GB_STR_LEN+1]; // 行政区域（必选）
	double Block; // 警区（可选）
	char Address[GB_MAX_STR_LEN+1]; // 当为设备时，安装地址（必选）
	int Parental; // 当为设备时，是否有子设备（必选）1有，0没有
	char ParentID[GB_DEVICEID_LEN+1]; // 父设备/区域/系统ID（可选，有父设备需要填写）
	int SafetyWay; // 信令安全模式（可选）缺省为0；0：不采用；2：S/MIME签名方式；3：S/MIME加密签名同时采用方式；4：数字摘要方式
	int RegisterWay; // 注方册式（必选）缺省为1；1：符合sip3261标准的认证注册模式；2：基于口令的双向认证注册模式；3：基于数字证书的双向认证注册模式
	char CertNum[GB_MAX_STR_LEN+1]; // 证书序列号（有证书的设备必选）
	int Certifiable; // 证书有效标识（有证书的设备必选）缺省为0；证书有效标识：0：无效  1：有效
	int ErrCode; // 无效原因码（有证书且证书无效的设备必选）
	char EndTime[GB_DATETIME_STR_LEN]; // 证书终止有效期（有证书的设备必选）
	int Secrecy; // 保密属性（必选）缺省为0；0：不涉密，1：涉密
	char IPAddress[GB_STR_LEN]; // 设备/区域/系统IP 地址（可选）
	int Port; // 设备/区域/系统端口（可选）
	char Password[GB_NAME_STR_LEN]; // 设备口令（可选）
	int Status; // 设备状态（必选）
	double Longitude; // 经度（可选）
	double Latitude; // 纬度（可选）
}gb_itemType;

typedef struct 
{
	int PTZType;  // 摄像机类型扩展，标识摄像机类型：1-球机；2-半球；3-固定枪机；4-遥控枪机。当目录项为摄像机时可选。
	int PositionType; // 摄像机位置类型扩展。1-省际检查站、2-党政机关、3-车站码头、4-中心广场、5-体育场馆、6-商业中心、7-宗教场所、8-校园周边、9-治安复杂区域、10-交通干线。当目录项为摄像机时可选。
	int RoomType; // 摄像机安装位置室外、室内属性。1-室外、2-室内。当目录项为摄像机时可选，缺省为1。
	int UseType; // 摄像机用途属性。1-治安、2-交通、3-重点。当目录项为摄像机时可选。
	int SupplyLightType; // 摄像机补光属性。1-无补光、2-红外补光、3-白光补光。当目录项为摄像机时可选，缺省为1。
	int DirectionType; // 摄像机监视方位属性。1-东、2-西、3-南、4-北、5-东南、6-东北、7-西南、8-西北。当目录项为摄像机时且为固定摄像机或设置看守位摄像机时可选。
	char Resolution[GB_NAME_STR_LEN*2]; // 摄像机支持的分辨率，可有多个分辨率值，各个取值见以“/”分隔。分辨率取值参见国标附录F中SDP f字段规定。当目录项为摄像机时可选。
	deviceIDType BusinessGroupID; // 虚拟组织所属的业务分组ID，业务分组根据特定的业务需求制定，一个业务分组包含一组特定的虚拟组织。
}gb_Query_Catalog_info;


typedef struct 
{
	gb_BaseInfo_Query BaseInfo;
	int SumNum;  // 查询结果总数（必选）	
	int Num;  //num表示目录项个数
	gb_itemType *DeviceList; // 设备目录项列表
	gb_Query_Catalog_info *info; // 扩展信息，可多项  
}gb_Query_Catalog_Rsp;




// 设备信息
typedef struct 
{
	gb_BaseInfo_Query BaseInfo;
	char DeviceName[GB_NAME_STR_LEN+1]; // 目标设备/区域/系统的名称（可选）
	int Result;  // 查询结果标志（必选）
	char Manufacturer[GB_NAME_STR_LEN+1]; // 设备生产商（可选）
	char Model[GB_NAME_STR_LEN+1]; // 设备型号（可选） 
	char Firmware[GB_NAME_STR_LEN+1];  // 设备固件版本（可选） 
	int Channel;  // 视频输入通道数（可选）
	char DeviceType[GB_NAME_STR_LEN+1];  
	int MaxCamera;  
	int MaxAlarm; 
	int MaxOut;  //  参考海康添加该字段，仅解码器使用
	char *Info; // 扩展信息，可多项
}gb_Query_DeviceInfo_Rsp;





// 文件目录检索
typedef struct 
{
	gb_BaseInfo_Query Query;
	char StartTime[GB_DATETIME_STR_LEN]; // 录像起始时间（可选）空表示不限
	char EndTime[GB_DATETIME_STR_LEN]; // 增加录像终止时间（可选）空表示到当前时间
	char FilePath[GB_MAX_FILEPATH_LEN+1]; // 文件路径名（可选）
	char Address[GB_MAX_FILEPATH_LEN+1]; // 录像地址（可选  支持不完全查询）
	int Secrecy; // 保密属性（可选）缺省为0；0：不涉密，1：涉密
	int Type; // 录像产生类型（可选）time 或alarm或manual或all
	char RecorderID[GB_DEVICEID_LEN+1]; // 录像触发者ID（可选）
	int IndistinctQuery; // 录像模糊查询属性（可选）缺省为0；0：不进行模糊查询，此时根据SIP消息中To头域URI中的ID值确定查询录像位置，若ID值为本域系统ID则进行中心历史记录检索，若为前端设备ID则进行前端设备历史记录检索；1：进行模糊查询，此时设备所在域应同时进行中心检索和前端检索并将结果统一返回。
}gb_RecordInfo_Query;

typedef struct
{
	deviceIDType DeviceID;
	char Name[GB_NAME_STR_LEN+1]; // 设备/区域名称（必选）
	char FilePath[GB_MAX_FILEPATH_LEN+1]; // 文件路径名（可选）
	char Address[GB_MAX_STR_LEN+1]; // 录像地址（可选）
	char StartTime[GB_DATETIME_STR_LEN]; // 录像开始时间（可选）
	char EndTime[GB_DATETIME_STR_LEN]; // 录像结束时间（可选）
	int Secrecy; // 保密属性（必选）缺省为0；0：不涉密，1：涉密
	int Type; // 录像产生类型（可选）time 或alarm或manual或all
	char RecorderID[GB_DEVICEID_LEN+1]; // 录像触发者ID（可选）
}gb_itemFileType;

typedef struct 
{
	gb_BaseInfo_Query BaseInfo;
	char Name[GB_NAME_STR_LEN+1]; // 设备/区域名称（必选）
	int SumNum;  // 查询结果总数（必选）
	int Num; // Num表示目录项个数
	gb_itemFileType *RecordList; // 文件目录项列表
	char *info; // 扩展信息，可多项
}gb_Query_RecordInfo_Rsp;






// 报警
typedef struct 
{
	gb_BaseInfo_Query Query;
	int StartAlarmPriority; // 报警起始级别（可选），0为全部，1为一级警情，2为二级警情，3为三级警情，4为四级警情
	int EndAlarmPriority; // 报警终止级别（可选），0为全部，1为一级警情，2为二级警情，3为三级警情，4为四级警情
	char AlarmMethod[GB_STR_LEN+1]; // 报警方式条件（可选），取值0为全部，1为电话报警，2为设备报警，3为短信报警，4为GPS报警，5为视频报警，6为设备故障报警，7其它报警；可以为直接组合如12为电话报警或设备报警
	char StartAlarmTime[GB_DATETIME_STR_LEN]; // 报警发生开始时间（可选）
	char EndAlarmTime[GB_DATETIME_STR_LEN]; // 报警发生终止时间（可选）
}gb_Alarm_Query;

typedef struct 
{
	gb_BaseInfo_Query BaseInfo;
	int AlarmPriority; // 报警级别（必选），1为一级警情，2为二级警情，3为三级警情，4为四级警情
	int AlarmMethod; // 报警方式（必选），取值1为电话报警，2为设备报警，3为短信报警，4为GPS报警，5为视频报警，6为设备故障报警，7其它报警
	char AlarmTime[GB_DATETIME_STR_LEN]; // 报警时间（必选）
	char AlarmDescription[GB_MAX_STR_LEN]; // 报警内容描述（可选）
	double Longitude; // 经度（可选）
	double Latitude; // 纬度（可选）
}gb_Alarm_Notify;



// 设备配置
#define GB_ConfigType_BasicParam			(0x01 << 0)
#define GB_ConfigType_VideoParamOpt		(0x01 << 1)
#define GB_ConfigType_VideoParamConfig		(0x01 << 2)
#define GB_ConfigType_AudioParamOpt		(0x01 << 3)
#define GB_ConfigType_AudioParamConfig		(0x01 << 4)
#define GB_ConfigType_SVACEncodeConfig	(0x01 << 5)
#define GB_ConfigType_SVACDecodeConfig	(0x01 << 6)

#define GB_IS_ConfigType_BasicParam(Type) 	(Type & GB_ConfigType_BasicParam)
#define GB_IS_ConfigType_VideoParamOpt(Type) 	(Type & GB_ConfigType_VideoParamOpt)
#define GB_IS_ConfigType_VideoParamConfig(Type) 	(Type & GB_ConfigType_VideoParamConfig
#define GB_IS_ConfigType_AudioParamOpt(Type) 	(Type & GB_ConfigType_AudioParamOpt)
#define GB_IS_ConfigType_AudioParamConfig(Type) 	(Type & GB_ConfigType_AudioParamConfig)
#define GB_IS_ConfigType_SVACEncodeConfig(Type) 	(Type & GB_ConfigType_SVACEncodeConfig)
#define GB_IS_ConfigType_SVACDecodeConfig(Type) 	(Type & GB_ConfigType_SVACDecodeConfig)

typedef struct 
{
	gb_BaseInfo_Query Query;
	int ConfigType; // 查询配置参数类型（必选），可查询的配置类型包括基本参数配置：BasicParam，视频参数配置范围：VideoParamOpt，视频参数当前配置：VideoParamConfig，音频参数配置范围：AudioParamOpt，音频参数当前配置：AudioParamConfig，SVAC编码配置：SVACEncodeConfig，SVAC 解码配置：SVACDecodeConfig。可同时查询多个配置类型，各类型以“/”分隔，可返回与查询SN值相同的多个响应，每个响应对应一个配置类型。
}gb_ConfigDownload_Query;

typedef struct
{
	char Name[GB_NAME_STR_LEN]; // 设备名称（必选）
	char DeviceID[GB_DEVICEID_LEN+1]; // 设备ID（必选）
	char SIPServerID[GB_DEVICEID_LEN+1]; // SIP服务器ID（必选）
	char SIPServerIP[GB_STR_LEN]; // SIP服务器IP（必选）
	int SIPServerPort; // SIP服务器端口（必选）
	char DomainName[GB_DEVICEID_LEN+1]; // SIP服务器域名（必选）
	int Expiration; // 注册过期时间（必选）
	char Password[GB_DEVICEID_LEN+1]; // 注册口令（必选）
	int HeartBeatInterval; // 心跳间隔时间（必选）
	int HeartBeatCount; // 心跳超时次数（必选）	
}gb_ConfigDownload_BasicParam;

typedef struct
{
	char VideoFormatOpt[GB_MAX_STR_LEN]; // 视频编码格式可选范围（必选）
	char ResolutionOpt[GB_MAX_STR_LEN]; // 分辨率可选范围（必选）
	char FrameRateOpt[GB_MAX_STR_LEN]; // 帧率可选范围（必选）
	char BitRateTypeOpt[GB_MAX_STR_LEN]; // 码率类型范围（必选）
	char VideoBitRateOpt[GB_MAX_STR_LEN]; //视频码率范围（必选）
	char DownloadSpeedOpt[GB_MAX_STR_LEN]; // 视频下载速度可选范围（必选）
}gb_ConfigDownload_VideoParamOpt;

typedef struct 
{
	char StreamName[GB_NAME_STR_LEN]; // 流名称(必选) 如第一个流 Stream1,第二个流 Stream2 
	char VideoFormat[GB_NAME_STR_LEN]; // 视频编码格式当前配置值(必选)
	char Resolution[GB_NAME_STR_LEN]; // 分辨率当前配置值(必选)
	char FrameRate[GB_NAME_STR_LEN]; // 帧率当前配置值(必选)
	char BitRateType[GB_NAME_STR_LEN]; // 码率类型配置值(必选)
	char VideoBitRate[GB_NAME_STR_LEN]; // 视频码率配置值(必选)
}gb_VideoParamAttributeType;

typedef struct
{
	int Num;
	gb_VideoParamAttributeType *Item;
}gb_ConfigDownload_VideoParamConfig;

typedef struct
{
	char AudioFormatOpt[GB_MAX_STR_LEN]; // 音频编码格式可选范围（必选）
	char AudioBitRateOpt[GB_MAX_STR_LEN]; // 音频码率可选范围（必选）
	char SamplingRateOpt[GB_MAX_STR_LEN]; // 采样率可选范围（必选）
}gb_ConfigDownload_AudioParamOpt;


typedef struct 
{
	char StreamName[GB_NAME_STR_LEN]; // 流名称(必选) 如第一个流 Stream1,第二个流 Stream2 
	char AudioFormat[GB_NAME_STR_LEN]; // 音频编码格式当前配置值(必选)
	char AudioBitRate[GB_NAME_STR_LEN]; // 音频码率当前配置值(必选)
	char SamplingRate[GB_NAME_STR_LEN]; // 采样率当前配置值(必选)
}gb_AudioParamAttributeType;

typedef struct
{
	int Num;
	gb_AudioParamAttributeType *Item;
}gb_ConfigDownload_AudioParamConfig;

typedef struct
{
	int ROISeq; // 感兴趣区域编号，取值范围1-16（必选）
	int TopLeft; // 感兴趣区域左上角坐标，取值范围0-19683（必选）
	int BottomRight; // 感兴趣区域右下角坐标，取值范围0-19683（必选）
	int ROIQP; // ROI区域编码质量等级，取值0：一般，1：较好，2：好，3：很好（必选）
}gb_ROI;

typedef struct
{
	int ROIFlag; // 感兴趣区域开关，取值0：关闭，1：打开（必选）
	int ROINumber; // 感兴趣区域数量，取值范围0-16（必选）
	gb_ROI ROI[GB_ROI_NUM]; // 感兴趣区域（必选）
	int BackGroundQP; // 背景区域编码质量等级，取值0：一般，1：较好，2：好，3：很好（必选）
	int BackGroundSkipFlag; // 背景跳过开关，取值0：关闭，1：打开（必选）
}gb_SVACEncodeConfig_ROIParam;

typedef struct
{
	int SVCFlag; // SVC开关，取值0：关闭，1：打开（必选）
	int SVCSTMMode; // 码流上传模式，取值0：基本层码流单独传输方式；1：基本层+1个增强层码流方式；2：基本层+2个增强层码流方式；3：基本层+3个增强层码流方式；（可选）
	int SVCSpaceDomainMode; // 空域编码方式，取值0：不使用；1：1级增强（1个增强层）；2：2级增强（2个增强层）；3：3级增强（3个增强层）（可选）
	int SVCTimeDomainMode; // 时域编码方式，取值0：不使用；1：1 级增强；2：2级增强；3：3级增强（可选）
}gb_SVACEncodeConfig_SVCParam;

typedef struct
{
	int TimeFlag; //绝对时间信息开关，取值0：关闭，1：打开（可选）
	int EventFlag; // 监控事件信息开关，取值0：关闭，1：打开（可选）
	int AlertFlag; // 报警信息开关，取值0：关闭，1：打开（可选）
}gb_SVACEncodeConfig_SurveillanceParam;

typedef struct
{
	int EncryptionFlag; //加密开关，取值0：关闭，1：打开（可选）
	int AuthenticationFlag; // 认证开关，取值0：关闭，1：打开（可选）
}gb_SVACEncodeConfig_EncryptParam;

typedef struct
{
	int AudioRecognitionFlag; //声音识别特征参数开关，取值0：关闭，1：打开
}gb_SVACEncodeConfig_AudioParam;

typedef struct
{
	int ROIParamFlag; // 0-禁用, 1-启用
	gb_SVACEncodeConfig_ROIParam ROIParam; // 感兴趣区域参数（可选）
	int SVCParamFlag; // 0-禁用, 1-启用
	gb_SVACEncodeConfig_SVCParam SVCParam; // SVC参数（可选）
	int SurveillanceParamFlag; // 0-禁用, 1-启用
	gb_SVACEncodeConfig_SurveillanceParam SurveillanceParam; // 监控专用信息参数（可选）
	int EncryptParamFlag; // 0-禁用, 1-启用
	gb_SVACEncodeConfig_EncryptParam EncryptParam; // 加密与认证参数（可选）
	int AudioParamFlag; // 0-禁用, 1-启用
	gb_SVACEncodeConfig_AudioParam AudioParam; // 音频参数（可选）
}gb_ConfigDownload_SVACEncodeConfig;

typedef struct
{
	int SVCSTMMode; // 码流上传模式，取值0：基本层码流单独传输方式；1：基本层+1个增强层码流方式；2：基本层+2个增强层码流方式；3：基本层+3个增强层码流方式；
}gb_SVACDecodeConfig_SVCParam;

typedef struct
{
	int TimeShowFlag; // 绝对时间信息显示开关，取值0：关闭，1：打开（可选）
	int EventShowFlag; // 监控事件信息显示开关，取值0：关闭，1：打开（可选）
	int AlerShowtFlag; // 报警信息显示开关，取值0：关闭，1：打开（可选）
}gb_SVACDecodeConfig_SurveillanceParam;

typedef struct
{
	int SVCParamFlag; // 0-禁用, 1-启用
	gb_SVACDecodeConfig_SVCParam SVCParam; // SVC参数（可选）
	int SurveillanceParamFlag; // 0-禁用, 1-启用
	gb_SVACDecodeConfig_SurveillanceParam SurveillanceParam; // 监控专用信息参数（可选）
}gb_ConfigDownload_SVACDecodeConfig;



typedef struct 
{
	gb_BaseInfo_Query BaseInfo;
	int Result;  // 查询结果标志（必选）
	gb_ConfigDownload_BasicParam 	*BasicParam; // 基本参数配置（可选）
	gb_ConfigDownload_VideoParamOpt *VideoParamOpt; // 视频参数配置范围（可选），各可选参数以“/”分隔
	gb_ConfigDownload_VideoParamConfig *VideoParamConfig; // 视频参数当前配置（可选）
	gb_ConfigDownload_AudioParamOpt *AudioParamOpt; //音频参数配置范围（可选），各可选参数以“/”分隔
	gb_ConfigDownload_AudioParamConfig *AudioParamConfig; // 音频参数当前配置（可选）
	gb_ConfigDownload_SVACEncodeConfig *SVACEncodeConfig; // SVAC编码配置（可选）
	gb_ConfigDownload_SVACDecodeConfig *SVACDecodeConfig; // SVAC解码配置（可选）
}gb_Query_ConfigDownload_Rsp;





// 设备预置位
typedef struct
{	
	int Flag; // 0-禁用, 1-启用
	char PresetID[GB_STR_LEN]; // 预置位编码（必选）
	char PresetName[GB_NAME_STR_LEN]; // 预置位名称（必选）
}gb_Preset_Info;

typedef struct 
{
	gb_BaseInfo_Query BaseInfo;
	int Num;  // 列表项个数
	gb_Preset_Info PresetList[GB_PRESET_NUM]; // 当前配置的预置位记录，当未配置预置位时不填写
}gb_Query_PersetQuery_Rsp;





// 查询请求结构体
typedef struct 
{
	gb_BaseInfo_Query 	*DeviceStatus; // 设备状态查询请求
	gb_Catalog_Query 	*Catalog; // 设备目录信息查询请求 	
	gb_BaseInfo_Query 	*DeviceInfo; // 设备信息查询请求 
	gb_RecordInfo_Query 	*RecordInfo; // 文件目录检索请求
	gb_Alarm_Query 		*Alarm; // 报警查询
	gb_ConfigDownload_Query *ConfigDownload; // 设备配置查询
	gb_BaseInfo_Query 	*PersetQuery; // 设备预置位查询
}gb_Query_Req_Struct;




// 控制命令结构体
typedef struct 
{
	int Length; // 播放窗口长度像素值（必选）
	int Width; // 播放窗口宽度像素值（必选）
	int MidPointX; // 拉框中心的横轴坐标像素值（必选）
	int MidPointY; // 拉框中心的纵轴坐标像素值（必选）
	int LengthX; // 拉框长度像素值（必选）
	int LengthY; // 拉框宽度像素值（必选）
}gb_DragZoom_CTRL;

typedef struct 
{
	gb_BaseInfo_Query BaseInfo;
	char PTZCmd[GB_STR_LEN+1]; // 球机/云台控制命令（可选，控制码应符合附录A中A.3中的规定)
	int  TeleBoot; // 远程启动控制命令（可选）,1-启动
	int  RecordCmd; // 录像控制命令（可选）
	int  GuardCmd; // 报警布防/撤防命令（可选）
	int  AlarmCmd; // 报警复位命令（可选）,1-复位
	int DragZoomFlag;  //  0-无信息，1-拉框放大控制命令，2-拉框缩小控制命令（可选）
	gb_DragZoom_CTRL DragZoom;
}gb_DeviceControl_Req;

typedef struct 
{
	gb_BaseInfo_Query BaseInfo;
	int BasicParamFlag; //  0-无信息，1-有配置
	gb_ConfigDownload_BasicParam BasicParam; // 基本参数配置（可选）
}gb_DeviceConfig_Req;



typedef struct
{
	gb_DeviceControl_Req *DeviceControl;
	gb_DeviceConfig_Req *DeviceConfig;
}gb_Control_Req_Struct;

typedef struct 
{
	gb_BaseInfo_Query BaseInfo;
	int Result;  // 执行结果标志（必选）
}gb_DeviceControl_Rsp;




typedef struct 
{
	gb_BaseInfo_Query BaseInfo;
	int NotifyType; // 通知事件类型（必选），取值“121”表示历史媒体文件发送结束
}gb_MediaStatus_Notify;

/**************************      End Struct_DEF    ********************************/




/*************************      GB_API_FUNC    ***********************************************/

char * gb_buffer_find (const char *haystack, size_t haystack_len, const char *needle);
int gb_sip_messages_parse(osip_event_t **osip_event, char *buffer, size_t buffer_len);
void gb_sip_free(osip_event_t *se);
int gb_generating_register (osip_message_t ** reg, char *transport, char *from, char *proxy, char *contact, int expires, char *localip, int port, int ncseq);
int gb_create_authorization_header (osip_www_authenticate_t * wa, const char *rquri, const char *username, const char *passwd, const char *ha1, osip_authorization_t ** auth, const char *method, const char *pCNonce, int iNonceCount);
int gb_generating_MESSAGE(osip_message_t ** reg, char *transport, char *from, char *to,char *proxy, char *localip, int port, int ncseq, void *cmd_struct, gb_CommandType_enum cmdType);
int gb_generating_NOTIFY(osip_message_t ** reg, char *transport, char *from, char *to,char *proxy, char *localip, int port, int ncseq, void *cmd_struct, gb_CommandType_enum cmdType,char *Sub_State, char *reason,int expires);
int gb_build_response_message (osip_message_t ** dest, osip_dialog_t * dialog, int status, osip_message_t * request, char *content_type, char *body, int bodylen);
int gb_build_response_Contact (osip_message_t *response, char *localip, int port, char *username);
int gb_parser_Req_XML(char *buf, int *code, void **dest);
/*************************      END GB_API_FUNC    *******************************************/


#endif
