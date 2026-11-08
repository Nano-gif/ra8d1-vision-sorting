# Edge Impulse v15 Integration Notes

This folder is the reserved firmware component for the v15 96x96 Visual AD model.

Current status:

- Disabled by default through `APP_USING_EDGE_IMPULSE_V15`.
- The v15 export is copied into `edge_impulse_export/`.
- `ei_v15_inference.cpp` implements the stable C wrapper, but it is only compiled when `APP_USING_EDGE_IMPULSE_V15` is enabled.
- The board-side image preprocessing path is already implemented in `APP/ai_app.c`.
- The current model profile is fixed to `cap_v15_96`: input `96x96 RGB888`, grid `5x5`, threshold `9.3`.

Why it is disabled:

- The exported Edge Impulse package is a C++ library.
- The current RT-Thread Studio generated `Debug` makefile only builds `.c` files with `arm-none-eabi-gcc`.
- The generated linker command also uses `arm-none-eabi-gcc`, not `arm-none-eabi-g++`.
- Directly copying the full SDK into the project would add hundreds of `.c/.cc/.cpp` files and make errors hard to isolate.

Minimal integration sequence:

1. Enable C++ compilation in the project build flow. Done, verified by `cpp_smoke_cmd`.
2. Copy the v15 export into `edge_impulse_export/`. Done.
3. Add a small C wrapper around `run_classifier()` so the C application code only calls a stable C API. Done.
4. Enable `APP_USING_EDGE_IMPULSE_V15` for the first real SDK build test.
5. Add only the missing TensorFlow Micro source files reported by the compiler/linker.
6. Replace the temporary detect backend with `ei_v15_run_rgb88896()` after the SDK component links successfully.

Expected C wrapper boundary:

```c
int ei_v15_run_rgb88896(const uint8_t *input_rgb888,
                        uint32_t input_size,
                        ai_app_result_t *out_result);
```

The wrapper should convert Edge Impulse `ei_impulse_result_t` fields into `ai_app_result_t`:

- `visual_ad_result.max_value` -> `score_x10`
- `visual_ad_result.mean_value` -> `mean_x10`
- `visual_ad_count` -> `hot_cells`
- `visual_ad_grid_cells[]` -> `heatmap[]`

The wrapper input is the existing `APP/ai_app.c` output buffer:

- `input_rgb888`: `96x96 RGB888`
- `input_size`: must be `AI_APP_INPUT_BYTES`
- `signal.get_data`: packs each RGB pixel into one EI image feature value, `0xRRGGBB`

Do not change LVGL, CSV storage, camera overlay, or `detect_app` flow when adding the real model. The intended replacement point is only the inference backend.
