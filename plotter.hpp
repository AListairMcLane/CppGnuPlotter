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
#include <algorithm>
#include <utility>

#if defined(_WIN32)
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <windows.h>
    #include <io.h>
    #include <fcntl.h>
#endif

namespace Graphing {

    namespace detail {

#if defined(_WIN32)
        // MSVCRT/UCRT's _popen()/_pclose() serialize on a process-wide internal
        // lock that tracks spawned child processes: while any thread is blocked
        // inside _pclose() waiting for one gnuplot window to close, _popen() on
        // ANY other thread blocks too -- indefinitely, since that lock isn't
        // released until the first window is closed. With one render thread per
        // Plotter, that's fatal to concurrent windows: the second and later
        // show() calls hang forever inside _popen() and their windows never
        // appear. Spawning via raw CreateProcess + a manual pipe, and waiting
        // via WaitForSingleObject instead of _pclose(), avoids that lock
        // entirely and lets multiple windows open truly concurrently.
        struct GnuplotSession {
            FILE* pipe = nullptr;
            HANDLE hProcess = nullptr;
        };

        inline GnuplotSession open_gnuplot_session(bool persistent) {
            GnuplotSession session;

            SECURITY_ATTRIBUTES sa{ sizeof(sa), nullptr, TRUE };
            HANDLE read_end = nullptr, write_end = nullptr;
            if (!CreatePipe(&read_end, &write_end, &sa, 0)) {
                return session;
            }
            SetHandleInformation(write_end, HANDLE_FLAG_INHERIT, 0);

            STARTUPINFOA si{};
            si.cb = sizeof(si);
            si.dwFlags |= STARTF_USESTDHANDLES;
            si.hStdInput = read_end;
            si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
            si.hStdError = GetStdHandle(STD_ERROR_HANDLE);

            std::string cmdline = persistent ? "gnuplot -persistent" : "gnuplot";
            std::vector<char> cmdbuf(cmdline.begin(), cmdline.end());
            cmdbuf.push_back('\0'); // CreateProcessA may write into this buffer

            PROCESS_INFORMATION pi{};
            BOOL ok = CreateProcessA(nullptr, cmdbuf.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr, &si, &pi);
            CloseHandle(read_end);
            if (!ok) {
                CloseHandle(write_end);
                return session;
            }
            CloseHandle(pi.hThread);

            int fd = _open_osfhandle(reinterpret_cast<intptr_t>(write_end), _O_TEXT);
            session.pipe = (fd != -1) ? _fdopen(fd, "w") : nullptr;
            session.hProcess = pi.hProcess;
            return session;
        }

        inline void close_gnuplot_session(GnuplotSession& session) {
            if (session.pipe) fclose(session.pipe);
            if (session.hProcess) {
                WaitForSingleObject(session.hProcess, INFINITE);
                CloseHandle(session.hProcess);
            }
        }
#else
        struct GnuplotSession {
            FILE* pipe = nullptr;
        };

        inline GnuplotSession open_gnuplot_session(bool persistent) {
            GnuplotSession session;
            session.pipe = popen(persistent ? "gnuplot -persistent" : "gnuplot", "w");
            return session;
        }

        inline void close_gnuplot_session(GnuplotSession& session) {
            if (session.pipe) pclose(session.pipe);
        }
#endif

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
        // type codes:
        //   'l' line/scatter/step (2 cols: x,y)
        //   'i' 2D heatmap/image  (3 cols: x,y,z)
        //   's' 3D surface        (3 cols grid: x,y,z, blank line per scan-line)
        //   'c' contour           (3 cols grid: x,y,z, blank line per scan-line)
        //   'g' 3D scatter/line   (3 cols: x,y,z)
        //   'f' filled/shaded     (3 cols: x,y1,y2)
        //   'b' bar/histogram     (3 cols: x,value,width)
        //   'e' errorbars         (4 cols: x,y,ylow,yhigh)
        //   'v' vector/quiver     (4 cols: x,y,dx,dy)
        //   'w' boxplot           (2 cols: category_index,value)
        //   'r' polar             (2 cols: theta,r)
        std::vector<std::vector<double>> X_sets;
        std::vector<std::vector<double>> Y_sets;
        std::vector<std::vector<double>> Z_sets;
        std::vector<std::vector<double>> W_sets; // only meaningful for type[i] == 'e' or 'v'
        std::vector<std::string> colours;
        std::vector<std::string> names;
        std::vector<std::string> style;
        std::vector<std::string> smoothed;
        std::vector<double> fill_alpha; // only meaningful where type[i] == 'f'
        std::vector<char> side;
        std::vector<char> type;

