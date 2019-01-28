/* 标准库头文件 */
#include <string>
/* 私有头文件 */
#include "ts_struct.h"
#include "ts_demux.h"

CParseTS::CParseTS():
es_cb_(NULL),
es_cb_user_param_(NULL),
es_audio_param_get_(false),
ts_pmt_pid_(-1),
ts_video_pid_(-1),
ts_audio_pid_(-1),
es_video_data_index_(0),
es_audio_data_index_(0),
es_video_data_(NULL),
es_audio_data_(NULL)
{
	return;
}

CParseTS::~CParseTS()
{
	if(NULL != es_video_data_)
	{
		free(es_video_data_);
		es_video_data_ = NULL;
	}

	if(NULL != es_audio_data_)
	{
		free(es_audio_data_);
		es_audio_data_ = NULL;
	}

	return;
}

/* 初始化解析TS流 */
int CParseTS::init_parse()
{
	es_video_data_ = (unsigned char*)malloc(ES_BUFFER);
	if(NULL == es_video_data_)
	{
		printf("failed to malloc video es cache memory!");
		return -1;
	}

	es_audio_data_ = (unsigned char*)malloc(ES_BUFFER);
	if(NULL == es_audio_data_)
	{
		printf("failed to malloc audio es cache memory!");
		return -1;
	}

	return 0;
}

/* 设置接收es裸流数据的数据回调函数 */
int CParseTS::set_es_callback(es_callback es_cb, void* user_param)
{
	es_cb_ = es_cb;
	es_cb_user_param_ = user_param;

	return 0;
}

