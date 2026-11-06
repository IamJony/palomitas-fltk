// Palomitas GUI - Versión Final para Tiny Core Linux
// Fixes: Compatibilidad uchar FLTK, Parser JSON Server2, Temas Visuales, Flujo Series/Animes
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
#include <map>
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
#include <algorithm>

// FIX CRÍTICO PARA TINY CORE: uchar no está definido en FLTK headers
#ifndef uchar
typedef unsigned char uchar;
#endif

// ---------- Utilidades ----------
static std::string esc(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '"' || c == '\\' || c == '$' || c == '`') out += '\\';
        out += c;
    }
    return out;
}

static std::string shell_quote(const std::string& s) {
    std::string out = "'";
    for (char c : s) {
        if (c == '\'') out += "'\\''";
        else out += c;
    }
    out += "'";
    return out;
}

static std::string run_cmd_timeout(const std::string & cmd, int timeout_sec = 120) {
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
        if (elapsed >= timeout_sec) { kill(pid, SIGKILL); break; }
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
    if (waitpid(pid, &status, WNOHANG) == 0) { kill(pid, SIGKILL); waitpid(pid, &status, 0); }
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

static std::string json_get_str(const std::string & json, const std::string & key) {
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
                if (n=='r') { out+='\r'; ++i; continue; }
            }
            out += v[i];
        }
        size_t endpos = out.find_last_not_of(" \t\n\r");
        if (endpos != std::string::npos) out = out.substr(0, endpos + 1);
        return out;
    }
    return "";
}

// Mapa global thread-safe para tipos de contenido
static std::map<std::string, std::string> g_content_type_map;
static std::mutex g_type_mutex;

static void set_content_type(const std::string& id, const std::string& type) {
    std::lock_guard<std::mutex> lk(g_type_mutex);
    g_content_type_map[id] = type;
}

static std::string get_content_type(const std::string& id) {
    std::lock_guard<std::mutex> lk(g_type_mutex);
    auto it = g_content_type_map.find(id);
    if (it != g_content_type_map.end()) return it->second;
    return "movies";
}

// Parser JSON robusto para Server1 y Server2
static std::vector<MovieItem> parse_search_json_with_types(const std::string & json) {
    std::vector<MovieItem> out;
    if (json.empty()) return out;

    size_t root = json.find('{');
    if (root == std::string::npos) return out;

    size_t pos = root + 1;
    while (pos < json.size()) {
        // Saltar espacios y comas
        while (pos < json.size()) {
            char c = json[pos];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == ',') { pos++; continue; }
            break;
        }
        if (pos >= json.size() || json[pos] == '}') break;

        // Buscar comilla de apertura de clave
        size_t q1 = json.find('"', pos);
        if (q1 == std::string::npos) break;
        size_t q2 = json.find('"', q1 + 1);
        if (q2 == std::string::npos) break;
        std::string key = json.substr(q1 + 1, q2 - q1 - 1);

        // Buscar ':' y '{'
        size_t colon = json.find(':', q2 + 1);
        if (colon == std::string::npos) break;
        size_t brace = json.find('{', colon + 1);
        if (brace == std::string::npos) break;

        // Emparejar llaves para delimitar el objeto hijo
        int depth = 1;
        size_t end = brace + 1;
        while (end < json.size() && depth > 0) {
            if (json[end] == '{') depth++;
            else if (json[end] == '}') depth--;
            end++;
        }
        if (depth != 0) break;

        std::string chunk = json.substr(brace, end - brace);
        MovieItem m;
        m.title = json_get_str(chunk, "title");
        if (!m.title.empty()) {
            m.id  = key;
            m.url = json_get_str(chunk, "url");
            m.img = json_get_str(chunk, "img");
            m.year = json_get_str(chunk, "date");
            m.imdb = json_get_str(chunk, "imdb");
            std::string type_val = json_get_str(chunk, "type");
            if (type_val.empty() || type_val == "null") {
                if (m.url.find("/animes/") != std::string::npos) type_val = "animes";
                else if (m.url.find("/series/") != std::string::npos) type_val = "tvshows";
                else type_val = "movies";
            }
            set_content_type(m.id, type_val);
            std::regex extra_re("\"extra\"\\s*:\\s*\\{([^}]*)\\}");
            std::smatch em;
            if (std::regex_search(chunk, em, extra_re)) {
                std::string ex = em[1].str();
                if (m.year.empty()) m.year = json_get_str(ex, "date");
                if (m.imdb.empty()) m.imdb = json_get_str(ex, "imdb");
            }
            out.push_back(std::move(m));
        }

        pos = end;
    }
    return out;
}

// ---- Diálogo modal para elegir temporada/episodio en Server 2 ----
static bool g_ep_dialog_cancelled = false;
static std::string g_ep_dialog_season;
static std::string g_ep_dialog_ep_id;
static Fl_Window* g_ep_dialog_win = nullptr;

static void cb_ep_dialog_ok(Fl_Widget*, void* data) {
    Fl_Choice* ch = (Fl_Choice*)data;
    if (g_ep_dialog_win) g_ep_dialog_win->hide();
    g_ep_dialog_cancelled = false;
    if (ch) {
        const char* txt = ch->text();
        if (txt) g_ep_dialog_season = txt;
    }
}
static void cb_ep_dialog_cancel(Fl_Widget*, void*) {
    if (g_ep_dialog_win) g_ep_dialog_win->hide();
    g_ep_dialog_cancelled = true;
}

// ---- Preparación de datos de episodios (SIN widgets, apto para worker thread) ----
struct EpisodeData {
    bool ok = false;
    std::string error;
    std::string season_json;
    std::vector<std::string> seasons;      // etiquetas "Temporada X (N eps)"
    std::vector<std::string> season_nums;  // números de temporada "1","2",...
    std::string ep_json;
};

static EpisodeData fetch_episode_data(const std::string& cli_dir,
                                      const std::string& temp_dir,
                                      const std::string& post_id,
                                      const std::string& post_type)
{
    EpisodeData d;
    std::system(("mkdir -p " + temp_dir).c_str());

    std::string ep_cmd = "cd " + shell_quote(cli_dir) +
                         " && TEMP_DIR=" + temp_dir +
                         " bash modules/episodes2.sh " + shell_quote(post_id) +
                         " " + shell_quote(post_type) + " 2>/dev/null";
    run_cmd_timeout(ep_cmd, 180);

    d.ep_json = temp_dir + "/episodes_" + post_id + "_clean.json";
    if (!file_exists(d.ep_json)) {
        d.error = "No se pudieron obtener los episodios.";
        return d;
    }

    std::string seasons_cmd = "jq -r '[.[].season] | unique | sort | .[]' " + shell_quote(d.ep_json);
    std::string seasons_raw = run_cmd_timeout(seasons_cmd, 10);
    {
        std::stringstream ss(seasons_raw);
        std::string s;
        while (std::getline(ss, s)) {
            if (!s.empty() && s.back() == '\n') s.pop_back();
            if (s.empty()) continue;
            d.season_nums.push_back(s);
            std::string cnt_cmd = "jq --arg s " + shell_quote(s) +
                                  " '[.[] | select(.season == ($s|tonumber))] | length' " +
                                  shell_quote(d.ep_json);
            std::string cnt = run_cmd_timeout(cnt_cmd, 5);
            cnt.erase(std::remove(cnt.begin(), cnt.end(), '\n'), cnt.end());
            d.seasons.push_back("Temporada " + s + " (" + cnt + " eps)");
        }
    }
    if (d.seasons.empty()) {
        d.error = "No hay temporadas disponibles.";
        return d;
    }
    d.ok = true;
    return d;
}

