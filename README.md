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

**Lines, points & regions**

| Method | Description |
|---|---|
| `add_plot(x, y, name, colour, line, smooth)` | Line plot on the left y-axis. `line` is `"solid"`, `"dashed"`, or `"dotted"`. `smooth` bezier-smooths the line. |
| `add_plot_right(x, y, name, colour, line, smooth)` | Same as `add_plot`, but plotted against a second y-axis on the right. |
| `add_scatter(x, y, name, colour, point_shape=3, point_size=2)` | Scatter points on the left axis. `point_shape` is a gnuplot point type index. |
| `add_scatter_right(x, y, name, colour, point_shape=3, point_size=2)` | Scatter points against the right y-axis. |
| `add_step(x, y, name, colour, mode="steps")` | Step/staircase line. `mode` is `"steps"`, `"fsteps"`, or `"histeps"`. |
| `add_shaded_region(x1, y1, x2, y2, name, colour, alpha=0.3)` | Fills the region between two curves (e.g. a confidence band). |
| `add_errorbars(x, y, yerr, name, colour, point_size=1)` | Points with y-error bars (`yerr` added/subtracted from `y`). |
| `add_vector(x, y, dx, dy, name, colour)` | Vector/quiver field: an arrow `(dx, dy)` drawn from each `(x, y)`. |
| `add_polar(theta, r, name, colour, style_kind="lines")` | Polar plot; `theta` in radians. `style_kind` is `"lines"` or `"points"`. |

**Distributions & categories**

| Method | Description |
|---|---|
| `add_histogram(samples, name, colour, bins=10)` | Binned frequency histogram computed from a raw sample vector. |
| `add_histogram2d(x, y, name, xbins=20, ybins=20)` | 2D histogram/hexbin: bins scattered `(x, y)` samples onto a grid of counts and renders it like `add_intensity`. |
| `add_bar(categories, values, name, colour, width=0.6)` | Single-series categorical bar chart. Sets this `Plotter`'s x-tics to `categories`. |
| `add_bar_grouped(categories, group_names, group_values, group_colours, group_width=0.8)` | Multiple bars per category, side by side. `group_values` is one vector per group. |
| `add_bar_stacked(categories, layer_names, layer_values, layer_colours, width=0.6)` | Layers stacked bottom-to-top per category (`layer_values[0]` is the bottom layer). |
| `add_boxplot(categories, samples, name, colour)` | Box-and-whisker plot: one box per category, quartiles computed by gnuplot from the raw `samples[i]`. |
| `add_stacked_area(x, series_names, series_values, series_colours, alpha=0.7)` | Cumulative filled layers (e.g. composition-over-time data), stacked in the order given. |

**Heatmaps & 3D**