/* 填充TS数据，数据长度必须以TS头起始，长度为ts packet整数倍 */
void CParseTS::put_pkt_data(unsigned char* pkt_data, int pkt_data_len)
{
	if(NULL == pkt_data || TS_PACKET_LEN > pkt_data_len)
	{	
		printf("invalid input param!");
		return;
	}

	/* 截取一个ts packet */
	unsigned char* ts_data_tmp = pkt_data;
	pkt_data += TS_PACKET_LEN;
	pkt_data_len -= TS_PACKET_LEN;

	int errcode = 0;

	/* 解析一个完整的ts packet */
	while(pkt_data_len >= 0)	
	{
		TS_TSPacketType_E ts_type;
		int ts_head_offset = 0;		//ts头长度

		/* 从ts packet之中解析ts packet type, ts头长度 */
		errcode = parse_a_ts_packet(ts_data_tmp, ts_type, ts_head_offset);
		if(0 != errcode)
		{
			/* 解析失败则截取下一个ts packet进行解析 */
			ts_data_tmp = pkt_data;
			pkt_data += TS_PACKET_LEN;
			pkt_data_len -= TS_PACKET_LEN;
			continue;
		}

		/* 当前ts packet负载为pat 数据， 且是第一次获取到该数据，则计算pmt pid */
		if(TS_TS_PACKET_TYPE_PAT == ts_type && -1 == ts_pmt_pid_)
		{
			/* 偏移数据至pat data起始 */
			unsigned char* pat_data = ts_data_tmp + ts_head_offset;

			/* 判断当前pat表是否可用，不可用则解析下一个 ts packet
			*  
			* pat data 之中的 
			* table id 应为 0x00 
			* current_next_indicator 表示当前pat表是否可用，如果该字段为0则当前pat表无效 
			*
			*/
			if((0x00 != pat_data[0]) || (0 == (pat_data[5]&0x01)))	
			{
				ts_data_tmp = pkt_data;
				pkt_data += TS_PACKET_LEN;
				pkt_data_len -= TS_PACKET_LEN;
				continue;
			}

			/* 计算section_length，section_length减去pat节目表之外的字节数为pat节目表字节数，除以每个节目表的长度4字节为节目表的数量 */
			char byte2_len_sz = pat_data[1]&0x0F;
			int byte2_len_i = (int)(byte2_len_sz<<8);
			int byte3_len_i = (int)pat_data[2];
			int cycle_count = (byte2_len_i+byte3_len_i-9)/4;		

			/* 查找节目表之中的节目编号program_number字段为 0x0001的节目，其为pmt，其节目id即pid */
			for(int i = 0; i != cycle_count; i++)
			{
				int index = 8 + 4*i;
				if(pat_data[index] == 0x00 && pat_data[index+1] == 0x01)
				{
					/* 计算pmt pid */
					char byte_high_sz = pat_data[index+2]&0x1F;
					int byte_higt_i = (int)(byte_high_sz<<8);
					int byte_low_i = (int)pat_data[index+3];

					ts_pmt_pid_ = byte_higt_i + byte_low_i;

					break;
				}
			}
		}
		/* 当前ts packet为pmt数据，且 video_id 和 audio_id 未解析完全，则解析 pmt data之中的 video_id 与 audio_id */
		else if(TS_TS_PACKET_TYPE_PMT == ts_type && (-1 == ts_video_pid_ || -1 == ts_audio_pid_))
		{
			/* 偏移至pmt data起始 */
			unsigned char* pmt_data = ts_data_tmp + ts_head_offset;

			/* 判断当前pmt表是否可用，不可用则解析下一个 ts packet
			*  
			* pmt data 之中的 
			* table id 应为 0x02 
			* current_next_indicator 表示当前pmt表是否可用，如果该字段为0则当前pmt表无效 
			*
			*/
			if((0x02 != pmt_data[0]) || (0 == (pmt_data[5]&0x01)))	
			{
				ts_data_tmp = pkt_data;
				pkt_data += TS_PACKET_LEN;
				pkt_data_len -= TS_PACKET_LEN;
				continue;
			}

			/* 计算section_length，section_length减去pmt节目表之外的字节数为pmt节目表字节数, 然后挨个解析pmt节目表，找到video节目表与audio节目表，解析编码类型和pid */
			char byte2_len_sz = pmt_data[1]&0x0F;
			int byte2_len_i = (int)(byte2_len_sz<<8);
			int byte3_len_i = (int)pmt_data[2];
			int cycle_len = byte2_len_i+byte3_len_i-13;		
			int index = 12;
			while(cycle_len > 0)
			{
				/* 节目表 stream_type == 0x1B 说明当前节目表为 H264 编码的视频节目表 */
				if(pmt_data[index] == 0x1B)	
				{
					/* 计算video pid，更新 encode_type */
					char byte_high_sz = pmt_data[index+1]&0x1F;
					int byte_higt_i = (int)(byte_high_sz<<8);
					int byte_low_i = (int)pmt_data[index+2];

					ts_video_pid_ = byte_higt_i + byte_low_i;

					es_param_.video_param.video_encode_type = TS_VIDEO_ENCODE_TYPE_H264;
				}
				/* 节目表 stream_type == 0x0F 说明当前节目表为 AAC 编码的音频节目表 */
				if(pmt_data[index] == 0x0F)	
				{
					/* 计算audio pid，更新 encode_type */
					char byte_high_sz = pmt_data[index+1]&0x1F;
					int byte_higt_i = (int)(byte_high_sz<<8);
					int byte_low_i = (int)pmt_data[index+2];

					ts_audio_pid_ = byte_higt_i + byte_low_i;

					es_param_.audio_param.audio_encode_type = TS_AUDIO_ENCODE_TYPE_AAC;		
				}
				/* 节目表 stream_type == 0x90 说明当前节目表为 G711 编码的音频节目表 */
				if(pmt_data[index] == 0x90)	//g711编码音频
				{
					/* 计算audio pid，更新 encode_type */
					char byte_high_sz = pmt_data[index+1]&0x1F;
					int byte_higt_i = (int)(byte_high_sz<<8);
					int byte_low_i = (int)pmt_data[index+2];

					ts_audio_pid_ = byte_higt_i + byte_low_i;

					es_param_.audio_param.audio_encode_type = TS_AUDIO_ENCODE_TYPE_PCMA;	//g711
				}

				/* 解析出ES_info_length字段，根据该字段计算下一个节目的位置 */
				int byte_high = pmt_data[index+3]&0x0F;
				int byte_low = pmt_data[index+4]&0xFF;
				int es_info_len = byte_high + byte_low;
				index += 4 + es_info_len + 1;
				cycle_len = cycle_len - es_info_len - 4 - 1;
			}
		}
		/* 当前ts packet负载为 video pes数据 */
		else if(TS_TS_PACKET_TYPE_VIDEO == ts_type)	
		{
			/* 跳过 ts 头偏移到 负载数据位置 */
			unsigned char* pes_data = ts_data_tmp + ts_head_offset;
			unsigned int pes_data_len = TS_PACKET_LEN - ts_head_offset;

			/* 找到pes数据包之中的es裸流的位置 */
			unsigned char* es_data = get_es_pos(pes_data);
			unsigned int es_length = get_es_length(pes_data, pes_data_len);

			/* 如果当前负载不是以pes头起始，或者es 数据不是从nalu开始则将负载数据拷贝到视频缓存
			*
			* 理论上来说ts的每一个 pes 包都负载一帧数据, pes包长度超过pes包长度字段能表示的长度范围后以0填充，一般为I帧; 海康的好像不理论
			*/
			if(false == is_pes_begin(pes_data) || false == is_startcode_begin(es_data))
			{
				cpy_data_to_video_es_memory(es_data, es_length);
			}
			/* 否则先触发ES数据回调函数将ES缓存中数据回调后再缓存数据负载 */
			else
			{
				unsigned char* es_all_v = es_video_data_;
				int es_all_len = es_video_data_index_;

				do
				{
					/* 判断当前缓存之中的数据是不是以nalu起始的完整的数据 */
					int remain_data_len_f = es_all_len;
					unsigned char* nalu_head_first = find_nalu_startcode(es_all_v, remain_data_len_f);
					/* 不是完整的数据则跳过当前数据，清空缓存 */
					if(NULL == nalu_head_first)
					{
						break;
					}

					while(5 < remain_data_len_f)	//nalu head len
					{
						/* 过滤掉不关心的数据， 此处过滤掉分隔符帧*/
						if(0x09 == nalu_head_first[4])	//宇视码流有0x0000000109分隔符
						{
							nalu_head_first += 4;
							remain_data_len_f -= 4;
							nalu_head_first = find_nalu_startcode(nalu_head_first, remain_data_len_f);
							if(NULL == nalu_head_first)
							{
								break;
							}
						}

						/* 判断缓存之中是否为超过一个nalu的数据 */
						int remain_data_len_s = remain_data_len_f-4;
						unsigned char* nalu_head_second = find_nalu_startcode(nalu_head_first+4, remain_data_len_s);
						/* 不是 */
						if(NULL == nalu_head_second)
						{
							/* 触发视频流回调 */
							es_param_.video_param.is_i_frame = es_is_i_frame((const unsigned char*)nalu_head_first);
							es_param_.video_param.frame_type = get_video_frame_type(nalu_head_first);
							es_param_.es_type = TS_ES_TYPE_VIDEO;
							if(NULL != es_cb_)
								es_cb_((unsigned char*)nalu_head_first, remain_data_len_f, es_param_, es_cb_user_param_);

							break;
						}
						/* 是 */
						else
						{
							/* 触发视频流回调 */
							es_param_.video_param.is_i_frame = es_is_i_frame((const unsigned char*)nalu_head_first);
							es_param_.video_param.frame_type = get_video_frame_type(nalu_head_first);
							es_param_.es_type = TS_ES_TYPE_VIDEO;

							int touch_data_len = remain_data_len_f - remain_data_len_s;
							if(NULL != es_cb_)
								es_cb_((unsigned char*)nalu_head_first, touch_data_len, es_param_, es_cb_user_param_);

							/* 继续解析下一个nalu并进行回调 */
							nalu_head_first = nalu_head_second;
							remain_data_len_f = remain_data_len_s;
						}

					}
				}while(false);

				/* 清空视频ES缓存 */
				memset(es_video_data_, 0, ES_BUFFER);
				es_video_data_index_ = 0;
				es_param_.video_param.is_i_frame = false;

				/* 更新下一帧数据的pts和dts*/
				es_param_.video_param.pts = get_es_pts(pes_data);		//获取pts
				es_param_.video_param.dts = get_es_dts(pes_data);

				/* 缓存这一个ts包的负载 */
				cpy_data_to_video_es_memory(es_data, es_length);
			}
		}
		/* 当前ts packet负载为 audio pes数据 */
		else if(TS_TS_PACKET_TYPE_AUDIO == ts_type)	
		{
			/* 跳到ts负载位置 */
			unsigned char* pes_data = ts_data_tmp + ts_head_offset;
			unsigned int pes_data_len = TS_PACKET_LEN - ts_head_offset;

			/* ts的数据音频采样率和通道数没有更新的时候先更新采样率和通道数 */
			if(!es_audio_param_get_)
			{
				if(update_es_audio_common_param(pes_data, pes_data_len))
				{
					/* 更新失败则直接解析下一个ts packet */
					ts_data_tmp = pkt_data;
					pkt_data += TS_PACKET_LEN;
					pkt_data_len -= TS_PACKET_LEN;
					continue;
				}
				es_audio_param_get_ = true;
				continue;
			}

			/* 跳到裸流位置 */
			unsigned char* es_data = get_es_pos(pes_data);
			unsigned int es_length = get_es_length(pes_data, pes_data_len);

			/* 一个pes包为一帧音频帧，如果遇到pes header则音频缓存之中为一个完整的 音频帧则触发ES回调，反之则进行数据缓存 */
			if(!is_pes_begin(pes_data))
			{
				cpy_data_to_audio_es_memory(es_data, es_length);
			}
			else
			{
				/* 触发回调 */
				es_param_.es_type = TS_ES_TYPE_AUDIO;
				if (es_cb_)
					es_cb_((unsigned char*)es_audio_data_, es_audio_data_index_, es_param_, es_cb_user_param_);

				/* 清空缓存 */
				memset(es_audio_data_, 0, ES_BUFFER);
				es_audio_data_index_ = 0;

				/* 计算新一帧数据的pts，并缓存新的一帧数据 */
				es_param_.audio_param.pts = get_es_pts(pes_data);		//获取pts
				cpy_data_to_audio_es_memory(es_data, es_length);
			}
		}
		else
		{
			/* unknow ts format */
		}

		/* 解析紧接着的下一个ts packet */
		ts_data_tmp = pkt_data;
		pkt_data += TS_PACKET_LEN;
		pkt_data_len -= TS_PACKET_LEN;
	}

	return ;
}

