#ifndef AUDIO_FFT_H
#define AUDIO_FFT_H

// 通用音频FFT模块 - 硬件抽象层
// 提供17个频段的频谱数据，供Lua脚本使用

// 初始化音频FFT模块（不启动采集）
int audio_fft_init();

// 启动音频采集和FFT处理
int audio_fft_start();

// 停止音频采集和FFT处理
void audio_fft_stop();

// 获取频谱数据（17个频段，0-255范围）
const float* audio_fft_get_spectrum();

// 清理资源
void audio_fft_cleanup();

#endif
