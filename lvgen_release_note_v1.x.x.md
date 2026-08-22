## LVGL Code Generator (`lvgen`) v1.1.0

Minor release of **lvgen** focused on enhancing the **Parent-Child UX in `lvgen-ui` (Visual Drag & Drop Web Editor)**, making widget hierarchy, container nesting, and drag-and-drop operations intuitive, visual, and seamless.

### Key Features
* **Interactive Drop-Target Highlighting**: Real-time cyan dashed outline and pulse glow on hoverable container widgets (Screen, Obj, Button) during drag operations.
* **Direct Container Hover Drop**: Widgets dragged from the Palette automatically detect and bind to the hovered container with automatic relative X/Y coordinate calculation.
* **Layer Tree Drag & Drop Re-parenting**: Re-arrange widget hierarchy directly inside the Layer Tree panel with built-in loop/cycle prevention.
* **Inspector Parent Field**: Displays the parent container ID in the Property Inspector with 1-click navigation to select the parent widget on the canvas.
* **Canvas Visual Nesting & Clipping**: Child widgets are visually clipped within container bounds (`overflow: hidden`) with subtle nesting depth indicators and child count badges.

| Feature | Subsystem / Component | Description |
| :--- | :--- | :--- |
| **Drop-Target Highlight** | Web Canvas (`lvgen-ui`) | Dynamic cyan pulse outline and target badge when dragging widgets over valid container targets. |
| **Direct Container Drop** | Drag & Drop Engine | Automatically binds new widgets to the container under cursor and computes relative X/Y offsets. |
| **Layer Tree Re-parenting** | Hierarchy Inspector | Drag-and-drop layer tree nodes to re-assign parent containers with circular dependency checks. |
| **Inspector Parent Navigation** | Property Inspector | Dedicated Parent field in Inspector showing container ID with 1-click selection of the parent widget. |
| **Canvas Visual Nesting** | Render Engine & CSS | Containers clip child elements (`overflow: hidden`) and render nested depth styling with child count badges. |

### Installation
```bash
git clone -b v1.1.0 https://github.com/hphuc15/lvgen.git
cd lvgen
pip install -r requirements.txt
pip install -e .
```
