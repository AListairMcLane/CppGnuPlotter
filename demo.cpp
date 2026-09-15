// Demo for the async-capable plotter.hpp (v3).
//
// Build (from this directory):
//   g++ -std=c++17 -pthread demo.cpp -o demo             (Linux/macOS)
//   g++ -std=c++17 -static demo.cpp -o demo.exe          (Windows, MSVC dev prompt)
//
// Requires gnuplot installed and on PATH.
// 
// show() returns as soon as the plot is handed to a background thread,
// instead of blocking until the window is closed. That means your program
// keeps doing useful work (or opening more plots) right after calling
// show(). This demo makes that visible by printing progress messages while
// windows are open, and shows the Figure type for combining several plots
// into one window.
//
// The tour covers every plot type in plotter.hpp: line, scatter, step,
// dual-axis, shaded regions, error bars, heatmaps, surfaces, contours,
// vector fields, 3D scatter/line, histograms (1D/2D), bar charts
// (simple/grouped/stacked), box plots, polar plots and stacked area.

#include <vector>
#include <string>
#include <cmath>
#include <iostream>
#include <chrono>
#include <thread>

#include "plotter.hpp"

using Graphing::Plotter;
using Graphing::Figure;

constexpr double PI = 3.14159265358979323846;

static void simulate_work(const char* label, int steps) {
    for (int i = 1; i <= steps; i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
}

int main() {

    // --- 1. show() doesn't block --------------------------------------------
    {
        std::vector<double> x, y;
        for (int i = 0; i <= 100; i++) {
            double t = i * (2 * PI / 100.0);
            x.push_back(t);
            y.push_back(std::sin(t));
        }

        Plotter p;
        p.setTitle("Plot 1: sin(x) -- opened asynchronously");
        p.setXlab("x");
        p.setYlab("sin(x)");
        p.set_legend(true);
        p.add_plot(x, y, "sin(x)", "blue", "solid", false);

        std::cout << "Calling show()...\n";
        p.show();
        std::cout << "show() already returned -- the window renders in the background.\n";
    }

    Graphing::wait_for_all_plots();
    simulate_work("after plot 1", 5);

    // --- 2. Heatmap, surface and contour from the same grid data -------------
    {
        std::vector<double> x, y, z;
        for (double x_i = 0; x_i <= 50; x_i++) {
            for (double y_i = 0; y_i <= 50; y_i++) {
                double z_i = std::sin(x_i / 4) + std::cos(y_i / 4);
                x.push_back(x_i);
                y.push_back(y_i);
                z.push_back(z_i);
            }
        }

        Plotter i;
        i.setTitle("Plot 2: intensity/heatmap");
        i.add_intensity(x, y, z, "Intensity");
        i.set_x_lim(0, 50);
        i.set_y_lim(0, 50);
        i.show();

        Plotter s;
        s.setTitle("Plot 3: 3D surface");
        s.add_surf(x, y, z, "Surf");
        s.show();

        Plotter c;
        c.setTitle("Plot 4: contour lines (same data as the heatmap)");
        c.setXlab("x"); c.setYlab("y");
        c.add_contour(x, y, z, "Contour", "black");
        c.show();
    }

    Graphing::wait_for_all_plots();
    simulate_work("after plots 2-4", 5);

    // --- 3. Combine several plots into one figure (grid layout) -------------
    {
        Figure fig(2, 2);
        fig.setTitle("Plot 5: four panels placed in a wider 2x5 grid");

        {
            std::vector<double> x, sin_y, cos_y;
            for (int i = 0; i <= 100; i++) {
                double t = i * (2 * PI / 100.0);
                x.push_back(t);
                sin_y.push_back(std::sin(t));
                cos_y.push_back(std::cos(t));
            }
            fig.at(0, 0).setTitle("sin & cos");
            fig.at(0, 0).setXlab("x"); fig.at(0, 0).setYlab("f(x)");
            fig.at(0, 0).set_legend(true);
            fig.at(0, 0).add_plot(x, sin_y, "sin(x)", "blue", "solid", false);
            fig.at(0, 0).add_plot(x, cos_y, "cos(x)", "red", "dashed", false);
        }

        {
            std::vector<double> x, y;
            for (int i = 0; i <= 40; i++) {
                double t = i * (2 * PI / 40.0);
                x.push_back(t);
                y.push_back(std::sin(t) + 0.15 * std::sin(t * 13.0));
            }
            fig.at(0, 1).setTitle("scatter");
            fig.at(0, 1).setXlab("x"); fig.at(0, 1).setYlab("y");
            fig.at(0, 1).add_scatter(x, y, "samples", "black", 7, 1.2);
        }

        {
            std::vector<double> x, temp_c, pressure_kpa;
            for (int i = 0; i <= 50; i++) {
                x.push_back(i);
                temp_c.push_back(20 + 5 * std::sin(i / 8.0));
                pressure_kpa.push_back(101 + 30 * std::cos(i / 15.0) + std::cos(i));
            }
            fig.at(1, 0).setTitle("dual axis");
            fig.at(1, 0).setXlab("t"); fig.at(1, 0).setYlab("temp (C)");
            fig.at(1, 0).setYRlab("pressure (kPa)");
            fig.at(1, 0).set_legend(true);
            fig.at(1, 0).add_plot(x, temp_c, "temperature", "orange", "solid", false);
            fig.at(1, 0).add_scatter_right(x, pressure_kpa, "pressure", "black", 3, 0.8);
        }

        {
            std::vector<double> x, y, y1, y2;
            for (int i = 1; i <= 30; i++) {
                x.push_back(i);
                y.push_back(std::pow(1.5, i));
                y1.push_back(std::pow(1.3, i));
                y2.push_back(std::pow(1.8, i));
            }
            fig.at(1, 1).setTitle("log scale");
            fig.at(1, 1).setXlab("n"); fig.at(1, 1).setYlab("1.5^n");
            fig.at(1, 1).setYlog();
            fig.at(1, 1).add_shaded_region(x, y1, x, y2, "Range", "orange", 0.8);
            fig.at(1, 1).add_plot(x, y, "1.5^n", "dark-cyan", "solid", false);
        }

        fig.show();
        std::cout << "Figure with 4 panels launched in a single window.\n";
    }

    Graphing::wait_for_all_plots();
    simulate_work("after figure", 5);

    // --- 4. Error bars: measured points with uncertainty ---------------------
    {
        std::vector<double> x, y, yerr;
        for (int i = 0; i <= 10; i++) {
            x.push_back(i);
            y.push_back(5 + 2 * std::sin(i / 2.0));
            yerr.push_back(0.3 + 0.05 * i);
        }

        Plotter p;
        p.setTitle("Plot 6: measurements with error bars");
        p.setXlab("sample"); p.setYlab("value");
        p.set_legend(true);
        p.add_errorbars(x, y, yerr, "measured", "dark-red");
        p.show();
    }

    // --- 5. Step plot: a discrete/digital-style signal ------------------------
    {
        std::vector<double> x, y;
        for (int i = 0; i <= 20; i++) {
            x.push_back(i);
            y.push_back(std::floor(3.0 * std::sin(i / 3.0)));
        }

        Plotter p;
        p.setTitle("Plot 7: step plot");
        p.setXlab("t"); p.setYlab("level");
        p.add_step(x, y, "signal", "blue");
        p.show();
    }

    // --- 6. Vector/quiver field: a simple converging flow field --------------
    {
        std::vector<double> x, y, dx, dy;
        for (double xi = -5; xi <= 5; xi += 1.0) {
            for (double yi = -5; yi <= 5; yi += 1.0) {
                x.push_back(xi);
                y.push_back(yi);
                dx.push_back(-xi * 0.3);
                dy.push_back(-yi * 0.3);
            }
        }

        Plotter p;
        p.setTitle("Plot 8: vector field");
        p.setXlab("x"); p.setYlab("y");
        p.set_x_lim(-6, 6);
        p.set_y_lim(-6, 6);
        p.add_vector(x, y, dx, dy, "flow", "dark-cyan");
        p.show();
    }

    // --- 7. Polar plot: a rose curve ------------------------------------------
    {
        std::vector<double> theta, r;
        for (int i = 0; i <= 200; i++) {
            double t = i * (2 * PI / 200.0);
            theta.push_back(t);
            r.push_back(1 + 0.5 * std::cos(3 * t));
        }

        Plotter p;
        p.setTitle("Plot 9: polar plot (rose curve)");
        p.add_polar(theta, r, "r = 1 + 0.5cos(3theta)", "purple");
        p.show();
    }

    // --- 8. 3D scatter and 3D trajectory (helix) ------------------------------
    {
        std::vector<double> hx, hy, hz;
        std::vector<double> sx, sy, sz;
        for (int i = 0; i <= 200; i++) {
            double t = i * (8 * PI / 200.0);
            hx.push_back(std::cos(t));
            hy.push_back(std::sin(t));
            hz.push_back(t / 5.0);

            if (i % 5 == 0) {
                sx.push_back(std::cos(t) * 1.2 + 0.1 * std::sin(t * 7));
                sy.push_back(std::sin(t) * 1.2 + 0.1 * std::cos(t * 5));
                sz.push_back(t / 5.0 + 0.1 * std::sin(t * 3));
            }
        }

        Plotter p;
        p.setTitle("Plot 10: 3D line (helix) + 3D scatter");
        p.set_legend(true);
        p.add_line3d(hx, hy, hz, "helix", "blue");
        p.add_scatter3d(sx, sy, sz, "samples", "red", 7, 1.0);
        p.show();
    }
    
    Graphing::wait_for_all_plots();
    simulate_work("after error bars / step / vector / polar / 3D", 5);

    // --- 9. Distributions and categorical data --------------------------------
    {
        Figure fig(2, 3);
        fig.setTitle("Plot 11: histograms, bar charts and box plots");

        {
            std::vector<double> samples;
            for (int i = 0; i < 500; i++) {
                // Deterministic pseudo-noise (no <random>): sum of a few
                // incommensurate sinusoids approximates a bell-ish spread.
                double v = std::sin(i * 12.9898) * 43758.5453;
                v = v - std::floor(v);
                double v2 = std::sin(i * 78.233) * 12345.678;
                v2 = v2 - std::floor(v2);
                samples.push_back(5.0 + 2.0 * (v + v2 - 1.0));
            }
            fig.at(0, 0).setTitle("histogram");
            fig.at(0, 0).setXlab("value"); fig.at(0, 0).setYlab("count");
            fig.at(0, 0).add_histogram(samples, "samples", "steelblue", 20);
        }

        {
            std::vector<std::string> categories = {"A", "B", "C", "D"};
            std::vector<double> values = {12, 19, 7, 15};
            fig.at(0, 1).setTitle("bar chart");
            fig.at(0, 1).setYlab("value");
            fig.at(0, 1).add_bar(categories, values, "counts", "orange");
        }

        {
            std::vector<std::string> categories = {"Q1", "Q2", "Q3", "Q4"};
            std::vector<std::string> group_names = {"Product A", "Product B"};
            std::vector<std::vector<double>> group_values = {
                {10, 14, 9, 18},
                {6, 8, 12, 10}
            };
            std::vector<std::string> group_colours = {"steelblue", "orange"};
            fig.at(0, 2).setTitle("grouped bar chart");
            fig.at(0, 2).setYlab("revenue");
            fig.at(0, 2).set_legend(true);
            fig.at(0, 2).add_bar_grouped(categories, group_names, group_values, group_colours);
        }

        {
            std::vector<std::string> categories = {"2021", "2022", "2023"};
            std::vector<std::string> layer_names = {"Solar", "Wind", "Gas"};
            std::vector<std::vector<double>> layer_values = {
                {5, 8, 13},
                {7, 9, 10},
                {20, 16, 12}
            };
            std::vector<std::string> layer_colours = {"gold", "skyblue", "gray"};
            fig.at(1, 0).setTitle("stacked bar chart");
            fig.at(1, 0).setYlab("TWh");
            fig.at(1, 0).set_legend(true);
            fig.at(1, 0).add_bar_stacked(categories, layer_names, layer_values, layer_colours);
        }

        {
            std::vector<std::string> categories = {"Control", "Treatment A", "Treatment B"};
            std::vector<std::vector<double>> samples(3);
            for (size_t g = 0; g < categories.size(); g++) {
                for (int i = 0; i < 40; i++) {
                    samples[g].push_back(5.0 + 2.0 * g + std::sin(i * 0.7 + g) + 0.3 * std::cos(i * 1.3));
                }
            }
            fig.at(1, 1).setTitle("box plot");
            fig.at(1, 1).setYlab("response");
            fig.at(1, 1).add_boxplot(categories, samples, "distribution", "seagreen");
        }

        {
            std::vector<double> x;
            std::vector<std::string> names = {"Product", "Marketing", "Support"};
            std::vector<std::vector<double>> values(3);
            for (int i = 0; i <= 20; i++) {
                x.push_back(i);
                values[0].push_back(3 + 0.5 * i);
                values[1].push_back(2 + std::sin(i / 3.0) * 1.5 + 1.5);
                values[2].push_back(1 + std::cos(i / 4.0) * 0.8 + 0.8);
            }
            std::vector<std::string> colours = {"steelblue", "orange", "seagreen"};
            fig.at(1, 2).setTitle("stacked area");
            fig.at(1, 2).setXlab("week"); fig.at(1, 2).setYlab("headcount");
            fig.at(1, 2).set_legend(true);
            fig.at(1, 2).add_stacked_area(x, names, values, colours);
        }

        fig.show();
        std::cout << "Figure with 6 panels (histogram/bar/boxplot/stacked area) launched.\n";
    }

    Graphing::wait_for_all_plots();
    simulate_work("after distributions figure", 5);

    // --- 10. 2D histogram/hexbin: density of scattered samples ---------------
    {
        std::vector<double> x, y;
        for (int i = 0; i < 2000; i++) {
            double v1 = std::sin(i * 12.9898) * 43758.5453; v1 -= std::floor(v1);
            double v2 = std::sin(i * 78.233) * 12345.678;   v2 -= std::floor(v2);
            double v3 = std::sin(i * 37.719) * 26123.987;   v3 -= std::floor(v3);
            double v4 = std::sin(i * 94.673) * 51234.321;   v4 -= std::floor(v4);
            x.push_back(5.0 * (v1 + v2 - 1.0));
            y.push_back(5.0 * (v3 + v4 - 1.0));
        }

        Plotter p;
        p.setTitle("Plot 12: 2D histogram (hexbin-style density)");
        p.setXlab("x"); p.setYlab("y");
        p.add_histogram2d(x, y, "density", 25, 25);
        p.show();
    }

    std::cout << "Demo complete. Close the plot windows whenever you're done with them.\n";
    return 0;
}
