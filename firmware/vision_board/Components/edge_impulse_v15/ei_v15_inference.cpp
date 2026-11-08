/*
 * This file includes large parts of the Edge Impulse/TFLM inference path via
 * headers. RT-Thread Studio Debug builds compile project sources with -O0 by
 * default, which makes inference several times slower. Keep this translation
 * unit optimized even when the rest of the firmware is built as Debug.
 */
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize ("O3")
#endif

#include "ei_v15_inference.h"

#ifdef APP_USING_EDGE_IMPULSE_V15

#include <rtthread.h>
#include "hal_data.h"
#include <new>
#include <stdarg.h>
#include <string.h>

/*
 * Keep the TFLite tensor arena out of the RT-Thread heap. The v15 model needs
 * 183808 bytes, and repeated UI/camera activity can fragment the heap enough
 * to make dynamic arena allocation fail.
 */
#define EI_CLASSIFIER_ALLOCATION_STATIC 1
#define EI_TENSOR_ARENA_LOCATION .ai_sram
#define EI_V15_ENABLE_TFLM_TIMING 1
#define EI_V15_USE_STATIC_DSP_FEATURE_MATRIX 1
#define EI_V15_STATIC_DSP_FEATURE_FLOATS AI_APP_INPUT_BYTES

#include "edge-impulse-sdk/classifier/ei_run_classifier.h"
#include "edge-impulse-sdk/dsp/returntypes.hpp"

typedef struct
{
    uint32_t block_id;
    uint32_t model_size;
    uint32_t arena_size;
    uint64_t setup_us;
    uint64_t allocate_us;
    uint64_t input_us;
    uint64_t invoke_us;
    uint64_t output_us;
} ei_v15_tflm_timing_graph_t;

typedef struct
{
    const uint8_t *input_rgb888;
    uint32_t input_size;
} ei_v15_signal_ctx_t;

static ei_v15_signal_ctx_t g_ei_v15_signal_ctx;
static ei_impulse_handle_t *g_ei_v15_impulse_handle = RT_NULL;
alignas(ei_impulse_handle_t) static uint8_t g_ei_v15_impulse_handle_storage[sizeof(ei_impulse_handle_t)];
static uint64_t g_ei_v15_signal_us = 0ULL;
static ei_v15_tflm_timing_graph_t g_ei_v15_tflm_graphs[4];
static uint8_t g_ei_v15_tflm_graph_count = 0U;
static int8_t g_ei_v15_tflm_current_graph = -1;
static volatile rt_bool_t g_ei_v15_profile_next = RT_FALSE;
static rt_bool_t g_ei_v15_profile_active = RT_FALSE;
static rt_bool_t g_ei_v15_external_active = RT_FALSE;
static const uint8_t *g_ei_v15_graph0_model = RT_NULL;
static const uint8_t *g_ei_v15_graph1_model = RT_NULL;
static uint32_t g_ei_v15_graph0_model_size = 0U;
static uint32_t g_ei_v15_graph1_model_size = 0U;
static uint32_t g_ei_v15_graph0_arena_size = tflite_learn_933567_63_arena_size;
static uint32_t g_ei_v15_graph1_arena_size = tflite_learn_933567_64_arena_size;
static uint16_t g_ei_v15_threshold_x10 = AI_APP_THRESHOLD_X10;
/* EI image DSP writes one float feature per RGB byte before graph0 invoke. */
static float g_ei_v15_dsp_feature_buffer[EI_V15_STATIC_DSP_FEATURE_FLOATS]
BSP_ALIGN_VARIABLE(64)
BSP_PLACE_IN_SECTION(".sdram");

static void ei_v15_fixup_generated_globals(void);
static ei_impulse_handle_t *ei_v15_get_impulse_handle(void);
static int ei_v15_get_signal_data(size_t offset, size_t length, float *out_ptr);
static uint16_t ei_v15_float_to_x10(float value);
static uint8_t ei_v15_heat_value(float value);
static uint16_t ei_v15_i64_to_u16_ms(int64_t value);
static uint16_t ei_v15_u64_us_to_u16_ms(uint64_t value);
static uint64_t ei_v15_tflm_graph_total_us(uint8_t index);
static void ei_v15_tflm_timing_dump(const ei_impulse_result_t *ei_result);
static void ei_v15_convert_result(const ei_impulse_result_t *ei_result,
                                  ai_app_result_t *out_result);

