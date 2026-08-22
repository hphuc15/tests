## LVGL Code Generator (`lvgen`) v1.0.0

First major release of **lvgen**, a modular Python tool designed to automatically generate standard Embedded C/C++ source code for the LVGL (Light and Versatile Graphics Library) from UI specifications or a visual web canvas.

### Key Features
* **Modular / Plugin Architecture**: Every LVGL Widget (Label, Button, Slider, Image, etc.) is an independent plugin.
* **Intermediate Representation (IR)**: Decouples UI tree definition from C code generation logic.
* **Embedded C-Friendly Output**: Generates `ui.h`, `ui.c`, and `ui_events.c` ready for microcontrollers.
* **Visual Web Studio**: Built-in visual drag-and-drop UI editor (`lvgen serve`).
* **Developer CLI**: Quickly extend the tool with new widget plugins using `lvgen make-plugin <widget_name>`.

| Feature | Subsystem / Command | Description |
| :--- | :--- | :--- |
| **Modular Architecture** | Widget Plugin System | Every LVGL Widget (Label, Button, Slider, Image, etc.) is implemented as an independent plugin. |
| **Intermediate Representation** | IR Engine | Decouples UI tree definition from C code generation logic for maximum flexibility. |
| **Embedded C-Friendly** | Code Generator | Generates clean `ui.h`, `ui.c`, and `ui_events.c` ready for microcontrollers (ESP32, STM32, etc.). |
| **Visual Web Studio** | `lvgen serve` | Built-in visual drag-and-drop UI editor accessed directly via web browser. |
| **Developer CLI** | `lvgen make-plugin` | CLI tool to quickly boilerplate and extend the generator with new custom widget plugins. |

### Installation
```bash
git clone -b v1.0.0 https://github.com/hphuc15/lvgen.git
cd lvgen
pip install -r requirements.txt
pip install -e .
```
