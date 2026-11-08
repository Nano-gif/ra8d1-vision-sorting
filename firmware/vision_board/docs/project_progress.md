# Vision Board Project Progress

## 2026-05-18

Current detect-model direction:

- Detect app now has a lightweight model-profile layer before the real Edge Impulse SDK is migrated.
- The default profile is `cap_v8_96`, matching the current preferred first real-inference target:
  - Camera source: `320x240 RGB565`.
  - Board-side ROI: crop `x=40,y=0,w=240,h=240`.
  - AI input: resize ROI to `96x96 RGB`.
  - Visual AD grid: `5x5`.
  - Visual GMM threshold: `5.9`.
- A second profile is reserved for `cap_v14_240`:
  - Camera source: `320x240 RGB565`.
  - Board-side ROI: crop `x=40,y=0,w=240,h=240`.
  - AI input: use `240x240 RGB` directly without downscale.
  - Visual AD grid: `14x14`.
  - Visual GMM threshold: `6.4`.
  - TFLite arena is much larger, about `1690086` bytes, so SDRAM allocation is expected.
- MSH command `detect_model_cmd` can show or switch the current fake-backend model profile:
  - `detect_model_cmd`: show current profile.
  - `detect_model_cmd v8` or `detect_model_cmd 96`: switch to `cap_v8_96`.
  - `detect_model_cmd v14` or `detect_model_cmd 240`: switch to `cap_v14_240`.
- Profile switching is intentionally blocked while detection is running or a single detection pass is pending. The model name, threshold, grid size, heatmap and CSV record must stay consistent for one complete inference result.
- The fake heatmap is no longer fixed to one hardcoded model. It uses the current profile grid size, so the UI and GLCDC overlay can be validated against both `5x5` and `14x14` layouts before real inference is connected.
- Settings page model management now has a first real loading boundary:
  - The model list is now scanned from `/vision/models`.
  - Each subfolder under `/vision/models` is treated as one selectable model package.
  - The folder name can be user-defined by product, for example `/vision/models/bottle_cap_v1`.
  - `model.ini` inside the package is the metadata source. First-version parsed keys are `name`, `profile`, `input`, `grid`, and `threshold`.
  - TFLite filenames are fixed to `model.tflite` and `anomaly_model.tflite` in the first version. `main` and `anomaly` keys in `model.ini` are only reserved for future custom filenames.
  - If `model.ini` is missing, the folder still appears and falls back to the current `cap_v8_96` metadata. This keeps early TF-card testing simple.
  - Clicking a model row records the selected model index.
  - The Load button calls `model_app_load_package()` through the board settings command handler.
  - `model_app` applies the `detect_app` profile from `model.ini` and checks the future package files `model.tflite` and `anomaly_model.tflite`.
  - Actual SDRAM allocation and reading the two `.tflite` files are intentionally left inside `model_app`, so later Edge Impulse integration does not require rewriting LVGL page code.
  - First real inference target remains fixed to `profile=cap_v8_96`. Custom product package names are allowed, but the profile should stay `cap_v8_96` until the 96x96 Edge Impulse migration is validated.

Current image-format decision:

- The board keeps saving collected photos as BMP for now. BMP is simple, lossless for the current RGB565 frame path, and useful as the raw debug/archive format on the TF card.
- Edge Impulse upload should use a PC-side preprocessing tool later, not direct BMP upload.
- The PC tool should read TF-card BMP files, crop the same ROI as the board, optionally resize to the selected model input size, and export PNG.
- PNG is preferred over JPG for the upload pipeline because PNG is lossless. That keeps color and pixel distribution closer to the board-side inference input, which matters for visual anomaly detection.
- The same preprocessing parameters must be shared by training/upload and board inference. If the model is trained with `320x240 -> crop 240x240 -> resize 96x96`, the board must use the same crop and resize path before calling Edge Impulse.
- A first PC helper exists at `pc_tools/vision_dataset_tool.py`:
  - `convert-images` converts board RGB565 BMP captures to PNG with `crop x=40,y=0,w=240,h=240` and default `96x96` resize.
  - `init-model-package` creates a standard model package template with `model.ini`.
  - `package-model` copies Edge Impulse `.tflite` files into the board package layout as `model.tflite` and `anomaly_model.tflite`.
  - This is intentionally a command-line tool first. A GUI/exe upper-computer can wrap it later after the preprocessing and package rules stabilize.

