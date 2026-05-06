#ifndef AUDIO_FFT_H
#define AUDIO_FFT_H

// 通用音频FFT模块 - 硬件抽象层
// 提供原始FFT幅度数据，供Lua脚本自定义处理

// 初始化音频FFT模块（不启动采集）
int audio_fft_init();

// 启动音频采集和FFT处理
int audio_fft_start();

// 停止音频采集和FFT处理
void audio_fft_stop();

// 获取FFT原始幅度数据（512个频点）
const double* audio_fft_get_magnitude();

// 获取FFT数据长度
int audio_fft_get_magnitude_length();

// 清理资源
void audio_fft_cleanup();

#endif