/**
* @brief 解析一个完整的ts数据包
*
* @param[in] ts_data ts数据
* @param[out] ts_type ts数据解析结果
* @param[out] ts_head_offset ts数据头长度
*
*/
int CParseTS::parse_a_ts_packet(unsigned char* ts_data, TS_TSPacketType_E &ts_type, int &ts_head_offset)
{
	/* 校验TS包头 */
	if(0x47 != ts_data[0])		
	{
		return -1;
	}

	/* 解析 adaptation_field_control 字段的负载标识，判断当前TS包是否含有负载，对没有含有负载的TS数据包进行过滤 */
	if(0 == (ts_data[3]&0x10))	
	{
		return -1;
	}

	/* 计算TS包头(这样计算对接解析宇视，海康，松下，霍尼韦尔厂商的TS码流没有问题)
	*
	*  解析 adaptation_field_control 字段之中的自适应标识，判断当前TS包是否有自适应字段，如果有自适应字段长度则把自适应字段加入TS包头长度之中，
	*如果有自适应字段，则adaptation_field_control字段后紧接着一个字节标识在该字节后的自适应字段的长度是多少。
	*
	*  adaptation_field_control 字段：TS 4字节包头之中的最后一个字节的高两位
	*  ‘00’保留；‘01’为无自适应域，仅含有效负载；‘10’为仅含自适应域，无有效负载；‘11’为同时带有自适应域和有效负载
	*/
	ts_head_offset = 4;			
	if(0 != (ts_data[3]&0x20))
	{
		ts_head_offset += 1;	
		int adaptation_field_length = (int)ts_data[4];		
		ts_head_offset += adaptation_field_length;
	}

	/* 以下代码解析TS包头之中的PID字段(TS包头之中第二个字节的低5位和TS包头之中第三个字节)，判断当前TS数据包的类型 */
	
	/* 如果 PID == 0x00 则当前TS包负载为PAT */
	if(0 == (ts_data[1]&0x1F) && 0 == (ts_data[2]&0xFF))
	{
		ts_type = TS_TS_PACKET_TYPE_PAT;
		/* 如果当前数据包是PAT/PMT，则TS负载的开始前在TS的自适应字段之后还有一个调整字段，自适应字段后的第一个字节标识其后紧跟着的调整字段长度是多少 */
		ts_head_offset += ts_data[ts_head_offset] + 1;

		return 0;
	}

	/* 如果当前TS包不是PAT，且PMT PID未被解析出来，则直接返回失败 */
	if(-1 == ts_pmt_pid_)
		return -1;

	/* 如果 PID = PMT PID 则当前TS数据包负载为 PMT */
	int pmt_pid_13bit = ts_pmt_pid_&0x00001fff;
	int pmt_pid_low_8bit = pmt_pid_13bit&0x000000ff;
	int pmt_pid_high_8bit = pmt_pid_13bit>>8;
	if(pmt_pid_high_8bit == (ts_data[1]&0x1F) && pmt_pid_low_8bit == ts_data[2])
	{
		ts_type = TS_TS_PACKET_TYPE_PMT;
		/* 如果当前数据包是PAT/PMT，则TS负载的开始前在TS的自适应字段之后还有一个调整字段，自适应字段后的第一个字节标识其后紧跟着的调整字段长度是多少 */
		ts_head_offset += ts_data[ts_head_offset] + 1;

		return 0;
	}

	/* 如果当前TS包不是PAT和PMT，且VIDEO PID未被解析出来，则直接返回失败 */
	if(-1 == ts_video_pid_)
		return -1;

	/* 如果 PID = VIDEO PID 则当前TS数据包负载为 VIDEO */
	int video_pid_13bit = ts_video_pid_&0x00001fff;
	int video_pid_low_8bit = video_pid_13bit&0x000000ff;
	int video_pid_high_8bit = video_pid_13bit>>8;
	if(video_pid_high_8bit == (ts_data[1]&0x1F) && video_pid_low_8bit == ts_data[2])
	{
		ts_type = TS_TS_PACKET_TYPE_VIDEO;

		return 0;
	}

	/* 如果当前TS包不是PAT、PMT和VIDEO，且AUDIO PID未被解析出来，则直接返回失败 */
	if(-1 == ts_audio_pid_)
		return -1;

	/* 如果 PID = VIDEO PID 则当前TS数据包负载为 AUDIO */
	int audio_pid_13bit = ts_audio_pid_&0x00001fff;
	int audio_pid_low_8bit = audio_pid_13bit&0x000000ff;
	int audio_pid_high_8bit = audio_pid_13bit>>8;
	if(audio_pid_high_8bit == (ts_data[1]&0x1F) && audio_pid_low_8bit == ts_data[2])
	{
		ts_type = TS_TS_PACKET_TYPE_AUDIO;

		return 0;
	}

	return -1;
}