Next practical step:

- Keep the fake detect backend only as a UI/storage integration harness.
- Start the real Edge Impulse migration with the smaller `96x96` non-compiled TFLite profile first, because it has lower memory and latency risk.
- After the `96x96` chain is validated end-to-end, evaluate whether the `240x240` v14 model gives enough accuracy improvement to justify the larger arena and longer inference time.

## 2026-05-16

Current confirmed state:

- Camera basic bring-up is complete. OV5640 capture, CEU frame acquisition and GLCDC Layer 2 realtime preview are already usable on the 4.3 inch LCD.
- Camera preview is started automatically after UI initialization, so normal power-on no longer needs manual `camera_preview_lcd_start`.
- Detect page and Collect page both use the camera preview area. Other pages hide or suspend the camera layer to avoid covering LVGL popups or non-camera pages.
- Detect page preview performance after UI timer optimization is acceptable for the current stage, roughly around 13 to 15 FPS in the latest board-side test.
- Detect ROI, anomaly marker/heat area and Collect guide overlays are drawn on the camera layer path instead of using LVGL image widgets for realtime video.
- TF card storage is working through SDHI1 + DMAC + RT-Thread DFS/ElmFat. The card mounts at `/`, and `/vision` directory initialization is verified.
- TF card hotplug has a first usable implementation. Card removal triggers unmount/state reset, and reinsertion can rescan/remount the `sd` block device in the current test.
- `/vision/config.ini` now has a lightweight runtime config layer. `config_app` can load/save default model, UI language, PWM backlight brightness and detect-image saving mode.
- Settings page brightness is already connected to PWM. The config layer now keeps the RAM cache in sync when brightness/language changes, and the Settings page Save Config button writes the cache back to `config.ini`.
- Because SD mounting is asynchronous, Settings page retries config loading when the page is shown. This lets config values take effect even if LVGL was initialized before the TF card finished mounting.
- BMP photo saving is verified. Captured `320x240 RGB565` frames can be saved under `/vision/collect_photos/...` and opened normally on PC.
- Collect page is connected to real capture and storage. Capture and Auto now save real BMP files instead of only updating simulated progress.
- Collect Auto mode now captures only the currently selected angle. It stops when that angle reaches its target count, so the user can manually reposition the object before starting the next angle.
- Collection session behavior is aligned with the current UI:
  - Entering or leaving the Collect page does not create or end a session.
  - A session starts lazily on the first successful saved photo.
  - Capture and Auto are photo actions, not session boundaries.
  - Clear before full collection deletes current session photos and keeps the same session directory.
  - Clear after full collection ends the current session, and the next capture creates the next `session_xxx`.

Current storage layout:

- `/vision/models/cap_v8_96`: reserved for the first Edge Impulse model package.
- `/vision/collect_photos/session_xxx/0001.bmp`: collected training/reference photos.
- `/vision/detect_records/records.csv`: reserved for detection history records.
- `/vision/config.ini`: small runtime config file.

Important current decision:

- The next storage feature should be the detection record append/read framework, not the full AI result logic.
- Model-specific threshold, anomaly heatmap interpretation and pure-vision gating should wait until the Edge Impulse model is deployed and real output data is available.
- The preferred future object-arrival strategy is still pure-vision preprocessing/gating. External sensors are only a backup option.
- Board-side collected photos will continue to use BMP for now. Edge Impulse upload does not accept the current BMP files, so BMP is treated as the board-side raw capture/debug format, not the final cloud upload format.
- A PC-side preprocessing tool is reserved for later. It should batch convert BMP to Edge-Impulse-compatible PNG/JPG, and can also handle crop, resize, compression, folder selection and upload-set generation. This can later be packaged as a small PC exe or upper-computer tool.

Next practical step:

