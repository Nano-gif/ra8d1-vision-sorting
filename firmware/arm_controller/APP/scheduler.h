#ifndef __SCHEDULER_H_
#define __SCHEDULER_H_

#include "mydefine.h"

/* 调度器初始化 */
void scheduler_init(void);
/* 轮询执行到期任务，在主循环中反复调用 */
void scheduler_run(void);

#endif