void ei_v15_tflm_timing_reset(void);
void ei_v15_tflm_timing_begin_graph(uint32_t block_id, uint32_t model_size, uint32_t arena_size);
void ei_v15_tflm_timing_add_setup(uint64_t elapsed_us);
void ei_v15_tflm_timing_add_allocate(uint64_t elapsed_us);
void ei_v15_tflm_timing_add_input(uint64_t elapsed_us);
void ei_v15_tflm_timing_add_invoke(uint64_t elapsed_us);
void ei_v15_tflm_timing_add_output(uint64_t elapsed_us);
bool ei_v15_should_profile_tflm(void);
extern "C" float *ei_v15_get_static_dsp_feature_buffer(uint32_t required_floats);

int ei_v15_run_rgb88896(const uint8_t *input_rgb888,
                        uint32_t input_size,
                        ai_app_result_t *out_result)
{
    signal_t signal;
    ei_impulse_result_t ei_result;
    EI_IMPULSE_ERROR err;
    rt_tick_t tick_start;
    rt_tick_t tick_end;

    if ((input_rgb888 == RT_NULL) ||
        (input_size != AI_APP_INPUT_BYTES) ||
        (out_result == RT_NULL))
    {
        return -RT_ERROR;
    }

    memset(out_result, 0, sizeof(*out_result));
    memset(&ei_result, 0, sizeof(ei_result));
    g_ei_v15_signal_us = 0ULL;
    ei_v15_tflm_timing_reset();

    /*
     * signal.total_length is the raw feature count expected by EI.
     * For image models this is width * height. get_data returns one RGB888
     * pixel packed into one float value, which matches EI's image DSP input.
     */
    g_ei_v15_signal_ctx.input_rgb888 = input_rgb888;
    g_ei_v15_signal_ctx.input_size = input_size;
    signal.total_length = EI_CLASSIFIER_RAW_SAMPLE_COUNT;
    signal.get_data = &ei_v15_get_signal_data;

    tick_start = rt_tick_get();
    err = run_classifier(ei_v15_get_impulse_handle(), &signal, &ei_result, false);
    tick_end = rt_tick_get();

    if (err != EI_IMPULSE_OK)
    {
        rt_kprintf("[ei_v15] run_classifier failed: %d\n", (int)err);
        return -RT_ERROR;
    }

    ei_v15_tflm_timing_dump(&ei_result);

    ei_v15_convert_result(&ei_result, out_result);
    out_result->duration_ms = (uint16_t)((tick_end - tick_start) * 1000U / RT_TICK_PER_SECOND);
    out_result->dsp_ms = ei_v15_i64_to_u16_ms(ei_result.timing.dsp);
    out_result->classification_ms = ei_v15_i64_to_u16_ms(ei_result.timing.classification);
    out_result->anomaly_ms = ei_v15_i64_to_u16_ms(ei_result.timing.anomaly);
    out_result->postprocessing_ms = ei_v15_i64_to_u16_ms(ei_result.timing.postprocessing);
    out_result->signal_ms = ei_v15_u64_us_to_u16_ms(g_ei_v15_signal_us);
    out_result->tflm_setup_ms = ei_v15_u64_us_to_u16_ms(g_ei_v15_tflm_graphs[0].setup_us +
                                                        g_ei_v15_tflm_graphs[1].setup_us +
                                                        g_ei_v15_tflm_graphs[2].setup_us +
                                                        g_ei_v15_tflm_graphs[3].setup_us);
    out_result->tflm_allocate_ms = ei_v15_u64_us_to_u16_ms(g_ei_v15_tflm_graphs[0].allocate_us +
                                                           g_ei_v15_tflm_graphs[1].allocate_us +
                                                           g_ei_v15_tflm_graphs[2].allocate_us +
                                                           g_ei_v15_tflm_graphs[3].allocate_us);
    out_result->tflm_invoke_ms = ei_v15_u64_us_to_u16_ms(g_ei_v15_tflm_graphs[0].invoke_us +
                                                         g_ei_v15_tflm_graphs[1].invoke_us +
                                                         g_ei_v15_tflm_graphs[2].invoke_us +
                                                         g_ei_v15_tflm_graphs[3].invoke_us);
    out_result->tflm_output_ms = ei_v15_u64_us_to_u16_ms(g_ei_v15_tflm_graphs[0].output_us +
                                                         g_ei_v15_tflm_graphs[1].output_us +
                                                         g_ei_v15_tflm_graphs[2].output_us +
                                                         g_ei_v15_tflm_graphs[3].output_us);
    out_result->tflm_graph0_ms = ei_v15_u64_us_to_u16_ms(ei_v15_tflm_graph_total_us(0U));
    out_result->tflm_graph1_ms = ei_v15_u64_us_to_u16_ms(ei_v15_tflm_graph_total_us(1U));

    return RT_EOK;
}

