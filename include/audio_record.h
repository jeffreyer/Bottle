#ifndef AUDIO_RECORD_H
#define AUDIO_RECORD_H

#include <stdint.h>

// 音频录制模块 - 提供原始音频数据录制功能

// 初始化音频录制模块
int audio_record_init();

// 开始录制到指定文件
// file_path: 文件路径（例如 "/extflash/rec_001.pcm"）
// 返回: 0=成功, <0=失败
int audio_record_start(const char* file_path);

// 停止录制
void audio_record_stop();

// 检查是否正在录制
bool audio_record_is_recording();

// 获取当前录制的字节数
uint32_t audio_record_get_bytes_written();

// 清理资源
void audio_record_cleanup();

#endif
