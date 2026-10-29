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
#include <array>
#include <regex>
#include <functional>
#include <unistd.h>
#include <sys/stat.h>

// ---------- Utilidades ----------

// Ejecuta un comando shell y devuelve su stdout
static std::string run_cmd(const std::string& cmd) {
    fprintf(stderr, "[run_cmd] CMD: %s\n", cmd.c_str());
    std::array<char, 4096> buffer;
    std::string result;
    FILE* fp = popen(cmd.c_str(), "r");
    if (!fp) { fprintf(stderr, "[run_cmd] popen FALLO\n"); return ""; }
    while (fgets(buffer.data(), buffer.size(), fp) != nullptr) result += buffer.data();
    int rc = pclose(fp);
    fprintf(stderr, "[run_cmd] rc=%d, salida (%zu bytes): %s\n", rc, result.size(),
            result.substr(0, 200).c_str());
    return result;
}

// Lee un archivo completo a string
static std::string read_file(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) return "";
    std::stringstream ss; ss << f.rdbuf(); return ss.str();
}

// Extrae el valor string de una clave JSON simple
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

// Cambia el sufijo -WxH de la URL por -420x630 (o lo añade si no hay sufijo)
static std::string upgrade_image_url(const std::string& url) {
    if (url.empty()) return url;
    std::string base = url, suffix;
    auto qpos = base.find_first_of("?#");
    if (qpos != std::string::npos) { suffix = base.substr(qpos); base = base.substr(0, qpos); }
    auto slash = base.find_last_of('/'), dot = base.find_last_of('.');
    std::string ext, name = base;
    if (dot != std::string::npos && (slash == std::string::npos || dot > slash)) { ext = base.substr(dot); name = base.substr(0, dot); }
    std::regex re("-\\d+x\\d+$");
    if (std::regex_search(name, re)) name = std::regex_replace(name, re, "-420x630");
    else { auto ls = name.find_last_of('/'); std::string fn = (ls==std::string::npos)?name:name.substr(ls+1); if (fn.size()>8) name += "-420x630"; }
    return name + ext + suffix;
}

// Mismo que arriba pero con -683x1024 (fallback)
static std::string upgrade_image_url_alt(const std::string& url) {
    if (url.empty()) return url;
    std::string base = url, suffix;
    auto qpos = base.find_first_of("?#");
    if (qpos != std::string::npos) { suffix = base.substr(qpos); base = base.substr(0, qpos); }
    auto slash = base.find_last_of('/'), dot = base.find_last_of('.');
    std::string ext, name = base;
    if (dot != std::string::npos && (slash == std::string::npos || dot > slash)) { ext = base.substr(dot); name = base.substr(0, dot); }
    std::regex re("-\\d+x\\d+$");
    if (std::regex_search(name, re)) name = std::regex_replace(name, re, "-683x1024");
    else { auto ls = name.find_last_of('/'); std::string fn = (ls==std::string::npos)?name:name.substr(ls+1); if (fn.size()>8) name += "-683x1024"; }
    return name + ext + suffix;
}

// Parsea el JSON de búsqueda (formato { "ID": { title, url, img, extra:{date,imdb} } })
static std::vector<MovieItem> parse_search_json(const std::string& json) {
    std::vector<MovieItem> out;
    std::regex block_re("\"(\\d+)\"\\s*:\\s*\\{");
    auto begin = std::sregex_iterator(json.begin(), json.end(), block_re);
    auto end = std::sregex_iterator();
    std::vector<std::pair<std::string, size_t>> starts;
    for (auto it = begin; it != end; ++it) starts.emplace_back((*it)[1].str(), (size_t)it->position());
    for (size_t i = 0; i < starts.size(); ++i) {
        size_t start = starts[i].second;
        size_t stop = (i+1 < starts.size()) ? starts[i+1].second : json.size();
        std::string chunk = json.substr(start, stop - start);
        MovieItem m;
        m.id = starts[i].first;
        m.title = json_get_str(chunk, "title");
        m.url = json_get_str(chunk, "url");
        m.img = json_get_str(chunk, "img");
        std::regex extra_re("\"extra\"\\s*:\\s*\\{([^}]*)\\}");
        std::smatch em;
        if (std::regex_search(chunk, em, extra_re)) {
            std::string ex = em[1].str();
            m.year = json_get_str(ex, "date");
            m.imdb = json_get_str(ex, "imdb");
        }
        if (!m.title.empty()) out.push_back(std::move(m));
    }
    return out;
}

