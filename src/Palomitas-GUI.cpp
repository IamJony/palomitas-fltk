#include <FL/Fl.H>
#include <FL/Fl_Window.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_Scroll.H>
#include <FL/Fl_Widget.H>
#include <FL/Fl_Shared_Image.H>
#include <FL/Fl_RGB_Image.H>
#include <FL/fl_draw.H>
#include <FL/fl_ask.H>

#include "themes.h"
#include "library.h"

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <array>
#include <regex>
#include <functional>
#include <thread>
#include <mutex>
#include <chrono>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <poll.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <limits.h>

// ---------- Utilidades ----------
static std::string shell_quote(const std::string& s) {
    std::string out = "'";
    for (char c : s) {
        if (c == '\'') out += "'\\''";
        else out += c;
    }
    out += "'";
    return out;
}

static std::string run_cmd_timeout(const std::string& cmd, int timeout_sec = 120) {
    int pipefd[2];
    if (pipe(pipefd) != 0) return "";

    pid_t pid = fork();
    if (pid < 0) { close(pipefd[0]); close(pipefd[1]); return ""; }
    if (pid == 0) {
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);
        execl("/bin/sh", "sh", "-c", cmd.c_str(), (char*)nullptr);
        _exit(127);
    }

    close(pipefd[1]);
    std::string result;
    char buf[4096];
    auto start = std::chrono::steady_clock::now();

    while (true) {
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - start).count();
        if (elapsed >= timeout_sec) {
            kill(pid, SIGKILL);
            break;
        }
        struct pollfd pfd{pipefd[0], POLLIN, 0};
        int pr = poll(&pfd, 1, 500);
        if (pr < 0 && errno != EINTR) break;
        if (pr == 0) continue;
        ssize_t n = read(pipefd[0], buf, sizeof(buf));
        if (n > 0) result.append(buf, n);
        else if (n == 0) break;
        else if (errno != EINTR) break;
    }
    close(pipefd[0]);

    int status = 0;
    if (waitpid(pid, &status, WNOHANG) == 0) {
        kill(pid, SIGKILL);
        waitpid(pid, &status, 0);
    }
    return result;
}

static std::string read_file(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) return "";
    std::stringstream ss; ss << f.rdbuf(); return ss.str();
}

static bool file_exists(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0 && st.st_size > 0;
}

static std::string json_get_str(const std::string& json, const std::string& key) {
    std::regex re("\"" + key + "\"\\s*:\\s*\"((?:[^\"\\\\]|\\\\.)*)\"");
    std::smatch m;
    if (std::regex_search(json, m, re)) {
        std::string v = m[1].str(), out;
        for (size_t i = 0; i < v.size(); ++i) {
            if (v[i] == '\\' && i + 1 < v.size()) {
                char n = v[i+1];
                if (n=='/') { out+='/'; ++i; continue; }
                if (n=='"') { out+='"'; ++i; continue; }
                if (n=='n') { out+='\n'; ++i; continue; }
                if (n=='t') { out+='\t'; ++i; continue; }
            }
            out += v[i];
        }
        return out;
    }
    return "";
}

static std::string to_max_res_url(const std::string& url) {
    if (url.empty()) return url;
    std::string base = url, suffix;
    auto qpos = base.find_first_of("?#");
    if (qpos != std::string::npos) { suffix = base.substr(qpos); base = base.substr(0, qpos); }

    auto slash = base.find_last_of('/');
    auto dot   = base.find_last_of('.');
    std::string dir, ext;
    if (dot != std::string::npos && (slash == std::string::npos || dot > slash)) {
        dir = base.substr(0, dot);
        ext = base.substr(dot);
    } else {
        dir = base;
        ext = ".jpg";
    }

    std::regex re("-\\d+x\\d+$");
    if (std::regex_search(dir, re))
        dir = std::regex_replace(dir, re, "");

    return dir + "-768x1152" + ext + suffix;
}

static std::string upgrade_image_url(const std::string& url) {
    return to_max_res_url(url);
}

static std::string upgrade_image_url_alt(const std::string& url) {
    return to_max_res_url(url);
}

static std::vector<MovieItem> parse_search_json(const std::string& json) {
    std::vector<MovieItem> out;
    if (json.empty()) return out;

    std::regex title_re("\"title\"\\s*:\\s*\"");
    auto end = std::sregex_iterator();

    size_t search_from = 0;
    while (search_from < json.size()) {
        auto it = std::sregex_iterator(json.begin() + search_from, json.end(), title_re);
        if (it == end) break;

        size_t title_pos = search_from + (size_t)it->position();

        size_t brace_open = std::string::npos;
        int depth = 0;
        for (size_t i = title_pos; i > 0; --i) {
            char c = json[i-1];
            if (c == '}') ++depth;
            else if (c == '{') {
                if (depth == 0) { brace_open = i - 1; break; }
                --depth;
            }
        }
        if (brace_open == std::string::npos) { search_from = title_pos + 1; continue; }

        size_t brace_close = std::string::npos;
        depth = 0;
        for (size_t i = brace_open; i < json.size(); ++i) {
            char c = json[i];
            if (c == '{') ++depth;
            else if (c == '}') {
                if (--depth == 0) { brace_close = i; break; }
            }
        }
        if (brace_close == std::string::npos) { search_from = title_pos + 1; continue; }

        std::string chunk = json.substr(brace_open, brace_close - brace_open + 1);

        MovieItem m;
        m.title = json_get_str(chunk, "title");
        if (!m.title.empty()) {
            m.url = json_get_str(chunk, "url");
            m.img = json_get_str(chunk, "img");

            m.id = json_get_str(chunk, "id");
            if (m.id.empty()) {
                std::hash<std::string> h;
                m.id = std::to_string(h(m.title));
            }

            std::regex extra_re("\"extra\"\\s*:\\s*\\{([^}]*)\\}");
            std::smatch em;
            if (std::regex_search(chunk, em, extra_re)) {
                std::string ex = em[1].str();
                m.year = json_get_str(ex, "date");
                m.imdb = json_get_str(ex, "imdb");
            }
            if (m.year.empty()) m.year = json_get_str(chunk, "date");
            if (m.imdb.empty()) m.imdb = json_get_str(chunk, "imdb");

            out.push_back(std::move(m));
        }

        search_from = brace_close + 1;
    }

    return out;
}

