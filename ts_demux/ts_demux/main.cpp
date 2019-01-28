#include <stdio.h>
#include "ts_struct.h"
#include "ts_demux.h"

void __stdcall es_data(unsigned char* es_data, int es_data_len, TS_ESParam_S es_param, void* user_param)
{
	if(NULL == es_data || 0 > es_data_len)
	{
		return;
	}

	static FILE* fp_h264 = fopen("demo.h264", "wb+");
	if(NULL == fp_h264)
	{
		return;
	}
	static bool sps_write = false;
	static bool pps_write = false;

	if(TS_ES_TYPE_VIDEO == es_param.es_type)
	{
		if(false == sps_write)
		{
			if (TS_ES_FRAME_TYPE_SPS == es_param.video_param.frame_type)
			{
				fwrite(es_data, 1, es_data_len, fp_h264);
				sps_write = true;
				return;
			}
			else
			{
				return;
			}
		}

		if(false == pps_write)
		{
			if (TS_ES_FRAME_TYPE_PPS == es_param.video_param.frame_type)
			{
				fwrite(es_data, 1, es_data_len, fp_h264);
				pps_write = true;
				return;
			}
			else
			{
				return;
			}
		}

		if(	TS_ES_FRAME_TYPE_SPS == es_param.video_param.frame_type ||
			TS_ES_FRAME_TYPE_PPS == es_param.video_param.frame_type ||
			TS_ES_FRAME_TYPE_SEI == es_param.video_param.frame_type)
		{
			return;
		}

		fwrite(es_data, 1, es_data_len, fp_h264);
	}

	return;
}

int main()
{
	CParseTS parse_ts_instance;
	parse_ts_instance.init_parse();
	parse_ts_instance.set_es_callback(es_data, 0);

	char ts_data[188] = {0};
	FILE* fp = fopen("demo.ts", "rb");
	if(NULL == fp)
	{
		printf("failed to open ts file!");
		return -1;
	}

	int read_data_len = 0;
	while(read_data_len = fread(ts_data, 1, TS_PACKET_LEN, fp))
	{
		if(TS_PACKET_LEN != read_data_len) break;
		parse_ts_instance.put_pkt_data((unsigned char*)ts_data, TS_PACKET_LEN);
	}

	return 0;
}