#ifndef PALOMITAS_LIBRARY_H
#define PALOMITAS_LIBRARY_H
#include <string>
#include <vector>

struct MovieItem {
    std::string id, title, url, img, year, imdb;
};

struct Library {
    std::vector<MovieItem> favorites;
    std::vector<std::pair<std::string, std::vector<MovieItem>>> playlists;
    static Library& instance();
    void load();
    void save();
    void add_favorite(const MovieItem& it);
    void remove_favorite(const std::string& id);
    bool is_favorite(const std::string& id) const;
    void create_playlist(const std::string& name);
    void rename_playlist(const std::string& old_name, const std::string& new_name);
    void remove_playlist(const std::string& name);
    void add_to_playlist(const std::string& name, const MovieItem& it);
    void remove_from_playlist(const std::string& name, const std::string& id);
};
#endif