// ---------- Imagen de fondo (cover, escala al tamaño del widget) ----------
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

// ---------- Botón plano con esquinas redondeadas ----------
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
            fl_font(FL_HELVETICA_BOLD, 13);
            fl_color(active_r() ? fg : C_DIM);
            fl_draw(label(), x(), y(), w(), h(), FL_ALIGN_CENTER | FL_ALIGN_INSIDE);
        }
    }
    int handle(int e) override {
        if (e == FL_ENTER || e == FL_LEAVE) { hover = (e == FL_ENTER); redraw(); }
        return Fl_Button::handle(e);
    }
};

// ---------- Label simple ----------
class Label : public Fl_Widget {
public:
    std::string text;
    int size = 13;
    Fl_Font font = FL_HELVETICA;
    int fg_role = 0;   // 0=TEXT, 1=DIM, 2=ACCENT, 3=GREEN, 4=RED
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

// ---------- Enlace clicable ----------
class LinkLabel : public Fl_Widget {
public:
    std::string text;
    int size = 12;
    Fl_Font font = FL_HELVETICA;
    bool hover = false;
    std::function<void()> on_click;
    LinkLabel(int x, int y, int w, int h, const char* t = "") : Fl_Widget(x, y, w, h), text(t) {}
    void draw() override {
        fl_font(font, size);
        fl_color(hover ? C_ACCENT : C_DIM);
        fl_draw(text.c_str(), x(), y(), w(), h(), FL_ALIGN_RIGHT | FL_ALIGN_INSIDE);
    }
    int handle(int e) override {
        switch (e) {
            case FL_ENTER: hover = true; redraw(); return 1;
            case FL_LEAVE: hover = false; redraw(); return 1;
            case FL_PUSH:
                if (Fl::event_button() == FL_LEFT_MOUSE) {
                    if (on_click) on_click();
                    return 1;
                }
                return 0;
            default:
                return Fl_Widget::handle(e);
        }
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
        // Fondo redondeado
        int r = 8;
        fl_color(selected ? C_SEL : C_PANEL);
        fl_rectf(x(), y() + r, w(), h() - 2*r);
        fl_rectf(x() + r, y(), w() - 2*r, h());
        fl_pie(x(), y(), 2*r, 2*r, 90, 180);
        fl_pie(x() + w() - 2*r, y(), 2*r, 2*r, 0, 90);
        fl_pie(x(), y() + h() - 2*r, 2*r, 2*r, 180, 270);
        fl_pie(x() + w() - 2*r, y() + h() - 2*r, 2*r, 2*r, 270, 360);

        // Carátula
        int pad = 10, img_w = w() - pad*2, img_h = h() - 68;
        int ix = x() + pad, iy = y() + pad;
        std::string img_path = "/tmp/TEMP_Palomitas/poster_" + movie->id + ".img";
        Fl_Shared_Image* img = Fl_Shared_Image::get(img_path.c_str());
        if (img && img->w() > 0) {
            int pw = img->w(), ph = img->h();
            double ar = (double)pw / ph;
            int dw = img_w, dh = (int)(img_w / ar);
            if (dh > img_h) { dh = img_h; dw = (int)(img_h * ar); }
            Fl_Image* s1 = (dw < pw || dh < ph) ? img->copy(pw/2, ph/2) : (Fl_Image*)img;
            Fl_Image* s2 = (s1 && (dw < s1->w() || dh < s1->h())) ? s1->copy(dw, dh) : s1;
            if (s2) { int dx = ix + (img_w - s2->w())/2; int dy = iy + (img_h - s2->h())/2; s2->draw(dx, dy); }
            if (s2 && s2 != img && s2 != s1) delete s2;
            if (s1 && s1 != img) delete s1;
        } else {
            fl_draw_box(FL_BORDER_BOX, ix, iy, img_w, img_h, C_DIM);
            fl_color(C_TEXT); fl_font(FL_HELVETICA, 12);
            fl_draw("Sin imagen", ix, iy, img_w, img_h, FL_ALIGN_CENTER);
        }

        // Título y metadatos
        std::string t = movie->title;
        if (t.size() > 42) t = t.substr(0, 39) + "...";
        int ty = iy + img_h + 8;
        fl_color(selected ? FL_WHITE : C_TEXT);
        fl_font(FL_HELVETICA_BOLD, 13);
        fl_draw(t.c_str(), x() + pad, ty, w() - pad*2, 34,
                FL_ALIGN_LEFT | FL_ALIGN_TOP | FL_ALIGN_WRAP | FL_ALIGN_CLIP);
        fl_color(selected ? FL_WHITE : C_DIM);
        fl_font(FL_HELVETICA, 11);
        std::string meta = "Año " + movie->year + "   IMDb: " + movie->imdb;
        fl_draw(meta.c_str(), x() + pad, ty + 38, w() - pad*2, 16,
                FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    }
};

// ---------- Ventana principal (detecta SHOW para redibujar al restaurar) ----------
class MainWindow : public Fl_Window {
public:
    MainWindow(int w, int h, const char* title) : Fl_Window(w, h, title) {}
    void resize(int X, int Y, int W, int H) override {
        Fl_Window::resize(X, Y, W, H);
        if (on_resize_cb) on_resize_cb(W, H);
    }
    int handle(int e) override {
        // Al restaurar desde minimizado, diferir el redibujado para no reentrar
        if (e == FL_SHOW) {
            Fl::add_timeout(0.05, [](void* data) {
                auto* win = (MainWindow*)data;
                if (!win || !win->visible()) return;
                if (win->on_resize_cb) win->on_resize_cb(win->w(), win->h());
                win->redraw();
            }, this);
        }
        return Fl_Window::handle(e);
    }
    std::function<void(int,int)> on_resize_cb;
};

// ---------- App principal ----------
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
    LinkLabel* github_link = nullptr;

