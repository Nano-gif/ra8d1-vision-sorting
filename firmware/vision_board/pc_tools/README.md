# Vision Board PC Tools

This directory contains PC-side helper scripts for the Vision Board project.

## Convert Board BMP Captures To PNG

The board saves collected photos as RGB565 BMP files under the TF card:

```text
/vision/collect_photos/session_xxx/0001.bmp
```

Edge Impulse should use PNG files generated with the same preprocessing path that the board will use for inference:

```text
320x240 RGB565 BMP -> crop x=40,y=0,w=240,h=240 -> resize 96x96 -> PNG
```

Example:

```powershell
python .\pc_tools\vision_dataset_tool.py convert-images `
  --input .\data\collect_photos `
  --output .\data\cap_train_png `
  --roi 40,0,240,240 `
  --size 96x96
```

If you want all PNG files in one flat folder:

```powershell
python .\pc_tools\vision_dataset_tool.py convert-images `
  --input .\data\collect_photos `
  --output .\data\cap_train_png `
  --flat
```

The tool also writes `manifest.csv` into the output folder.

The same conversion is also available in the desktop HMI:

```powershell
python .\pc_tools\vision_pc_hmi.py --board-ip 192.168.1.88 --udp-port 16000
```

Open the sample conversion page in the HMI, choose the BMP capture directory,
choose the PNG output directory, keep the default ROI `40,0,240,240` and size
`96x96`, then start the conversion.

The HMI sample conversion page can also generate a PC-side template vision
profile from the same normal BMP captures. This is enabled by default through
`同时生成 PC 检测配置` (visible under the engineering mode toggle). Name the
model in the page before converting; PNG files land in
`<output root>/<model name>/`, while the PC vision profile is stored in a
portable local config library keyed by model name:

```text
<output root>/
  <model name>/
    0001.png
    manifest.csv

vision_pc_tool/pc_vision_configs/<model name>/
  vision_profile.json
  reference_template.png
  normal_tolerance.png
  valid_mask.png
```

`vision_pc_tool/` lives next to the exe (or next to `pc_tools/` in dev) so it
travels with the bundled HMI. The model management page reads the most recent
model name from `vision_pc_tool/recent.json` so you do not need to retype it
when packing an Edge Impulse export.

The first few normal images are treated as centered reference samples. Keep
those captures straight and centered when collecting data.

## Create A Board Model Package Template

Create a standard model package folder:

```powershell
python .\pc_tools\vision_dataset_tool.py init-model-package `
  --output .\models\bottle_cap_v1 `
  --name bottle_cap_v1
```

The generated folder contains:

```text
bottle_cap_v1/
  model.ini
```

Later, after Edge Impulse export is ready, put these two files into the same folder:

```text
model.tflite
anomaly_model.tflite
```

Then copy the folder to the TF card:

```text
/vision/models/<name>/
```

## Pack An Edge Impulse Export

When you already have an Edge Impulse C++ MCU export directory, pack it directly:

```powershell
python .\pc_tools\ei_model_pack.py `
  --source <path\to\ei_cpp_mcu_export> `
  --output .\models\icon_v15_96 `
  --name icon_v15_96
```

The wrapper reads `model_metadata.h`, `model_variables.h`, and the TFLite model headers to fill:

```text
input, grid, threshold, arena0, arena1, project_id, deploy_version, sha256
```

The package format is not limited to `96x96`, but the current board runtime can only execute model packages matching the compiled runtime profile. Unsupported packages are still listed by `model_scan`; `model_load` keeps the built-in model active and reports `runtime profile unsupported`.

If automatic `.tflite` selection is wrong, pass explicit paths:

```powershell
python .\pc_tools\ei_model_pack.py `
  --source <path\to\ei_cpp_mcu_export> `
  --output .\models\icon_v15_96 `
  --name icon_v15_96 `
  --main-tflite <path\to\main.tflite> `
  --anomaly-tflite <path\to\anomaly.tflite>
```

The generated package must be copied to the TF card as:

```text
/vision/models/<name>/
  model.tflite
  anomaly_model.tflite
  model.ini
```

Board-side validation commands:

```text
model_scan
model_load icon_v15_96
model_stat
detect_model_cmd
detect_app_once_cmd
```

## Run The PC HMI

The desktop HMI listens to the board UDP status stream, renders the parsed
fields into an operator dashboard, sends supported UDP control commands, and
provides local PC tools such as sample image conversion.

Install GUI dependency once:

```powershell
python -m pip install -r pc_tools\requirements.txt
```

Run:

```powershell
python .\pc_tools\vision_pc_hmi.py --board-ip 192.168.1.88 --udp-port 16000
```

The HMI code is organized under:

```text
pc_tools/hmi/
  main.py        # command-line entry
  window.py      # main window and status update logic
  widgets.py     # reusable status cards and field groups
  style.py       # Qt stylesheet
  formatters.py  # status field formatting helpers
```

Board side should keep `pc_status` enabled.  If the PC IP is not
`192.168.1.10`, set the board target first:

```text
pc_status target <PC_IP> 16000
pc_status on
```

If the GUI reports that UDP listening failed, close any other tool that is
already using port `16000`, for example `w5500_udp_recv.py`.