/* 从音频pes数据包之中提取音频的采样率和通道数进行更新 */
int CParseTS::update_es_audio_common_param(unsigned char* pes_data, int pes_data_len)
{
	/* 入参有效值判断， 长度无效的包也进行过滤 */
	if(NULL == pes_data || 9 > pes_data_len)		
	{
		return -1;
	}

	/* 根据pes包的startcode判断当前数据包是否为完整pes包，不是的数据包也进行过滤 */
	if(false == is_pes_begin(pes_data))
	{
		return -1;
	}

	/* 如果当前TS流之中的音频编码类型为g711, 则通道数与采样率是固定数值 */
	if (es_param_.audio_param.audio_encode_type == TS_AUDIO_ENCODE_TYPE_PCMA)
	{
		/* g711通道数固定为1， 采样率固定为8000 */
		es_param_.audio_param.channels = 1;
		es_param_.audio_param.samples_rate = 8000;
	}
	/* 如果当前TS流之中的音频编码类型为aac，则通道数和采样率根据aac数据之中的adts头进行解析 */
	else if (es_param_.audio_param.audio_encode_type == TS_AUDIO_ENCODE_TYPE_AAC)
	{
		/* 计算pes包头长度(9字节的固定长度+可选长度，第9个字节的数据内容为固定长度后可选包头的长度) */
		int es_begin_distance = pes_data[8]+9;
		/* pes负载数据不足三个字节的时候直接过滤(三个字节的负载时解析通道数与采样率的关键) */
		if(es_begin_distance+3 > pes_data_len)		
		{
			return -1;
		}

		/* pes的负载数据如果不是adts startcode(0xFFF)则直接返回错误 */
		if(!(0xFF == (pes_data[es_begin_distance]&0xFF) && 0xF0 == (pes_data[es_begin_distance]&0xF0)))		//adts startcode
		{
			return -1;
		}

		/* 解析负载之中的 sampling_frequency_index 字段，根据解析出来的字段内容对应采样率 */
		int frequency = (pes_data[es_begin_distance+2]&0x3C)>>2;
		if(0 == frequency)
		{
			es_param_.audio_param.samples_rate = 96000;
		}
		else if(1 == frequency)
		{
			es_param_.audio_param.samples_rate = 88200;
		}
		else if(2 == frequency)
		{
			es_param_.audio_param.samples_rate = 64000;
		}
		else if(3 == frequency)
		{
			es_param_.audio_param.samples_rate = 48000;
		}
		else if(4 == frequency)
		{
			es_param_.audio_param.samples_rate = 44100;
		}
		else if(5 == frequency)
		{
			es_param_.audio_param.samples_rate = 32000;
		}
		else if(6 == frequency)
		{
			es_param_.audio_param.samples_rate = 24000;
		}
		else if(7 == frequency)
		{
			es_param_.audio_param.samples_rate = 22050;
		}
		else if(8 == frequency)
		{
			es_param_.audio_param.samples_rate = 16000;
		}
		else if(9 == frequency)
		{
			es_param_.audio_param.samples_rate = 12000;
		}
		else if(10 == frequency)
		{
			es_param_.audio_param.samples_rate = 11025;
		}
		else if(11 == frequency)
		{
			es_param_.audio_param.samples_rate = 8000;
		}
		else if(12 == frequency)
		{
			es_param_.audio_param.samples_rate = 7350;
		}
		else
		{
			printf("unkown audio frequency");
			return -1;
		}

		/* 解析负载之中的 channel_configuration 字段，根据解析出来的字段内容对应声道数 */
		int channles = ((pes_data[es_begin_distance+2]&0x01)<<2) + ((pes_data[es_begin_distance+3]&0xC0)>>6);
		if(0 == channles)						//这个地方发现 松下的相机解析出来的channles是 0，直接赋值 2 才会正常
		{
			es_param_.audio_param.channels = 2;	
		}
		else if(1 == channles)
		{
			es_param_.audio_param.channels = 1;
		}
		else if(2 == channles)
		{
			es_param_.audio_param.channels = 2;
		}
		else if(3 == channles)
		{
			es_param_.audio_param.channels = 3;
		}
		else if(4 == channles)
		{
			es_param_.audio_param.channels = 4;
		}
		else if(5 == channles)
		{
			es_param_.audio_param.channels = 5;
		}
		else if(6 == channles)
		{
			es_param_.audio_param.channels = 6;
		}
		else if(7 == channles)
		{
			es_param_.audio_param.channels = 8;
		}
		else
		{
			printf("unknow audio channle num!");
			return -1;
		}
	}
	else
	{
		printf("know audio decode type!");
		return  -1;
	}

	return 0;
}