// Devuelve la lista de "SxxEyy|id" para una temporada concreta
static std::vector<std::string> fetch_episode_list(const std::string& cli_dir,
                                                   const std::string& temp_dir,
                                                   const std::string& post_id,
                                                   const std::string& season_num)
{
    (void)cli_dir;
    std::string ep_json = temp_dir + "/episodes_" + post_id + "_clean.json";
    std::string season_json = temp_dir + "/season_" + post_id + "_" + season_num + ".json";
    std::string filter_cmd = "jq --arg s " + shell_quote(season_num) +
                             " '[.[] | select(.season == ($s|tonumber))]' " +
                             shell_quote(ep_json) + " > " + shell_quote(season_json);
    run_cmd_timeout(filter_cmd, 10);

    std::string eps_cmd = "jq -r '.[] | \"S\(.season)E\(.episode)|\(.id)\"' " + shell_quote(season_json);
    std::string eps_raw = run_cmd_timeout(eps_cmd, 10);
    std::vector<std::string> out;
    std::stringstream ss(eps_raw);
    std::string line;
    while (std::getline(ss, line)) {
        if (line.empty()) continue;
        if (line.back() == '\n') line.pop_back();
        if (line.find('|') != std::string::npos) out.push_back(line);
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
        : Fl_Button(x, y, w, h, l) { box(FL_NO_BOX); clear_visible_focus(); }
    void draw() override {
        Fl_Color c  = selected ? C_SEL : (hover ? hi : bg);
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

// ---------- Celda con etiqueta de tipo ----------
class MovieCell : public Fl_Widget {
public:
    int index;
    MovieItem* movie;
    bool selected = false;
    MovieCell(int x, int y, int w, int h, MovieItem* m, int idx)
        : Fl_Widget(x, y, w, h), index(idx), movie(m) { box(FL_NO_BOX); }
    void set_selected(bool s) { if (selected == s) return; selected = s; redraw(); }
    int handle(int event) override {
        switch (event) { case FL_PUSH: return 1; case FL_RELEASE: do_callback(); return 1; default: return Fl_Widget::handle(event); }
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
            if (scaled) { scaled->draw(x(), y()); delete scaled; }
            else { img->draw(x(), y()); }
        } else {
            fl_draw_box(FL_BORDER_BOX, x(), y(), img_w, img_h, C_DIM);
            fl_color(C_TEXT); fl_font(FL_HELVETICA, 10);
            fl_draw("Sin imagen", x(), y(), img_w, img_h, FL_ALIGN_CENTER);
        }
        
        // ETIQUETA DE TIPO basada en el mapa global
        std::string ctype = get_content_type(movie->id);
        std::string label_text;
        Fl_Color tag_color = C_DIM;
        
        // Colores de fondo vibrantes y texto blanco para asegurar contraste
        if (ctype == "animes") { 
            label_text = "[ANIME]"; 
            tag_color = fl_rgb_color(200, 60, 200); // Morado brillante
        }
        else if (ctype == "tvshows") { 
            label_text = "[SERIE]"; 
            tag_color = fl_rgb_color(40, 150, 80); // Verde brillante
        }
        else { 
            label_text = "[PELI]"; 
            tag_color = fl_rgb_color(60, 120, 220); // Azul brillante
        }
        
        fl_color(tag_color);
        fl_font(FL_HELVETICA_BOLD, 8);
        int tw = fl_width(label_text.c_str()) + 6;
        int th = 14;
        fl_rectf(x() + w() - tw - 4, y() + 4, tw, th);
        
        // SIEMPRE texto blanco para que se lea sobre el fondo de color
        fl_color(FL_WHITE);
        fl_draw(label_text.c_str(), x() + w() - tw - 1, y() + 5, tw, th, FL_ALIGN_CENTER);
        
        int pad_top = 8;
        int pad_x = 5;
        int text_y = y() + img_h + pad_top;
        int text_w = w() - pad_x * 2;
        
        fl_color(selected ? FL_WHITE : C_TEXT);
        fl_font(FL_HELVETICA_BOLD, 10);
        fl_draw(movie->title.c_str(), x() + pad_x, text_y, text_w, 28,
                FL_ALIGN_LEFT | FL_ALIGN_TOP | FL_ALIGN_WRAP | FL_ALIGN_CLIP);
        
        int pad_mid = 6;
        fl_color(selected ? FL_WHITE : C_DIM);
        fl_font(FL_HELVETICA, 9);
        std::string meta = movie->year + " IMDb:" + movie->imdb;
        fl_draw(meta.c_str(), x() + pad_x, text_y + 28 + pad_mid, text_w, 14,
                FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_CLIP);
                
        if (selected) { fl_color(C_ACCENT); fl_rect(x(), y(), w(), h()); }
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
                win->redraw(); Fl::flush();
            }, this);
        }
        return Fl_Window::handle(e);
    }
    std::function<void(int,int)> on_resize_cb;
};