void ei_v15_profile_next_inference(void)
{
    g_ei_v15_profile_next = RT_TRUE;
}

int ei_v15_use_external_models(const uint8_t *graph0_model,
                               uint32_t graph0_model_size,
                               uint32_t graph0_arena_size,
                               const uint8_t *graph1_model,
                               uint32_t graph1_model_size,
                               uint32_t graph1_arena_size,
                               uint16_t threshold_x10)
{
    if ((graph0_model == RT_NULL) || (graph1_model == RT_NULL) ||
        (graph0_model_size == 0U) || (graph1_model_size == 0U) ||
        (graph0_arena_size == 0U) || (graph1_arena_size == 0U) ||
        (threshold_x10 == 0U))
    {
        return -RT_ERROR;
    }

    g_ei_v15_graph0_model = graph0_model;
    g_ei_v15_graph0_model_size = graph0_model_size;
    g_ei_v15_graph0_arena_size = graph0_arena_size;
    g_ei_v15_graph1_model = graph1_model;
    g_ei_v15_graph1_model_size = graph1_model_size;
    g_ei_v15_graph1_arena_size = graph1_arena_size;
    g_ei_v15_threshold_x10 = threshold_x10;
    g_ei_v15_external_active = RT_TRUE;

    ei_v15_fixup_generated_globals();
    return RT_EOK;
}

void ei_v15_use_builtin_model(void)
{
    g_ei_v15_external_active = RT_FALSE;
    g_ei_v15_graph0_model = RT_NULL;
    g_ei_v15_graph1_model = RT_NULL;
    g_ei_v15_graph0_model_size = 0U;
    g_ei_v15_graph1_model_size = 0U;
    g_ei_v15_graph0_arena_size = tflite_learn_933567_63_arena_size;
    g_ei_v15_graph1_arena_size = tflite_learn_933567_64_arena_size;
    g_ei_v15_threshold_x10 = AI_APP_THRESHOLD_X10;

    ei_v15_fixup_generated_globals();
}

bool ei_v15_should_profile_tflm(void)
{
    return (g_ei_v15_profile_active == RT_TRUE);
}

extern "C" float *ei_v15_get_static_dsp_feature_buffer(uint32_t required_floats)
{
    if (required_floats > EI_V15_STATIC_DSP_FEATURE_FLOATS)
    {
        return RT_NULL;
    }

    return g_ei_v15_dsp_feature_buffer;
}

void ei_v15_tflm_timing_reset(void)
{
    memset(g_ei_v15_tflm_graphs, 0, sizeof(g_ei_v15_tflm_graphs));
    g_ei_v15_tflm_graph_count = 0U;
    g_ei_v15_tflm_current_graph = -1;
    g_ei_v15_profile_active = g_ei_v15_profile_next;
    g_ei_v15_profile_next = RT_FALSE;
}

void ei_v15_tflm_timing_begin_graph(uint32_t block_id, uint32_t model_size, uint32_t arena_size)
{
    if (g_ei_v15_tflm_graph_count >= (sizeof(g_ei_v15_tflm_graphs) / sizeof(g_ei_v15_tflm_graphs[0])))
    {
        g_ei_v15_tflm_current_graph = -1;
        return;
    }

    g_ei_v15_tflm_current_graph = (int8_t)g_ei_v15_tflm_graph_count;
    g_ei_v15_tflm_graphs[g_ei_v15_tflm_graph_count].block_id = block_id;
    g_ei_v15_tflm_graphs[g_ei_v15_tflm_graph_count].model_size = model_size;
    g_ei_v15_tflm_graphs[g_ei_v15_tflm_graph_count].arena_size = arena_size;
    g_ei_v15_tflm_graph_count++;
}

