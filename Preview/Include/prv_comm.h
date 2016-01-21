/******************************************************************************

  Copyright (C), 
  
	******************************************************************************
	File Name     : prv_comm.h
	Version       : Initial Draft
	Author        : 
	Created       : 2010/06/29
	Description   : preview common header file
	History       :
	1.Date        : 
    Author        : 
    Modification  : Created file
	
******************************************************************************/

#ifndef __PRV_COMM_H__
#define __PRV_COMM_H__


#ifdef __cplusplus
#if __cplusplus
extern "C"{
#endif
#endif /* End of #ifdef __cplusplus */




	
#define CHECK_RET(express/*,name*/)\
			do{\
				HI_S32 __s32Ret;\
				__s32Ret = express;\
				if (HI_SUCCESS != __s32Ret)\
				{\
					TRACE(SCI_TRACE_HIGH, MOD_PRV, "\033[0;31m%s failed at %s: LINE: %d with %#010x: \033[0;33m%s\033[0;39m\n\033[0;39m", #express, __FUNCTION__, __LINE__, __s32Ret, PRV_GetErrMsg(__s32Ret));\
					return HI_FAILURE;\
				}\
			}while(0)


#define CHECK(express/*,name*/)\
			do{\
				HI_S32 __s32Ret;\
				__s32Ret = express;\
				if (HI_SUCCESS != __s32Ret)\
				{\
					TRACE(SCI_TRACE_HIGH, MOD_PRV, "\033[0;31m%s failed at %s: LINE: %d with %#010x: \033[0;33m%s\033[0;39m\n\033[0;39m", #express, __FUNCTION__, __LINE__, __s32Ret, PRV_GetErrMsg(__s32Ret));\
				}\
			}while(0)

#define CHECK2(express,retval)\
			do{\
				retval = express;\
				if (HI_SUCCESS != retval)\
				{\
					TRACE(SCI_TRACE_HIGH, MOD_PRV, "\033[0;31m%s failed at %s: LINE: %d with %#010x: \033[0;33m%s\033[0;39m\n\033[0;39m", #express, __FUNCTION__, __LINE__, retval, PRV_GetErrMsg(retval));\
				}\
			}while(0)
	


/* PRINT_COLOR     : 35(PURPLE) 34(BLUE) 33(YELLOW) 32(GREEN) 31(RED) */
#define PRINT_RED(s)        TRACE(SCI_TRACE_NORMAL, MOD_PRV, "\033[0;31m%s\033[0;39m", s)
#define PRINT_GREEN(s)      TRACE(SCI_TRACE_NORMAL, MOD_PRV, "\033[0;32m%s\033[0;39m", s)
#define PRINT_YELLOW(s)     TRACE(SCI_TRACE_NORMAL, MOD_PRV, "\033[0;33m%s\033[0;39m", s)
#define PRINT_BLUE(s)       TRACE(SCI_TRACE_NORMAL, MOD_PRV, "\033[0;34m%s\033[0;39m", s)
#define PRINT_PURPLE(s)     TRACE(SCI_TRACE_NORMAL, MOD_PRV, "\033[0;35m%s\033[0;39m", s)

#define TEXT_COLOR_RED(s)		"\033[0;31m"s"\033[0;39m"
#define TEXT_COLOR_GREEN(s)		"\033[0;32m"s"\033[0;39m"
#define TEXT_COLOR_YELLOW(s)	"\033[0;33m"s"\033[0;39m"
#define TEXT_COLOR_BLUE(s)		"\033[0;34m"s"\033[0;39m"
#define TEXT_COLOR_PURPLE(s)	"\033[0;35m"s"\033[0;39m"


/*
#define RET_SUCCESS(s)\
	do\
	{\
		TRACE(SCI_TRACE_NORMAL, MOD_PRV, "\033[0;32m%s %s%s\033[0;39m", __FUNCTION__, s, " SUCCESS!\n");\
		return HI_SUCCESS;\
	} while (0)
*/

#define RET_SUCCESS(s) do{return HI_SUCCESS;}while(0)

#define RET_FAILURE(s)\
	do\
	{\
		TRACE(SCI_TRACE_HIGH, MOD_PRV, "\033[0;31m%s line:%d %s%s\033[0;39m", __FUNCTION__, __LINE__, s, " FAILURE!\n");\
		return HI_FAILURE;\
	} while (0)







#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif /* End of #ifdef __cplusplus */

#endif