        // (position, label) pairs for categorical x-axes (bar / histogram / boxplot).
        std::vector<std::pair<double, std::string>> xtic_labels;

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

        static void write_global_cosmetic(FILE* pipe, int width, int height, int window_id) {
            // gnuplot's qt terminal serves multiple windows from one shared
            // gnuplot_qt helper process and, left unpositioned, places each new
            // window at the same default spot -- concurrent show() calls (each
            // its own gnuplot subprocess/window, but sharing that one Qt server)
            // then stack exactly on top of one another instead of appearing
            // side by side. An explicit window number and a cascading position
            // (derived from the render counter) keeps them visibly distinct.
            const int px = 40 * (window_id % 15);
            const int py = 40 * (window_id % 10);

            std::vector<std::string> cmds = {
                "set style fill solid 1.0",
                "set object 1 rectangle from graph 0,0 to graph 1,1 behind fillcolor rgb \"#f2f2f2\"",
                "set grid",
                "set mxtics 10",
                "set mytics 10",
                "set terminal qt " + std::to_string(window_id) + " size " + std::to_string(width) + "," + std::to_string(height)
                    + " position " + std::to_string(px) + "," + std::to_string(py) + " font \",12\"",
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

                if (type[i] == 'l' || type[i] == 'w' || type[i] == 'r') {
                    for (size_t j = 0; j < X_sets[i].size(); j++) {
                        fprintf(fp, "%f %f\n", X_sets[i][j], Y_sets[i][j]);
                    }
                } else if (type[i] == 'i' || type[i] == 'f' || type[i] == 'b' || type[i] == 'g') {
                    for (size_t j = 0; j < X_sets[i].size(); j++) {
                        // For 'f', Z_sets[i] holds the second Y trace. For 'b', it holds box width.
                        fprintf(fp, "%f %f %f\n", X_sets[i][j], Y_sets[i][j], Z_sets[i][j]);
                    }
                } else if (type[i] == 'e' || type[i] == 'v') {
                    for (size_t j = 0; j < X_sets[i].size(); j++) {
                        // For 'e': x, y, ylow(abs), yhigh(abs). For 'v': x, y, dx, dy.
                        fprintf(fp, "%f %f %f %f\n", X_sets[i][j], Y_sets[i][j], Z_sets[i][j], W_sets[i][j]);
                    }
                } else if (type[i] == 's' || type[i] == 'c') {
                    // pm3d/contour need scan-lines (runs of constant x) separated by a
                    // blank line to know where one row of the grid ends and the
                    // next begins -- otherwise it treats the whole series as one
                    // undifferentiated blob of points and draws garbage. Points
                    // are expected to already be grouped by x (as add_surf's/
                    // add_contour's caller would naturally generate a grid, x-major).
                    for (size_t j = 0; j < X_sets[i].size(); j++) {
                        if (j > 0 && X_sets[i][j] != X_sets[i][j - 1]) {
                            fprintf(fp, "\n");
                        }
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

            if (y_lim_set) {
                fprintf(pipe, "set yrange [%f:%f]\n", y_low, y_high);
            } else {
                // Autoscale fits the y-axis to the data's min/max, but bars are
                // drawn from a 0 baseline -- if every bar is non-negative and
                // none reaches down to 0, plain autoscale crops the axis above
                // 0 and can hide a short bar's base (or all of it) entirely.
                bool bar_present = false;
                bool bars_nonnegative = true;
                for (size_t i = 0; i < type.size(); i++) {
                    if (type[i] != 'b') continue;
                    bar_present = true;
                    for (double v : Y_sets[i]) {
                        if (v < 0) { bars_nonnegative = false; break; }
                    }
                }
                if (bar_present && bars_nonnegative) fprintf(pipe, "set yrange [0:*]\n");
                else fprintf(pipe, "set yrange [*:*]\n");
            }

            if (z_lim_set) fprintf(pipe, "set cbrange [%f:%f]\n", z_low, z_high);
            else fprintf(pipe, "set cbrange [*:*]\n");

            if (y_r_lim_set) fprintf(pipe, "set y2range [%f:%f]\n", y_r_low, y_r_high);
            else fprintf(pipe, "set y2range [*:*]\n");

            // Always emit one branch or the other -- a Figure reuses one gnuplot
            // session across cells, so a previous cell's categorical xtics must
            // be explicitly reset or they leak into the next cell's numeric axis.
            if (!xtic_labels.empty()) {
                fprintf(pipe, "set xtics (");
                for (size_t i = 0; i < xtic_labels.size(); i++) {
                    fprintf(pipe, "%s\"%s\" %f", (i == 0 ? "" : ", "), xtic_labels[i].second.c_str(), xtic_labels[i].first);
                }
                fprintf(pipe, ") font \",12\"\n");
            } else {
                fprintf(pipe, "set xtics autofreq font \",12\"\n");
            }
        }

        bool empty() const {
            return X_sets.empty();
        }

        void write_plot_commands(FILE* pipe, const std::vector<std::string>& file_paths) const {
            std::vector<std::string> plot2d_pieces;
            std::vector<std::string> plot3d_pieces;
            std::vector<std::string> contour_pieces;
            std::vector<std::string> polar_pieces;

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
                } else if (type[i] == 'f') {
                    piece << "'" << file_paths[i]
                        << "' using 1:2:3 with filledcurves fs transparent solid "
                        << fill_alpha[i] << " fc rgb '" << colours[i] << "' title '" << names[i] << "'";
                    plot2d_pieces.push_back(piece.str());
                } else if (type[i] == 'e') {
                    piece << "'" << file_paths[i] << "' using 1:2:3:4 with " << style[i]
                        << " '" << colours[i] << "' lw 2 title '" << names[i] << "'";
                    plot2d_pieces.push_back(piece.str());
                } else if (type[i] == 'v') {
                    piece << "'" << file_paths[i]
                        << "' using 1:2:3:4 with vectors head filled size screen 0.02,15,45 lc rgb '"
                        << colours[i] << "' lw 2 title '" << names[i] << "'";
                    plot2d_pieces.push_back(piece.str());
                } else if (type[i] == 'b') {
                    piece << "'" << file_paths[i] << "' using 1:2:3 with " << style[i]
                        << " '" << colours[i] << "' title '" << names[i] << "'";
                    plot2d_pieces.push_back(piece.str());
                } else if (type[i] == 'w') {
                    // gnuplot's "with boxplot" pools every row of a piece into a single
                    // box regardless of its x value -- it does not group by x itself.
                    // add_boxplot() therefore emits one series (and so one piece) per
                    // category; only the first carries a title, the rest are notitle
                    // so the legend doesn't get one blank entry per extra box.
                    piece << "'" << file_paths[i] << "' using 1:2 with " << style[i] << " '" << colours[i] << "'";
                    if (!names[i].empty()) piece << " title '" << names[i] << "'";
                    else piece << " notitle";
                    plot2d_pieces.push_back(piece.str());
                } else if (type[i] == 's') {
                    piece << "'" << file_paths[i] << "' with pm3d title '" << names[i] << "'";
                    plot3d_pieces.push_back(piece.str());
                } else if (type[i] == 'g') {
                    piece << "'" << file_paths[i] << "' with " << style[i]
                        << " '" << colours[i] << "' lw 2 title '" << names[i] << "'";
                    plot3d_pieces.push_back(piece.str());
                } else if (type[i] == 'c') {
                    piece << "'" << file_paths[i] << "' with lines lc rgb '" << colours[i]
                        << "' title '" << names[i] << "'";
                    contour_pieces.push_back(piece.str());
                } else if (type[i] == 'r') {
                    piece << "'" << file_paths[i] << "' with " << style[i]
                        << " '" << colours[i] << "' lw 2 title '" << names[i] << "'";
                    polar_pieces.push_back(piece.str());
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
            if (!contour_pieces.empty()) {
                // Bracketed and reset around its own splot -- 'unset surface'/'set
                // view map' apply to the whole splot command, so contour can't share
                // one with a normal pm3d surface, and the reset keeps a later cell's
                // 3D plot (in the same gnuplot session, for Figure) unaffected.
                fprintf(pipe, "set contour base\n");
                fprintf(pipe, "set cntrparam levels auto 10\n");
                fprintf(pipe, "unset surface\n");
                fprintf(pipe, "set view map\n");
                // Without this, gnuplot treats each contour level as its own
                // "curve" and auto-increments line colour/key entry per level,
                // overriding our explicit lc rgb and flooding the legend with
                // one entry per level instead of one for the whole series.
                fprintf(pipe, "unset clabel\n");
                fprintf(pipe, "splot %s\n", join(contour_pieces).c_str());
                fprintf(pipe, "unset contour\n");
                fprintf(pipe, "set surface\n");
                fprintf(pipe, "set view 60,30,1,1\n");
                // clabel only affects contour rendering, and every contour this
                // library draws wants it off, so it's left unset rather than
                // restored (gnuplot has no bare "set clabel" -- it needs a format
                // string argument).
            }
            if (!polar_pieces.empty()) {
                // Same reasoning as contour: 'set polar' reinterprets the whole plot
                // command's coordinates, so it gets its own statement and is reset
                // afterwards rather than leaking into later cartesian plots.
                fprintf(pipe, "set polar\n");
                fprintf(pipe, "set angles radians\n");
                fprintf(pipe, "plot %s\n", join(polar_pieces).c_str());
                fprintf(pipe, "unset polar\n");
            }
        }

        void render_standalone(bool hold) const {
            if (empty()) {
                std::cerr << "Plotter: show() called with no series added, nothing to plot\n";
                return;
            }

            const int window_id = detail::plot_id_counter().fetch_add(1);
            const std::filesystem::path temp_dir = std::filesystem::temp_directory_path();
            const std::string prefix = "plotter_" + std::to_string(window_id);
            const std::vector<std::string> file_paths = write_data_files(temp_dir, prefix);
            
            detail::GnuplotSession session = detail::open_gnuplot_session(hold);
            FILE* gnupipe = session.pipe;
            if (!gnupipe) {
                std::cerr << "Plotter: failed to start gnuplot. Is it installed and on PATH?\n";
                return;
            }

            write_global_cosmetic(gnupipe, 500, 500, window_id);
            write_settings(gnupipe);
            write_plot_commands(gnupipe, file_paths);

            if (hold) {
                fprintf(gnupipe, "pause mouse close\n");
            }

            detail::close_gnuplot_session(session);

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
            W_sets.push_back({});
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
            W_sets.push_back({});
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
            W_sets.push_back({});
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
            W_sets.push_back({});
            colours.push_back(colour);
            names.push_back(name);
            fill_alpha.push_back(0.0);

            style.push_back("points pt " + std::to_string(point_shape) + " ps " + std::to_string(point_size) + " lc rgb");
            smoothed.push_back("");
        }
        
        void add_step(const std::vector<double>& x, const std::vector<double>& y, const char* name, const char* colour, const char* mode = "steps") {
            if (x.size() != y.size()) {
                std::cerr << "Plotter::add_step: x and y size mismatch for series '" << name << "', skipped\n";
                return;
            }

            type.push_back('l');
            side.push_back('l');
            X_sets.push_back(x);
            Y_sets.push_back(y);
            Z_sets.push_back(y);
            W_sets.push_back({});
            colours.push_back(colour);
            names.push_back(name);
            fill_alpha.push_back(0.0);

            std::string m = "steps";
            if (std::strcmp(mode, "fsteps") == 0) m = "fsteps";
            else if (std::strcmp(mode, "histeps") == 0) m = "histeps";
            style.push_back(m + " lc rgb");
            smoothed.push_back("");
        }

        void add_intensity(const std::vector<double>& x, const std::vector<double>& y, const std::vector<double>& z, const char* name) {
            type.push_back('i');
            side.push_back('l');
            X_sets.push_back(x);
            Y_sets.push_back(y);
            Z_sets.push_back(z);
            W_sets.push_back({});
            colours.push_back("null");
            names.push_back(name);
            fill_alpha.push_back(0.0);
            style.push_back("");
            smoothed.push_back("");
        }
        
        void add_histogram2d(const std::vector<double>& x, const std::vector<double>& y, const char* name, int xbins = 20, int ybins = 20) {
            if (x.size() != y.size()) {
                std::cerr << "Plotter::add_histogram2d: x and y size mismatch for series '" << name << "', skipped\n";
                return;
            }
            if (x.empty()) {
                std::cerr << "Plotter::add_histogram2d: no samples for series '" << name << "', skipped\n";
                return;
            }
            if (xbins < 1) xbins = 1;
            if (ybins < 1) ybins = 1;

            double xlo = x[0], xhi = x[0], ylo = y[0], yhi = y[0];
            for (double v : x) { xlo = std::min(xlo, v); xhi = std::max(xhi, v); }
            for (double v : y) { ylo = std::min(ylo, v); yhi = std::max(yhi, v); }
            if (xhi <= xlo) { xhi = xlo + 1.0; xbins = 1; }
            if (yhi <= ylo) { yhi = ylo + 1.0; ybins = 1; }
            const double xw = (xhi - xlo) / xbins;
            const double yw = (yhi - ylo) / ybins;

            std::vector<double> counts(static_cast<size_t>(xbins) * static_cast<size_t>(ybins), 0.0);
            for (size_t k = 0; k < x.size(); k++) {
                int xi = static_cast<int>((x[k] - xlo) / xw);
                int yi = static_cast<int>((y[k] - ylo) / yw);
                if (xi >= xbins) xi = xbins - 1;
                if (yi >= ybins) yi = ybins - 1;
                if (xi < 0) xi = 0;
                if (yi < 0) yi = 0;
                counts[static_cast<size_t>(xi) * static_cast<size_t>(ybins) + static_cast<size_t>(yi)] += 1.0;
            }

            std::vector<double> gx, gy, gz;
            gx.reserve(counts.size()); gy.reserve(counts.size()); gz.reserve(counts.size());
            for (int xi = 0; xi < xbins; xi++) {
                for (int yi = 0; yi < ybins; yi++) {
                    gx.push_back(xlo + (xi + 0.5) * xw);
                    gy.push_back(ylo + (yi + 0.5) * yw);
                    gz.push_back(counts[static_cast<size_t>(xi) * static_cast<size_t>(ybins) + static_cast<size_t>(yi)]);
                }
            }

            add_intensity(gx, gy, gz, name);
        }

        void add_surf(const std::vector<double>& x, const std::vector<double>& y, const std::vector<double>& z, const char* name) {
            type.push_back('s');
            side.push_back('l');
            X_sets.push_back(x);
            Y_sets.push_back(y);
            Z_sets.push_back(z);
            W_sets.push_back({});
            colours.push_back("null");
            names.push_back(name);
            fill_alpha.push_back(0.0);
            style.push_back("");
            smoothed.push_back("");
        }
        
        void add_contour(const std::vector<double>& x, const std::vector<double>& y, const std::vector<double>& z, const char* name, const char* colour) {
            type.push_back('c');
            side.push_back('l');
            X_sets.push_back(x);
            Y_sets.push_back(y);
            Z_sets.push_back(z);
            W_sets.push_back({});
            colours.push_back(colour);
            names.push_back(name);
            fill_alpha.push_back(0.0);
            style.push_back("");
            smoothed.push_back("");
        }
        
        void add_scatter3d(const std::vector<double>& x, const std::vector<double>& y, const std::vector<double>& z, const char* name, const char* colour, int point_shape = 7, double point_size = 1) {
            if (x.size() != y.size() || x.size() != z.size()) {
                std::cerr << "Plotter::add_scatter3d: x/y/z size mismatch for series '" << name << "', skipped\n";
                return;
            }

            type.push_back('g');
            side.push_back('l');
            X_sets.push_back(x);
            Y_sets.push_back(y);
            Z_sets.push_back(z);
            W_sets.push_back({});
            colours.push_back(colour);
            names.push_back(name);
            fill_alpha.push_back(0.0);
            style.push_back("points pt " + std::to_string(point_shape) + " ps " + std::to_string(point_size) + " lc rgb");
            smoothed.push_back("");
        }
        
        void add_line3d(const std::vector<double>& x, const std::vector<double>& y, const std::vector<double>& z, const char* name, const char* colour, const char* line = "solid") {
            if (x.size() != y.size() || x.size() != z.size()) {
                std::cerr << "Plotter::add_line3d: x/y/z size mismatch for series '" << name << "', skipped\n";
                return;
            }

            type.push_back('g');
            side.push_back('l');
            X_sets.push_back(x);
            Y_sets.push_back(y);
            Z_sets.push_back(z);
            W_sets.push_back({});
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
            W_sets.push_back({});
            colours.push_back(colour);
            names.push_back(name);
            fill_alpha.push_back(alpha);

            style.push_back("");
            smoothed.push_back("");
        }
        
        void add_stacked_area(const std::vector<double>& x, const std::vector<std::string>& series_names, const std::vector<std::vector<double>>& series_values, const std::vector<std::string>& series_colours, double alpha = 0.7) {
            if (series_names.size() != series_values.size() || series_names.size() != series_colours.size()) {
                std::cerr << "Plotter::add_stacked_area: series_names/series_values/series_colours size mismatch, skipped\n";
                return;
            }
            const size_t k = series_names.size();
            if (k == 0) return;
            for (size_t l = 0; l < k; l++) {
                if (series_values[l].size() != x.size()) {
                    std::cerr << "Plotter::add_stacked_area: series '" << series_names[l] << "' size mismatch, skipped\n";
                    return;
                }
            }

            std::vector<double> running(x.size(), 0.0);
            for (size_t l = 0; l < k; l++) {
                std::vector<double> top(x.size());
                for (size_t i = 0; i < x.size(); i++) top[i] = running[i] + series_values[l][i];

                type.push_back('f');
                side.push_back('l');
                X_sets.push_back(x);
                Y_sets.push_back(running);
                Z_sets.push_back(top);
                W_sets.push_back({});
                colours.push_back(series_colours[l]);
                names.push_back(series_names[l]);
                fill_alpha.push_back(alpha);
                style.push_back("");
                smoothed.push_back("");

                running = top;
            }
        }

        void add_errorbars(const std::vector<double>& x, const std::vector<double>& y, const std::vector<double>& yerr, const char* name, const char* colour, double point_size = 1) {
            if (x.size() != y.size() || x.size() != yerr.size()) {
                std::cerr << "Plotter::add_errorbars: size mismatch for series '" << name << "', skipped\n";
                return;
            }

            std::vector<double> ylow(x.size()), yhigh(x.size());
            for (size_t j = 0; j < x.size(); j++) {
                ylow[j] = y[j] - yerr[j];
                yhigh[j] = y[j] + yerr[j];
            }

            type.push_back('e');
            side.push_back('l');
            X_sets.push_back(x);
            Y_sets.push_back(y);
            Z_sets.push_back(ylow);
            W_sets.push_back(yhigh);
            colours.push_back(colour);
            names.push_back(name);
            fill_alpha.push_back(0.0);
            style.push_back("yerrorbars pt 7 ps " + std::to_string(point_size) + " lc rgb");
            smoothed.push_back("");
        }
        
        void add_vector(const std::vector<double>& x, const std::vector<double>& y, const std::vector<double>& dx, const std::vector<double>& dy, const char* name, const char* colour) {
            if (x.size() != y.size() || x.size() != dx.size() || x.size() != dy.size()) {
                std::cerr << "Plotter::add_vector: x/y/dx/dy size mismatch for series '" << name << "', skipped\n";
                return;
            }

            type.push_back('v');
            side.push_back('l');
            X_sets.push_back(x);
            Y_sets.push_back(y);
            Z_sets.push_back(dx);
            W_sets.push_back(dy);
            colours.push_back(colour);
            names.push_back(name);
            fill_alpha.push_back(0.0);
            style.push_back("");
            smoothed.push_back("");
        }
        
        void add_histogram(const std::vector<double>& samples, const char* name, const char* colour, int bins = 10) {
            if (samples.empty()) {
                std::cerr << "Plotter::add_histogram: no samples for series '" << name << "', skipped\n";
                return;
            }
            if (bins < 1) bins = 1;

            double lo = samples[0], hi = samples[0];
            for (double v : samples) {
                lo = std::min(lo, v);
                hi = std::max(hi, v);
            }
            if (hi <= lo) {
                hi = lo + 1.0;
                bins = 1;
            }
            const double bin_width = (hi - lo) / bins;

            std::vector<double> centers(bins), counts(bins, 0.0), widths(bins, bin_width * 0.95);
            for (int i = 0; i < bins; i++) centers[i] = lo + (i + 0.5) * bin_width;
            for (double v : samples) {
                int idx = static_cast<int>((v - lo) / bin_width);
                if (idx >= bins) idx = bins - 1;
                if (idx < 0) idx = 0;
                counts[idx] += 1.0;
            }

            type.push_back('b');
            side.push_back('l');
            X_sets.push_back(centers);
            Y_sets.push_back(counts);
            Z_sets.push_back(widths);
            W_sets.push_back({});
            colours.push_back(colour);
            names.push_back(name);
            fill_alpha.push_back(0.0);
            style.push_back("boxes lc rgb");
            smoothed.push_back("");
        }
        
        void add_bar(const std::vector<std::string>& categories, const std::vector<double>& values, const char* name, const char* colour, double width = 0.6) {
            if (categories.size() != values.size()) {
                std::cerr << "Plotter::add_bar: categories and values size mismatch for series '" << name << "', skipped\n";
                return;
            }

            xtic_labels.clear();
            std::vector<double> x(categories.size()), w(categories.size(), width);
            for (size_t i = 0; i < categories.size(); i++) {
                x[i] = static_cast<double>(i);
                xtic_labels.emplace_back(x[i], categories[i]);
            }

            type.push_back('b');
            side.push_back('l');
            X_sets.push_back(x);
            Y_sets.push_back(values);
            Z_sets.push_back(w);
            W_sets.push_back({});
            colours.push_back(colour);
            names.push_back(name);
            fill_alpha.push_back(0.0);
            style.push_back("boxes lc rgb");
            smoothed.push_back("");
        }
        
        void add_bar_grouped(const std::vector<std::string>& categories, const std::vector<std::string>& group_names, const std::vector<std::vector<double>>& group_values, const std::vector<std::string>& group_colours, double group_width = 0.8) {
            if (group_names.size() != group_values.size() || group_names.size() != group_colours.size()) {
                std::cerr << "Plotter::add_bar_grouped: group_names/group_values/group_colours size mismatch, skipped\n";
                return;
            }
            const size_t n = categories.size();
            const size_t k = group_names.size();
            if (k == 0) return;
            const double bar_width = group_width / static_cast<double>(k);

            xtic_labels.clear();
            for (size_t i = 0; i < n; i++) xtic_labels.emplace_back(static_cast<double>(i), categories[i]);

            for (size_t g = 0; g < k; g++) {
                if (group_values[g].size() != n) {
                    std::cerr << "Plotter::add_bar_grouped: group '" << group_names[g] << "' values size mismatch, skipped\n";
                    continue;
                }
                const double offset = -group_width / 2.0 + bar_width * (g + 0.5);
                std::vector<double> x(n), w(n, bar_width * 0.95);
                for (size_t i = 0; i < n; i++) x[i] = static_cast<double>(i) + offset;

                type.push_back('b');
                side.push_back('l');
                X_sets.push_back(x);
                Y_sets.push_back(group_values[g]);
                Z_sets.push_back(w);
                W_sets.push_back({});
                colours.push_back(group_colours[g]);
                names.push_back(group_names[g]);
                fill_alpha.push_back(0.0);
                style.push_back("boxes lc rgb");
                smoothed.push_back("");
            }
        }
        
        void add_bar_stacked(const std::vector<std::string>& categories, const std::vector<std::string>& layer_names, const std::vector<std::vector<double>>& layer_values, const std::vector<std::string>& layer_colours, double width = 0.6) {
            if (layer_names.size() != layer_values.size() || layer_names.size() != layer_colours.size()) {
                std::cerr << "Plotter::add_bar_stacked: layer_names/layer_values/layer_colours size mismatch, skipped\n";
                return;
            }
            const size_t n = categories.size();
            const size_t k = layer_names.size();
            if (k == 0) return;
            for (size_t l = 0; l < k; l++) {
                if (layer_values[l].size() != n) {
                    std::cerr << "Plotter::add_bar_stacked: layer '" << layer_names[l] << "' values size mismatch, skipped\n";
                    return;
                }
            }

            xtic_labels.clear();
            for (size_t i = 0; i < n; i++) xtic_labels.emplace_back(static_cast<double>(i), categories[i]);

            std::vector<std::vector<double>> cumulative(k, std::vector<double>(n, 0.0));
            for (size_t i = 0; i < n; i++) {
                double running = 0.0;
                for (size_t l = 0; l < k; l++) {
                    running += layer_values[l][i];
                    cumulative[l][i] = running;
                }
            }

            std::vector<double> x(n);
            for (size_t i = 0; i < n; i++) x[i] = static_cast<double>(i);
            
            for (size_t l = k; l-- > 0; ) {
                std::vector<double> w(n, width);
                type.push_back('b');
                side.push_back('l');
                X_sets.push_back(x);
                Y_sets.push_back(cumulative[l]);
                Z_sets.push_back(w);
                W_sets.push_back({});
                colours.push_back(layer_colours[l]);
                names.push_back(layer_names[l]);
                fill_alpha.push_back(0.0);
                style.push_back("boxes lc rgb");
                smoothed.push_back("");
            }
        }
        
        void add_boxplot(const std::vector<std::string>& categories, const std::vector<std::vector<double>>& samples, const char* name, const char* colour) {
            if (categories.size() != samples.size()) {
                std::cerr << "Plotter::add_boxplot: categories and samples size mismatch for series '" << name << "', skipped\n";
                return;
            }

            xtic_labels.clear();
            bool any = false;
            bool title_used = false;
            for (size_t i = 0; i < categories.size(); i++) {
                xtic_labels.emplace_back(static_cast<double>(i), categories[i]);
                if (samples[i].empty()) continue;
                any = true;

                std::vector<double> x(samples[i].size(), static_cast<double>(i));

                type.push_back('w');
                side.push_back('l');
                X_sets.push_back(x);
                Y_sets.push_back(samples[i]);
                Z_sets.push_back({});
                W_sets.push_back({});
                colours.push_back(colour);
                names.push_back(title_used ? "" : name);
                fill_alpha.push_back(0.0);
                style.push_back("boxplot lc rgb");
                smoothed.push_back("");
                title_used = true;
            }
            if (!any) {
                std::cerr << "Plotter::add_boxplot: no samples for series '" << name << "', skipped\n";
            }
        }
        
        void add_polar(const std::vector<double>& theta, const std::vector<double>& r, const char* name, const char* colour, const char* style_kind = "lines") {
            if (theta.size() != r.size()) {
                std::cerr << "Plotter::add_polar: theta and r size mismatch for series '" << name << "', skipped\n";
                return;
            }

            type.push_back('r');
            side.push_back('l');
            X_sets.push_back(theta);
            Y_sets.push_back(r);
            Z_sets.push_back(r);
            W_sets.push_back({});
            colours.push_back(colour);
            names.push_back(name);
            fill_alpha.push_back(0.0);
            style.push_back(std::strcmp(style_kind, "points") == 0 ? "points lc rgb" : "lines lc rgb");
            smoothed.push_back("");
        }

        void show(bool hold = true) const {
            auto snapshot = std::make_shared<Plotter>(*this);
            detail::launch_render([snapshot, hold]() {
                snapshot->render_standalone(hold);
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
            const int window_id = detail::plot_id_counter().fetch_add(1);
            const std::filesystem::path temp_dir = std::filesystem::temp_directory_path();
            const std::string fig_prefix = "figure_" + std::to_string(window_id);

            std::vector<std::vector<std::string>> all_paths(cells_.size());
            for (size_t i = 0; i < cells_.size(); i++) {
                all_paths[i] = cells_[i].write_data_files(temp_dir, fig_prefix + "_" + std::to_string(i));
            }

            detail::GnuplotSession session = detail::open_gnuplot_session(hold);
            FILE* gnupipe = session.pipe;
            if (!gnupipe) {
                std::cerr << "Figure: failed to start gnuplot. Is it installed and on PATH?\n";
                return;
            }

            Plotter::write_global_cosmetic(gnupipe, 480 * cols_, 380 * rows_, window_id);

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
                fprintf(gnupipe, "pause mouse close\n");
            }

            detail::close_gnuplot_session(session);

            for (const auto& paths : all_paths) {
                for (const auto& path : paths) {
                    std::remove(path.c_str());
                }
            }
        }
    };

}