    std::vector<MovieItem> movies;
    int selected_index = -1;
    std::string project_dir;

    enum View { V_HOME, V_SEARCH, V_FAVS };
    View current_view = V_HOME;
    View prev_view = V_HOME;
    std::vector<MovieItem> prev_movies;
    std::string prev_query;

    bool busy = false;
    int blink_state = 0;
    std::string busy_msg;

    static constexpr int CELL_W = 200;
    static constexpr int CELL_H = 340;
    static constexpr int GAP = 14;
    static constexpr int PADDING = 14;
    static constexpr int SIDE_W = 110;

    PalomitasGUI(int w, int h, const char* title) : project_dir(get_project_dir()) {
        fl_register_images();
        window = new MainWindow(w, h, title);
        window->on_resize_cb = [this](int W, int H) { layout(W, H); };

        Fl_Box* header = new Fl_Box(SIDE_W + 20, 10, w - SIDE_W - 200, 30, "Palomitas");
        header->labelfont(FL_BOLD);
        header->labelsize(22);
        header->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
        header->labelcolor(C_TEXT);

        theme_choice = new Fl_Choice(w - 130, 12, 120, 28, "Tema:");
        for (int i = 0; i < theme_count; ++i) theme_choice->add(themes[i].name);
        theme_choice->value(g_theme_index);
        theme_choice->callback(cb_theme, this);

        fav_side_btn = new ModernButton(10, 60, SIDE_W - 20, 40, "Favoritos");
        fav_side_btn->labelsize(11);
        fav_side_btn->callback(cb_show_favs, this);

        fav_count_label = new Label(10, 104, SIDE_W - 20, 18);
        fav_count_label->size = 10;
        fav_count_label->fg_role = 1;
        fav_count_label->al = FL_ALIGN_CENTER;
        fav_count_label->set("0 peliculas");

        back_btn = new ModernButton(w - 290, 12, 90, 28, "Volver");
        back_btn->callback(cb_back, this);
        back_btn->deactivate();

        search_input = new Fl_Input(SIDE_W + 70, 55, w - SIDE_W - 220, 34, "Buscar:");
        search_input->when(FL_WHEN_ENTER_KEY);
        search_input->callback(cb_search, this);

        search_btn = new ModernButton(w - 130, 55, 100, 34, "Buscar");
        search_btn->callback(cb_search, this);

        scroll = new Fl_Scroll(SIDE_W + 10, 100, w - SIDE_W - 20, h - 260);
        scroll->box(FL_FLAT_BOX);
        scroll_spacer = new Fl_Box(scroll->x(), scroll->y(), scroll->w() - 2, 10);
        scroll->end();

        // Cargar imagen de fondo si existe
        {
            std::string img_path = project_dir + "/asset/home.jpeg";
            Fl_Shared_Image* home_img = Fl_Shared_Image::get(img_path.c_str());
            if (home_img && home_img->w() > 0) home_shared_img = home_img;
        }

        int bx = SIDE_W + 20;
        play_btn = new ModernButton(bx, h - 120, 130, 34, "Reproducir");
        play_btn->callback(cb_play, this);
        play_btn->deactivate();

        fav_btn = new ModernButton(bx + 145, h - 120, 170, 34, "Favoritos");
        fav_btn->callback(cb_fav, this);
        fav_btn->deactivate();

        clean_btn = new ModernButton(w - 130, h - 120, 100, 34, "Limpiar");
        clean_btn->callback(cb_clean, this);

        status_label = new Label(20, h - 70, w - 180, 30);
        status_label->font = FL_HELVETICA;
        status_label->size = 13;
        status_label->fg_role = 1;
        status_label->set("Listo. Escribe algo o explora favoritos.");

        github_link = new LinkLabel(w - 150, h - 70, 130, 30, "github/IamJony");
        github_link->on_click = []() {
            std::string cmd = "xdg-open https://github.com/IamJony >/dev/null 2>&1 &";
            std::system(cmd.c_str());
        };

        window->end();
        window->resizable(window);

        g_refresh_cb = [this]() { this->apply_colors(); };
        apply_colors();
        update_fav_count();
    }