- Add a `storage_app` detection-record API that appends one CSV row to `/vision/detect_records/records.csv`.
- Use simulated OK/NG records first so the Data page and TF-card record flow can be tested before Edge Impulse inference is integrated.

Latest implementation notes:

- First-stage Detect app runtime framework has been added.
  - `APP/detect_app.c/.h` owns the Detect page backend state: Start, Stop, Refresh, stage state, latest snapshot and latest heatmap.
  - Current inference backend is intentionally fake. It runs once per second after Start, produces periodic OK/NG results, updates a model-profile-driven anomaly heatmap, and appends rows to `/vision/detect_records/records.csv`.
  - The fake backend uses the same public API shape expected by the later Edge Impulse backend, so future work should replace only the inference block instead of rewriting the UI/data/storage path.
  - Detect page now defaults to `STOPPED + IDLE` instead of simulated running.
  - Detect page result card, score bar, metric cards and heatmap now refresh when `frame_id` changes. The 80 ms stage timer is paused while the Detect page is idle to avoid unnecessary LVGL work.
  - GLCDC Layer 2 detect overlay is synchronized when a new Detect result is rendered, so the camera framebuffer heatmap follows the current backend result.
  - `APP/SConscript` includes `#/board/lvgl/ui` because `detect_app.h` depends on the shared `ui_data.h` types.
- Edge Impulse migration checkpoint:
  - The newest exported model checked locally is `edge_impulse_project/v14`.
  - v7/v8 exports use `96x96 RGB` input, Visual GMM threshold `5.9`; v7 is compiled, v8 is non-compiled TFLite.
  - v14 export uses `240x240 RGB` input, Visual GMM threshold `6.4`, two TFLite learning blocks, non-compiled TFLite.
  - Current camera frame is `320x240 RGB565`, so the real backend should crop ROI `x=40,y=0,size=240` and convert RGB565 ROI pixels to Edge Impulse RGB input.
  - v14 post-processing config is Visual Anomaly with `grid_size_x = 14`, `grid_size_y = 14`. The fake detect heatmap now follows the selected model profile, so it can simulate both v8 `5x5` and v14 `14x14`.
  - Edge Impulse Visual AD real output should come from `ei_impulse_result_t`:
    - `visual_ad_result.max_value`: max visual anomaly score.
    - `visual_ad_result.mean_value`: mean visual anomaly score.
    - `visual_ad_count`: number of anomalous grid cells after post-processing.
    - `visual_ad_grid_cells[i]`: per-cell rectangle and score. It reuses bounding-box fields: `x`, `y`, `width`, `height`, `value`.
  - The UI adapter should convert `visual_ad_grid_cells` into the project `ui_ai_heatmap_t` / `camera_app_heatmap_t` representation. Do not hardcode fake `5x5` blocks for the real model.
  - Visual AD threshold is represented in UI/storage as x10 fixed-point. v14 uses `6.4` (`threshold_x10 = 64`).
  - The v14 exported model reports about `1690086` bytes TFLite arena requirement, so the real TensorFlow Lite Micro arena should be planned in SDRAM.
  - The preferred first real-inference target is v8 `96x96`, because it reduces preprocessing, arena and inference-time risk while the integration path is still being validated.
- `camera_app` now keeps the latest GLCDC Layer 2 preview FPS as an internal `fps_x10` value instead of printing FPS repeatedly through serial logs.
- Detect page FPS metric is now refreshed from the real camera preview FPS once per second. The serial `lcd preview fps=...` debug print path is no longer needed for normal observation.
- `storage_app` now has a first detection-record API:
  - `storage_app_append_detect_record()` appends one result row to `/vision/detect_records/records.csv`.
  - `storage_app_load_recent_detect_records()` reads recent CSV rows back into RAM for the Data page.
  - `storage_detect_record_test_cmd` appends one fake OK/NG record from msh for board-side validation.
