# plotter.hpp
A single-header, drop-in C++ wrapper around [gnuplot](http://www.gnuplot.info/). No build system, no linking — copy `plotter.hpp` into your project and `#include` it.

It works by writing your data to temporary files and piping commands to a `gnuplot` subprocess, so it needs gnuplot installed and reachable on `PATH`. It doesn't try to be a full gnuplot binding: it covers a handful of common plot types with a small, opinionated API.

## Requirements
- A C++17 compiler (uses `std::filesystem`)
- [gnuplot](http://www.gnuplot.info/) installed and on `PATH`
  - macOS: `brew install gnuplot`
  - Debian/Ubuntu: `sudo apt install gnuplot`
  - Windows: [gnuplot installer](http://www.gnuplot.info/download.html) — during setup, add gnuplot to your `PATH`
- Tested against gnuplot 6.0. Older versions (5.x) should work for the plot types used here but haven't been verified.

## Install
Copy `plotter.hpp` into your project and include it:

```cpp
#include "plotter.hpp"
```

That's it — there's nothing to build or link.
## Quick start

```cpp
#include <vector>
#include "plotter.hpp"

int main() {
    std::vector<double> x = {0, 1, 2, 3, 4};
    std::vector<double> y = {0, 1, 4, 9, 16};

    Graphing::Plotter p;
    p.setTitle("y = x^2");
    p.setXlab("x");
    p.setYlab("y");
    p.set_legend(true);

    p.add_plot(x, y, "x^2", "blue", "solid", false);
    p.show();
}
```

Build with `g++ -std=c++17 main.cpp -o main` (or the equivalent on your compiler) and run.

See `demo.cpp` for a runnable tour of every feature below — build it with:

```
g++ -std=c++17 demo.cpp -o demo   # Linux/macOS
cl /std:c++17 demo.cpp            # Windows (MSVC dev prompt)
```

## API
Everything lives in `Graphing::Plotter`. Add one or more series, configure the chart, then call `show()`.

### Adding data
| Method | Description |
|---|---|
| `add_plot(x, y, name, colour, line, smooth)` | Line plot on the left y-axis. `line` is `"solid"`, `"dashed"`, or `"dotted"`. `smooth` bezier-smooths the line. |
| `add_plot_right(x, y, name, colour, line, smooth)` | Same as `add_plot`, but plotted against a second y-axis on the right. |
| `add_scatter(x, y, name, colour, point_shape=3, point_size=2)` | Scatter points on the left axis. `point_shape` is a gnuplot point type index. |
| `add_scatter_right(x, y, name, colour, point_shape=3, point_size=2)` | Scatter points against the right y-axis. |
| `add_shaded_region(x1, y1, x2, y2, name, colour, alpha=0.3)` | Fills the region between two curves (e.g. a confidence band). |
| `add_intensity(x, y, z, name)` | 2D heatmap/colormap from `(x, y, z)` triples (`gnuplot`'s `with image`). |
| `add_surf(x, y, z, name)` | 3D surface (`gnuplot`'s `splot ... with pm3d`). |

`colour` is any [gnuplot color name](http://www.gnuplot.info/docs_6.0/loc3986.html) (e.g. `"red"`, `"skyblue"`, `"orange-red"`) or a hex string (e.g. `"#DC143C"`). Run `gnuplot -e "show colornames"` to list the names your gnuplot build supports — the set isn't identical to CSS/X11 color names (e.g. `"crimson"` is commonly missing).

Series with mismatched `x`/`y` (or `x`/`y`/`z`) vector sizes are skipped with a warning printed to `stderr` rather than crashing.

### Chart configuration
| Method | Description |
|---|---|
| `setTitle(text)` | Chart title. |
| `setXlab(text)` / `setYlab(text)` / `setYRlab(text)` | Axis labels (left x, left y, right y). |
| `set_legend(bool)` | Show/hide the legend. |
| `set_key_loc(0 or 1)` | Legend position: `0` = top-left (default), non-zero = top-right. |
| `set_x_lim(low, high)` / `set_y_lim(low, high)` / `set_y_r_lim(low, high)` / `set_z_lim(low, high)` | Fix axis ranges instead of autoscaling. |
| `setXlog()` / `setYlog()` | Log-scale (base 10) that axis. |
| `show(hold = true)` | Render everything added so far. `hold = true` passes `-persistent` to gnuplot so the window stays open after the call returns; `hold = false` closes it immediately. |

Each `Plotter` instance accumulates series until you call `show()`; a second `show()` call re-renders everything added so far (including anything added since the first call) in a new gnuplot window.

## Known limitations
- **Series names double as text**, not just labels: they're used verbatim in gnuplot's `title '...'` clause, so a name containing a single quote (`'`) will break the generated gnuplot command. Stick to plain text without quotes.
- **`add_surf` data isn't grid-blocked.** gnuplot's `splot ... with pm3d` renders a clean mesh when scan-lines (rows of constant `y`) are separated by blank lines in the data file; this library writes a flat list of `(x, y, z)` triples without those separators. For a small/simple grid it still renders something reasonable (see the demo), but don't expect a polished continuous surface for larger or irregular grids.
- **Default terminal is `qt`.** If your gnuplot build doesn't include the Qt terminal, edit the two `set terminal qt ...` lines near the top of `Build()` in `plotter.hpp` to `wxt`, `x11` (Linux), or `aqua` (macOS).
- **Not thread-safe.** A `Plotter` instance isn't meant to be used from multiple threads concurrently.
- This wraps a handful of common gnuplot plot types for convenience, not the full gnuplot command surface. For anything more exotic, pipe to gnuplot directly.

## License
MIT — see [LICENSE](LICENSE).