    ~PalomitasGUI() { if (g_refresh_cb) g_refresh_cb = nullptr; }

    void show() { window->show(); }

    // Actualiza el status y detiene el parpadeo
    void set_status(const std::string& msg) {
        busy = false; busy_msg.clear();
        status_label->fg_role = 1; status_label->set(msg);
    }

    // Inicia el status parpadeante
    void start_busy(const std::string& msg) {
        busy = true; busy_msg = msg; blink_state = 0;
        status_label->fg_role = 3; status_label->set(msg);
        status_label->redraw();
        window->redraw();
        Fl::check();
        Fl::add_timeout(0.3, blink_timer_cb, this);
    }

    // Detiene el parpadeo y muestra el mensaje final
    void stop_busy(const std::string& msg) {
        busy = false; busy_msg.clear();
        status_label->fg_role = 3;
        status_label->set(msg);
        status_label->redraw();
        window->redraw();
        Fl::check();
    }

    static void blink_timer_cb(void* data) {
        auto* self = (PalomitasGUI*)data;
        if (!self->busy) return;
        self->blink_state ^= 1;
        std::string s = self->busy_msg;
        if (self->blink_state) {
            s += "...";
            self->status_label->fg_role = 3;  // verde
        } else {
            s += "   ";
            self->status_label->fg_role = 2;  // acento
        }
        self->status_label->set(s);
        self->status_label->redraw();
        self->window->redraw();
        Fl::check();  // forzar repintado inmediato
        Fl::add_timeout(0.3, blink_timer_cb, self);
    }