- Data page now refreshes its recent-record list when entering the Data page. If TF card records are readable, it shows records from `records.csv`; if storage is not ready, it falls back to the existing fake UI data.
- `records.csv` schema is now `timestamp,model,result,score,threshold,duration_ms,image_path`. If the old shorter header is detected during storage initialization, the file is rebuilt with the new header.
- `config_app` commands for board-side validation:
  - `config_show_cmd`: print the current RAM config cache.
  - `config_load_cmd`: reload `/vision/config.ini`.
  - `config_save_cmd`: save the current RAM cache to `/vision/config.ini`.

## 2026-05-13

Current architecture decision:

- Camera realtime preview uses GLCDC Layer 2 directly, not an LVGL image widget.
- The camera frame format is `RGB565`, size `320x240`.
- LVGL remains responsible for page shell, panels, buttons, status text, history, settings and non-realtime UI.
- ROI boxes and future anomaly heatmap overlays should be drawn into the Layer 2 framebuffer path, so the realtime camera path does not force LVGL to redraw full video frames.
- `ui_data` and `ui_ai_adapter` remain the stable boundary between the AI/data layer and UI layer.

Completed:

- OV5640 camera driver has been integrated from the Vision Board camera example.
- CEU capture works with `sensor_snapshot()`.
- `camera_test` can capture a valid `320x240 RGB565` frame.
- `camera_preview_start` can measure raw capture FPS.
- `camera_preview_lcd_start` can show the camera frame through GLCDC Layer 2 on the 4.3 inch LCD.
- GLCDC Layer 2 is configured as `320x240`, currently placed at Detect page camera window `(116,124)`.
- LCD preview is capped at 15 FPS to reserve CPU time for LVGL and later AI inference.
- Page-aware Layer 2 visibility has a first implementation: Detect and Collect pages show/resume preview, other pages hide Layer 2 and pause camera snapshots.
- Runtime Layer 2 coordinate switching is added. Detect uses `(116,124)`, Collect uses `(102,124)` without moving the LVGL Collect UI layout.
- Per-page preview FPS control is added. Detect uses 15 FPS, Collect uses 30 FPS.
- Collect guide popup now suspends/hides GLCDC Layer 2 while the popup is visible, then resumes preview when the popup is closed.
- Detect/Collect camera overlays are now driven from `camera_app`: Detect draws ROI and anomaly heatmap into the Layer 2 framebuffer, Collect draws the placement guide into the Layer 2 framebuffer.
- UI page switching now synchronizes Layer 2 overlay mode and detect heatmap/ROI data, so camera overlay behavior follows page state instead of always using a fixed preview mode.
- Inspection flow direction is now clarified: after pressing Start on screen, conveyor runs, object arrival is detected, conveyor pauses for capture/inference, then the downstream action depends on the inference result.
- A pure-vision gating idea is reserved as fallback or backup to sensor trigger: use conveyor-background segmentation plus ROI bounding-box completeness checks to avoid running AD on partially entered objects.
- TF card storage is brought up through SDHI1 + DMAC + RT-Thread DFS/ElmFat. The board can mount the card at `/`, create `/vision`, and write/read test files.
- BMP still-image saving is verified on board. `camera_save_bmp_test` captures one `320x240 RGB565` frame and saves a PC-readable BMP file under the collection photo area.
- Camera preview starts automatically after UI initialization. Users no longer need to run `camera_preview_lcd_start` from msh after every power-on.
- Collect page capture is now connected to real camera/storage flow. Pressing Capture requests the next preview frame, saves it as BMP into the active collect session, and only then increments the UI progress counter.
- Collect page Auto mode uses the same real capture/save path repeatedly instead of simulated progress.
- Collect page Auto mode is limited to the angle selected when Auto starts. After the current angle reaches its target count, Auto stops so the user can reposition the object before starting the next angle.
- Collect page Reset/Clear follows the agreed session boundary:
  - If the current collection is not complete, delete generated BMP files inside the active session, keep the same `session_xxx` directory, and restart numbering from `0001.bmp`.
  - If the current collection is complete, end the active session. The next successful capture creates the next unused `session_xxx` directory.
- TF-card hotplug handling now resets storage_app RAM state on unmount and actively rescans/remounts the `sd` block device after insertion.

In progress:

