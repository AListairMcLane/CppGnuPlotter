#pragma once

#include <iostream>
#include <cstdio>
#include <cstring>
#include <cctype>
#include <vector>
#include <string>
#include <sstream>
#include <filesystem>
#include <thread>
#include <mutex>
#include <memory>
#include <functional>
#include <atomic>

#if defined(_WIN32)
    #define PLOTTER_POPEN  _popen
    #define PLOTTER_PCLOSE _pclose
#else
    #define PLOTTER_POPEN  popen
    #define PLOTTER_PCLOSE pclose
#endif

namespace Graphing {

    namespace detail {

        inline std::atomic<int>& plot_id_counter() {
            static std::atomic<int> counter{0};
            return counter;
        }

        inline std::string sanitize_for_filename(const std::string& name) {
            std::string out = name;
            for (char& c : out) {
                if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '-' && c != '.') {
                    c = '_';
                }
            }
            return out;
        }
        
        struct RenderRegistry {
            std::mutex m;
            std::vector<std::thread> threads;

            void add(std::function<void()> job) {
                std::lock_guard<std::mutex> lock(m);
                threads.emplace_back(std::move(job));
            }

            void show_threads(){
                std::lock_guard<std::mutex> lock(m);
                for (auto& t : threads) {
                    std::cout << &t << std::endl;
                }
            }

            void join_all() {
                std::lock_guard<std::mutex> lock(m);
                for (auto& t : threads) {
                    std::cout << &t << std::endl;
                    if (t.joinable()) t.join();
                }
                threads.clear();
            }

            ~RenderRegistry() {
                join_all();
            }
        };

        inline RenderRegistry& registry() {
            static RenderRegistry r;
            return r;
        }