    // Directorio del ejecutable (para localizar search.sh, iframe.sh, embed.sh y asset/)
    std::string get_project_dir() {
        char buf[4096];
        ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
        if (n > 0) {
            buf[n] = '\0';
            std::string p(buf);
            auto pos = p.find_last_of('/');
            if (pos != std::string::npos) return p.substr(0, pos);
        }
        return ".";
    }

    // Crea el BgImage si hay imagen cargada y el scroll ya tiene tamaño
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

    // Recoloca todos los widgets y fuerza redibujado
    void layout(int W, int H) {
        int rx = SIDE_W + 10;
        search_input->resize(SIDE_W + 70, 55, W - SIDE_W - 220, 34);
        search_btn->resize(W - 130, 55, 100, 34);
        theme_choice->resize(W - 130, 12, 120, 28);
        back_btn->resize(W - 290, 12, 90, 28);
        scroll->resize(rx, 100, W - rx - 10, H - 260);
        if (bg_image) bg_image->resize(scroll->x(), scroll->y(), scroll->w(), scroll->h());

        int bx = SIDE_W + 20;
        play_btn->resize(bx, H - 120, 130, 34);
        fav_btn->resize(bx + 145, H - 120, 170, 34);
        clean_btn->resize(W - 130, H - 120, 100, 34);
        status_label->resize(20, H - 70, W - 180, 30);
        github_link->resize(W - 150, H - 70, 130, 30);
        fav_side_btn->resize(10, 60, SIDE_W - 20, 40);
        if (fav_count_label) fav_count_label->resize(10, 104, SIDE_W - 20, 18);

        if (!movies.empty()) rebuild_grid();
        ensure_bg_image();
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

    // Aplica los colores del tema actual a todos los widgets
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
        if (github_link) github_link->redraw();
        for (auto* c : cells) c->redraw();
        scroll->redraw();
        window->redraw();
        Fl::flush();
    }

    void rebuild_grid();
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

    // Actualiza el contador de favoritos
    void update_fav_count() {
        if (!fav_count_label) return;
        int n = (int)Library::instance().favorites.size();
        char buf[32];
        snprintf(buf, sizeof buf, "%d pelicula%s", n, n == 1 ? "" : "s");
        fav_count_label->set(buf);
        fav_count_label->redraw();
    }

    // Guarda el estado actual para el botón Volver
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

    // Añade o quita la película seleccionada de favoritos
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

    // Muestra los favoritos en la grilla
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