void ei_v15_tflm_timing_add_setup(uint64_t elapsed_us)
{
    if (g_ei_v15_tflm_current_graph >= 0)
    {
        g_ei_v15_tflm_graphs[g_ei_v15_tflm_current_graph].setup_us += elapsed_us;
    }
}

void ei_v15_tflm_timing_add_allocate(uint64_t elapsed_us)
{
    if (g_ei_v15_tflm_current_graph >= 0)
    {
        g_ei_v15_tflm_graphs[g_ei_v15_tflm_current_graph].allocate_us += elapsed_us;
    }
}

void ei_v15_tflm_timing_add_input(uint64_t elapsed_us)
{
    if (g_ei_v15_tflm_current_graph >= 0)
    {
        g_ei_v15_tflm_graphs[g_ei_v15_tflm_current_graph].input_us += elapsed_us;
    }
}

void ei_v15_tflm_timing_add_invoke(uint64_t elapsed_us)
{
    if (g_ei_v15_tflm_current_graph >= 0)
    {
        g_ei_v15_tflm_graphs[g_ei_v15_tflm_current_graph].invoke_us += elapsed_us;
    }
}

void ei_v15_tflm_timing_add_output(uint64_t elapsed_us)
{
    if (g_ei_v15_tflm_current_graph >= 0)
    {
        g_ei_v15_tflm_graphs[g_ei_v15_tflm_current_graph].output_us += elapsed_us;
    }
}

static ei_impulse_handle_t *ei_v15_get_impulse_handle(void)
{
    if (g_ei_v15_impulse_handle == RT_NULL)
    {
        ei_v15_fixup_generated_globals();

        /*
         * RT-Thread startup may not run C++ global constructors before msh
         * commands are used. Construct the EI handle explicitly so
         * process_impulse() always receives a valid handle->impulse pointer.
         */
        g_ei_v15_impulse_handle = new (g_ei_v15_impulse_handle_storage) ei_impulse_handle_t(&impulse_933567_7);
    }

    return g_ei_v15_impulse_handle;
}

static void ei_v15_fixup_generated_globals(void)
{
    ei_config_tflite_graph_t *graph_63 =
        const_cast<ei_config_tflite_graph_t *>(&ei_config_graph_933567_63);
    ei_config_tflite_graph_t *graph_64 =
        const_cast<ei_config_tflite_graph_t *>(&ei_config_graph_933567_64);

    if (g_ei_v15_external_active == RT_TRUE)
    {
        tflite_learn_933567_63 = g_ei_v15_graph0_model;
        tflite_learn_933567_63_len = g_ei_v15_graph0_model_size;
        graph_63->model = g_ei_v15_graph0_model;
        graph_63->model_size = g_ei_v15_graph0_model_size;
        graph_63->arena_size = g_ei_v15_graph0_arena_size;

        tflite_learn_933567_64 = g_ei_v15_graph1_model;
        tflite_learn_933567_64_len = g_ei_v15_graph1_model_size;
        graph_64->model = g_ei_v15_graph1_model;
        graph_64->model_size = g_ei_v15_graph1_model_size;
        graph_64->arena_size = g_ei_v15_graph1_arena_size;
    }
    else
    {
        /*
         * The EI export uses non-trivial C++ global initialization for INCBIN model
         * pointers. On this RT-Thread target those constructors are not guaranteed
         * to run before msh commands invoke inference, so rewrite the graph config
         * from linker-provided INCBIN symbols explicitly.
         */
        tflite_learn_933567_63 = gincbin_tflite_learn_933567_63_data;
        tflite_learn_933567_63_len = gincbin_tflite_learn_933567_63_size;
        graph_63->model = gincbin_tflite_learn_933567_63_data;
        graph_63->model_size = gincbin_tflite_learn_933567_63_size;
        graph_63->arena_size = tflite_learn_933567_63_arena_size;

        tflite_learn_933567_64 = gincbin_tflite_learn_933567_64_data;
        tflite_learn_933567_64_len = gincbin_tflite_learn_933567_64_size;
        graph_64->model = gincbin_tflite_learn_933567_64_data;
        graph_64->model_size = gincbin_tflite_learn_933567_64_size;
        graph_64->arena_size = tflite_learn_933567_64_arena_size;
    }

    ei_fill_result_visual_anomaly_f32_config_933567_64.threshold =
        (float)g_ei_v15_threshold_x10 / 10.0f;
}

