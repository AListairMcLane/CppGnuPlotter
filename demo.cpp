// Demo for the async-capable plotter.hpp (v2).
//
// Build (from this directory):
//   g++ -std=c++17 -pthread demo.cpp -o demo   (Linux/macOS)
//   cl /std:c++17 demo.cpp                     (Windows, MSVC dev prompt)
//
// Requires gnuplot installed and on PATH.
//
// The key difference from the original plotter.hpp: show() returns as soon
// as the plot is handed to a background thread, instead of blocking until
// the window is closed. That means your program keeps doing useful work
// (or opening more plots) right after calling show(). This demo makes that
// visible by printing progress messages while windows are open, and shows
// the new Figure type for combining several plots into one window.

#include <vector>
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
        std::cout << "  [" << label << "] doing other work... (" << i << "/" << steps << ")\n";
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

    simulate_work("after plot 1", 5);

    // --- 2. Several independent plots open side by side ---------------------
    // Neither of these waits on the first plot, or on each other.
    {
        std::vector<double> x, y, z;
        for (double x_i = 0; x_i <= 50; x_i++) {
            for (double y_i = 0; y_i<=50; y_i++){
                double z_i = std::sin(x_i / 4) + std::cos(y_i / 4);
                x.push_back(x_i);
                y.push_back(y_i);
                z.push_back(z_i);
            }
        }

        Plotter i;
        i.add_intensity(x, y, z, "Instensity");
        i.set_x_lim(0,50);
        i.set_y_lim(0,50);
        i.show();

        Plotter s;
        s.add_surf(x, y, z, "Surf");
        s.show();
    }

    simulate_work("after plots 2 & 3", 5);

    // --- 3. Combine several plots into one figure (grid layout) -------------
    // Figure arranges any number of Plotter panels into a single gnuplot
    // window via `set multiplot`. Each panel keeps its own title, axis
    // labels, legend, limits etc, exactly like a standalone Plotter.
    //
    // The grid is deliberately wider (2x5) than the number of panels used
    // (4) to demonstrate that at(row, col) picks an exact position -- the
    // unused cells stay blank instead of the used ones bunching up on the
    // left.
    {
        Figure fig(2, 5);
        fig.setTitle("Plot 4: four panels placed in a wider 2x5 grid");

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

    simulate_work("after figure", 5);

    std::cout << "\nAll work done. wait_for_all_plots() just makes sure every "
                 "show() call above has been fully sent to gnuplot before we exit "
                 "-- it's optional, and doesn't wait for you to close the windows "
                 "(gnuplot's own terminal keeps them open independently).\n";
    Graphing::wait_for_all_plots();

    std::cout << "Demo complete. Close the plot windows whenever you're done with them.\n";
    return 0;
}