    void do_search();
    void do_play();
    void do_clean();
    void play_stream_mplayer(const std::string& stream_url, const std::string& player_url, const std::string& title);
    void download_posters_parallel(const std::vector<size_t>& pending);
};

// Descarga las carátulas en paralelo usando wget (más estable en Tiny Core)
void PalomitasGUI::download_posters_parallel(const std::vector<size_t>& pending) {
    if (pending.empty()) return;
    std::string jobs_file = "/tmp/TEMP_Palomitas/_dl_jobs.txt";
    {
        std::ofstream jf(jobs_file);
        for (size_t idx : pending) {
            const MovieItem& m = movies[idx];
            // Pasamos la URL original y la variante 420x630 como respaldo
            jf << m.id << '\t' << m.img << '\t' << upgrade_image_url(m.img) << '\n';
        }
    }
    
    std::string script = "/tmp/TEMP_Palomitas/_dl_one.sh";
    std::string log_file = "/tmp/TEMP_Palomitas/download.log";
    {
        std::ofstream sf(script);
        sf << "#!/bin/sh\n"
              "line=\"$1\"\n"
              "id=$(echo \"$line\" | cut -f1)\n"
              "orig=$(echo \"$line\" | cut -f2)\n"
              "big=$(echo \"$line\" | cut -f3)\n"
              "out=\"/tmp/TEMP_Palomitas/poster_${id}.img\"\n"
              "ua='Mozilla/5.0 (X11; Linux x86_64; rv:146.0) Gecko/20100101 Firefox/146.0'\n"
              "ref='https://onlypelis.com/'\n"
              "try_one() {\n"
              // SIN 'local' para compatibilidad con ash/BusyBox
              "  url=\"$1\"; label=\"$2\"\n"
              "  rm -f \"$out\"\n"
              // wget con opciones largas y referer en formato header
              "  wget -q --no-check-certificate --timeout=30 --tries=2 -U \"$ua\" --header=\"Referer: $ref\" -O \"$out\" \"$url\" 2>>" + log_file + "\n"
              "  sz=$(stat -c%s \"$out\" 2>/dev/null || echo 0)\n"
              "  if [ \"$sz\" -gt 1000 ]; then \n"
              "      echo \"[DL $id] OK ($sz B) <- $label\" >> " + log_file + "\n"
              "      return 0; \n"
              "  fi\n"
              "  echo \"[DL $id] FALLO: Tamaño $sz, URL: $url\" >> " + log_file + "\n"
              "  return 1\n"
              "}\n"
              // Intentamos la original, si falla probamos la de 420x630
              "try_one \"$orig\" \"orig\" || try_one \"$big\" \"big(420x630)\" || rm -f \"$out\"\n";
    }
    std::system(("chmod +x '" + script + "'").c_str());
    std::system(("rm -f " + log_file).c_str());
    // Ejecutamos en paralelo (8 hilos)
    std::system(("cat '" + jobs_file + "' | xargs -P 8 -n 1 -d '\\n' '" + script + "'").c_str());
}

// Reconstruye la grilla de carátulas
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
    download_posters_parallel(pending);

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
    // El spacer debe ser el último hijo para que Fl_Scroll calcule bien el área total
    scroll->add(scroll_spacer);
    // Resetear el scroll al principio y forzar repintado completo
    scroll->scroll_to(0, 0);
    scroll->redraw();
    scroll->damage(FL_DAMAGE_ALL);
    window->redraw();
    window->damage(FL_DAMAGE_ALL);
    Fl::check();
    Fl::flush();
}

// Busca usando search.sh (que escribe el JSON en /tmp/TEMP_Palomitas/buscar.json)
void PalomitasGUI::do_search() {
    std::string q = search_input->value() ? search_input->value() : "";
    if (q.empty()) { set_status("Escribe un termino de busqueda."); return; }
    if (current_view != V_SEARCH) save_state();
    current_view = V_SEARCH;
    start_busy("Buscando: " + q);
    window->redraw(); Fl::check();
    clear_cells();
    movies.clear();
    selected_index = -1;
    play_btn->deactivate();
    fav_btn->deactivate();
    std::string qq = q; for (auto& c : qq) if (c == '\'') c = ' ';
    std::system("rm -f /tmp/TEMP_Palomitas/buscar.json");
    run_cmd("cd '" + project_dir + "/palomitas-cli' && bash search.sh '" + qq + "' 2>&1");
    std::string json = read_file("/tmp/TEMP_Palomitas/buscar.json");
    fprintf(stderr, "[do_search] json len=%zu\n", json.size());
    if (json.empty()) { stop_busy("No hay resultados o fallo search.sh"); return; }
    movies = parse_search_json(json);
    if (movies.empty()) { stop_busy("No se encontraron resultados."); return; }
    start_busy("Descargando caratulas");
    window->redraw(); Fl::check();
    rebuild_grid();
    // Forzar un repintado completo del área de resultados
    scroll->scroll_to(0, 0);
    scroll->redraw();
    scroll->damage(FL_DAMAGE_ALL);
    window->redraw();
    window->damage(FL_DAMAGE_ALL);
    Fl::check();
    Fl::flush();
    stop_busy(std::to_string(movies.size()) + " resultados para: " + q);
}