// ---------- Imagen de fondo ----------
class BgImage : public Fl_Box {
public:
    Fl_Shared_Image* img = nullptr;
    BgImage(int x, int y, int w, int h) : Fl_Box(x, y, w, h) { box(FL_NO_BOX); }
    void set_image(Fl_Shared_Image* i) { img = i; redraw(); }
    void draw() override {
        if (!img || img->w() <= 0) return;
        int pw = img->w(), ph = img->h();
        double ar_img = (double)pw / ph;
        double ar_box = (double)w() / h();
        int dw, dh;
        if (ar_img > ar_box) { dh = h(); dw = (int)(dh * ar_img); }
        else                 { dw = w(); dh = (int)(dw / ar_img); }
        if (dw <= 0 || dh <= 0) return;
        Fl_Image* scaled = img->copy(dw, dh);
        if (!scaled) return;
        int dx = x() + (w() - dw) / 2;
        int dy = y() + (h() - dh) / 2;
        fl_push_clip(x(), y(), w(), h());
        scaled->draw(dx, dy);
        fl_pop_clip();
        delete scaled;
    }
};

// ---------- Botón moderno ----------
class ModernButton : public Fl_Button {
public:
    Fl_Color bg = C_BTN, hi = C_BTN_HI, fg = C_TEXT;
    bool hover = false, selected = false;
    int radius = 6;
    ModernButton(int x, int y, int w, int h, const char* l = nullptr)
        : Fl_Button(x, y, w, h, l) {
        box(FL_NO_BOX); clear_visible_focus();
    }
    void draw() override {
        Fl_Color c = selected ? C_SEL : (hover ? hi : bg);
        int r = radius;
        fl_color(c);
        fl_rectf(x(), y() + r, w(), h() - 2*r);
        fl_rectf(x() + r, y(), w() - 2*r, h());
        fl_pie(x(), y(), 2*r, 2*r, 90, 180);
        fl_pie(x() + w() - 2*r, y(), 2*r, 2*r, 0, 90);
        fl_pie(x(), y() + h() - 2*r, 2*r, 2*r, 180, 270);
        fl_pie(x() + w() - 2*r, y() + h() - 2*r, 2*r, 2*r, 270, 360);
        if (label()) {
            fl_font(FL_HELVETICA_BOLD, 12);
            fl_color(active_r() ? fg : C_DIM);
            fl_draw(label(), x(), y(), w(), h(), FL_ALIGN_CENTER | FL_ALIGN_INSIDE);
        }
    }
    int handle(int e) override {
        if (e == FL_ENTER || e == FL_LEAVE) { hover = (e == FL_ENTER); redraw(); }
        return Fl_Button::handle(e);
    }
};

// ---------- Label ----------
class Label : public Fl_Widget {
public:
    std::string text;
    int size = 13;
    Fl_Font font = FL_HELVETICA;
    int fg_role = 0;
    Fl_Align al = FL_ALIGN_LEFT;
    Label(int x, int y, int w, int h) : Fl_Widget(x, y, w, h) {}
    void set(const std::string& t) { text = t; redraw(); if (parent()) parent()->redraw(); }
    void draw() override {
        fl_font(font, size);
        Fl_Color c = (fg_role == 0) ? C_TEXT : (fg_role == 1) ? C_DIM : (fg_role == 2) ? C_ACCENT : (fg_role == 3) ? C_GREEN : C_RED;
        fl_color(c);
        fl_draw(text.c_str(), x(), y(), w(), h(), al | FL_ALIGN_INSIDE);
    }
};

// ---------- Celda de película ----------
class MovieCell : public Fl_Widget {
public:
    int index;
    MovieItem* movie;
    bool selected = false;

    MovieCell(int x, int y, int w, int h, MovieItem* m, int idx)
        : Fl_Widget(x, y, w, h), index(idx), movie(m) { box(FL_NO_BOX); }

    void set_selected(bool s) { if (selected == s) return; selected = s; redraw(); }

    int handle(int event) override {
        switch (event) {
            case FL_PUSH: return 1;
            case FL_RELEASE: do_callback(); return 1;
            default: return Fl_Widget::handle(event);
        }
    }