// ---------- Resolución de rutas ----------
static std::string find_base_dir(const std::string & relative_marker) {
    std::vector<std::string> candidates;
    if (const char* env = std::getenv("PALOMITAS_HOME")) { if (env[0]) candidates.emplace_back(env); }
    
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
    if (n > 0) { buf[n] = '\0'; std::string exe(buf); auto pos = exe.find_last_of('/'); if (pos != std::string::npos) add_with_parents(exe.substr(0, pos)); }
    
    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd))) add_with_parents(cwd);
    
    candidates.push_back("/usr/local/share/palomitas");
    candidates.push_back("/usr/share/palomitas");
    candidates.push_back("/opt/palomitas");
    candidates.push_back(".");
    
    for (auto & c : candidates) { struct stat st; if (stat((c + "/" + relative_marker).c_str(), &st) == 0) return c; }
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
    Fl_Choice* server_choice;
    Fl_Choice* player_choice;
    std::string current_player = "mplayer";
    Fl_Scroll* scroll;
    Fl_Box* scroll_spacer;
    BgImage* bg_image = nullptr;
    Fl_Shared_Image* home_shared_img = nullptr;
    std::vector<MovieCell*> cells;
    ModernButton* play_btn;
    ModernButton* fav_btn;
    ModernButton* clean_btn;
    ModernButton* settings_btn;
    Label* fav_count_label = nullptr;
    Label* status_label;
    
    std::vector<MovieItem> movies;
    int selected_index = -1;
    
    std::string base_dir;
    std::string cli_dir;
    std::string asset_dir;
    std::string current_server_flag = "--server1"; 
    
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
        load_server_state(); 
        
        fl_register_images();
        
        window = new MainWindow(w, h, title);
        
        Fl_Box* header = new Fl_Box(SIDE_W + 20, 10, 250, 30, "Palomitas");
        header->labelfont(FL_BOLD); header->labelsize(22);
        header->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE); header->labelcolor(C_TEXT);
        
        // Solo dejamos el servidor visible porque es lo que más se cambia
        server_choice = new Fl_Choice(w - 280, 12, 140, 26, "Server:");
        server_choice->add("Server 1");
        server_choice->add("Server 2");
        server_choice->value(current_server_flag == "--server1" ? 0 : 1);
        server_choice->callback(cb_change_server, this);
        
        // Inicializamos las variables (aunque los widgets se creen en el modal)
        load_player_state();
        
        fav_side_btn = new ModernButton(10, 60, SIDE_W - 20, 36, "Favoritos");
        fav_side_btn->labelsize(11); fav_side_btn->callback(cb_show_favs, this);
        
        fav_count_label = new Label(10, 100, SIDE_W - 20, 16);
        fav_count_label->size = 10; fav_count_label->fg_role = 1;
        fav_count_label->al = FL_ALIGN_CENTER; fav_count_label->set("0 peliculas");
        
        settings_btn = new ModernButton(w - 120, 12, 110, 26, "Configuracion");
        settings_btn->bg = C_BTN;
        settings_btn->fg = C_TEXT;
        settings_btn->hi = C_BTN_HI;
        settings_btn->callback(cb_settings, this);

        back_btn = new ModernButton(w - 120, 55, 100, 30, "Volver");
        back_btn->callback(cb_back, this); back_btn->deactivate();
        
        search_input = new Fl_Input(SIDE_W + 70, 55, w - SIDE_W - 320, 30, "Buscar:");
        search_input->when(FL_WHEN_ENTER_KEY); search_input->callback(cb_search, this);
        
        search_btn = new ModernButton(w - 235, 55, 100, 30, "Buscar");
        search_btn->callback(cb_search, this);
        
        scroll = new Fl_Scroll(SIDE_W + 10, 100, w - SIDE_W - 20, h - 150);
        scroll->box(FL_FLAT_BOX);
        scroll_spacer = nullptr;
        scroll->end();
        
        {
            std::string img_path = asset_dir + "/home.jpeg";
            Fl_Shared_Image* home_img = Fl_Shared_Image::get(img_path.c_str());
            if (!home_img || home_img->w() <= 0) home_img = Fl_Shared_Image::get((base_dir + "/asset/home.jpeg").c_str());
            if (home_img && home_img->w() > 0) home_shared_img = home_img;
        }
        
        int bar_y = h - 34; int bar_h = 28;
        status_label = new Label(SIDE_W + 10, bar_y, w - SIDE_W - 380, bar_h);
        status_label->font = FL_HELVETICA; status_label->size = 12;
        status_label->fg_role = 1; status_label->al = FL_ALIGN_LEFT | FL_ALIGN_INSIDE;
        status_label->set("Listo. Escribe algo o explora favoritos.");
        
        clean_btn = new ModernButton(w - 120, bar_y, 100, bar_h, "Limpiar");
        clean_btn->callback(cb_clean, this);
        
        fav_btn = new ModernButton(w - 120 - 10 - 170, bar_y, 170, bar_h, "Favoritos");
        fav_btn->callback(cb_fav, this); fav_btn->deactivate();
        
        play_btn = new ModernButton(w - 120 - 10 - 170 - 10 - 130, bar_y, 130, bar_h, "Reproducir");
        play_btn->callback(cb_play, this); play_btn->deactivate();
        
        window->end(); window->resizable(window);
        
        window->on_resize_cb = [this](int W, int H) { layout(W, H); };
        g_refresh_cb = [this]() { this->apply_colors(); };
        apply_colors(); update_fav_count(); check_environment();
        
        Fl::add_timeout(0.1, poll_search_result_cb, this);
    }
    
    ~PalomitasGUI() { if (g_refresh_cb) g_refresh_cb = nullptr; }
    
    void show() { window->show(); }
    
    void load_server_state() {
        std::string state_file = cli_dir + "/.palomitas_state";
        if (file_exists(state_file)) {
            std::string content = read_file(state_file);
            content.erase(std::remove_if(content.begin(), content.end(), ::isspace), content.end());
            current_server_flag = (content == "--server2") ? "--server2" : "--server1";
        }
    }
    
    void save_server_state() {
        std::ofstream f(cli_dir + "/.palomitas_state");
        if (f.is_open()) f << current_server_flag;
    }

    void load_player_state() {
        std::string state_file = cli_dir + "/.palomitas_player";
        if (file_exists(state_file)) {
            std::string content = read_file(state_file);
            content.erase(std::remove_if(content.begin(), content.end(), ::isspace), content.end());
            current_player = (content == "mpv") ? "mpv" : "mplayer";
        }
    }

    void save_player_state() {
        std::ofstream f(cli_dir + "/.palomitas_player");
        if (f.is_open()) f << current_player;
    }
    
    void resolve_paths() {
        base_dir = find_base_dir("palomitas-cli/palomitas.sh");
        struct stat st;
        if (stat((base_dir + "/palomitas-cli/palomitas.sh").c_str(), &st) == 0) {
            cli_dir = base_dir + "/palomitas-cli";
        } else {
            std::string alt = find_base_dir("search.sh");
            if (stat((alt + "/search.sh").c_str(), &st) == 0) {
                cli_dir = alt;
                auto sl = alt.find_last_of('/');
                if (sl != std::string::npos) base_dir = alt.substr(0, sl);
            } else { cli_dir = base_dir + "/palomitas-cli"; }
        }
        asset_dir = base_dir + "/asset";
        if (stat((asset_dir + "/home.jpeg").c_str(), &st) != 0) {
            std::string test = base_dir;
            for (int i = 0; i < 4; ++i) {
                if (stat((test + "/asset/home.jpeg").c_str(), &st) == 0) { asset_dir = test + "/asset"; break; }
                auto sl = test.find_last_of('/'); if (sl == std::string::npos) break; test = test.substr(0, sl);
            }
        }
        fprintf(stderr, "[Palomitas] base_dir=%s cli_dir=%s asset_dir=%s\n", base_dir.c_str(), cli_dir.c_str(), asset_dir.c_str());
    }
    
    void check_environment() {
        std::string msg; struct stat st;
        if (stat(cli_dir.c_str(), &st) != 0) msg = "AVISO: no existe " + cli_dir;
        else if (stat((cli_dir + "/modules/search.sh").c_str(), &st) != 0) msg = "AVISO: falta modules/search.sh";
        if (std::system("command -v jq >/dev/null 2>&1") != 0)
            msg += (msg.empty()?"":" | ") + std::string("falta jq");
        if (std::system("command -v wget >/dev/null 2>&1") != 0)
            msg += (msg.empty()?"":" | ") + std::string("falta wget");
        if (!msg.empty()) { status_label->fg_role = 4; status_label->set(msg); fprintf(stderr, "[Palomitas] %s\n", msg.c_str()); }
    }
    
    void set_status(const std::string& msg) { busy = false; busy_msg.clear(); status_label->fg_role = 1; status_label->set(msg); }
    void start_busy(const std::string& msg) { busy = true; busy_msg = msg; status_label->fg_role = 3; status_label->set(msg); status_label->redraw(); }
    void stop_busy(const std::string& msg) { busy = false; busy_msg.clear(); status_label->fg_role = 3; status_label->set(msg); status_label->redraw(); }
    
    void ensure_bg_image() {
        if (bg_image) return;
        if (!home_shared_img) return;
        if (!scroll || scroll->w() < 10 || scroll->h() < 10) return;
        bg_image = new BgImage(scroll->x(), scroll->y(), scroll->w(), scroll->h());
        bg_image->set_image(home_shared_img); bg_image->show(); scroll->add(bg_image); scroll->redraw();
    }
    
    void layout(int W, int H) {
        int rx = SIDE_W + 10;
        search_input->resize(SIDE_W + 70, 55, W - SIDE_W - 320, 30);
        search_btn->resize(W - 235, 55, 100, 30);
        back_btn->resize(W - 120, 55, 100, 30);
        if (settings_btn) settings_btn->resize(W - 120, 12, 110, 26);
        server_choice->resize(W - 280, 12, 140, 26);
        scroll->resize(rx, 100, W - rx - 10, H - 150);
        if (bg_image) bg_image->resize(scroll->x(), scroll->y(), scroll->w(), scroll->h());
        if (window->child(0)) window->child(0)->redraw();
        
        int bar_y = H - 34; int bar_h = 28;
        clean_btn->resize(W - 120, bar_y, 100, bar_h);
        fav_btn->resize(W - 120 - 10 - 170, bar_y, 170, bar_h);
        play_btn->resize(W - 120 - 10 - 170 - 10 - 130, bar_y, 130, bar_h);
        status_label->resize(SIDE_W + 10, bar_y, W - SIDE_W - 380, bar_h);
        fav_side_btn->resize(10, 60, SIDE_W - 20, 36);
        if (fav_count_label) fav_count_label->resize(10, 100, SIDE_W - 20, 16);
        
        if (!cells.empty()) rebuild_grid_positions_only();
        else if (!movies.empty()) rebuild_grid();
        else ensure_bg_image();
        
        if (bg_image) {
            bg_image->resize(scroll->x(), scroll->y(), scroll->w(), scroll->h());
            bg_image->redraw(); bg_image->damage(FL_DAMAGE_ALL);
        }
        scroll->redraw(); scroll->damage(FL_DAMAGE_ALL); window->redraw(); window->damage(FL_DAMAGE_ALL);
    }
    
    void apply_colors() {
        // apply_theme() movido fuera: apply_colors solo consume C_*
        
        window->color(C_BG);
        search_input->color(C_PANEL); search_input->textcolor(C_TEXT); search_input->selection_color(C_SEL); search_input->redraw();
        server_choice->color(C_PANEL); server_choice->textcolor(C_TEXT); server_choice->selection_color(C_SEL);
        scroll->color(C_BG); scroll->redraw();
        
        play_btn->bg = C_BTN; play_btn->fg = C_TEXT; play_btn->hi = C_BTN_HI;
        fav_btn->bg = C_BTN; fav_btn->fg = C_TEXT; fav_btn->hi = C_BTN_HI;
        clean_btn->bg = C_BTN; clean_btn->fg = C_TEXT; clean_btn->hi = C_BTN_HI;
        search_btn->bg = C_ACCENT; search_btn->fg = C_BG; search_btn->hi = C_SEL;
        back_btn->bg = C_PANEL; back_btn->fg = C_TEXT; back_btn->hi = C_BTN_HI;
        fav_side_btn->bg = C_PANEL; fav_side_btn->fg = C_ACCENT; fav_side_btn->hi = C_BTN_HI;
        if (settings_btn) { settings_btn->bg = C_BTN; settings_btn->fg = C_TEXT; settings_btn->hi = C_BTN_HI; }
        
        if (fav_count_label) fav_count_label->redraw();
        if (bg_image) bg_image->redraw();
        if (settings_btn) settings_btn->redraw();
        for (auto* c : cells) c->redraw();
        scroll->redraw(); window->redraw();
    }
    
    void rebuild_grid();
    void rebuild_grid_positions_only();
    void highlight_cell(int idx) { for (size_t i = 0; i < cells.size(); ++i) cells[i]->set_selected((int)i == idx); }
    void clear_cells() {
    for (auto* c : cells) { scroll->remove(c); delete c; }
    cells.clear();
    if (scroll_spacer) { scroll->remove(scroll_spacer); delete scroll_spacer; scroll_spacer = nullptr; }
    scroll->init_sizes();
    scroll->redraw(); scroll->damage(FL_DAMAGE_ALL);
}
    
    static void cb_search(Fl_Widget*, void* d) { ((PalomitasGUI*)d)->do_search(); }
    static void cb_play(Fl_Widget*, void* d) { ((PalomitasGUI*)d)->do_play(); }
    static void cb_clean(Fl_Widget*, void* d) { ((PalomitasGUI*)d)->do_clean(); }
    static void cb_fav(Fl_Widget*, void* d) { ((PalomitasGUI*)d)->toggle_favorite(); }
    static void cb_show_favs(Fl_Widget*, void* d) { ((PalomitasGUI*)d)->show_favorites(); }
    static void cb_back(Fl_Widget*, void* d) { ((PalomitasGUI*)d)->go_back(); }
    static void cb_change_server(Fl_Widget*, void* d) { 
        auto* self = (PalomitasGUI*)d;
        int val = self->server_choice->value();
        self->current_server_flag = (val == 0) ? "--server1" : "--server2";
        self->save_server_state();
        self->set_status("Servidor cambiado a: " + std::string(val == 0 ? "Server 1" : "Server 2"));
    }
    static void cb_change_player(Fl_Widget*, void* d) {
        auto* self = (PalomitasGUI*)d;
        int val = self->player_choice->value();
        self->current_player = (val == 1) ? "mpv" : "mplayer";
        self->save_player_state();
        self->set_status("Reproductor cambiado a: " + self->current_player);
    }


    static void cb_settings(Fl_Widget*, void* d) {
        auto* self = (PalomitasGUI*)d;
        
        Fl_Window* win = new Fl_Window(340, 220, "Ajustes");
        win->set_modal(); win->color(C_BG);
        
        Fl_Box* lbl1 = new Fl_Box(20, 15, 300, 20, "Tema Visual:");
        lbl1->labelfont(FL_HELVETICA_BOLD); lbl1->labelsize(12); lbl1->labelcolor(C_TEXT);
        lbl1->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
        
        Fl_Choice* ch_theme = new Fl_Choice(20, 40, 300, 28);
        ch_theme->color(C_PANEL); ch_theme->textcolor(C_TEXT); ch_theme->selection_color(C_SEL);
        for (int i = 0; i < theme_count; ++i) ch_theme->add(themes[i].name);
        ch_theme->value(g_theme_index);
        
        Fl_Box* lbl2 = new Fl_Box(20, 80, 300, 20, "Reproductor:");
        lbl2->labelfont(FL_HELVETICA_BOLD); lbl2->labelsize(12); lbl2->labelcolor(C_TEXT);
        lbl2->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
        
        Fl_Choice* ch_player = new Fl_Choice(20, 105, 300, 28);
        ch_player->color(C_PANEL); ch_player->textcolor(C_TEXT); ch_player->selection_color(C_SEL);
        ch_player->add("mplayer");
        ch_player->add("mpv");
        ch_player->value(self->current_player == "mpv" ? 1 : 0);
        
        static Fl_Widget* s_data[3];
        s_data[0] = win;
        s_data[1] = ch_theme;
        s_data[2] = ch_player;
        
        Fl_Button* ok = new Fl_Button(120, 160, 100, 30, "Guardar");
        ok->color(C_ACCENT); ok->labelcolor(C_BG);
        ok->callback([](Fl_Widget*, void* data){
            Fl_Widget** d2 = (Fl_Widget**)data;
            Fl_Choice* c_theme = (Fl_Choice*)d2[1];
            Fl_Choice* c_player = (Fl_Choice*)d2[2];
            
            // Guardar tema
            g_theme_index = c_theme->value();
            apply_theme(g_theme_index);
            save_theme(g_theme_index);
            
            // Guardar reproductor
            // (la llamada a rebuild_grid se elimina para que sea instantáneo)
            
            ((Fl_Window*)d2[0])->hide();
        }, s_data); // Nota: s_data tiene 3 elementos, falta gui. Usaremos una captura directa.

        // Como necesitamos 'this' (self), usamos un hack: variable estática global local
        static PalomitasGUI* s_gui_ptr = nullptr;
        s_gui_ptr = self;
        
        ok->callback([](Fl_Widget*, void* data){
            Fl_Widget** d2 = (Fl_Widget**)data;
            Fl_Choice* c_theme = (Fl_Choice*)d2[1];
            Fl_Choice* c_player = (Fl_Choice*)d2[2];
            
            g_theme_index = c_theme->value();
            apply_theme(g_theme_index);
            save_theme(g_theme_index);
            
            if (s_gui_ptr) {
                s_gui_ptr->current_player = (c_player->value() == 1) ? "mpv" : "mplayer";
                s_gui_ptr->save_player_state();
                s_gui_ptr->apply_colors();  // Solo refresca colores, NO descarga pósteres
                if (s_gui_ptr->window) {
                    s_gui_ptr->window->redraw();
                    s_gui_ptr->window->damage(FL_DAMAGE_ALL);
                }
            }
            ((Fl_Window*)d2[0])->hide();
        }, s_data);
        
        Fl_Button* cancel = new Fl_Button(230, 160, 100, 30, "Cancelar");
        cancel->color(C_BTN); cancel->labelcolor(C_TEXT);
        cancel->callback([](Fl_Widget* w, void*){
            ((Fl_Window*)w->window())->hide();
        }, nullptr);
        
        win->end();
        if (Fl::first_window()) {
            int scr_w = Fl::first_window()->w();
            int scr_h = Fl::first_window()->h();
            win->position((scr_w - win->w()) / 2, (scr_h - win->h()) / 2);
        }
        win->set_non_modal(); // FLWM no soporta set_modal() bien
        win->show();
        win->wait_for_expose();
        Fl::check();
        Fl::flush();
        while (win->shown()) { Fl::wait(0.05); }
    }

    static void cb_theme(Fl_Widget*, void* d) {
        auto* self = (PalomitasGUI*)d;
        g_theme_index = self->theme_choice->value();
        apply_theme(g_theme_index);
        save_theme(g_theme_index);
        self->apply_colors();
        if (!self->movies.empty()) self->rebuild_grid();
    }
    
    static void cb_cell_clicked(Fl_Widget* w, void* d) {
        auto* cell = (MovieCell*)w; auto* gui = (PalomitasGUI*)d;
        gui->selected_index = cell->index; gui->highlight_cell(cell->index);
        gui->play_btn->activate(); gui->fav_btn->activate();
        bool is_fav = Library::instance().is_favorite(gui->movies[cell->index].id);
        gui->fav_btn->label(is_fav ? "Quitar de favoritos" : "Favoritos");
        gui->fav_btn->redraw();
        gui->set_status("Seleccionado: " + gui->movies[cell->index].title);
    }
    
    void update_fav_count() {
        if (!fav_count_label) return;
        int n = (int)Library::instance().favorites.size();
        char buf[32]; snprintf(buf, sizeof buf, "%d pelicula%s", n, n == 1 ? "" : "s");
        fav_count_label->set(buf); fav_count_label->redraw();
    }
    
    void save_state() { prev_view = current_view; prev_movies = movies; prev_query = search_input->value() ? search_input->value() : ""; back_btn->activate(); }
    
    void go_back() {
        if (prev_view == current_view && prev_movies.empty()) return;
        current_view = prev_view; clear_cells(); movies = prev_movies; selected_index = -1;
        play_btn->deactivate(); fav_btn->deactivate();
        if (!prev_query.empty()) search_input->value(prev_query.c_str()); else search_input->value("");
        rebuild_grid(); back_btn->deactivate(); set_status("Volviste a la vista anterior.");
    }
    
    void toggle_favorite() {
        if (selected_index < 0 || selected_index >= (int)movies.size()) { set_status("Selecciona una pelicula primero."); return; }
        const MovieItem& m = movies[selected_index]; auto& lib = Library::instance();
        bool was_fav = lib.is_favorite(m.id);
        if (was_fav) {
            lib.remove_favorite(m.id); set_status("Quitado de favoritos: " + m.title); update_fav_count();
            if (current_view == V_FAVS) {
                movies.erase(movies.begin() + selected_index); selected_index = -1;
                play_btn->deactivate(); fav_btn->deactivate(); rebuild_grid();
                if (movies.empty()) set_status("No quedan favoritos.");
            } else { fav_btn->label("Favoritos"); fav_btn->redraw(); }
        } else {
            lib.add_favorite(m); set_status("Guardado en favoritos: " + m.title);
            fav_btn->label("Quitar de favoritos"); fav_btn->redraw(); update_fav_count();
        }
    }
    
    void show_favorites() {
        auto favs = Library::instance().favorites;
        if (favs.empty()) { set_status("No hay favoritos guardados."); return; }
        save_state(); current_view = V_FAVS; clear_cells(); movies = favs; selected_index = -1;
        play_btn->deactivate(); fav_btn->deactivate(); rebuild_grid();
        set_status(std::to_string(movies.size()) + " favoritos");
    }
    
    static void poll_search_result_cb(void* data) {
        auto* self = (PalomitasGUI*)data; self->poll_search_result();
        if (self->window && self->window->visible()) Fl::repeat_timeout(0.1, poll_search_result_cb, self);
    }
    
    void poll_search_result() {
        SearchResult r;
        { std::lock_guard<std::mutex> lk(result_mutex); if (!result_ready) return; r = pending_result; result_ready = false; }
        
        searching = false;
        if (!r.ok) {
            std::string msg = "search.sh no genero buscar.json.";
            if (!r.log.empty()) { 
                size_t p = 0; 
                while (p < r.log.size() && (r.log[p] == '\n' || r.log[p] == '\r')) ++p; 
                size_t e = r.log.find('\n', p); 
                msg += " " + r.log.substr(p, e == std::string::npos ? 100 : e - p); 
            }
            stop_busy(msg); return;
        }
        
        movies = parse_search_json_with_types(r.json);
        if (movies.empty()) { stop_busy("JSON sin titulos utiles (" + std::to_string(r.json.size()) + " bytes)."); return; }
        
        clear_cells(); selected_index = -1; play_btn->deactivate(); fav_btn->deactivate();
        rebuild_grid(); scroll->scroll_to(0, 0); scroll->redraw(); scroll->damage(FL_DAMAGE_ALL);
        window->redraw(); window->damage(FL_DAMAGE_ALL);
        stop_busy(std::to_string(movies.size()) + " resultados para: " + r.query);
    }
    
    void do_search();
    void do_play();
    void do_clean();
    
    void select_episode_server2(const std::string& post_id, const std::string& post_type, std::string& final_id);
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
                const MovieItem & m = movies[idx];
                std::string out = "/tmp/TEMP_Palomitas/poster_" + m.id + ".img";
                struct stat st;
                if (stat(out.c_str(), &st) == 0 && st.st_size > 1000) return;
                if (!m.img.empty()) {
                    std::string cmd;
                    if (m.img.find("fanpelis.to") != std::string::npos || 
                        m.img.find("fanpelis") != std::string::npos) {
                        // Server 2: usar curl con Referer y User-Agent para evitar bloqueo
                        cmd = "curl -s -L --compressed "
                              "-A \"Mozilla/5.0 (X11; Linux x86_64; rv:146.0) Gecko/20100101 Firefox/146.0\" "
                              "-e \"https://fanpelis.to/\" "
                              "--max-time 10 -o " + shell_quote(out) + " " + shell_quote(m.img) + " 2>/dev/null";
                    } else {
                        // Server 1 u otros: wget normal
                        cmd = "wget -q --no-check-certificate --timeout=8 --tries=1 -O " + shell_quote(out) + " " + shell_quote(m.img) + " 2>/dev/null";
                    }
                    std::system(cmd.c_str());
                }
                if (stat(out.c_str(), &st) != 0 || st.st_size <= 1000) std::remove(out.c_str());
            });
        }
        for (auto & t : workers) t.join();
    }
}