static int ei_v15_get_signal_data(size_t offset, size_t length, float *out_ptr)
{
    size_t i;
    uint64_t start_us;

    if ((out_ptr == NULL) ||
        (g_ei_v15_signal_ctx.input_rgb888 == NULL) ||
        ((offset + length) > EI_CLASSIFIER_RAW_SAMPLE_COUNT))
    {
        return ei::EIDSP_PARAMETER_INVALID;
    }

    /*
     * EI image DSP expects one float per pixel, with RGB packed as:
     * 0xRRGGBB. The camera preprocessing has already produced RGB byte order.
     */
    start_us = ei_read_timer_us();

    for (i = 0; i < length; i++)
    {
        uint32_t pixel_index = (uint32_t)(offset + i);
        uint32_t byte_index = pixel_index * AI_APP_INPUT_CHANNELS;
        uint32_t packed_rgb = ((uint32_t)g_ei_v15_signal_ctx.input_rgb888[byte_index] << 16) |
                              ((uint32_t)g_ei_v15_signal_ctx.input_rgb888[byte_index + 1U] << 8) |
                              ((uint32_t)g_ei_v15_signal_ctx.input_rgb888[byte_index + 2U]);

        out_ptr[i] = (float)packed_rgb;
    }

    g_ei_v15_signal_us += ei_read_timer_us() - start_us;

    return ei::EIDSP_OK;
}

static uint16_t ei_v15_float_to_x10(float value)
{
    if (value <= 0.0f)
    {
        return 0U;
    }

    if (value > 6553.5f)
    {
        return 65535U;
    }

    return (uint16_t)((value * 10.0f) + 0.5f);
}

static uint8_t ei_v15_heat_value(float value)
{
    float threshold = (float)g_ei_v15_threshold_x10 / 10.0f;

    if (value <= 0.0f)
    {
        return 0U;
    }

    if (threshold <= 0.0f)
    {
        threshold = EI_CLASSIFIER_THRESHOLD;
    }

    if (value >= threshold)
    {
        return 180U;
    }

    return (uint8_t)((value * 180.0f / threshold) + 0.5f);
}

static uint16_t ei_v15_i64_to_u16_ms(int64_t value)
{
    if (value <= 0)
    {
        return 0U;
    }

    if (value > 65535)
    {
        return 65535U;
    }

    return (uint16_t)value;
}

static uint16_t ei_v15_u64_us_to_u16_ms(uint64_t value)
{
    value = (value + 500ULL) / 1000ULL;
    if (value > 65535ULL)
    {
        return 65535U;
    }

    return (uint16_t)value;
}

static uint64_t ei_v15_tflm_graph_total_us(uint8_t index)
{
    if (index >= g_ei_v15_tflm_graph_count)
    {
        return 0ULL;
    }

    return g_ei_v15_tflm_graphs[index].setup_us +
           g_ei_v15_tflm_graphs[index].allocate_us +
           g_ei_v15_tflm_graphs[index].input_us +
           g_ei_v15_tflm_graphs[index].invoke_us +
           g_ei_v15_tflm_graphs[index].output_us;
}