    void draw() override {
        fl_push_clip(x(), y(), w(), h());

        int r = 6;
        fl_color(selected ? C_SEL : C_PANEL);
        fl_rectf(x(), y() + r, w(), h() - 2*r);
        fl_rectf(x() + r, y(), w() - 2*r, h());
        fl_pie(x(), y(), 2*r, 2*r, 90, 180);
        fl_pie(x() + w() - 2*r, y(), 2*r, 2*r, 0, 90);
        fl_pie(x(), y() + h() - 2*r, 2*r, 2*r, 180, 270);
        fl_pie(x() + w() - 2*r, y() + h() - 2*r, 2*r, 2*r, 270, 360);

        int text_block_h = 62;
        int img_w = w();
        int img_h = h() - text_block_h;
        if (img_h < 1) img_h = 1;

        std::string img_path = "/tmp/TEMP_Palomitas/poster_" + movie->id + ".img";
        Fl_Shared_Image* img = Fl_Shared_Image::get(img_path.c_str());

        if (img && img->w() > 0) {
            Fl_Image* scaled = img->copy(img_w, img_h);
            if (scaled) {
                scaled->draw(x(), y());
                delete scaled;
            } else {
                img->draw(x(), y());
            }
        } else {
            fl_draw_box(FL_BORDER_BOX, x(), y(), img_w, img_h, C_DIM);
            fl_color(C_TEXT); fl_font(FL_HELVETICA, 10);
            fl_draw("Sin imagen", x(), y(), img_w, img_h, FL_ALIGN_CENTER);
        }

        int pad_top = 8;
        int pad_x = 5;

        int text_y = y() + img_h + pad_top;
        int text_w = w() - pad_x * 2;

        std::string t = movie->title;
        fl_color(selected ? FL_WHITE : C_TEXT);
        fl_font(FL_HELVETICA_BOLD, 10);
        fl_draw(t.c_str(), x() + pad_x, text_y, text_w, 28,
                FL_ALIGN_LEFT | FL_ALIGN_TOP | FL_ALIGN_WRAP | FL_ALIGN_CLIP);

        int pad_mid = 6;

        fl_color(selected ? FL_WHITE : C_DIM);
        fl_font(FL_HELVETICA, 9);
        std::string meta = movie->year + "  IMDb:" + movie->imdb;
        fl_draw(meta.c_str(), x() + pad_x, text_y + 28 + pad_mid, text_w, 14,
                FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_CLIP);

        if (selected) {
            fl_color(C_ACCENT);
            fl_rect(x(), y(), w(), h());
        }

        fl_pop_clip();
    }
};

// ---------- Ventana principal ----------
class MainWindow : public Fl_Window {
public:
    MainWindow(int w, int h, const char* title) : Fl_Window(w, h, title) {}
    void resize(int X, int Y, int W, int H) override {
        Fl_Window::resize(X, Y, W, H);
        if (on_resize_cb) on_resize_cb(W, H);
    }
    int handle(int e) override {
        if (e == FL_SHOW) {
            Fl::add_timeout(0.1, [](void* data) {
                auto* win = (MainWindow*)data;
                if (!win || !win->visible()) return;
                if (win->on_resize_cb) win->on_resize_cb(win->w(), win->h());
                win->redraw();
                Fl::flush();
            }, this);
        }
        return Fl_Window::handle(e);
    }
    std::function<void(int,int)> on_resize_cb;
};

// ---------- Resolución universal de rutas ----------
static std::string find_base_dir(const std::string& relative_marker) {
    std::vector<std::string> candidates;

    if (const char* env = std::getenv("PALOMITAS_HOME")) {
        if (env[0]) candidates.emplace_back(env);
    }

    auto add_with_parents = [&](std::string dir) {
        for (int i = 0; i < 6 && !dir.empty() && dir != "/"; ++i) {
            candidates.push_back(dir);
            auto sl = dir.find_last_of('/');
            if (sl == std::string::npos || sl == 0) break;
            dir = dir.substr(0, sl);
        }
    };

    char buf[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n > 0) {
        buf[n] = '\0';
        std::string exe(buf);
        auto pos = exe.find_last_of('/');
        if (pos != std::string::npos) add_with_parents(exe.substr(0, pos));
    }

    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd))) add_with_parents(cwd);

    candidates.push_back("/usr/local/share/palomitas");
    candidates.push_back("/usr/share/palomitas");
    candidates.push_back("/opt/palomitas");
    candidates.push_back(".");

    for (auto& c : candidates) {
        struct stat st;
        if (stat((c + "/" + relative_marker).c_str(), &st) == 0)
            return c;
    }
    return candidates.empty() ? "." : candidates.front();
}

// ---------- App ----------
class PalomitasGUI {
public:
    MainWindow* window;
    Fl_Input* search_input;
    ModernButton* search_btn;
    ModernButton* back_btn;
    ModernButton* fav_side_btn;
    Fl_Choice* theme_choice;
    Fl_Scroll* scroll;
    Fl_Box* scroll_spacer;
    BgImage* bg_image = nullptr;
    Fl_Shared_Image* home_shared_img = nullptr;
    std::vector<MovieCell*> cells;
    ModernButton* play_btn;
    ModernButton* fav_btn;
    ModernButton* clean_btn;
    Label* fav_count_label = nullptr;
    Label* status_label;

    std::vector<MovieItem> movies;
    int selected_index = -1;

    std::string base_dir;
    std::string cli_dir;
    std::string asset_dir;

    enum View { V_HOME, V_SEARCH, V_FAVS };
    View current_view = V_HOME;
    View prev_view = V_HOME;
    std::vector<MovieItem> prev_movies;
    std::string prev_query;

    bool busy = false;
    bool searching = false;
    std::string busy_msg;

    std::mutex result_mutex;
    bool result_ready = false;
    struct SearchResult {
        std::string json;
        std::string log;
        std::string query;
        bool ok = false;
    } pending_result;

    static constexpr int CELL_W = 120;
    static constexpr int CELL_H = 195;
    static constexpr int GAP = 10;
    static constexpr int PADDING = 10;
    static constexpr int SIDE_W = 110;