// Reproduce un stream con yt-dlp + mplayer usando el referer del player
void PalomitasGUI::play_stream_mplayer(const std::string& stream_url,
                                        const std::string& player_url,
                                        const std::string& title) {
    if (stream_url.empty()) { set_status("URL de stream vacia."); return; }
    std::string referrer;
    std::regex re("^(https?://[^/]+)");
    std::smatch m;
    if (std::regex_search(player_url, m, re)) referrer = m[1].str();
    if (referrer.empty()) { set_status("No se pudo extraer el referrer."); return; }
    std::string which = run_cmd("command -v yt-dlp 2>/dev/null");
    if (which.empty()) { set_status("yt-dlp no encontrado."); return; }
    auto esc_dq = [](std::string s) {
        std::string o;
        for (char c : s) { if (c == '"' || c == '\\' || c == '$' || c == '`') o += '\\'; o += c; }
        return o;
    };
    start_busy("Cargando stream: " + title);
    // Escapar el título para pasarlo a mplayer
    std::string safe_title = title;
    for (auto& c : safe_title) if (c == '"' || c == '\\') c = ' ';
    std::string cmd =
        "yt-dlp --add-header \"Referer: " + esc_dq(referrer) + "\""
        " -S \"res:480\" -o - \"" + esc_dq(stream_url) + "\""
        " 2>/tmp/TEMP_Palomitas/ytdlp.log"
        " | mplayer -demuxer lavf"
        " -title \"" + safe_title + "\""
        " -"
        " >/tmp/TEMP_Palomitas/mplayer.log 2>&1 &";
    std::system(cmd.c_str());
    stop_busy("Reproduciendo: " + title);
}

// Extrae iframe y stream con los scripts, luego reproduce
void PalomitasGUI::do_play() {
    if (selected_index < 0 || selected_index >= (int)movies.size()) {
        set_status("Selecciona una pelicula primero.");
        return;
    }
    const MovieItem& m = movies[selected_index];
    start_busy("Extrayendo iframe: " + m.title);
    window->redraw(); Fl::check();
    run_cmd("cd '" + project_dir + "/palomitas-cli' && bash iframe.sh '" + m.url + "' 2>&1");
    std::string peli = read_file("/tmp/TEMP_Palomitas/peli.json");
    std::string player_url = json_get_str(peli, "embed_url");
    if (player_url.empty()) player_url = json_get_str(peli, "url");
    if (player_url.empty()) { stop_busy("No se pudo obtener el player URL."); return; }
    start_busy("Extrayendo stream...");
    window->redraw(); Fl::check();
    run_cmd("cd '" + project_dir + "/palomitas-cli' && bash embed.sh '" + player_url + "' '" + m.url + "' 2>&1");
    std::string output = read_file("/tmp/TEMP_Palomitas/output.json");
    std::string stream = json_get_str(output, "video");
    if (stream.empty() || stream == "null") { stop_busy("No se pudo extraer el stream."); return; }
    play_stream_mplayer(stream, player_url, m.title);
}

// Limpia la grilla y los temporales
void PalomitasGUI::do_clean() {
    std::system("rm -f /tmp/TEMP_Palomitas/* 2>/dev/null");
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
    set_status("Todo limpio.");
}

// ---------- main ----------
int main(int argc, char** argv) {
    const int win_w = 900;
    const int win_h = 600;
    g_theme_index = load_saved_theme();
    Library::instance().load();
    PalomitasGUI gui(win_w, win_h, "Palomitas FLTK");
    gui.window->size_range(700, 500);   // mínimo 700x500, pero se puede maximizar
    gui.show();
    apply_theme(g_theme_index);
    gui.apply_colors();
    gui.layout(win_w, win_h);
    // Forzar un primer redibujado completo
    Fl::add_timeout(0.1, [](void* g) {
        auto* gui = (PalomitasGUI*)g;
        gui->layout(gui->window->w(), gui->window->h());
    }, &gui);
    return Fl::run();
}