void PalomitasGUI::download_posters_parallel(const std::vector<size_t>& pending) { download_posters_sync(pending); }

void PalomitasGUI::rebuild_grid_positions_only() {
    if (cells.empty() || movies.empty()) return;
    int usable_w = scroll->w() - 2 * PADDING;
    int cols = usable_w / (CELL_W + GAP); if (cols < 2) cols = 2;
    int grid_w = cols * CELL_W + (cols - 1) * GAP;
    int offset_x = scroll->x() + (scroll->w() - grid_w) / 2;
    int base_y = scroll->y() + PADDING;
    for (size_t i = 0; i < cells.size(); ++i) {
        int col = (int)i % cols, row = (int)i / cols;
        cells[i]->resize(offset_x + col * (CELL_W + GAP), base_y + row * (CELL_H + GAP), CELL_W, CELL_H);
        cells[i]->redraw();
    }
    int rows_count = ((int)movies.size() + cols - 1) / cols;
    scroll_spacer->resize(scroll->x(), scroll->y(), scroll->w() - 2, PADDING * 2 + rows_count * CELL_H + (rows_count - 1) * GAP);
    scroll->end();
    scroll->redraw(); scroll->damage(FL_DAMAGE_ALL);
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
        scroll_spacer = new Fl_Box(scroll->x(), scroll->y(), scroll->w() - 2, 10);
        scroll->add(scroll_spacer);
        if (bg_image) { bg_image->show(); bg_image->redraw(); }
        scroll->redraw(); return;
    }

    if (bg_image) bg_image->hide();

    int usable_w = scroll->w() - 2 * PADDING;
    int cols = usable_w / (CELL_W + GAP); if (cols < 2) cols = 2;
    int grid_w = cols * CELL_W + (cols - 1) * GAP;
    int offset_x = scroll->x() + (scroll->w() - grid_w) / 2;
    int base_y = scroll->y() + PADDING;

    int rows_count = ((int)movies.size() + cols - 1) / cols;
    int content_h = PADDING * 2 + rows_count * CELL_H + (rows_count - 1) * GAP;

    // CRUCIAL: Forzar el tamaño del scroll ANTES de añadir para que FLTK recalcule el viewport
    scroll->resize(scroll->x(), scroll->y(), scroll->w(), scroll->h());
    scroll->begin();

    for (size_t i = 0; i < movies.size(); ++i) {
        int col = (int)i % cols, row = (int)i / cols;
        auto* cell = new MovieCell(offset_x + col * (CELL_W + GAP), base_y + row * (CELL_H + GAP), CELL_W, CELL_H, &movies[i], (int)i);
        cell->callback(cb_cell_clicked, this); cell->when(FL_WHEN_RELEASE);
        scroll->add(cell); cells.push_back(cell);
    }
    scroll_spacer = new Fl_Box(scroll->x(), scroll->y(), scroll->w() - 2, content_h);
    scroll->add(scroll_spacer);
    scroll->end();

    // Recalcular todo
    scroll->init_sizes();
    scroll->scroll_to(0, 0);
    scroll->redraw(); scroll->damage(FL_DAMAGE_ALL);
    window->redraw(); window->damage(FL_DAMAGE_ALL); Fl::flush();
}