    PalomitasGUI(int w, int h, const char* title) {
        resolve_paths();

        fl_register_images();
        window = new MainWindow(w, h, title);
        window->on_resize_cb = [this](int W, int H) { layout(W, H); };

        Fl_Box* header = new Fl_Box(SIDE_W + 20, 10, w - SIDE_W - 200, 30, "Palomitas");
        header->labelfont(FL_BOLD);
        header->labelsize(22);
        header->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
        header->labelcolor(C_TEXT);

        theme_choice = new Fl_Choice(w - 130, 12, 120, 26, "Tema:");
        for (int i = 0; i < theme_count; ++i) theme_choice->add(themes[i].name);
        theme_choice->value(g_theme_index);
        theme_choice->callback(cb_theme, this);

        fav_side_btn = new ModernButton(10, 60, SIDE_W - 20, 36, "Favoritos");
        fav_side_btn->labelsize(11);
        fav_side_btn->callback(cb_show_favs, this);

        fav_count_label = new Label(10, 100, SIDE_W - 20, 16);
        fav_count_label->size = 10;
        fav_count_label->fg_role = 1;
        fav_count_label->al = FL_ALIGN_CENTER;
        fav_count_label->set("0 peliculas");

        back_btn = new ModernButton(w - 290, 12, 90, 26, "Volver");
        back_btn->callback(cb_back, this);
        back_btn->deactivate();

        search_input = new Fl_Input(SIDE_W + 70, 55, w - SIDE_W - 220, 30, "Buscar:");
        search_input->when(FL_WHEN_ENTER_KEY);
        search_input->callback(cb_search, this);

        search_btn = new ModernButton(w - 130, 55, 100, 30, "Buscar");
        search_btn->callback(cb_search, this);

        scroll = new Fl_Scroll(SIDE_W + 10, 100, w - SIDE_W - 20, h - 150);
        scroll->box(FL_FLAT_BOX);
        scroll_spacer = new Fl_Box(scroll->x(), scroll->y(), scroll->w() - 2, 10);
        scroll->end();

        {
            std::string img_path = asset_dir + "/home.jpeg";
            Fl_Shared_Image* home_img = Fl_Shared_Image::get(img_path.c_str());
            if (!home_img || home_img->w() <= 0) {
                home_img = Fl_Shared_Image::get((base_dir + "/asset/home.jpeg").c_str());
            }
            if (home_img && home_img->w() > 0) home_shared_img = home_img;
        }

        // ---- Fila inferior: status a la izquierda, botones a la derecha ----
        int bar_y = h - 34;
        int bar_h = 28;

        status_label = new Label(SIDE_W + 10, bar_y, w - SIDE_W - 380, bar_h);
        status_label->font = FL_HELVETICA;
        status_label->size = 12;
        status_label->fg_role = 1;
        status_label->al = FL_ALIGN_LEFT | FL_ALIGN_INSIDE;
        status_label->set("Listo. Escribe algo o explora favoritos.");

        clean_btn = new ModernButton(w - 120, bar_y, 100, bar_h, "Limpiar");
        clean_btn->callback(cb_clean, this);

        fav_btn = new ModernButton(w - 120 - 10 - 170, bar_y, 170, bar_h, "Favoritos");
        fav_btn->callback(cb_fav, this);
        fav_btn->deactivate();

        play_btn = new ModernButton(w - 120 - 10 - 170 - 10 - 130, bar_y, 130, bar_h, "Reproducir");
        play_btn->callback(cb_play, this);
        play_btn->deactivate();

        window->end();
        window->resizable(window);

        g_refresh_cb = [this]() { this->apply_colors(); };
        apply_colors();
        update_fav_count();

        check_environment();

        Fl::add_timeout(0.1, poll_search_result_cb, this);
    }

    ~PalomitasGUI() { if (g_refresh_cb) g_refresh_cb = nullptr; }

    void show() { window->show(); }

    void resolve_paths() {
        base_dir = find_base_dir("palomitas-cli/search.sh");

        struct stat st;
        if (stat((base_dir + "/palomitas-cli/search.sh").c_str(), &st) != 0) {
            if (stat((base_dir + "/search.sh").c_str(), &st) == 0) {
                cli_dir = base_dir;
                auto sl = base_dir.find_last_of('/');
                base_dir = (sl != std::string::npos) ? base_dir.substr(0, sl) : ".";
            } else {
                std::string alt = find_base_dir("search.sh");
                if (stat((alt + "/search.sh").c_str(), &st) == 0) {
                    cli_dir = alt;
                    auto sl = alt.find_last_of('/');
                    base_dir = (sl != std::string::npos) ? alt.substr(0, sl) : ".";
                } else {
                    cli_dir = base_dir + "/palomitas-cli";
                }
            }
        } else {
            cli_dir = base_dir + "/palomitas-cli";
        }

        asset_dir = base_dir + "/asset";
        if (stat((asset_dir + "/home.jpeg").c_str(), &st) != 0) {
            std::string test = base_dir;
            for (int i = 0; i < 4; ++i) {
                if (stat((test + "/asset/home.jpeg").c_str(), &st) == 0) {
                    asset_dir = test + "/asset";
                    break;
                }
                auto sl = test.find_last_of('/');
                if (sl == std::string::npos) break;
                test = test.substr(0, sl);
            }
        }

        fprintf(stderr, "[Palomitas] base_dir=%s\n", base_dir.c_str());
        fprintf(stderr, "[Palomitas] cli_dir=%s\n", cli_dir.c_str());
        fprintf(stderr, "[Palomitas] asset_dir=%s\n", asset_dir.c_str());
    }

    void check_environment() {
        std::string msg;
        struct stat st;
        if (stat(cli_dir.c_str(), &st) != 0) {
            msg = "AVISO: no existe " + cli_dir;
        } else if (stat((cli_dir + "/search.sh").c_str(), &st) != 0) {
            msg = "AVISO: falta search.sh en " + cli_dir;
        }
        if (std::system("command -v jq >/dev/null 2>&1") != 0)
            msg += (msg.empty() ? "" : " | ") + std::string("falta jq");
        if (std::system("command -v wget >/dev/null 2>&1") != 0)
            msg += (msg.empty() ? "" : " | ") + std::string("falta wget");
        if (!msg.empty()) {
            status_label->fg_role = 4;
            status_label->set(msg);
            fprintf(stderr, "[Palomitas] %s\n", msg.c_str());
        }
    }

    void set_status(const std::string& msg) {
        busy = false; busy_msg.clear();
        status_label->fg_role = 1; status_label->set(msg);
    }

