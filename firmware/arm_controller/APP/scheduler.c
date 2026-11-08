#include "scheduler.h"

typedef struct{
	void (*task_func)(void);
	uint32_t rate_ms;
	uint32_t last_ms;
}task_t;

/* 任务表：{任务函数, 周期 ms, 上次执行时刻} */
static task_t scheduler_task[] = {
		{debug_uart_task,5,0},
		{modbus_slave_task,5,0},
		{arm_manual_task,10,0},
		{hmi_uart_task,5,0},
		{arm_action_task,5,0},
		{arm_control_task,5,0},
		{buzzer_task,5,0},
};

static uint8_t task_num = 0;

void scheduler_init(void)
{
	task_num = sizeof(scheduler_task) / sizeof(task_t);
}

void scheduler_run(void)
{
	for(uint8_t i = 0;i < task_num; i++)
	{
		uint32_t now_time = g_tick;
		if(now_time - scheduler_task[i].last_ms >= scheduler_task[i].rate_ms)
		{
			scheduler_task[i].last_ms = now_time;
			scheduler_task[i].task_func();
		}
	}
}
