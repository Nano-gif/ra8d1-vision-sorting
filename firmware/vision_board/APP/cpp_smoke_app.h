#ifndef CPP_SMOKE_APP_H
#define CPP_SMOKE_APP_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 从 C 代码调用一个最小的 C++ 函数（C++ 编译链冒烟测试）。
 *
 * 参数：
 * - a、b：参与计算的两个整数。
 *
 * 返回：
 * - 在 .cpp 文件中计算的确定值（a + b + 7）。
 *
 * 说明：
 * - 用于确认工程能编译 .cpp 源文件，且 C 代码能链接 extern "C" 的 C++ 封装；
 *   Edge Impulse 推理库同样依赖这条 C/C++ 混合编译链。
 */
int32_t cpp_smoke_add_from_cpp(int32_t a, int32_t b);

#ifdef __cplusplus
}
#endif

#endif /* CPP_SMOKE_APP_H */