/* 寻找一段裸数据之中nalu startcode的位置 */
unsigned char* CParseTS::find_nalu_startcode(unsigned char* data, int &data_len)
{
	if(NULL == data || data_len < 4)
	{
		return NULL; 
	}

	data += 3;
	data_len -= 3;

	while(data_len >= 0)
	{
		if(*data == 0x01)
		{
			if(0x00 == *(data-1) && 0x00 == *(data-2) && 0x00 == *(data-3))
			{
				data_len += 3;	//因为数据位置向前移动3个字节，所以data_len应该加3
				return data-3;	
			}
			else
			{
				data += 4;
				data_len -= 4;
				continue;
			} 	
		}
		data += 1;
		data_len -= 1;
	}

	return NULL;
}

/* 拷贝一个从pes数据包之中解析出来的视频裸流到视频ES缓存之中 */
int CParseTS::cpy_data_to_video_es_memory(unsigned char* data, int data_len)
{
	if(NULL == data || data_len <= 0)
	{
		return -1;
	}

	if(data_len <= ES_BUFFER - es_video_data_index_)
	{
		memcpy(es_video_data_+es_video_data_index_, data, data_len);
		es_video_data_index_ += data_len;
		return data_len;
	}
	else
	{
		return -1;
	}
}

/* 拷贝一个从pes数据包之中解析出来的音频裸流到音频ES缓存之中 */
int CParseTS::cpy_data_to_audio_es_memory(unsigned char* data, int data_len)
{
	if(NULL == data || data_len <= 0)
	{
		return -1;
	}

	if(data_len <= ES_BUFFER - es_audio_data_index_)
	{
		memcpy(es_audio_data_+es_audio_data_index_, data, data_len);
		es_audio_data_index_ += data_len;
		return data_len;
	}
	else
	{
		return -1;
	}
}

