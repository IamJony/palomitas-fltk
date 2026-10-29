#include "library.h"
#include "cJSON.h"
#include <fstream>
#include <sys/stat.h>
#include <unistd.h>
#include <cstdlib>
#include <algorithm>

Library& Library::instance() { static Library lib; return lib; }

static std::string data_path() {
    const char* home = getenv("HOME");
    std::string dir = std::string(home ? home : "/tmp") + "/.config/palomitas-fltk";
    mkdir(dir.c_str(), 0700);
    return dir + "/data.json";
}
static MovieItem item_from_json(cJSON* o) {
    MovieItem it; cJSON* v;
    if ((v = cJSON_GetObjectItemCaseSensitive(o, "id")) && cJSON_IsString(v)) it.id = v->valuestring;
    if ((v = cJSON_GetObjectItemCaseSensitive(o, "title")) && cJSON_IsString(v)) it.title = v->valuestring;
    if ((v = cJSON_GetObjectItemCaseSensitive(o, "url")) && cJSON_IsString(v)) it.url = v->valuestring;
    if ((v = cJSON_GetObjectItemCaseSensitive(o, "img")) && cJSON_IsString(v)) it.img = v->valuestring;
    if ((v = cJSON_GetObjectItemCaseSensitive(o, "year")) && cJSON_IsString(v)) it.year = v->valuestring;
    if ((v = cJSON_GetObjectItemCaseSensitive(o, "imdb")) && cJSON_IsString(v)) it.imdb = v->valuestring;
    return it;
}
static cJSON* item_to_json(const MovieItem& it) {
    cJSON* o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "id", it.id.c_str());
    cJSON_AddStringToObject(o, "title", it.title.c_str());
    cJSON_AddStringToObject(o, "url", it.url.c_str());
    cJSON_AddStringToObject(o, "img", it.img.c_str());
    cJSON_AddStringToObject(o, "year", it.year.c_str());
    cJSON_AddStringToObject(o, "imdb", it.imdb.c_str());
    return o;
}
void Library::load() {
    favorites.clear(); playlists.clear();
    std::ifstream f(data_path());
    if (!f.is_open()) return;
    std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    cJSON* root = cJSON_Parse(content.c_str());
    if (!root) return;
    if (!cJSON_IsObject(root)) { cJSON_Delete(root); return; }
    cJSON* favs = cJSON_GetObjectItemCaseSensitive(root, "favorites");
    cJSON* item;
    cJSON_ArrayForEach(item, favs) { MovieItem it = item_from_json(item); if (!it.title.empty()) favorites.push_back(it); }
    cJSON* pls = cJSON_GetObjectItemCaseSensitive(root, "playlists");
    cJSON_ArrayForEach(item, pls) {
        if (!item->string) continue;
        std::string name = item->string;
        bool exists = false;
        for (auto& pl : playlists) if (pl.first == name) exists = true;
        if (exists) continue;
        std::vector<MovieItem> items;
        cJSON* sub;
        cJSON_ArrayForEach(sub, item) { MovieItem it = item_from_json(sub); if (!it.title.empty()) items.push_back(it); }
        playlists.push_back({name, items});
    }
    cJSON_Delete(root);
}
void Library::save() {
    cJSON* root = cJSON_CreateObject();
    cJSON* favs = cJSON_CreateArray();
    for (auto& it : favorites) cJSON_AddItemToArray(favs, item_to_json(it));
    cJSON_AddItemToObject(root, "favorites", favs);
    cJSON* pls = cJSON_CreateObject();
    for (auto& pl : playlists) {
        cJSON* arr = cJSON_CreateArray();
        for (auto& it : pl.second) cJSON_AddItemToArray(arr, item_to_json(it));
        cJSON_AddItemToObject(pls, pl.first.c_str(), arr);
    }
    cJSON_AddItemToObject(root, "playlists", pls);
    char* str = cJSON_PrintUnformatted(root);
    std::ofstream f(data_path()); f << str; f.close();
    free(str); cJSON_Delete(root);
}
void Library::add_favorite(const MovieItem& it) { if (!is_favorite(it.id)) { favorites.push_back(it); save(); } }
void Library::remove_favorite(const std::string& id) {
    favorites.erase(std::remove_if(favorites.begin(), favorites.end(),
        [&](const MovieItem& m){ return m.id == id; }), favorites.end()); save();
}
bool Library::is_favorite(const std::string& id) const {
    for (auto& m : favorites) if (m.id == id) return true;
    return false;
}
void Library::create_playlist(const std::string& name) {
    for (auto& pl : playlists) if (pl.first == name) return;
    playlists.push_back({name, {}}); save();
}
void Library::rename_playlist(const std::string& old_name, const std::string& new_name) {
    for (auto& pl : playlists) if (pl.first == old_name) { pl.first = new_name; save(); return; }
}
void Library::remove_playlist(const std::string& name) {
    playlists.erase(std::remove_if(playlists.begin(), playlists.end(),
        [&](const std::pair<std::string, std::vector<MovieItem>>& p) {
            return p.first == name;
        }), playlists.end());
    save();
}
void Library::add_to_playlist(const std::string& name, const MovieItem& it) {
    for (auto& pl : playlists) if (pl.first == name) {
        for (auto& x : pl.second) if (x.id == it.id) return;
        pl.second.push_back(it); save(); return;
    }
    playlists.push_back({name, {it}}); save();
}
void Library::remove_from_playlist(const std::string& name, const std::string& id) {
    for (auto& pl : playlists) if (pl.first == name) {
        pl.second.erase(std::remove_if(pl.second.begin(), pl.second.end(),
            [&](const MovieItem& m){ return m.id == id; }), pl.second.end()); save(); return;
    }
}