static void ei_v15_tflm_timing_dump(const ei_impulse_result_t *ei_result)
{
    uint8_t i;
    uint32_t graph_copy_ms = 0U;

    if ((g_ei_v15_profile_active != RT_TRUE) || (ei_result == RT_NULL))
    {
        return;
    }

    for (i = 0U; i < g_ei_v15_tflm_graph_count; i++)
    {
        rt_kprintf("[ei] graph%u block=%u setup=%u ms alloc=%u ms input=%u ms invoke=%u ms out=%u ms arena=%u model=%u\n",
                   (unsigned int)i,
                   (unsigned int)g_ei_v15_tflm_graphs[i].block_id,
                   (unsigned int)ei_v15_u64_us_to_u16_ms(g_ei_v15_tflm_graphs[i].setup_us),
                   (unsigned int)ei_v15_u64_us_to_u16_ms(g_ei_v15_tflm_graphs[i].allocate_us),
                   (unsigned int)ei_v15_u64_us_to_u16_ms(g_ei_v15_tflm_graphs[i].input_us),
                   (unsigned int)ei_v15_u64_us_to_u16_ms(g_ei_v15_tflm_graphs[i].invoke_us),
                   (unsigned int)ei_v15_u64_us_to_u16_ms(g_ei_v15_tflm_graphs[i].output_us),
                   (unsigned int)g_ei_v15_tflm_graphs[i].arena_size,
                   (unsigned int)g_ei_v15_tflm_graphs[i].model_size);
    }

    if (g_ei_v15_tflm_graph_count > 1U)
    {
        graph_copy_ms = (uint32_t)ei_v15_u64_us_to_u16_ms(g_ei_v15_tflm_graphs[0].output_us +
                                                          g_ei_v15_tflm_graphs[1].input_us);
    }

    rt_kprintf("[ei] graph0_invoke=%u ms graph0_alloc=%u ms\n",
               (unsigned int)ei_v15_u64_us_to_u16_ms(g_ei_v15_tflm_graphs[0].invoke_us),
               (unsigned int)ei_v15_u64_us_to_u16_ms(g_ei_v15_tflm_graphs[0].allocate_us));
    rt_kprintf("[ei] graph0->graph1_copy=%u ms\n", (unsigned int)graph_copy_ms);
    rt_kprintf("[ei] graph1_invoke=%u ms graph1_alloc=%u ms\n",
               (unsigned int)ei_v15_u64_us_to_u16_ms(g_ei_v15_tflm_graphs[1].invoke_us),
               (unsigned int)ei_v15_u64_us_to_u16_ms(g_ei_v15_tflm_graphs[1].allocate_us));
    rt_kprintf("[ei] postprocess=%u ms\n", (unsigned int)ei_v15_i64_to_u16_ms(ei_result->timing.postprocessing));
}

static void ei_v15_convert_result(const ei_impulse_result_t *ei_result,
                                  ai_app_result_t *out_result)
{
    uint32_t i;
    uint64_t start_us;

    start_us = ei_read_timer_us();
    memset(out_result, 0, sizeof(*out_result));
    out_result->grid_w = AI_APP_GRID_W;
    out_result->grid_h = AI_APP_GRID_H;
    out_result->threshold_x10 = g_ei_v15_threshold_x10;
    out_result->score_x10 = ei_v15_float_to_x10(ei_result->visual_ad_result.max_value);
    out_result->mean_x10 = ei_v15_float_to_x10(ei_result->visual_ad_result.mean_value);
    out_result->anomaly = (out_result->score_x10 > out_result->threshold_x10) ? RT_TRUE : RT_FALSE;

    for (i = 0; (i < ei_result->visual_ad_count) && (i < AI_APP_GRID_CELLS); i++)
    {
        const ei_impulse_result_bounding_box_t *cell = &ei_result->visual_ad_grid_cells[i];
        uint32_t grid_x = (uint32_t)cell->x / (AI_APP_INPUT_W / AI_APP_GRID_W);
        uint32_t grid_y = (uint32_t)cell->y / (AI_APP_INPUT_H / AI_APP_GRID_H);
        uint32_t grid_index;

        if (grid_x >= AI_APP_GRID_W)
        {
            grid_x = AI_APP_GRID_W - 1U;
        }
        if (grid_y >= AI_APP_GRID_H)
        {
            grid_y = AI_APP_GRID_H - 1U;
        }

        grid_index = grid_y * AI_APP_GRID_W + grid_x;
        out_result->heatmap[grid_index] = ei_v15_heat_value(cell->value);
        out_result->hot_cells++;
    }

    out_result->result_ms = ei_v15_u64_us_to_u16_ms(ei_read_timer_us() - start_us);
}

#endif /* APP_USING_EDGE_IMPULSE_V15 */
