#pragma once

/*  
*  解复用ts类声明
*/
class CParseTS
{
public:
	CParseTS();
	~CParseTS();

	/* 设置接收es裸流数据的数据回调函数 */
	int set_es_callback(es_callback es_cb, void* user_param);

	/* 初始化解复用 */
	int init_parse();

	/* 填充TS数据，数据长度必须以TS头起始，长度为ts packet整数倍 */
	void put_pkt_data(unsigned char* pkt_data, int pkt_data_len);

	/* TS流里面是否有音频数据 */
	bool has_audio_stream();

protected:
private:
	/**
	* @brief 解析一个完整的ts数据包
	*
	* @param[in] ts_data ts数据
	* @param[out] ts_type ts数据解析结果
	* @param[out] ts_head_offset ts数据头长度
	*
	*/
	int parse_a_ts_packet(unsigned char* ts_data, TS_TSPacketType_E &ts_type, int &ts_head_offset);

	/* 从音频pes数据包之中提取音频的采样率和通道数进行更新 */
	int update_es_audio_common_param(unsigned char* pes_data, int pes_data_len);

	/* 拷贝一个从pes数据包之中解析出来的视频裸流到视频ES缓存之中 */
	int cpy_data_to_video_es_memory(unsigned char* data, int data_len);

	/* 拷贝一个从pes数据包之中解析出来的音频裸流到音频ES缓存之中 */
	int cpy_data_to_audio_es_memory(unsigned char* data, int data_len);

	/* 判断pes包是否有效(起始于pes startcode) */
	bool is_pes_begin(unsigned char* pes_data);

	/* 判断es帧数据是否有效(起始于nalu startcode) */
	bool is_startcode_begin(unsigned char* es_data);

	/* 获取一个pes数据包之中负载ES数据的偏移位置 */
	unsigned char* get_es_pos(unsigned char* pes_data);

	/* 获取一个pes数据包之中负载ES数据的长度 */
	int get_es_length(unsigned char* pes_data, int pes_len);

	/* 从pes数据的pes头之中解析pts */
	unsigned __int64 get_es_pts(const unsigned char* pes_data);

	/* 从pes数据的pes头之中解析dts */
	unsigned __int64 get_es_dts(const unsigned char* pes_data);

	/* 判断一帧数据是否为关键帧 */
	bool es_is_i_frame(const unsigned char* es_data);

	/* 寻找一段裸数据之中nalu startcode的位置 */
	static unsigned char* find_nalu_startcode(unsigned char* data, int &data_len);

	/* 获取视频帧类型 */
	TS_ESFrameType_E get_video_frame_type(unsigned char* es_data);

	es_callback es_cb_;
	void* es_cb_user_param_;		// 保存的用户回调函数及用户参数

	int ts_pmt_pid_;				// pmt pid
	int ts_video_pid_;				// video pid
	int ts_audio_pid_;				// audio pid

	TS_ESParam_S es_param_;
	bool es_audio_param_get_;		// 是否已经更新过音频参数

	unsigned char* es_video_data_;  // 视频ES数据缓存
	int es_video_data_index_;       // 视频ES缓存数据的长度

	unsigned char* es_audio_data_;	// 音频ES数据缓存
	int es_audio_data_index_;		// 音频ES缓存数据的长度
};