    void start_busy(const std::string& msg) {
        busy = true; busy_msg = msg;
        status_label->fg_role = 3; status_label->set(msg);
        status_label->redraw();
    }

    void stop_busy(const std::string& msg) {
        busy = false; busy_msg.clear();
        status_label->fg_role = 3;
        status_label->set(msg);
        status_label->redraw();
    }

    void ensure_bg_image() {
        if (bg_image) return;
        if (!home_shared_img) return;
        if (!scroll) return;
        if (scroll->w() < 10 || scroll->h() < 10) return;
        bg_image = new BgImage(scroll->x(), scroll->y(), scroll->w(), scroll->h());
        bg_image->set_image(home_shared_img);
        bg_image->show();
        scroll->add(bg_image);
        scroll->redraw();
    }

    void layout(int W, int H) {
        int rx = SIDE_W + 10;
        search_input->resize(SIDE_W + 70, 55, W - SIDE_W - 220, 30);
        search_btn->resize(W - 130, 55, 100, 30);
        theme_choice->resize(W - 130, 12, 120, 26);
        back_btn->resize(W - 290, 12, 90, 26);
        scroll->resize(rx, 100, W - rx - 10, H - 150);
        if (bg_image) bg_image->resize(scroll->x(), scroll->y(), scroll->w(), scroll->h());

        // Fila inferior: status izquierda, botones derecha (misma Y)
        int bar_y = H - 34;
        int bar_h = 28;
        clean_btn->resize(W - 120, bar_y, 100, bar_h);
        fav_btn->resize(W - 120 - 10 - 170, bar_y, 170, bar_h);
        play_btn->resize(W - 120 - 10 - 170 - 10 - 130, bar_y, 130, bar_h);
        status_label->resize(SIDE_W + 10, bar_y, W - SIDE_W - 380, bar_h);

        fav_side_btn->resize(10, 60, SIDE_W - 20, 36);
        if (fav_count_label) fav_count_label->resize(10, 100, SIDE_W - 20, 16);

        if (!cells.empty()) {
            rebuild_grid_positions_only();
        } else if (!movies.empty()) {
            rebuild_grid();
        } else {
            ensure_bg_image();
        }

        if (bg_image) {
            bg_image->resize(scroll->x(), scroll->y(), scroll->w(), scroll->h());
            bg_image->redraw();
            bg_image->damage(FL_DAMAGE_ALL);
        }
        scroll->redraw();
        scroll->damage(FL_DAMAGE_ALL);
        window->redraw();
        window->damage(FL_DAMAGE_ALL);
    }

    void apply_colors() {
        window->color(C_BG);
        search_input->color(C_PANEL);
        search_input->textcolor(C_TEXT);
        search_input->selection_color(C_SEL);
        theme_choice->color(C_PANEL);
        theme_choice->textcolor(C_TEXT);
        theme_choice->selection_color(C_SEL);
        scroll->color(C_BG);
        scroll->redraw();
        play_btn->bg = C_BTN; play_btn->fg = C_TEXT; play_btn->hi = C_BTN_HI;
        fav_btn->bg = C_BTN; fav_btn->fg = C_TEXT; fav_btn->hi = C_BTN_HI;
        clean_btn->bg = C_BTN; clean_btn->fg = C_TEXT; clean_btn->hi = C_BTN_HI;
        search_btn->bg = C_ACCENT; search_btn->fg = C_BG; search_btn->hi = C_SEL;
        back_btn->bg = C_PANEL; back_btn->fg = C_TEXT; back_btn->hi = C_BTN_HI;
        fav_side_btn->bg = C_PANEL; fav_side_btn->fg = C_ACCENT; fav_side_btn->hi = C_BTN_HI;
        if (fav_count_label) fav_count_label->redraw();
        if (bg_image) bg_image->redraw();
        for (auto* c : cells) c->redraw();
        scroll->redraw();
        window->redraw();
    }

    void rebuild_grid();
    void rebuild_grid_positions_only();
    void highlight_cell(int idx) { for (size_t i = 0; i < cells.size(); ++i) cells[i]->set_selected((int)i == idx); }
    void clear_cells() {
        for (auto* c : cells) delete c;
        cells.clear();
        scroll->redraw();
        scroll->damage(FL_DAMAGE_ALL);
    }

    static void cb_search(Fl_Widget*, void* d) { ((PalomitasGUI*)d)->do_search(); }
    static void cb_play(Fl_Widget*, void* d) { ((PalomitasGUI*)d)->do_play(); }
    static void cb_clean(Fl_Widget*, void* d) { ((PalomitasGUI*)d)->do_clean(); }
    static void cb_fav(Fl_Widget*, void* d) { ((PalomitasGUI*)d)->toggle_favorite(); }
    static void cb_show_favs(Fl_Widget*, void* d) { ((PalomitasGUI*)d)->show_favorites(); }
    static void cb_back(Fl_Widget*, void* d) { ((PalomitasGUI*)d)->go_back(); }
    static void cb_theme(Fl_Widget*, void* d) {
        auto* self = (PalomitasGUI*)d;
        g_theme_index = self->theme_choice->value();
        apply_theme(g_theme_index);
        save_theme(g_theme_index);
        if (!self->movies.empty()) self->rebuild_grid();
    }
    static void cb_cell_clicked(Fl_Widget* w, void* d) {
        auto* cell = (MovieCell*)w;
        auto* gui = (PalomitasGUI*)d;
        gui->selected_index = cell->index;
        gui->highlight_cell(cell->index);
        gui->play_btn->activate();
        gui->fav_btn->activate();
        bool is_fav = Library::instance().is_favorite(gui->movies[cell->index].id);
        gui->fav_btn->label(is_fav ? "Quitar de favoritos" : "Favoritos");
        gui->fav_btn->redraw();
        gui->set_status("Seleccionado: " + gui->movies[cell->index].title);
    }