- Validate board-side overlay behavior: Detect ROI/heatmap, Collect guide box, and page switching/hide timing.
- Reduce Detect page idle CPU cost by removing fake pipeline timer over-refresh and moving stage-rail updates toward event-driven behavior.
- Collect page UI has been bound to `storage_app` collection sessions and camera capture. Next validation should focus on board-side interaction timing, TF-card write latency and whether the synchronous first version causes visible UI blocking during Capture/Auto.

Storage plan:

- TF card storage will start with a deliberately small directory layout under one project folder named `vision`.
- First version directory layout:
  - `vision/models`: Edge Impulse model packages. The first fixed package is `vision/models/cap_v8_96`, containing the future model files and `model.ini`.
  - `vision/collect_photos`: photos captured from the Collect page. The first version groups photos by collection session, for example `vision/collect_photos/session_001/0001.bmp`. Angle-specific folders are not required for now.
  - `vision/detect_records`: inspection record files. The first version uses `records.csv` for Data page recent records, statistics and CSV export.
  - `vision/config.ini`: simple runtime configuration such as brightness, language, default model, ROI and whether inspection images should be saved.
- First version will use BMP for saved photos because the current camera/display/AI path already uses `320x240 RGB565`. Saving BMP only needs a small file header plus the existing RGB565 frame data, avoiding JPEG encoding and OV5640 output-format switching during the initial filesystem bring-up.
- JPEG capture is kept as a later optimization. OV5640 can output JPEG, but using it cleanly would require a separate capture path or format switching between RGB565 realtime preview/inference and JPEG still-image saving.
- Inspection images are optional in the first version. Detection records must be saved first; later the system can add "save NG image only" or "save all images" as a config option.
- Current storage module boundary:
  - `storage_app_init()` prepares the `/vision` directory tree and default files.
  - `storage_app_save_rgb565_bmp()` writes a generic RGB565 BMP, including bottom-up row order and 4-byte row padding.
  - `storage_app_collect_begin_session()` creates the first unused session folder, for example `/vision/collect_photos/session_001`.
  - `storage_app_collect_save_rgb565_bmp()` saves frames into the active session as `0001.bmp`, `0002.bmp`, and so on. If no session is active, it starts one automatically.
  - `storage_app_collect_clear_current_session()` deletes the generated BMP sequence in the active session and resets the next image index to `0001.bmp`.

Collection session rule:

- A new session is not created when entering/leaving the Collect page.
- A session starts lazily on the first successful Capture/Auto save.
- Capture and Auto are photo actions, not session boundaries.
- Reset/Clear before completing the required photos keeps the current session and removes its images.
- Reset/Clear after completing the required photos ends the current session; the next capture starts the next session folder.

Next:

- Integrate Edge Impulse Visual AD model. Current exported model input is `240x240`, so camera `320x240` must be cropped or resized before inference.
- Add record append API for Data page with `vision/detect_records/records.csv`.
- If later needed, add a small app-side overlay API for dynamic boxes or markers beyond the current detect/collect presets.
- Product-arrival trigger direction: pure-vision preprocessing/gating is the preferred future path; external sensor trigger is a backup option.
## 2026-05-19

Edge Impulse integration direction:

- The first firmware-side AI target is fixed to the newest 96x96 Visual AD package exported from Edge Impulse v15.
- The current firmware does not import the full Edge Impulse SDK yet. A lightweight `APP/ai_app.c/.h` layer has been added first so the image input path can be validated without making the build system complex.
- `ai_app` defines the board-side preprocessing contract:
  - source frame: `320x240 RGB565` from OV5640/CEU.
  - ROI: crop `x=40,y=0,w=240,h=240`.
  - model input: bilinear resize to `96x96 RGB888`, byte count `27648`.
  - Visual AD grid: `5x5`.
  - threshold: `9.3`.
  - planned TFLM arena: `183808` bytes.