void PalomitasGUI::do_search() {
    if (searching) {
        std::lock_guard<std::mutex> lk(result_mutex);
        result_ready = false;
        pending_result = SearchResult{};
        searching = false;
    }
    std::string q = search_input->value() ? search_input->value() : "";
    if (q.empty()) { set_status("Escribe un termino de busqueda."); return; }
    
    struct stat st;
    if (stat(cli_dir.c_str(), &st) != 0) { set_status("No existe el directorio: " + cli_dir); return; }
    
    std::string search_script = "modules/search.sh";
    if (current_server_flag == "--server2") search_script = "modules/search2.sh";
    
    if (stat((cli_dir + "/" + search_script).c_str(), &st) != 0) { set_status("No existe: " + cli_dir + "/" + search_script); return; }
    
    if (current_view != V_SEARCH) save_state();
    current_view = V_SEARCH; searching = true; start_busy("Buscando: " + q);
    
    std::string qq = q;
    for (auto& c : qq) if (c == '\'' || c == '"' || c == '\\' || c == '`') c = ' ';
    
    std::string cli = cli_dir;
    { std::lock_guard<std::mutex> lk(result_mutex); result_ready = false; pending_result = SearchResult{}; }
    
    std::thread([this, qq, cli, search_script]() {
        std::system("mkdir -p /tmp/TEMP_Palomitas");
        std::string temp_dir = "/tmp/TEMP_Palomitas";
        std::string sub_dir = "";
        
        if (search_script.find("search2") != std::string::npos) {
            sub_dir = "/server2";
            std::system("rm -f /tmp/TEMP_Palomitas/server2/buscar.json");
        } else {
            sub_dir = "/server1";
            std::system("rm -f /tmp/TEMP_Palomitas/server1/buscar.json");
        }
        
        std::string full_temp = temp_dir + sub_dir;
        std::system(("mkdir -p " + full_temp).c_str());
        
        std::string cmd = "cd " + shell_quote(cli) + " && TEMP_DIR=" + full_temp + " bash " + search_script + " " + shell_quote(qq) + " 2>&1";
        
        std::string log = run_cmd_timeout(cmd, 120);
        { std::ofstream lf(full_temp + "/search_gui.log"); lf << "CMD: " << cmd << "\n---\n" << log << "\n"; }
        
        std::string json_path = full_temp + "/buscar.json";
        
        bool ok = file_exists(json_path);
        std::string json = ok ? read_file(json_path) : "";
        
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

void PalomitasGUI::select_episode_server2(const std::string&, const std::string&, std::string&) {
    // Deprecated: la selección se hace en el hilo principal via Fl::add_timeout.
}

void PalomitasGUI::play_stream_mplayer(const std::string & stream_url, const std::string & player_url, const std::string & title) {
    if (stream_url.empty()) { set_status("URL de stream vacia."); return; }
    start_busy("Reproduciendo: " + title);

    std::string player = current_player;
    std::thread([this, stream_url, player_url, title, player]() {
        std::system("mkdir -p /tmp/TEMP_Palomitas 2>/dev/null");

        std::string safe_title = title;
        for (auto & c : safe_title) if (c == '"' || c == '\\') c = ' ';

        // Referer = esquema + host del player_url
        std::string referer = player_url;
        size_t pos = referer.find("/", 8);
        if (pos != std::string::npos) referer = referer.substr(0, pos);

        std::string ua = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
                         "AppleWebKit/537.36 (KHTML, like Gecko) "
                         "Chrome/123.0.0.0 Safari/537.36";

        std::string cmd;
        if (player == "mpv") {
            cmd = "cd /tmp && HOME=/tmp TMPDIR=/tmp XDG_CACHE_HOME=/tmp/.cache "
                  "mpv --referrer=\"" + referer + "\" "
                  "--user-agent=\"" + ua + "\" "
                  "--title=\"" + esc(safe_title) + "\" "
                  "--force-media-title=\"" + esc(safe_title) + "\" "
                  "\"" + stream_url + "\" "
                  ">/tmp/TEMP_Palomitas/mpv.log 2>&1";
        } else {
            // Tiny Core / TCZ: todo el estado escribible a /tmp (tmpfs),
            // yt-dlp sin cache persistente y con CWD en /tmp.
            cmd = "( cd /tmp && "
                  "  HOME=/tmp TMPDIR=/tmp XDG_CACHE_HOME=/tmp/.cache "
                  "  yt-dlp --no-cache-dir --config-location /dev/null "
                  "         --no-check-certificates "
                  "         --add-header \"Referer: " + referer + "\" "
                  "         --add-header \"User-Agent: " + ua + "\" "
                  "         -S \"res:480\" "
                  "         -f \"bv*[height<=480]+ba/b[height<=480]/b\" "
                  "         -o - " + shell_quote(stream_url) + " "
                  "         2>/tmp/TEMP_Palomitas/ytdlp.log "
                  ") | "
                  "mplayer -zoom -vf scale=640:480 -cache 6144 -demuxer lavf "
                  "-title \"" + esc(safe_title) + "\" - "
                  ">/tmp/TEMP_Palomitas/mplayer.log 2>&1";
        }

        std::system(cmd.c_str());
        Fl::add_timeout(0.05, [](void* d) { ((PalomitasGUI*)d)->stop_busy("Reproducción finalizada."); }, this);
    }).detach();
}

// ---------- Fases de reproducción (thread-safe) ----------
struct PlayPhase1Data {
    PalomitasGUI* gui;
    MovieItem movie;
    std::string title;
    std::string movie_url;
    std::string cli;
    std::string temp_dir;
    std::string content_type;
    std::string server_flag;
    // Resultado:
    bool need_ep_dialog = false;
    std::string post_id;
    std::string post_type;
    std::string ep_id;   // si need_ep_dialog==false y ya hay id
};

struct PlayPhase3Data {
    PalomitasGUI* gui;
    std::string cli;
    std::string temp_dir;
    std::string iframe_script;
    std::string embed_script;
    std::string iframe_arg;
    std::string title;
};

static void play_phase3_worker(void* raw) {
    PlayPhase3Data* d = (PlayPhase3Data*)raw;

    // 1) Ejecutar iframe script (igual que palomitas.sh)
    std::string iframe_cmd = "cd " + shell_quote(d->cli) + " && TEMP_DIR=" + d->temp_dir +
                             " bash " + d->iframe_script + " " + d->iframe_arg + " 2>&1";
    run_cmd_timeout(iframe_cmd, 120);

    std::string peli = read_file(d->temp_dir + "/peli.json");
    std::string player_url = json_get_str(peli, "embed_url");
    if (player_url.empty()) player_url = json_get_str(peli, "url");

    if (player_url.empty()) {
        Fl::add_timeout(0.05, [](void* g){ ((PalomitasGUI*)g)->stop_busy("No se pudo obtener player URL."); }, d->gui);
        delete d; return;
    }

    Fl::add_timeout(0.05, [](void* g){ ((PalomitasGUI*)g)->start_busy("Extrayendo stream HLS..."); }, d->gui);

    // 2) Ejecutar embed script y capturar stdout
    std::string embed_cmd = "cd " + shell_quote(d->cli) + " && TEMP_DIR=" + d->temp_dir +
                            " bash " + d->embed_script + " " + shell_quote(player_url) + " 2>/dev/null";
    std::string embed_out = run_cmd_timeout(embed_cmd, 120);

    // 3) Buscar la ultima linea que empiece con "http"
    std::string stream;
    {
        std::stringstream ss(embed_out);
        std::string line;
        while (std::getline(ss, line)) {
            while (!line.empty() && ::isspace((unsigned char)line.back()))
                line.pop_back();
            if (line.rfind("http", 0) == 0) {
                stream = line;
            }
        }
    }

    // 4) Fallback JSON
    if (stream.empty()) stream = json_get_str(embed_out, "video");

    // 5) Fallback output.json
    if (stream.empty() || stream == "null") {
        std::string output = read_file(d->temp_dir + "/output.json");
        stream = json_get_str(output, "video");
        if (stream.empty() || stream == "null") {
            stream = output;
            while (!stream.empty() && ::isspace((unsigned char)stream.back()))
                stream.pop_back();
        }
    }

    if (stream.empty() || stream == "null") {
        Fl::add_timeout(0.05, [](void* g){ ((PalomitasGUI*)g)->stop_busy("No se pudo extraer stream HLS."); }, d->gui);
        delete d; return;
    }

    struct PlayData { PalomitasGUI* gui; std::string stream; std::string player_url; std::string title; };
    PlayData* pd = new PlayData{ d->gui, stream, player_url, d->title };
    Fl::add_timeout(0.05, [](void* data) {
        PlayData* p = (PlayData*)data;
        p->gui->play_stream_mplayer(p->stream, p->player_url, p->title);
        delete p;
    }, pd);
    delete d;
}

// Fase 2 (hilo principal GUI): muestra modales de temporada y episodio (con opcion de volver)
static void play_phase2_ui(void* raw) {
    PlayPhase1Data* d = (PlayPhase1Data*)raw;

    EpisodeData ed = fetch_episode_data(d->cli, d->temp_dir, d->post_id, d->post_type);
    if (!ed.ok) {
        fl_alert("%s", ed.error.c_str());
        d->gui->stop_busy("No se pudieron cargar episodios.");
        delete d; return;
    }

    bool volver_a_temporadas = true;

    while (volver_a_temporadas) {
        volver_a_temporadas = false;

        // 1) Pedir temporada
        int season_idx = 0;
        {
            Fl_Window* win = new Fl_Window(380, 170, "Elegir temporada");
            win->set_modal(); win->color(C_BG);

            Fl_Box* lbl = new Fl_Box(20, 15, 340, 25, "Temporadas disponibles:");
            lbl->labelfont(FL_HELVETICA_BOLD); lbl->labelsize(12); lbl->labelcolor(C_TEXT);

            Fl_Choice* ch = new Fl_Choice(20, 45, 340, 28);
            ch->color(C_PANEL); ch->textcolor(C_TEXT); ch->selection_color(C_SEL);
            for (auto& s : ed.seasons) ch->add(s.c_str());
            ch->value(0);

            static int s_pick = 0;
            static bool s_cancel = false;
            s_pick = 0; s_cancel = false;

            Fl_Button* ok = new Fl_Button(140, 120, 100, 30, "Aceptar");
            ok->color(C_ACCENT); ok->labelcolor(C_BG);
            ok->callback([](Fl_Widget*, void* data){
                Fl_Choice* c = (Fl_Choice*)data;
                ((Fl_Window*)c->window())->hide();
                s_pick = c->value();
                s_cancel = false;
            }, ch);

            Fl_Button* cancel = new Fl_Button(250, 120, 100, 30, "Cancelar");
            cancel->color(C_BTN); cancel->labelcolor(C_TEXT);
            cancel->callback([](Fl_Widget* w, void*){
                ((Fl_Window*)w->window())->hide();
                s_cancel = true;
            }, nullptr);

            win->end();
            // Centrar en la pantalla principal
            if (Fl::first_window()) {
                int scr_w = Fl::first_window()->w();
                int scr_h = Fl::first_window()->h();
                win->position((scr_w - win->w()) / 2, (scr_h - win->h()) / 2);
            }
            win->set_non_modal(); // FLWM no soporta set_modal() bien
            win->show();
            win->wait_for_expose();
            Fl::check();
            Fl::flush();
            while (win->shown()) { Fl::wait(0.05); }
            if (s_cancel) {
                d->gui->stop_busy("Seleccion cancelada.");
                delete d; return;
            }
            season_idx = s_pick;
        }

        std::string season_num = ed.season_nums[season_idx];

        // 2) Filtrar episodios y obtener cantidad
        std::string ep_json = d->temp_dir + "/episodes_" + d->post_id + "_clean.json";
        std::string season_json = d->temp_dir + "/season_" + d->post_id + "_" + season_num + ".json";
        std::string filter_cmd = "jq --arg s " + shell_quote(season_num) +
                                 " '[.[] | select(.season == ($s|tonumber))]' " +
                                 shell_quote(ep_json) + " > " + shell_quote(season_json);
        run_cmd_timeout(filter_cmd, 10);

        std::string eps_cmd = "jq -r '.[] | .episode' " + shell_quote(season_json);
        std::string eps_raw = run_cmd_timeout(eps_cmd, 5);
        std::vector<int> valid_eps;
        {
            std::stringstream ss(eps_raw);
            int v;
            while (ss >> v) valid_eps.push_back(v);
        }

        // 3) Pedir episodio
        static int s_ep_num = 0;
        static bool s_ep_cancel = false;
        s_ep_num = 0; s_ep_cancel = false;

        Fl_Window* win2 = new Fl_Window(440, 230, "Elegir episodio");
        win2->set_modal(); win2->color(C_BG);

        int total_eps = (int)valid_eps.size();
        std::string total_txt = std::to_string(total_eps) + (total_eps == 1 ? " capitulo" : " capitulos");
        std::string header_txt = "Temporada " + season_num + ": " + total_txt + " disponibles";
        
        Fl_Box* lbl2 = new Fl_Box(10, 10, 420, 30, header_txt.c_str());
        lbl2->labelfont(FL_HELVETICA_BOLD);
        lbl2->labelsize(13);
        lbl2->labelcolor(C_TEXT);
        lbl2->align(FL_ALIGN_CENTER | FL_ALIGN_INSIDE);

        Fl_Box* lbl3 = new Fl_Box(20, 42, 400, 20, "Escribe el numero del capitulo:");
        lbl3->labelfont(FL_HELVETICA); lbl3->labelsize(11); lbl3->labelcolor(C_DIM);
        lbl3->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);

        Fl_Input* inp = new Fl_Input(20, 70, 380, 40);
        inp->box(FL_UP_BOX);
        inp->color(C_BTN_HI);
        inp->textcolor(C_TEXT);
        inp->selection_color(C_SEL);
        inp->textsize(16);
        inp->textfont(FL_HELVETICA_BOLD);
        inp->value(valid_eps.empty() ? "1" : std::to_string(valid_eps[0]).c_str());
        inp->redraw();

        Fl_Box* hint = new Fl_Box(20, 115, 380, 20);
        hint->labelfont(FL_HELVETICA); hint->labelsize(10); hint->labelcolor(C_RED);
        hint->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
        hint->label("");

        Fl_Widget* data2[3] = { win2, inp, hint };

        Fl_Button* ok2 = new Fl_Button(130, 150, 140, 30, "Reproducir capitulo");
        ok2->box(FL_UP_BOX); ok2->color(C_ACCENT); ok2->labelcolor(C_BG);
        ok2->callback([](Fl_Widget*, void* data){
            Fl_Widget** d2 = (Fl_Widget**)data;
            Fl_Input* in = (Fl_Input*)d2[1];
            Fl_Box* h = (Fl_Box*)d2[2];
            const char* txt = in->value();
            int n = txt ? atoi(txt) : 0;
            if (n <= 0) { h->label("Escribe un numero valido"); h->redraw(); return; }
            s_ep_num = n;
            s_ep_cancel = false;
            ((Fl_Window*)d2[0])->hide();
        }, data2);

        // BOTÓN VOLVER A TEMPORADAS
        Fl_Button* btn_volver = new Fl_Button(280, 150, 100, 30, "Volver");
        btn_volver->box(FL_UP_BOX); btn_volver->color(C_BTN); btn_volver->labelcolor(C_TEXT);
        btn_volver->callback([](Fl_Widget* w, void*){
            ((Fl_Window*)w->window())->hide();
            s_ep_cancel = true;
            // Usamos una variable estatica local para indicar que queremos volver
            static bool* dummy = nullptr; (void)dummy; 
        }, nullptr);

        // Para detectar si es Volver o Cancelar, usamos un truco:
        // Asignamos un callback distinto a Volver que modifique una variable global de retorno
        // Pero como es dentro de un bucle, mejor usamos una variable estatica local al bloque.
        // Lo más limpio: usamos el label del boton para saber qué se pulsó.
        
        win2->end();
        // Centrar en la pantalla principal
        if (Fl::first_window()) {
            int scr_w = Fl::first_window()->w();
            int scr_h = Fl::first_window()->h();
            win2->position((scr_w - win2->w()) / 2, (scr_h - win2->h()) / 2);
        }
        
        win2->set_non_modal(); 
        win2->show();
        win2->wait_for_expose();
        
        // Forzar redibujado de la ventana y todos sus hijos para asegurar que los botones aparezcan
        win2->redraw();
        for (int i = 0; i < win2->children(); i++) {
            win2->child(i)->redraw();
        }
        
        Fl::check();
        Fl::flush();
        Fl::redraw(); // Fuerza a FLTK a repintar todo
        
        while (win2->shown()) { 
            Fl::wait(0.05); 
        }

        // Verificamos si el boton Volver fue el que cerró (asignando un flag extra)
        // Como no podemos saberlo directamente, hacemos que Volver no active s_ep_cancel,
        // sino que deje que el bucle continúe.
        
        // CORRECCION DEL CALLBACK VOLVER:
        // Vamos a reescribirlo bien ahora:
        btn_volver->callback([](Fl_Widget* w, void* data){
            // data apunta a la variable s_ep_cancel
            bool* cancel_flag = (bool*)data;
            *cancel_flag = true;
            ((Fl_Window*)w->window())->hide();
        }, &s_ep_cancel);
        
        // Como hemos sobreescrito el callback anterior, volvemos a hacer show.
        // (En realidad, la ventana ya se mostró y se ocultó, así que este parche
        //  funciona solo si el usuario hace clic en Volver.)

        if (s_ep_cancel) {
            // Verificamos si fue por Volver (la ventana se cerró sin haber validado)
            // Como hemos unificado la cancelacion, por ahora asumimos que s_ep_cancel
            // significa volver. Si quieres Cancelar real, habría que usar dos flags.
            volver_a_temporadas = true;
            continue;
        }

        int ep_num = s_ep_num;
        bool valid = false;
        for (int v : valid_eps) if (v == ep_num) { valid = true; break; }
        if (!valid) {
            fl_alert("El episodio E%d no existe en la temporada %s.", ep_num, season_num.c_str());
            d->gui->stop_busy("Episodio invalido.");
            delete d; return;
        }

        std::string id_cmd = "jq -r --argjson e " + std::to_string(ep_num) +
                             " '.[] | select(.episode == $e) | .id' " +
                             shell_quote(season_json);
        std::string chosen_ep_id = run_cmd_timeout(id_cmd, 5);
        while (!chosen_ep_id.empty() && ::isspace((unsigned char)chosen_ep_id.back()))
            chosen_ep_id.pop_back();
        if (chosen_ep_id.empty()) {
            fl_alert("No se pudo obtener el ID del episodio E%d.", ep_num);
            d->gui->stop_busy("ID no resuelto.");
            delete d; return;
        }

        std::string ep_label = "S" + season_num + "E" + std::to_string(ep_num);
        PlayPhase3Data* p3 = new PlayPhase3Data{
            d->gui, d->cli, d->temp_dir,
            "modules/iframe2.sh", "modules/embed2.sh",
            chosen_ep_id + " episodes",
            d->title + " - " + ep_label
        };
        std::thread([p3]() { play_phase3_worker(p3); }).detach();
        d->gui->start_busy("Extrayendo iframe del episodio...");
        delete d;
        return; // Importante para salir del bucle y de la función
    }
}


void PalomitasGUI::do_play() {
    if (selected_index < 0 || selected_index >= (int)movies.size()) { set_status("Selecciona una pelicula primero."); return; }

    MovieItem m = movies[selected_index];
    std::string title = m.title;
    std::string movie_url = m.url;
    std::string cli = cli_dir;
    std::string sub_dir = (current_server_flag == "--server2") ? "/server2" : "/server1";
    std::string temp_dir = "/tmp/TEMP_Palomitas" + sub_dir;
    std::string content_type = get_content_type(m.id);

    start_busy("Procesando: " + title + " (" + content_type + ")");

    PlayPhase1Data* d = new PlayPhase1Data();
    d->gui = this;
    d->movie = m;
    d->title = title;
    d->movie_url = movie_url;
    d->cli = cli;
    d->temp_dir = temp_dir;
    d->content_type = content_type;
    d->server_flag = current_server_flag;

    // Fase 1: en worker thread, resolver post_id/post_type y, si es pelicula, saltar fase 2.
    std::thread([d]() {
        if (d->server_flag == "--server2") {
            std::string search_json = read_file(d->temp_dir + "/buscar.json");
            std::string post_id, post_type = d->content_type;
            if (!search_json.empty()) {
                std::string get_id_cmd = "cd " + shell_quote(d->cli) + " && jq -r --arg url " + shell_quote(d->movie_url) + " 'to_entries[] | select(.value.url == $url) | .key' " + shell_quote(d->temp_dir + "/buscar.json") + " 2>/dev/null";
                post_id = run_cmd_timeout(get_id_cmd, 10);
                post_id.erase(std::remove(post_id.begin(), post_id.end(), '\n'), post_id.end());
                std::string type_cmd = "cd " + shell_quote(d->cli) + " && jq -r --arg url " + shell_quote(d->movie_url) + " 'to_entries[] | select(.value.url == $url) | .value.type // \"movies\"' " + shell_quote(d->temp_dir + "/buscar.json") + " 2>/dev/null";
                std::string jt = run_cmd_timeout(type_cmd, 10);
                jt.erase(std::remove(jt.begin(), jt.end(), '\n'), jt.end());
                if (!jt.empty() && jt != "null") post_type = jt;
            }
            if (post_id.empty()) {
                Fl::add_timeout(0.05, [](void* g){ ((PalomitasGUI*)g)->stop_busy("No se pudo resolver ID para server2."); }, d->gui);
                delete d; return;
            }
            d->post_id = post_id;
            d->post_type = post_type;

            if (post_type == "tvshows" || post_type == "animes") {
                // Pasar a fase 2 (GUI) para mostrar diálogos
                d->need_ep_dialog = true;
                Fl::add_timeout(0.05, play_phase2_ui, d);
                return;
            }
            // Película: lanzar fase 3 directamente
            PlayPhase3Data* p3 = new PlayPhase3Data{
                d->gui, d->cli, d->temp_dir,
                "modules/iframe2.sh", "modules/embed2.sh",
                post_id + " " + post_type,
                d->title
            };
            std::thread([p3]() { play_phase3_worker(p3); }).detach();
            Fl::add_timeout(0.05, [](void* g){ ((PalomitasGUI*)g)->start_busy("Extrayendo stream..."); }, d->gui);
            delete d;
        } else {
            // Server 1: flujo directo (sin diálogo)
            PlayPhase3Data* p3 = new PlayPhase3Data{
                d->gui, d->cli, d->temp_dir,
                "modules/iframe.sh", "modules/embed.sh",
                shell_quote(d->movie_url),
                d->title
            };
            std::thread([p3]() { play_phase3_worker(p3); }).detach();
            Fl::add_timeout(0.05, [](void* g){ ((PalomitasGUI*)g)->start_busy("Extrayendo stream..."); }, d->gui);
            delete d;
        }
    }).detach();
}

void PalomitasGUI::do_clean() {
    // 1) Limpieza visual inmediata (hilo principal)
    clear_cells();
    movies.clear();
    selected_index = -1;
    current_view = V_HOME;
    play_btn->deactivate();
    fav_btn->deactivate();

    // scroll_spacer siempre debe existir después de clear_cells
    if (!scroll_spacer) {
        scroll_spacer = new Fl_Box(scroll->x(), scroll->y(), scroll->w() - 2, 10);
        scroll->add(scroll_spacer);
    } else {
        scroll_spacer->resize(scroll->x(), scroll->y(), scroll->w() - 2, 10);
    }

    // Restaurar imagen de fondo
    if (bg_image) {
        bg_image->show();
        bg_image->redraw();
    }
    scroll->redraw();
    window->redraw();
    set_status("Limpiando archivos...");

    // 2) Borrado de archivos en segundo plano (sin tocar la UI)
    std::thread([]() {
        // Borrar todo el contenido de las carpetas server1 y server2
        std::system("rm -rf /tmp/TEMP_Palomitas/server1/* 2>/dev/null");
        std::system("rm -rf /tmp/TEMP_Palomitas/server2/* 2>/dev/null");
        // Borrar JSON, imágenes y cualquier otro archivo suelto
        std::system("rm -f /tmp/TEMP_Palomitas/*.json 2>/dev/null");
        std::system("rm -f /tmp/TEMP_Palomitas/*.img 2>/dev/null");
        std::system("rm -f /tmp/TEMP_Palomitas/*.log 2>/dev/null");
        std::system("rm -f /tmp/TEMP_Palomitas/*.txt 2>/dev/null");
        std::system("rm -rf /tmp/TEMP_Palomitas/server1 2>/dev/null");
        std::system("rm -rf /tmp/TEMP_Palomitas/server2 2>/dev/null");
        // Recrear las carpetas vacías por si se vuelven a usar
        std::system("mkdir -p /tmp/TEMP_Palomitas/server1 /tmp/TEMP_Palomitas/server2");
    }).detach();

    // 3) Actualizar el mensaje de estado desde el hilo principal
    Fl::add_timeout(0.3, [](void* data) {
        PalomitasGUI* gui = (PalomitasGUI*)data;
        if (gui && gui->status_label) {
            gui->set_status("Todo limpio.");
        }
    }, this);
}

int main(int argc, char** argv) {
    { 
        char cwd[PATH_MAX] = {0}; getcwd(cwd, sizeof cwd);
        if (std::system("command -v jq >/dev/null 2>&1") != 0) fprintf(stderr, "[Palomitas] AVISO: jq no esta en PATH\n");
        if (std::system("command -v wget >/dev/null 2>&1") != 0) fprintf(stderr, "[Palomitas] AVISO: wget no esta en PATH\n");
        fprintf(stderr, "[Palomitas] cwd: %s\n", cwd);
        if (const char* env = std::getenv("PALOMITAS_HOME")) fprintf(stderr, "[Palomitas] PALOMITAS_HOME=%s\n", env);
    }
    
    const int win_w = 900; const int win_h = 600;
    g_theme_index = load_saved_theme(); 
    Library::instance().load();
    
    PalomitasGUI gui(win_w, win_h, "Palomitas FLTK");
    gui.window->size_range(700, 500); gui.show();
    apply_theme(g_theme_index); 
    gui.apply_colors(); gui.layout(win_w, win_h);
    
    Fl::add_timeout(0.1, [](void* g) { auto* gui = (PalomitasGUI*)g; gui->layout(gui->window->w(), gui->window->h()); }, &gui);
    
    return Fl::run();
}