    void update_fav_count() {
        if (!fav_count_label) return;
        int n = (int)Library::instance().favorites.size();
        char buf[32];
        snprintf(buf, sizeof buf, "%d pelicula%s", n, n == 1 ? "" : "s");
        fav_count_label->set(buf);
        fav_count_label->redraw();
    }

    void save_state() {
        prev_view = current_view;
        prev_movies = movies;
        prev_query = search_input->value() ? search_input->value() : "";
        back_btn->activate();
    }

    void go_back() {
        if (prev_view == current_view && prev_movies.empty()) return;
        current_view = prev_view;
        clear_cells();
        movies = prev_movies;
        selected_index = -1;
        play_btn->deactivate();
        fav_btn->deactivate();
        if (!prev_query.empty()) search_input->value(prev_query.c_str());
        else search_input->value("");
        rebuild_grid();
        back_btn->deactivate();
        set_status("Volviste a la vista anterior.");
    }

    void toggle_favorite() {
        if (selected_index < 0 || selected_index >= (int)movies.size()) {
            set_status("Selecciona una pelicula primero.");
            return;
        }
        const MovieItem& m = movies[selected_index];
        auto& lib = Library::instance();
        bool was_fav = lib.is_favorite(m.id);

        if (was_fav) {
            lib.remove_favorite(m.id);
            set_status("Quitado de favoritos: " + m.title);
            update_fav_count();
            if (current_view == V_FAVS) {
                movies.erase(movies.begin() + selected_index);
                selected_index = -1;
                play_btn->deactivate();
                fav_btn->deactivate();
                rebuild_grid();
                if (movies.empty()) set_status("No quedan favoritos.");
            } else {
                fav_btn->label("Favoritos");
                fav_btn->redraw();
            }
        } else {
            lib.add_favorite(m);
            set_status("Guardado en favoritos: " + m.title);
            fav_btn->label("Quitar de favoritos");
            fav_btn->redraw();
            update_fav_count();
        }
    }

    void show_favorites() {
        auto favs = Library::instance().favorites;
        if (favs.empty()) { set_status("No hay favoritos guardados."); return; }
        save_state();
        current_view = V_FAVS;
        clear_cells();
        movies = favs;
        selected_index = -1;
        play_btn->deactivate();
        fav_btn->deactivate();
        rebuild_grid();
        set_status(std::to_string(movies.size()) + " favoritos");
    }

    static void poll_search_result_cb(void* data) {
        auto* self = (PalomitasGUI*)data;
        self->poll_search_result();
        if (self->window && self->window->visible()) {
            Fl::repeat_timeout(0.1, poll_search_result_cb, self);
        }
    }

    void poll_search_result() {
        SearchResult r;
        {
            std::lock_guard<std::mutex> lk(result_mutex);
            if (!result_ready) return;
            r = pending_result;
            result_ready = false;
        }

        searching = false;

        if (!r.ok) {
            std::string msg = "search.sh no genero buscar.json.";
            if (!r.log.empty()) {
                size_t p = 0;
                while (p < r.log.size() && (r.log[p] == '\n' || r.log[p] == '\r')) ++p;
                size_t e = r.log.find('\n', p);
                msg += " " + r.log.substr(p, e == std::string::npos ? 100 : e - p);
            }
            stop_busy(msg);
            return;
        }

        movies = parse_search_json(r.json);
        if (movies.empty()) {
            stop_busy("JSON sin titulos utiles (" + std::to_string(r.json.size()) + " bytes).");
            return;
        }

        clear_cells();
        selected_index = -1;
        play_btn->deactivate();
        fav_btn->deactivate();
        rebuild_grid();
        scroll->scroll_to(0, 0);
        scroll->redraw();
        scroll->damage(FL_DAMAGE_ALL);
        window->redraw();
        window->damage(FL_DAMAGE_ALL);
        stop_busy(std::to_string(movies.size()) + " resultados para: " + r.query);
    }

    void do_search();
    void do_play();
    void do_clean();
    void play_stream_mplayer(const std::string& stream_url, const std::string& player_url, const std::string& title);
    void download_posters_parallel(const std::vector<size_t>& pending);
    void download_posters_sync(const std::vector<size_t>& pending);
};

void PalomitasGUI::download_posters_sync(const std::vector<size_t>& pending) {
    if (pending.empty()) return;

    std::system("mkdir -p /tmp/TEMP_Palomitas");

    const int GROUP = 4;
    for (size_t g = 0; g < pending.size(); g += GROUP) {
        size_t end = std::min(pending.size(), g + GROUP);
        std::vector<std::thread> workers;

        for (size_t k = g; k < end; ++k) {
            size_t idx = pending[k];
            workers.emplace_back([this, idx]() {
                const MovieItem& m = movies[idx];
                std::string out = "/tmp/TEMP_Palomitas/poster_" + m.id + ".img";

                struct stat st;
                if (stat(out.c_str(), &st) == 0 && st.st_size > 1000) return;

                if (!m.img.empty()) {
                    std::string cmd = "wget -q --no-check-certificate --timeout=8 --tries=1 -O "
                        + shell_quote(out) + " " + shell_quote(m.img) + " 2>/dev/null";
                    std::system(cmd.c_str());
                }
                if (stat(out.c_str(), &st) != 0 || st.st_size <= 1000) {
                    std::remove(out.c_str());
                }
            });
        }
        for (auto& t : workers) t.join();
    }
}

void PalomitasGUI::download_posters_parallel(const std::vector<size_t>& pending) {
    download_posters_sync(pending);
}

