# Palomitas

Interfaz gráfica y CLI para buscar y reproducir películas en español desde terminal o escritorio, hecha con **FLTK** en C++ y scripts bash para la extracción de streams.

![Palomitas](capturas/ui.png)

## Características

- **GUI en FLTK** con grilla de carátulas, temas, favoritos y búsqueda.
- **CLI en bash** para usar desde terminal.
- Búsqueda por título
- Carátulas en alta resolución (420×630 / 683×1024).
- Reproductor con `mplayer` + `yt-dlp`.
- Favoritos persistentes en `~/.config/palomitas-fltk/data.json`.
