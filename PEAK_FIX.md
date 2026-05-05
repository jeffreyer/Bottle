# Peak下落问题修复

## 问题描述
除了第一个peak外，其他peak固定在顶部不下落。

## 根本原因
原代码在for循环内使用 `every 90ms`：
```bottle
for x in spectrum {
  peaks[x] = max(peaks[x] - 1, spectrum[x]) every 90ms
}
```

编译器为整个 `every` 语句只分配了一个定时器变量 `_tick_N`，导致：
- 第一次循环（x=0）：检查定时器，更新 peaks[0]，重置定时器
- 后续循环（x=1..16）：定时器刚被重置，条件不满足，跳过更新

结果：只有 peaks[0] 会下落。

## 解决方案
将peak下落逻辑移到独立循环，`every 90ms` 应用到整个循环：

```bottle
for x in spectrum {
  peaks[x] = max(peaks[x], spectrum[x])  // 每帧更新peak
}

for x in spectrum {
  peaks[x] = max(peaks[x] - 1, spectrum[x])  // 每90ms所有peak一起下落
} every 90ms

for x in spectrum {
  peak_y[x] = max(peaks[x] - 1, 0)  // 计算显示位置
}
```

## 修改的文件
- `modules_dev/rhythm_spectrum/main.bottle` (第30-42行)

## 测试步骤
1. 在VS Code中打开PlatformIO项目
2. 点击底部状态栏的"Build"按钮编译固件
3. 点击"Upload"按钮上传到设备
4. 观察LED显示，确认所有peak都能正常下落

## 预期结果
所有17个频谱柱的peak应该每90ms下落一个单位，而不是固定在顶部。