        inline void launch_render(std::function<void()> job) {
            registry().add(std::move(job));
        }

    } 

    inline void list_threads() {
        detail::registry().show_threads();
    }
    
    inline void wait_for_all_plots() {

        detail::registry().join_all();
    }
    
    class Plotter {
        friend class Figure;

    private:
        std::vector<std::vector<double>> X_sets;
        std::vector<std::vector<double>> Y_sets;
        std::vector<std::vector<double>> Z_sets;
        std::vector<std::string> colours;
        std::vector<std::string> names;
        std::vector<std::string> style;
        std::vector<std::string> smoothed;
        std::vector<double> fill_alpha; // only meaningful where type[i] == 'f'
        std::vector<char> side;
        std::vector<char> type;

        bool show_key = false;
        bool x_lim_set = false;
        bool y_lim_set = false;
        bool z_lim_set = false;
        bool y_r_lim_set = false;
        bool logX = false;
        bool logY = false;
        bool title = false;
        float x_low = 0, x_high = 0, y_low = 0, y_high = 0, y_r_low = 0, y_r_high = 0, z_low = 0, z_high = 0;
        float key_loc = 0;
        std::string x_lab_ptr = "X Axis";
        std::string y_lab_ptr = "Y Axis";
        std::string y_r_lab_ptr = " Y 2 Axis";
        std::string title_ptr = "title";

        static void write_global_cosmetic(FILE* pipe, int width, int height) {
            
            std::vector<std::string> cmds = {
                "set style fill solid 1.0",
                "set object 1 rectangle from graph 0,0 to graph 1,1 behind fillcolor rgb \"#f2f2f2\"",
                "set grid",
                "set mxtics 10",
                "set mytics 10",
                "set terminal qt size " + std::to_string(width) + "," + std::to_string(height),
                "set terminal qt font \",12\"",
                "set key font \",12\"",
                "set xtics font \",12\"",
                "set ytics font \",12\""
            };
            for (const auto& cmd : cmds) {
                fprintf(pipe, "%s\n", cmd.c_str());
            }
        }
        
        std::vector<std::string> write_data_files(const std::filesystem::path& dir, const std::string& prefix) const {
            std::vector<std::string> file_paths(X_sets.size());

            for (size_t i = 0; i < X_sets.size(); i++) {
                file_paths[i] = (dir / (prefix + "_" + std::to_string(i) + "_" + detail::sanitize_for_filename(names[i]) + ".dat")).string();

                FILE* fp = fopen(file_paths[i].c_str(), "w");
                if (!fp) {
                    std::cerr << "Plotter: failed to open temp data file '" << file_paths[i] << "' for writing\n";
                    continue;
                }

                if (type[i] == 'l') {
                    for (size_t j = 0; j < X_sets[i].size(); j++) {
                        fprintf(fp, "%f %f\n", X_sets[i][j], Y_sets[i][j]);
                    }
                }
                if (type[i] == 'i' || type[i] == 's' || type[i] == 'f') {
                    for (size_t j = 0; j < X_sets[i].size(); j++) {
                        // For 'f', Z_sets[i] holds the second Y trace.
                        fprintf(fp, "%f %f %f\n", X_sets[i][j], Y_sets[i][j], Z_sets[i][j]);
                    }
                }

                fclose(fp);
            }

            return file_paths;
        }
        
        void write_settings(FILE* pipe) const {
            bool right = false;
            for (auto& val : side) {
                if (val == 'r') { right = true; break; }
            }

            fprintf(pipe, "set xlabel \"%s\" font \",14\"\n", x_lab_ptr.c_str());
            fprintf(pipe, "set ylabel \"%s\" font \",14\"\n", y_lab_ptr.c_str());

            if (right) {
                fprintf(pipe, "set y2tics font \",12\"\n");
                fprintf(pipe, "set y2label \"%s\" font \",14\"\n", y_r_lab_ptr.c_str());
            } else {
                fprintf(pipe, "unset y2tics\n");
                fprintf(pipe, "unset y2label\n");
            }

            if (title) {
                fprintf(pipe, "set title \"%s\" font \",14\"\n", title_ptr.c_str());
            } else {
                fprintf(pipe, "unset title\n");
            }

            if (show_key) {
                fprintf(pipe, key_loc == 0 ? "set key left top\n" : "set key right top\n");
            } else {
                fprintf(pipe, "unset key\n");
            }

            fprintf(pipe, logX ? "set logscale x 10\n" : "unset logscale x\n");
            fprintf(pipe, logY ? "set logscale y 10\n" : "unset logscale y\n");

            if (x_lim_set) fprintf(pipe, "set xrange [%f:%f]\n", x_low, x_high);
            else fprintf(pipe, "set xrange [*:*]\n");

            if (y_lim_set) fprintf(pipe, "set yrange [%f:%f]\n", y_low, y_high);
            else fprintf(pipe, "set yrange [*:*]\n");

            if (z_lim_set) fprintf(pipe, "set cbrange [%f:%f]\n", z_low, z_high);
            else fprintf(pipe, "set cbrange [*:*]\n");

            if (y_r_lim_set) fprintf(pipe, "set y2range [%f:%f]\n", y_r_low, y_r_high);
            else fprintf(pipe, "set y2range [*:*]\n");
        }

        bool empty() const {
            return X_sets.empty();
        }
        
        void write_plot_commands(FILE* pipe, const std::vector<std::string>& file_paths) const {
            std::vector<std::string> plot2d_pieces;
            std::vector<std::string> plot3d_pieces;

            for (size_t i = 0; i < X_sets.size(); i++) {
                std::stringstream piece;

                if (type[i] == 'l') {
                    if (side[i] == 'r') {
                        piece << "'" << file_paths[i] << "' " << smoothed[i]
                            << " axes x1y2 with " << style[i] << " '" << colours[i] << "' lw 2"
                            << " title '" << names[i] << "'";
                    } else {
                        piece << "'" << file_paths[i] << "' " << smoothed[i]
                            << " with " << style[i] << " '" << colours[i] << "' lw 2"
                            << " title '" << names[i] << "'";
                    }
                    plot2d_pieces.push_back(piece.str());
                } else if (type[i] == 'i') {
                    piece << "'" << file_paths[i] << "' with image title '" << names[i] << "'";
                    plot2d_pieces.push_back(piece.str());
                } else if (type[i] == 's') {
                    piece << "'" << file_paths[i] << "' with pm3d title '" << names[i] << "'";
                    plot3d_pieces.push_back(piece.str());
                } else if (type[i] == 'f') {
                    piece << "'" << file_paths[i]
                        << "' using 1:2:3 with filledcurves fs transparent solid "
                        << fill_alpha[i] << " fc rgb '" << colours[i] << "' title '" << names[i] << "'";
                    plot2d_pieces.push_back(piece.str());
                }
            }

            auto join = [](const std::vector<std::string>& pieces) {
                std::stringstream out;
                for (size_t i = 0; i < pieces.size(); i++) {
                    out << (i == 0 ? "" : ", ") << pieces[i];
                }
                return out.str();
            };
            
            if (!plot2d_pieces.empty()) {
                fprintf(pipe, "plot %s\n", join(plot2d_pieces).c_str());
            }
            if (!plot3d_pieces.empty()) {
                fprintf(pipe, "splot %s\n", join(plot3d_pieces).c_str());
            }
        }
        
        void render_standalone() const {
            if (empty()) {
                std::cerr << "Plotter: show() called with no series added, nothing to plot\n";
                return;
            }

            const std::filesystem::path temp_dir = std::filesystem::temp_directory_path();
            const std::string prefix = "plotter_" + std::to_string(detail::plot_id_counter().fetch_add(1));
            const std::vector<std::string> file_paths = write_data_files(temp_dir, prefix);
            
            FILE* gnupipe = PLOTTER_POPEN("gnuplot -persistent", "w");
            if (!gnupipe) {
                std::cerr << "Plotter: failed to start gnuplot. Is it installed and on PATH?\n";
                return;
            }

            write_global_cosmetic(gnupipe, 500, 500);
            write_settings(gnupipe);
            write_plot_commands(gnupipe, file_paths);

            // Don't rely on "-persistent" alone to keep the child process (and thus
            // pclose()) blocked until the window closes -- when several gnuplot
            // instances are launched concurrently (one per render thread), the qt
            // terminal's handoff to its gnuplot_qt helper process can race, and the
            // parent gnuplot process can exit while the window is still open. This
            // explicit pause makes gnuplot itself block until the window closes,
            // regardless of that race.
            fprintf(gnupipe, "pause mouse close\n");

            PLOTTER_PCLOSE(gnupipe);

            for (const auto& path : file_paths) {
                std::remove(path.c_str());
            }
        }

    public:
        Plotter() {}

        void set_key_loc(float input) {
            key_loc = input;
        }

        void set_legend(bool enable) {
            show_key = enable;
        }

        void set_x_lim(float low, float high) {
            x_lim_set = true;
            x_low = low;
            x_high = high;
        }

        void set_y_lim(float low, float high) {
            y_lim_set = true;
            y_low = low;
            y_high = high;
        }

        void set_z_lim(float low, float high) {
            z_lim_set = true;
            z_low = low;
            z_high = high;
        }

        void set_y_r_lim(float low, float high) {
            y_r_lim_set = true;
            y_r_low = low;
            y_r_high = high;
        }

        void setXlog(void) {
            logX = true;
        }

        void setYlog(void) {
            logY = true;
        }

        void setXlab(const char* input_ptr) {
            x_lab_ptr = input_ptr;
        }

        void setYlab(const char* input_ptr) {
            y_lab_ptr = input_ptr;
        }

        void setYRlab(const char* input_ptr) {
            y_r_lab_ptr = input_ptr;
        }

        void setTitle(const char* input_ptr) {
            title = true;
            title_ptr = input_ptr;
        }

        void add_plot(const std::vector<double>& x, const std::vector<double>& y, const char* name, const char* colour, const char* line, bool smooth) {
            if (x.size() != y.size()) {
                std::cerr << "Plotter::add_plot: x and y size mismatch for series '" << name << "', skipped\n";
                return;
            }

            type.push_back('l');
            side.push_back('l');
            X_sets.push_back(x);
            Y_sets.push_back(y);
            Z_sets.push_back(y);
            colours.push_back(colour);
            names.push_back(name);
            fill_alpha.push_back(0.0);

            if (std::strcmp(line, "dashed") == 0) {
                style.push_back("lines dt 2 lc rgb");
            } else if (std::strcmp(line, "dotted") == 0) {
                style.push_back("lines dt 3 lc rgb");
            } else {
                style.push_back("lines dt 1 lc rgb");
            }

            smoothed.push_back(smooth ? "smooth bezier" : "");
        }

        void add_plot_right(const std::vector<double>& x, const std::vector<double>& y, const char* name, const char* colour, const char* line, bool smooth) {
            if (x.size() != y.size()) {
                std::cerr << "Plotter::add_plot_right: x and y size mismatch for series '" << name << "', skipped\n";
                return;
            }

            type.push_back('l');
            side.push_back('r');
            X_sets.push_back(x);
            Y_sets.push_back(y);
            Z_sets.push_back(y);
            colours.push_back(colour);
            names.push_back(name);
            fill_alpha.push_back(0.0);

            if (std::strcmp(line, "dashed") == 0) {
                style.push_back("lines dt 2 lc rgb");
            } else if (std::strcmp(line, "dotted") == 0) {
                style.push_back("lines dt 3 lc rgb");
            } else {
                style.push_back("lines dt 1 lc rgb");
            }

            smoothed.push_back(smooth ? "smooth bezier" : "");
        }

        void add_scatter(const std::vector<double>& x, const std::vector<double>& y, const char* name, const char* colour, int point_shape = 3, double point_size = 2) {
            if (x.size() != y.size()) {
                std::cerr << "Plotter::add_scatter: x and y size mismatch for series '" << name << "', skipped\n";
                return;
            }

            type.push_back('l');
            side.push_back('l');
            X_sets.push_back(x);
            Y_sets.push_back(y);
            Z_sets.push_back(y);
            colours.push_back(colour);
            names.push_back(name);
            fill_alpha.push_back(0.0);

            style.push_back("points pt " + std::to_string(point_shape) + " ps " + std::to_string(point_size) + " lc rgb");
            smoothed.push_back("");
        }

        void add_scatter_right(const std::vector<double>& x, const std::vector<double>& y, const char* name, const char* colour, int point_shape = 3, double point_size = 2) {
            if (x.size() != y.size()) {
                std::cerr << "Plotter::add_scatter_right: x and y size mismatch for series '" << name << "', skipped\n";
                return;
            }

            type.push_back('l');
            side.push_back('r');
            X_sets.push_back(x);
            Y_sets.push_back(y);
            Z_sets.push_back(y);
            colours.push_back(colour);
            names.push_back(name);
            fill_alpha.push_back(0.0);

            style.push_back("points pt " + std::to_string(point_shape) + " ps " + std::to_string(point_size) + " lc rgb");
            smoothed.push_back("");
        }

        void add_intensity(const std::vector<double>& x, const std::vector<double>& y, const std::vector<double>& z, const char* name) {
            type.push_back('i');
            side.push_back('l');
            X_sets.push_back(x);
            Y_sets.push_back(y);
            Z_sets.push_back(z);
            colours.push_back("null");
            names.push_back(name);
            fill_alpha.push_back(0.0);
            style.push_back("");
            smoothed.push_back("");
        }

        void add_surf(const std::vector<double>& x, const std::vector<double>& y, const std::vector<double>& z, const char* name) {
            type.push_back('s');
            side.push_back('l');
            X_sets.push_back(x);
            Y_sets.push_back(y);
            Z_sets.push_back(z);
            colours.push_back("null");
            names.push_back(name);
            fill_alpha.push_back(0.0);
            style.push_back("");
            smoothed.push_back("");
        }

        void add_shaded_region(const std::vector<double>& x1, const std::vector<double>& y1, const std::vector<double>& x2, const std::vector<double>& y2, const char* name, const char* colour, double alpha = 0.3) {
            if (x1.size() != y1.size() || x2.size() != y2.size() || x1.size() != x2.size()) {
                std::cerr << "Plotter::add_shaded_region: size mismatch for series '" << name << "', skipped\n";
                return;
            }

            type.push_back('f');
            side.push_back('l');
            X_sets.push_back(x1);
            Y_sets.push_back(y1);
            Z_sets.push_back(y2);
            colours.push_back(colour);
            names.push_back(name);
            fill_alpha.push_back(alpha);
            
            style.push_back("");
            smoothed.push_back("");
        }
        
        void show() const {
            auto snapshot = std::make_shared<Plotter>(*this);
            detail::launch_render([snapshot]() {
                snapshot->render_standalone();
            });
        }
    };
    
    class Figure {
    public:
        Figure(int rows, int cols)
            : rows_(rows), cols_(cols), cells_(static_cast<size_t>(rows) * static_cast<size_t>(cols)) {}

        Plotter& at(int row, int col) {
            return cells_[static_cast<size_t>(row) * static_cast<size_t>(cols_) + static_cast<size_t>(col)];
        }

        void setTitle(const char* input_ptr) {
            title = true;
            title_ptr = input_ptr;
        }
        
        void show(bool hold = true) const {
            auto snapshot = std::make_shared<Figure>(*this);
            detail::launch_render([snapshot, hold]() {
                snapshot->render(hold);
            });
        }

    private:
        int rows_, cols_;
        std::vector<Plotter> cells_;
        bool title = false;
        std::string title_ptr = "title";

        void render(bool hold) const {
            const std::filesystem::path temp_dir = std::filesystem::temp_directory_path();
            const std::string fig_prefix = "figure_" + std::to_string(detail::plot_id_counter().fetch_add(1));

            std::vector<std::vector<std::string>> all_paths(cells_.size());
            for (size_t i = 0; i < cells_.size(); i++) {
                all_paths[i] = cells_[i].write_data_files(temp_dir, fig_prefix + "_" + std::to_string(i));
            }

            const char* gnuplotCmd = hold ? "gnuplot -persistent" : "gnuplot";
            FILE* gnupipe = PLOTTER_POPEN(gnuplotCmd, "w");
            if (!gnupipe) {
                std::cerr << "Figure: failed to start gnuplot. Is it installed and on PATH?\n";
                return;
            }
            
            Plotter::write_global_cosmetic(gnupipe, 480 * cols_, 380 * rows_);

            fprintf(gnupipe, "set multiplot layout %d,%d", rows_, cols_);
            if (title) {
                fprintf(gnupipe, " title \"%s\"", title_ptr.c_str());
            }
            fprintf(gnupipe, "\n");

            const double cell_w = 1.0 / cols_;
            const double cell_h = 1.0 / rows_;

            for (int row = 0; row < rows_; row++) {
                for (int col = 0; col < cols_; col++) {
                    const size_t idx = static_cast<size_t>(row) * static_cast<size_t>(cols_) + static_cast<size_t>(col);
                    const Plotter& cell = cells_[idx];
                    if (cell.empty()) {
                        continue;
                    }

                    fprintf(gnupipe, "set origin %f,%f\n", col * cell_w, (rows_ - 1 - row) * cell_h);
                    fprintf(gnupipe, "set size %f,%f\n", cell_w, cell_h);
                    cell.write_settings(gnupipe);
                    cell.write_plot_commands(gnupipe, all_paths[idx]);
                }
            }

            fprintf(gnupipe, "unset multiplot\n");

            if (hold) {
                // See Plotter::render_standalone() -- "-persistent" alone doesn't
                // reliably keep this process blocked until the window closes when
                // several gnuplot instances race to start up concurrently.
                fprintf(gnupipe, "pause mouse close\n");
            }

            PLOTTER_PCLOSE(gnupipe);

            for (const auto& paths : all_paths) {
                for (const auto& path : paths) {
                    std::remove(path.c_str());
                }
            }
        }
    };

}