void PalomitasGUI::rebuild_grid_positions_only() {
    if (cells.empty() || movies.empty()) return;

    int usable_w = scroll->w() - 2 * PADDING;
    int cols = usable_w / (CELL_W + GAP);
    if (cols < 2) cols = 2;
    int grid_w = cols * CELL_W + (cols - 1) * GAP;
    int offset_x = scroll->x() + (scroll->w() - grid_w) / 2;
    int base_y = scroll->y() + PADDING;

    for (size_t i = 0; i < cells.size(); ++i) {
        int col = (int)i % cols, row = (int)i / cols;
        int cx = offset_x + col * (CELL_W + GAP);
        int cy = base_y + row * (CELL_H + GAP);
        cells[i]->resize(cx, cy, CELL_W, CELL_H);
        cells[i]->redraw();
    }

    int rows_count = ((int)movies.size() + cols - 1) / cols;
    int total_h = PADDING * 2 + rows_count * CELL_H + (rows_count - 1) * GAP;
    scroll_spacer->resize(scroll->x(), scroll->y(), scroll->w() - 2, total_h);
    scroll->redraw();
    scroll->damage(FL_DAMAGE_ALL);
}

void PalomitasGUI::rebuild_grid() {
    clear_cells();
    std::vector<size_t> pending;
    for (size_t i = 0; i < movies.size(); ++i) {
        std::string path = "/tmp/TEMP_Palomitas/poster_" + movies[i].id + ".img";
        struct stat st;
        if (stat(path.c_str(), &st) == 0 && st.st_size > 20000) continue;
        if (stat(path.c_str(), &st) == 0) std::remove(path.c_str());
        pending.push_back(i);
    }
    download_posters_sync(pending);

    if (movies.empty()) {
        scroll_spacer->resize(scroll->x(), scroll->y(), scroll->w() - 2, 10);
        if (bg_image) { bg_image->show(); bg_image->redraw(); }
        scroll->redraw();
        return;
    }
    if (bg_image) bg_image->hide();

    int usable_w = scroll->w() - 2 * PADDING;
    int cols = usable_w / (CELL_W + GAP);
    if (cols < 2) cols = 2;
    int grid_w = cols * CELL_W + (cols - 1) * GAP;
    int offset_x = scroll->x() + (scroll->w() - grid_w) / 2;
    int base_y = scroll->y() + PADDING;
    for (size_t i = 0; i < movies.size(); ++i) {
        int col = (int)i % cols, row = (int)i / cols;
        int cx = offset_x + col * (CELL_W + GAP);
        int cy = base_y + row * (CELL_H + GAP);
        auto* cell = new MovieCell(cx, cy, CELL_W, CELL_H, &movies[i], (int)i);
        cell->callback(cb_cell_clicked, this);
        cell->when(FL_WHEN_RELEASE);
        scroll->add(cell);
        cells.push_back(cell);
    }
    int rows_count = ((int)movies.size() + cols - 1) / cols;
    int total_h = PADDING * 2 + rows_count * CELL_H + (rows_count - 1) * GAP;
    scroll_spacer->resize(scroll->x(), scroll->y(), scroll->w() - 2, total_h);
    scroll->add(scroll_spacer);
    scroll->scroll_to(0, 0);

    scroll->redraw();
    scroll->damage(FL_DAMAGE_ALL);
    window->redraw();
    window->damage(FL_DAMAGE_ALL);
    Fl::flush();
}

void PalomitasGUI::do_search() {
    if (searching) { set_status("Busqueda en curso..."); return; }
    std::string q = search_input->value() ? search_input->value() : "";
    if (q.empty()) { set_status("Escribe un termino de busqueda."); return; }

    struct stat st;
    if (stat(cli_dir.c_str(), &st) != 0) {
        set_status("No existe el directorio: " + cli_dir);
        return;
    }
    if (stat((cli_dir + "/search.sh").c_str(), &st) != 0) {
        set_status("No existe: " + cli_dir + "/search.sh");
        return;
    }

    if (current_view != V_SEARCH) save_state();
    current_view = V_SEARCH;
    searching = true;
    start_busy("Buscando: " + q);

    std::string qq = q;
    for (auto& c : qq) if (c == '\'' || c == '"' || c == '\\' || c == '`') c = ' ';
    std::string cli = cli_dir;

    {
        std::lock_guard<std::mutex> lk(result_mutex);
        result_ready = false;
        pending_result = SearchResult{};
    }

    std::thread([this, qq, cli]() {
        std::system("mkdir -p /tmp/TEMP_Palomitas");
        std::system("rm -f /tmp/TEMP_Palomitas/buscar.json");

        std::string cmd = "cd " + shell_quote(cli) + " && bash search.sh " + shell_quote(qq) + " 2>&1";
        std::string log = run_cmd_timeout(cmd, 120);

        {
            std::ofstream lf("/tmp/TEMP_Palomitas/search_gui.log");
            lf << "CMD: " << cmd << "\n---\n" << log << "\n";
        }

        bool ok = file_exists("/tmp/TEMP_Palomitas/buscar.json");
        std::string json = ok ? read_file("/tmp/TEMP_Palomitas/buscar.json") : "";

        {
            std::lock_guard<std::mutex> lk(result_mutex);
            pending_result.json = std::move(json);
            pending_result.log = std::move(log);
            pending_result.query = qq;
            pending_result.ok = ok;
            result_ready = true;
        }
    }).detach();
}

