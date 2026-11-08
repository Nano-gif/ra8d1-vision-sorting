#include <rtthread.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <new>

#include "edge-impulse-sdk/porting/ei_classifier_porting.h"

EI_IMPULSE_ERROR ei_run_impulse_check_canceled()
{
    return EI_IMPULSE_OK;
}

EI_IMPULSE_ERROR ei_sleep(int32_t time_ms)
{
    if (time_ms > 0)
    {
        rt_thread_mdelay((rt_int32_t)time_ms);
    }

    return EI_IMPULSE_OK;
}

uint64_t ei_read_timer_ms()
{
    return (uint64_t)rt_tick_get_millisecond();
}

uint64_t ei_read_timer_us()
{
    return ei_read_timer_ms() * 1000ULL;
}

void ei_putchar(char c)
{
    rt_kprintf("%c", c);
}

char ei_getchar(void)
{
    return 0;
}

void ei_printf(const char *format, ...)
{
    char buffer[192];
    char rt_format[192];
    size_t in_index;
    size_t out_index = 0U;
    va_list args;

    /*
     * RT-Thread's formatter on this target does not expand %zu. EI uses it in
     * allocator diagnostics, so translate it to %u before forwarding.
     */
    for (in_index = 0U; (format[in_index] != '\0') && (out_index < (sizeof(rt_format) - 1U)); in_index++)
    {
        if ((format[in_index] == '%') &&
            (format[in_index + 1U] == 'z') &&
            (format[in_index + 2U] == 'u'))
        {
            rt_format[out_index++] = '%';
            if (out_index < (sizeof(rt_format) - 1U))
            {
                rt_format[out_index++] = 'u';
            }
            in_index += 2U;
        }
        else
        {
            rt_format[out_index++] = format[in_index];
        }
    }
    rt_format[out_index] = '\0';

    va_start(args, format);
    rt_vsnprintf(buffer, sizeof(buffer), rt_format, args);
    va_end(args);

    rt_kprintf("%s", buffer);
}

void ei_printf_float(float f)
{
    int32_t value_x1000 = (int32_t)(f * 1000.0f);

    if (value_x1000 < 0)
    {
        rt_kprintf("-");
        value_x1000 = -value_x1000;
    }

    rt_kprintf("%d.%03d",
               (int)(value_x1000 / 1000),
               (int)(value_x1000 % 1000));
}

void *ei_malloc(size_t size)
{
    return rt_malloc((rt_size_t)size);
}

void *ei_calloc(size_t nitems, size_t size)
{
    return rt_calloc((rt_size_t)nitems, (rt_size_t)size);
}

void ei_free(void *ptr)
{
    rt_free(ptr);
}

void *operator new(size_t size)
{
    return ei_malloc(size);
}

void *operator new[](size_t size)
{
    return ei_malloc(size);
}

void *operator new(size_t size, const std::nothrow_t &) noexcept
{
    return ei_malloc(size);
}

void *operator new[](size_t size, const std::nothrow_t &) noexcept
{
    return ei_malloc(size);
}

void operator delete(void *ptr) noexcept
{
    ei_free(ptr);
}

void operator delete[](void *ptr) noexcept
{
    ei_free(ptr);
}

void operator delete(void *ptr, size_t) noexcept
{
    ei_free(ptr);
}

void operator delete[](void *ptr, size_t) noexcept
{
    ei_free(ptr);
}

void operator delete(void *ptr, const std::nothrow_t &) noexcept
{
    ei_free(ptr);
}

void operator delete[](void *ptr, const std::nothrow_t &) noexcept
{
    ei_free(ptr);
}

extern "C" int __cxa_guard_acquire(uint64_t *guard_object)
{
    return ((*guard_object & 1ULL) == 0ULL) ? 1 : 0;
}

extern "C" void __cxa_guard_release(uint64_t *guard_object)
{
    *guard_object |= 1ULL;
}

extern "C" void __cxa_guard_abort(uint64_t *guard_object)
{
    (void)guard_object;
}

namespace std
{
void __throw_bad_function_call()
{
    RT_ASSERT(0);
    while (1)
    {
    }
}

void __throw_length_error(const char *)
{
    RT_ASSERT(0);
    while (1)
    {
    }
}
}

void DebugLog(const char *s)
{
    if (s != RT_NULL)
    {
        rt_kprintf("%s", s);
    }
}
