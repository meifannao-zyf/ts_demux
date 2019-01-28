#pragma once

#define		ES_BUFFER		600000					/* ES 数据大小 */
#define		TS_PACKET_LEN	188						/* TS 数据包长度 */

/* pts解析宏声明 */
#ifndef AV_RB16
#   define AV_RB16(x)                        \
	((((const unsigned char*)(x))[0] << 8) | \
	((const unsigned char*)(x))[1])
#endif

/* 视频编码类型定义 */
typedef enum TS_VideoEncodeType_E
{
	TS_VIDEO_ENCODE_TYPE_INVALID = -1,
	TS_VIDEO_ENCODE_TYPE_H264
}TS_VideoEncodeType_E;

/* 音频编码类型定义 */
typedef enum TS_AudioEncodeType_E
{
	TS_AUDIO_ENCODE_TYPE_INVALID = -1,
	TS_AUDIO_ENCODE_TYPE_PCMA,						//g711
	TS_AUDIO_ENCODE_TYPE_AAC						//aac
}TS_AudioEncodeType_E;

/* ES数据的类型定义 */
typedef enum TS_ESType_E
{
	TS_ES_TYPE_INVALID = -1,
	TS_ES_TYPE_VIDEO,
	TS_ES_TYPE_AUDIO
}TS_ESType_E;

/* TS数据包类型定义 */
typedef enum TS_TSPacketType_E
{
	TS_TS_PACKET_TYPE_INVALID = -1,
	TS_TS_PACKET_TYPE_PAT,
	TS_TS_PACKET_TYPE_PMT,
	TS_TS_PACKET_TYPE_VIDEO,
	TS_TS_PACKET_TYPE_AUDIO
}TS_TSPacketType_E;

/* ES数据帧类型定义 */
typedef enum TS_ESFrameType_E
{
	TS_ES_FRAME_TYPE_INVALID = -1,
	TS_ES_FRAME_TYPE_DATA = 2,
	TS_ES_FRAME_TYPE_IDR = 5,
	TS_ES_FRAME_TYPE_SEI = 6,
	TS_ES_FRAME_TYPE_SPS = 7,
	TS_ES_FRAME_TYPE_PPS = 8,
}TS_ESFrameType_E;

/* ES回调数据之中的ES参数之中的视频参数定义 */
typedef struct TS_ESVideoParam_S
{
	TS_VideoEncodeType_E video_encode_type;			//视频编码类型
	bool is_i_frame;								//是否为关键帧
	__int64 pts;									//pts
	__int64 dts;									//dts
	TS_ESFrameType_E frame_type;					//帧类型

	TS_ESVideoParam_S()
	{
		video_encode_type = TS_VIDEO_ENCODE_TYPE_INVALID;
		is_i_frame = false;
		pts = 0;
		dts = 0;
		frame_type = TS_ES_FRAME_TYPE_INVALID;
	}
}TS_ESVideoParam_S;

/* ES回调数据之中的ES参数之中的音频参数定义 */
typedef struct TS_ESAudioParam_S
{
	TS_AudioEncodeType_E audio_encode_type;			//音频编码类型
	int channels;									//通道数
	int samples_rate;								//音频采样率
	__int64 pts;									//pts

	TS_ESAudioParam_S()
	{
		audio_encode_type = TS_AUDIO_ENCODE_TYPE_INVALID;
		channels = 1;								//g711默认配置
		samples_rate = 8000;	
		pts = 0;
	}
}TS_ESAudioParam_S;

/* ES回调之中的ES参数定义 */
typedef struct TS_ESParam_S
{
	TS_ESType_E es_type;							//es数据类型
	TS_ESVideoParam_S video_param;					//视频参数, 当es数据类型为视频时有效
	TS_ESAudioParam_S audio_param;					//音频参数, 当es数据类型为音频时有效

	TS_ESParam_S()
	{
		es_type = TS_ES_TYPE_INVALID;
	}
}TS_ESParam_S;

/**
* @brief ES数据回调函数
* @param[in] es_data	 ES数据内容
* @param[in] es_data_len ES数据内容长度
* @param[in] es_param	 ES数据参数
* @param[in] user_param	 用户参数
*
* @return void
* @note 当前未实现
*/
typedef void (__stdcall *es_callback)(unsigned char* es_data, int es_data_len, TS_ESParam_S es_param, void* user_param);