void PalomitasGUI::play_stream_mplayer(const std::string& stream_url,
                                        const std::string& player_url,
                                        const std::string& title) {
    if (stream_url.empty()) { set_status("URL de stream vacia."); return; }
    std::string referrer;
    std::regex re("^(https?://[^/]+)");
    std::smatch m;
    if (std::regex_search(player_url, m, re)) referrer = m[1].str();
    if (referrer.empty()) { set_status("No se pudo extraer el referrer."); return; }

    start_busy("Cargando stream: " + title);

    std::thread([this, stream_url, referrer, title]() {
        std::string which = run_cmd_timeout("command -v yt-dlp 2>/dev/null", 10);
        if (which.empty()) {
            Fl::add_timeout(0.05, [](void* d) {
                ((PalomitasGUI*)d)->stop_busy("yt-dlp no encontrado.");
            }, this);
            return;
        }

        auto esc_dq = [](std::string s) {
            std::string o;
            for (char c : s) {
                if (c == '"' || c == '\\' || c == '$' || c == '`') o += '\\';
                o += c;
            }
            return o;
        };

        std::string safe_title = title;
        for (auto& c : safe_title) if (c == '"' || c == '\\') c = ' ';

        std::string cmd =
            "yt-dlp --add-header \"Referer: " + esc_dq(referrer) + "\""
            " -f \"best[height<=480]\""
            " --no-part --no-cache-dir --buffer-size 1M"
            " -o - \"" + esc_dq(stream_url) + "\""
            " 2>/tmp/TEMP_Palomitas/ytdlp.log"
            " | mplayer -zoom -cache 4096 -demuxer lavf"
            " -title \"" + safe_title + "\""
            " -"
            " >/tmp/TEMP_Palomitas/mplayer.log 2>&1";
        std::system(cmd.c_str());

        Fl::add_timeout(0.05, [](void* d) {
            ((PalomitasGUI*)d)->stop_busy("Reproducción finalizada.");
        }, this);
    }).detach();
}

void PalomitasGUI::do_play() {
    if (selected_index < 0 || selected_index >= (int)movies.size()) {
        set_status("Selecciona una pelicula primero.");
        return;
    }

    MovieItem m = movies[selected_index];
    std::string title = m.title;
    std::string movie_url = m.url;
    std::string cli = cli_dir;

    start_busy("Extrayendo iframe: " + title);

    std::string* title_ptr = new std::string(title);

    std::thread([this, movie_url, title_ptr, cli, title]() {
        std::string cmd1 = "cd " + shell_quote(cli) + " && bash iframe.sh " + shell_quote(movie_url) + " 2>&1";
        run_cmd_timeout(cmd1, 120);
        std::string peli = read_file("/tmp/TEMP_Palomitas/peli.json");
        std::string player_url = json_get_str(peli, "embed_url");
        if (player_url.empty()) player_url = json_get_str(peli, "url");

        if (player_url.empty()) {
            Fl::add_timeout(0.05, [](void* d) {
                ((PalomitasGUI*)d)->stop_busy("No se pudo obtener el player URL.");
            }, this);
            delete title_ptr;
            return;
        }

        Fl::add_timeout(0.05, [](void* d) {
            ((PalomitasGUI*)d)->start_busy("Extrayendo stream...");
        }, this);
        delete title_ptr;

        std::string cmd2 = "cd " + shell_quote(cli) + " && bash embed.sh " + shell_quote(player_url) + " " + shell_quote(movie_url) + " 2>&1";
        run_cmd_timeout(cmd2, 120);
        std::string output = read_file("/tmp/TEMP_Palomitas/output.json");
        std::string stream = json_get_str(output, "video");

        if (stream.empty() || stream == "null") {
            Fl::add_timeout(0.05, [](void* d) {
                ((PalomitasGUI*)d)->stop_busy("No se pudo extraer el stream.");
            }, this);
            return;
        }

        struct PlayData {
            PalomitasGUI* gui;
            std::string stream;
            std::string player_url;
            std::string title;
        };
        PlayData* pd = new PlayData{ this, stream, player_url, title };

        Fl::add_timeout(0.05, [](void* data) {
            PlayData* p = (PlayData*)data;
            p->gui->play_stream_mplayer(p->stream, p->player_url, p->title);
            delete p;
        }, pd);

    }).detach();
}

void PalomitasGUI::do_clean() {
    clear_cells();
    movies.clear();
    selected_index = -1;
    current_view = V_HOME;
    play_btn->deactivate();
    fav_btn->deactivate();
    scroll_spacer->resize(scroll->x(), scroll->y(), scroll->w() - 2, 10);
    if (bg_image) { bg_image->show(); bg_image->redraw(); }
    scroll->redraw();
    window->redraw();
    set_status("Limpiando archivos...");

    std::thread([this]() {
        std::system("rm -f /tmp/TEMP_Palomitas/* 2>/dev/null");
        Fl::add_timeout(0.05, [](void* data) {
            ((PalomitasGUI*)data)->set_status("Todo limpio.");
        }, this);
    }).detach();
}

int main(int argc, char** argv) {
    {
        char cwd[PATH_MAX] = {0};
        getcwd(cwd, sizeof cwd);
        if (std::system("command -v jq >/dev/null 2>&1") != 0)
            fprintf(stderr, "[Palomitas] AVISO: jq no esta en PATH\n");
        if (std::system("command -v wget >/dev/null 2>&1") != 0)
            fprintf(stderr, "[Palomitas] AVISO: wget no esta en PATH\n");
        fprintf(stderr, "[Palomitas] cwd: %s\n", cwd);
        if (const char* env = std::getenv("PALOMITAS_HOME"))
            fprintf(stderr, "[Palomitas] PALOMITAS_HOME=%s\n", env);
    }

    const int win_w = 900;
    const int win_h = 600;
    g_theme_index = load_saved_theme();
    Library::instance().load();
    PalomitasGUI gui(win_w, win_h, "Palomitas FLTK");
    gui.window->size_range(700, 500);
    gui.show();
    apply_theme(g_theme_index);
    gui.apply_colors();
    gui.layout(win_w, win_h);
    Fl::add_timeout(0.1, [](void* g) {
        auto* gui = (PalomitasGUI*)g;
        gui->layout(gui->window->w(), gui->window->h());
    }, &gui);
    return Fl::run();
}