- `detect_app` 96x96 profile metadata has been aligned with v15 (`threshold=9.3`, `arena=183808`). The old `v8` command alias remains for compatibility, but `detect_model_cmd v15` is now the preferred command name.
- The current `ai_app_run_placeholder()` is not real AI inference. It only returns a result object with the same shape expected from the later Edge Impulse adapter. The later SDK migration should replace this function with a `run_classifier()` adapter instead of changing the LVGL, CSV or camera-overlay pipeline.
- Dynamic loading of `.tflite` files from TF card to SDRAM is still reserved. The simpler next step is to link one static C++ Edge Impulse export and verify real inference first.
- A board-side preprocessing validation command has been added:
  - command: `camera_app_ai_preprocess_test`.
  - it requests the next LCD preview frame and runs `ai_app_preprocess_rgb565_to_rgb88896()` immediately after `sensor_snapshot()` and cache invalidation.
  - the test is intentionally placed before `camera_app_draw_overlay()`, so the AI input is the raw camera frame without ROI/heatmap display pixels.
  - output prints preprocessing ticks, input byte count, a lightweight checksum and the first RGB bytes.
- Edge Impulse v15 SDK integration has a disabled component skeleton:
  - `Components/edge_impulse_v15/README.md` records the migration boundary and C wrapper plan.
  - `Components/edge_impulse_v15/ei_v15_inference.h` defines the future stable C API.
  - `Components/SConscript` only includes this component behind `APP_USING_EDGE_IMPULSE_V15`.
  - It is intentionally not enabled because the current RT-Thread Studio `Debug` makefile only builds `.c` files with `arm-none-eabi-gcc` and links with `gcc`; the exported EI library requires `.cpp/.cc` support and likely `g++` linking.
- A minimal C++ smoke test has been added before importing Edge Impulse:
  - `APP/cpp_smoke_probe.cpp` implements a tiny C++ class and exports `cpp_smoke_add_from_cpp()` through `extern "C"`.
  - `APP/cpp_smoke_app.c` exposes `cpp_smoke_cmd` in msh.
  - `APP/SConscript` now includes `Glob('*.cpp')`.
  - Expected board output after successful C++ build/link: `cpp_smoke_cmd` prints `smoke result=37`.
  - If the build reports `undefined reference to cpp_smoke_add_from_cpp`, the `.cpp` source was not compiled into the Debug makefile.
  - If the build reports C++ runtime/libstdc++ symbols, the next fix is linker-side C++ support.
  - The first smoke build failed exactly with `undefined reference to cpp_smoke_add_from_cpp`.
  - Root cause found: `.project` only had `org.eclipse.cdt.core.cnature`, so Eclipse/CDT treated the project as C-only and did not generate `.cpp` rules in `Debug/APP/subdir.mk`.
  - `.project` now includes `org.eclipse.cdt.core.ccnature`. RT-Thread Studio must refresh/reopen the project or regenerate managed makefiles before rebuilding.
  - Board validation passed: `cpp_smoke_cmd` prints `smoke result=37, expected=37`, confirming C code can call an `extern "C"` function implemented in C++.

Edge Impulse v15 static SDK staging:

- The v15 C++ export has been copied into `Components/edge_impulse_v15/edge_impulse_export/`.
- `Components/edge_impulse_v15/ei_v15_inference.cpp` now implements the planned wrapper:
  - input: `96x96 RGB888` buffer generated by `ai_app`.
  - `signal.get_data`: converts each RGB pixel into the EI image feature format `0xRRGGBB`.
  - calls `run_classifier()`.
  - converts `ei_impulse_result_t.visual_ad_result` and `visual_ad_grid_cells[]` into `ai_app_result_t`.
- `Components/edge_impulse_v15/SConscript` now compiles the wrapper, two generated model `.cpp` files and a very small initial SDK source set only when `APP_USING_EDGE_IMPULSE_V15` is enabled.
- The detect runtime is not switched to the real wrapper yet. Keep the existing placeholder path until the EI component passes compile/link; then replace only the backend inference call so LVGL, CSV, camera preview and overlay logic remain stable.
- Expected next step: enable `APP_USING_EDGE_IMPULSE_V15`, rebuild once, then add only the missing TensorFlow Micro source files reported by the first compiler/linker errors.