/* 判断pes包是否有效(起始于pes startcode) */
bool CParseTS::is_pes_begin(unsigned char* pes_data)
{
	if(NULL == pes_data)
	{
		return false;
	}

	if(!(pes_data[0] == 0x00 && pes_data[1] == 0x00 && pes_data[2] == 0x01))
	{
		return false;
	}
	else
	{
		return true;
	}
}

/* 判断es帧数据是否有效(起始于nalu startcode) */
bool CParseTS::is_startcode_begin(unsigned char* es_data)
{
	if(NULL == es_data)
	{
		return false;
	}

	if(!(es_data[0] == 0x00 && es_data[1] == 0x00 && es_data[2] == 0x00 && es_data[3] == 0x01))
	{
		return false;
	}
	else
	{
		return true;
	}
}

/* 获取一个pes数据包之中负载ES数据的偏移位置 */
unsigned char* CParseTS::get_es_pos(unsigned char* pes_data)
{
	unsigned char* pes_es_pos =  (true == is_pes_begin(pes_data)) ? pes_data+pes_data[8]+9 : pes_data;	

	return pes_es_pos;
}

/* 获取一个pes数据包之中负载ES数据的长度 */
int CParseTS::get_es_length(unsigned char* pes_data, int pes_len)
{
	int es_data_len = (true == is_pes_begin(pes_data)) ? pes_len-pes_data[8]-9 : pes_len;

	return es_data_len;
}