| Method | Description |
|---|---|
| `add_intensity(x, y, z, name)` | 2D heatmap/colormap from `(x, y, z)` triples (`gnuplot`'s `with image`). |
| `add_surf(x, y, z, name)` | 3D surface (`gnuplot`'s `splot ... with pm3d`). Expects grid-ordered data (see Known limitations). |
| `add_contour(x, y, z, name, colour)` | 2D contour lines from the same grid-ordered `(x, y, z)` data as `add_surf`. |
| `add_scatter3d(x, y, z, name, colour, point_shape=7, point_size=1)` | 3D point cloud, in arbitrary order (unlike `add_surf`/`add_contour`). |
| `add_line3d(x, y, z, name, colour, line="solid")` | 3D trajectory/parametric curve, drawn in the given point order. |

`colour` is any [gnuplot color name](http://www.gnuplot.info/docs_6.0/loc3986.html) (e.g. `"red"`, `"skyblue"`, `"orange-red"`) or a hex string (e.g. `"#DC143C"`). Run `gnuplot -e "show colornames"` to list the names your gnuplot build supports — the set isn't identical to CSS/X11 color names (e.g. `"crimson"` is commonly missing).

Series with mismatched vector sizes are skipped with a warning printed to `stderr` rather than crashing.

`add_bar`, `add_bar_grouped`, `add_bar_stacked`, and `add_boxplot` set this `Plotter`'s categorical x-tics — mixing one of these with a numeric-x plot type (e.g. `add_plot`) in the same `Plotter`/`Figure` cell isn't meaningful and isn't supported. When no bar-type series has an explicit `set_y_lim`, the y-axis is automatically anchored to 0 (rather than plain autoscale) whenever every bar value is non-negative, so a short bar can't get autoscaled out of view.

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

## Roadmap / missing functionality
The plot-type coverage is now fairly broad (see the API tables above). What's still missing is mostly around output, styling and axis handling rather than new chart types, grouped by category below.

### Output & export
- **Save-to-file** — currently every render goes to an interactive `qt` window; there's no way to write directly to PNG/SVG/PDF/EPS (gnuplot's `set terminal pngcairo` etc.). This matters a lot for scripted/batch runs and for generating figures for papers without a display attached.
- **Configurable terminal/backend** — no way to pick `wxt`/`x11`/`aqua`/headless at the API level; today it requires editing `plotter.hpp` directly (noted in Known limitations).
- **Figure size/DPI control for saved output** — distinct from the existing window `width`/`height`, since print figures need DPI, not pixels.

### Annotations & styling
- **Text labels / point annotations** — `set label` equivalent, for annotating specific data points or regions.
- **Reference lines** — horizontal/vertical lines at a given value (e.g. a threshold or a mean), and arbitrary line/arrow annotations.
- **Custom colorbar/palette selection** — `add_intensity`/`add_surf` currently use gnuplot's default palette with no way to pick a perceptually-uniform one (viridis, etc.) or set discrete color levels.
- **More marker/line style control** — arbitrary point shapes are supported via a raw gnuplot index (`point_shape`), but there's no named/enumerated set, and no control over marker fill vs. outline.
- **Legend placement beyond top-left/top-right** — `set_key_loc` only toggles between two corners; gnuplot supports outside-plot legends, bottom placement, and column layout.

### Axes & scales
- **Log scale on the right y-axis or z/colorbar axis** — `setXlog`/`setYlog` only cover the primary axes.
- **Date/time x-axis** — no support for `set xdata time`, needed for time-series data with real timestamps rather than numeric indices.
- **Aspect ratio / equal-scale axes** — `set size ratio -1` equivalent, important when x/y represent the same physical unit (e.g. spatial data).
- **3D view angle control** — `set view` for rotating/orienting `splot` output (surfaces, 3D scatter).

### Data handling
- **Gaps/missing data** — no defined behavior for `NaN` or missing points within a series (gnuplot can skip them with the right handling, but the library doesn't do anything special today).
- **Escaping for series/axis names** — noted in Known limitations: a name containing a single quote breaks the generated gnuplot command. Proper escaping would remove this footgun rather than just documenting it.

## Known limitations
- **Series names double as text**, not just labels: they're used verbatim in gnuplot's `title '...'` clause, so a name containing a single quote (`'`) will break the generated gnuplot command. Stick to plain text without quotes.
- **`add_surf`/`add_contour` expect grid-ordered data.** gnuplot's `splot ... with pm3d` (and contour) render a clean mesh from scan-lines (runs of constant `x`) separated by blank lines in the data file; this library inserts that blank line automatically whenever `x` changes between consecutive points. That means points must already be grouped by `x` (e.g. generated with `x` as the outer loop and `y` as the inner loop, as in the demo) — arbitrary/scattered point order won't produce a coherent surface.
- **Categorical x-axes are per-`Plotter`.** `add_bar`/`add_bar_grouped`/`add_bar_stacked`/`add_boxplot` all set the same underlying x-tics; calling more than one of them (or mixing with a numeric-x series) in the same `Plotter`/`Figure` cell isn't supported — give each its own cell.
- **Default terminal is `qt`.** If your gnuplot build doesn't include the Qt terminal, edit the two `set terminal qt ...` lines near the top of `Build()` in `plotter.hpp` to `wxt`, `x11` (Linux), or `aqua` (macOS).
- **Not thread-safe.** A `Plotter` instance isn't meant to be used from multiple threads concurrently.
- This wraps a handful of common gnuplot plot types for convenience, not the full gnuplot command surface. For anything more exotic, pipe to gnuplot directly.

## License
MIT — see [LICENSE](LICENSE).