/* 从pes数据的pes头之中解析pts */
unsigned __int64 CParseTS::get_es_pts(const unsigned char* pes_data) 
{
	pes_data += 9;		//偏移至pts所在位置
	return (unsigned __int64)(*pes_data & 0x0e) << 29 |
		(AV_RB16(pes_data + 1) >> 1) << 15 |
		AV_RB16(pes_data + 3) >> 1;
}

/* 从pes数据的pes头之中解析dts */
unsigned __int64 CParseTS::get_es_dts(const unsigned char* pes_data)
{
	pes_data += 14;		//偏移至dts所在位置
	return (unsigned __int64)(*pes_data & 0x0e) << 29 |
		(AV_RB16(pes_data + 1) >> 1) << 15 |
		AV_RB16(pes_data + 3) >> 1;
}

/* 判断一帧数据是否为关键帧， sps, pps也认为是关键帧 */
bool CParseTS::es_is_i_frame(const unsigned char* es_data)
{
	bool is_key_frame = ((es_data[4]&0x1F) == 5 || (es_data[4]&0x1F) == 2 || 
						 (es_data[4]&0x1F) == 7 || (es_data[4]&0x1F) == 8) ? true : false;

	return is_key_frame;
}

/* 获取视频帧类型 */
TS_ESFrameType_E CParseTS::get_video_frame_type(unsigned char* es_data)
{
	if(NULL == es_data)
	{
		return TS_ES_FRAME_TYPE_INVALID;
	}

	int type = es_data[4]&0x1F;
	switch(type){
	case 2:
		return TS_ES_FRAME_TYPE_DATA;
	case 5:
		return TS_ES_FRAME_TYPE_IDR;
	case 6:
		return TS_ES_FRAME_TYPE_SEI;
	case 7:
		return TS_ES_FRAME_TYPE_SPS;
	case 8:
		return TS_ES_FRAME_TYPE_PPS;
	}

	return TS_ES_FRAME_TYPE_INVALID;
}

/* TS流里面是否有音频数据 */
bool CParseTS::has_audio_stream()
{
	/* 如果寻找到音频 ts_audio_pid 会被更新掉的 */
	if(-1 == ts_audio_pid_)
	{
		return false;
	}
	else
	{
		return true;
	}
}