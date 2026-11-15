// Traducción de la interfaz al inglés (ver i18n.h).
#include "i18n.h"

#include "common.h"
#include "platform.h"

#include <string.h>

// Spanish UI text -> English. tools/check_i18n.py checks that every Spanish
// string in src/ is here and that the %-formats match.
static const struct {
    const char *es, *en;
} k_en[] = {
    // icon names (category editor)
    { "Mando", "Controller" },
    { "Disparos", "Shooters" },
    { "Aventura", "Adventure" },
    { "Carreras", "Racing" },
    { "Puzles", "Puzzles" },
    { "Estrella", "Star" },
    { "Corazón", "Heart" },
    { "Música", "Music" },
    { "Deportes", "Sports" },
    { "Acción", "Action" },
    { "Terror", "Horror" },
    { "Ciencia ficción", "Sci-fi" },
    { "Trofeo", "Trophy" },
    { "Corona", "Crown" },
    { "Multijugador", "Multiplayer" },
    { "Rol", "RPG" },
    { "Familia", "Family" },
    { "Joya", "Gem" },
    { "Fuego", "Fire" },
    { "Espacio", "Space" },
    { "Fiesta", "Party" },
    { "Plataformas", "Platformer" },
    { "Exploración", "Exploration" },
    { "Historia", "Story" },
    { "Reloj", "Clock" },
    { "Colección", "Collection" },
    { "Competición", "Competition" },
    // library
    { "Peleas", "Fighting" },
    { "Iconos de la consola desactivados: Pixmo Games se colgó leyéndolos (ver Ajustes)",
      "Console icons turned off: Pixmo Games froze while reading them (see Settings)" },
    { "Modo seguro: Pixmo Games no se cerró bien la última vez (sin descargas)",
      "Safe mode: Pixmo Games did not close properly last time (no downloads)" },
    { "Título %08X", "Title %08X" },
    { "Sin conexión a internet: se usan carátulas generadas", "No internet connection: using generated covers" },
    { "Homebrew de Aroma", "Aroma homebrew" },
    { "Consola Virtual", "Virtual Console" },
    { "Disco USB", "USB drive" },
    { "Disco óptico", "Disc" },
    { "Memoria del sistema", "System memory" },
    // start screen, PC test keyboard
    { "Intro = aceptar · Esc = cancelar (teclado de pruebas de PC)", "Enter = OK · Esc = cancel (PC test keyboard)" },
    // Cine view
    { "Juego", "Game" },
    { "VALORACIÓN", "RATING" },
    { "Oculto", "Hidden" },
    { "Juego de %s · modo Wii", "%s game · Wii mode" },
    { "Homebrew de Aroma (SD)", "Aroma homebrew (SD)" },
    { "Consola Virtual · %s", "Virtual Console · %s" },
    { "AÑO", "YEAR" },
    { "JUGADORES", "PLAYERS" },
    { "CÓDIGO", "CODE" },
    { "Jugar", "Play" },
    { "Opciones", "Options" },
    { "Quitar favorito", "Unfavorite" },
    { "Favorito", "Favorite" },
    { "Última vez: %s  ·  1 vez desde Pixmo Games", "Last played: %s  ·  launched once from Pixmo Games" },
    { "Última vez: %s  ·  %u veces desde Pixmo Games", "Last played: %s  ·  launched %u times from Pixmo Games" },
    { "Última vez: %s", "Last played: %s" },
    { "Todavía no lo juegas desde Pixmo Games", "Not played from Pixmo Games yet" },
    { "Descargando carátulas...", "Downloading covers..." },
    { "Leyendo juegos...", "Reading games..." },
    { "Categoría", "Category" },
    { "Buscar", "Search" },
    { "Ajustes", "Settings" },
    { "«%s»", "\"%s\"" },
    { "NUEVO", "NEW" },
    // home
    { "Todos", "All" },
    { "Recientes", "Recent" },
    { "Favoritos", "Favorites" },
    { "El juego vuelve a mostrarse", "The game is shown again" },
    { "Juego oculto", "Game hidden" },
    { "Juego oculto · lo recuperas en Ajustes > Juegos ocultos",
      "Game hidden · bring it back in Settings > Hidden games" },
    { "Añadido a Favoritos", "Added to Favorites" },
    { "Quitado de Favoritos", "Removed from Favorites" },
    { "Buscar juego por nombre o editor", "Search games by name or publisher" },
    { "No se pudo iniciar el juego", "The game could not be started" },
    { "DLC instalado", "DLC installed" },
    { "Última vez", "Last played" },
    { "Aún no desde aquí", "Not from here yet" },
    { "1 vez", "Once" },
    { "%u veces", "%u times" },
    { "Iniciado", "Launched" },
    { "Ubicación", "Location" },
    { "Actualización v%u", "Update v%u" },
    { "Sin actualizaciones", "No updates" },
    { "Versión", "Version" },
    { "Mandos", "Controllers" },
    { "Código", "Code" },
    { "No se encontraron juegos instalados", "No installed games found" },
    { "Pixmo Games muestra los juegos de la memoria del sistema y del disco USB.",
      "Pixmo Games shows the games in system memory and on the USB drive." },
    { "Ningún juego coincide con la búsqueda", "No games match your search" },
    { "Pulsa B para quitar la búsqueda.", "Press B to clear the search." },
    { "Aún no tienes favoritos", "No favorites yet" },
    { "Pulsa Y sobre un juego para marcarlo como favorito.", "Press Y on a game to mark it as a favorite." },
    { "Todavía no hay juegos recientes", "No recent games yet" },
    { "Los juegos que inicies aparecerán aquí.", "Games you start will show up here." },
    { "Esta categoría está vacía", "This category is empty" },
    { "Pulsa X sobre un juego y elige «Categorías» para añadirlo.",
      "Press X on a game and choose \"Categories\" to add it." },
    { "Cargando biblioteca...", "Loading library..." },
    { "%s  ·  1 juego", "%s  ·  1 game" },
    { "%s  ·  %d juegos", "%s  ·  %d games" },
    { "Todos los juegos", "All games" },
    { "Cambiar categoría", "Change category" },
    { "Ver juegos", "View games" },
    { "Opciones del juego", "Game options" },
    { "1 juego", "1 game" },
    { "%d juegos", "%d games" },
    { "Iniciando...", "Starting..." },
    // settings
    { "Vitrina", "Showcase" },
    { "Cuadrícula", "Grid" },
    { "Cine", "Cinema" },
    { "Nombre", "Name" },
    { "Última vez jugado", "Last played" },
    { "Más jugados", "Most played" },
    { "Editor", "Publisher" },
    { "Claro", "Light" },
    { "Oscuro", "Dark" },
    { "La última pestaña", "The last tab" },
    { "Automático", "Automatic" },
    { "Idioma", "Language" },
    { "Todas las regiones", "All regions" },
    { "Japón", "Japan" },
    { "América", "Americas" },
    { "Europa", "Europe" },
    { "Corea", "Korea" },
    { "Taiwán", "Taiwan" },
    { "Vista", "View" },
    { "Ordenar juegos por", "Sort games by" },
    { "Tema", "Theme" },
    { "Al abrir, mostrar", "On start, show" },
    { "Fondo con el arte del juego", "Game art background" },
    { "Descargar carátulas de GameTDB", "Download covers from GameTDB" },
    { "Leer iconos de la consola", "Read icons from the console" },
    { "Mostrar juegos ocultos", "Show hidden games" },
    { "Juegos ocultos", "Hidden games" },
    { "Efectos de sonido", "Sound effects" },
    { "Música ambiental", "Background music" },
    { "Mostrar reloj", "Show clock" },
    { "Abrir Pixmo Games al encender", "Open Pixmo Games at power-on" },
    { "Volver aquí al cerrar un juego", "Return here after closing a game" },
    { "Plugin de inicio (pixmo_inicio.wps)", "Startup plugin (pixmo_inicio.wps)" },
    { "No instalado: ver LEEME", "Not installed: see README" },
    { "Gestionar categorías", "Manage categories" },
    { "Volver a leer la biblioteca", "Reload library" },
    { "Controles", "Controls" },
    { "Acerca de Pixmo Games", "About Pixmo Games" },
    { "Salir al menú Wii U", "Exit to Wii U Menu" },
    // game options, categories
    { "Volver a mostrar este juego", "Show this game again" },
    { "Ocultar este juego", "Hide this game" },
    { "Ninguna", "None" },
    { "1 categoría", "1 category" },
    { "%d categorías", "%d categories" },
    { "Categorías", "Categories" },
    { "Quitar de Favoritos", "Remove from Favorites" },
    { "Añadir a Favoritos", "Add to Favorites" },
    { "Buscar la carátula otra vez", "Look for the cover again" },
    { "Reiniciar estadísticas", "Reset statistics" },
    { "Información del juego", "Game information" },
    { "Nueva categoría...", "New category..." },
    { "Crea categorías como «Juegos de disparos» y dales un logo",
      "Create categories like \"Shooting games\" and give them a logo" },
    { "Editar categoría", "Edit category" },
    { "Icono", "Icon" },
    { "Logo personalizado", "Custom logo" },
    { "Ninguno", "None" },
    { "Eliminar categoría", "Delete category" },
    { "Elige un icono", "Choose an icon" },
    { "Elige un color", "Choose a color" },
    { "Imágenes PNG/JPG de SD:/wiiu/apps/pixmogames/logos/", "PNG/JPG images from SD:/wiiu/apps/pixmogames/logos/" },
    { "Sin logo (usar el icono)", "No logo (use the icon)" },
    { "No hay imágenes todavía.", "No images yet." },
    { "Copia archivos PNG a la carpeta logos de la SD", "Copy PNG files to the logos folder on the SD card" },
    { "¿Salir de Pixmo Games?", "Exit Pixmo Games?" },
    { "Volverás al menú Wii U.", "You will return to the Wii U Menu." },
    { "Cancelar", "Cancel" },
    { "¿Eliminar «%s»?", "Delete \"%s\"?" },
    { "Los juegos no se borran, solo la categoría.", "The games are not deleted, only the category." },
    { "¿Reiniciar estadísticas?", "Reset statistics?" },
    { "Se borran las veces jugado y la última fecha guardada por Pixmo Games.",
      "Clears the play count and the last played date saved by Pixmo Games." },
    { "Reiniciar", "Reset" },
    { "Desmarca un juego para que vuelva a aparecer", "Uncheck a game to show it again" },
    { "y %d más (se ven con «Mostrar juegos ocultos»)", "and %d more (visible with \"Show hidden games\")" },
    { "No hay juegos ocultos.", "No hidden games." },
    { "Para ocultar uno: el botón del ojo en su ficha, o X > Ocultar este juego",
      "To hide one: the eye button on its page, or X > Hide this game" },
    { "¿Cómo quieres jugar?", "How do you want to play?" },
    { "Juego de %s · se inicia en modo Wii", "%s game · starts in Wii mode" },
    { "Con el Wii U GamePad", "With the Wii U GamePad" },
    { "control y pantalla", "controls and screen" },
    { "Con Wiimote, en la TV", "With a Wii Remote, on the TV" },
    { "Con Wiimote, en el GamePad", "With a Wii Remote, on the GamePad" },
    { "solo la pantalla", "screen only" },
    // game information
    { "Código de producto", "Product code" },
    { "%s (lo usan varios juegos)", "%s (shared by several games)" },
    { "ID de GameTDB", "GameTDB ID" },
    { "Región", "Region" },
    { "v%u instalada", "v%u installed" },
    { "No instalada", "Not installed" },
    { "Actualización", "Update" },
    { "Contenido descargable", "Downloadable content" },
    { "Instalado", "Installed" },
    { ", Wiimote + Nunchuk", ", Wii Remote + Nunchuk" },
    { ", Mando Clásico", ", Classic Controller" },
    { "Mandos compatibles", "Supported controllers" },
    { "Carátula", "Cover" },
    { "Descargada de GameTDB", "Downloaded from GameTDB" },
    { "Imagen de la SD", "Image from the SD card" },
    { "Generada con el arte del juego", "Made from the game's art" },
    { "Último guardado", "Last saved" },
    // about, controls
    { "Tu biblioteca de juegos de Wii U", "Your Wii U game library" },
    { "Juegos instalados", "Installed games" },
    { "Datos", "Data" },
    { "Carátulas propias", "Your own covers" },
    { "covers/ID.jpg  (ej. ARPE01.jpg)", "covers/ID.jpg  (e.g. ARPE01.jpg)" },
    { "Logos de categorías", "Category logos" },
    { "Carátulas en línea", "Online covers" },
    { "Hecho con", "Made with" },
    { "SDL2, wut y Aroma", "SDL2, wut and Aroma" },
    { "Jugar al juego seleccionado", "Play the selected game" },
    { "Opciones del juego: categorías, ocultar...", "Game options: categories, hide..." },
    { "Marcar o quitar de Favoritos", "Add to or remove from Favorites" },
    { "Categoría anterior / siguiente (con R)", "Previous / next category (with R)" },
    { "Saltar a la letra anterior / siguiente (con ZR)", "Jump to the previous / next letter (with ZR)" },
    { "Ir a la barra de categorías", "Go to the category bar" },
    { "Buscar por nombre o editor", "Search by name or publisher" },
    { "Atrás / quitar búsqueda / salir", "Back / clear search / exit" },
    { "Pantalla táctil: toca un juego para elegirlo y otra vez para jugar",
      "Touch screen: tap a game to select it, tap again to play" },
    // toasts, keyboard
    { "No se pueden crear más categorías", "No more categories can be created" },
    { "Categoría creada", "Category created" },
    { "Leyendo la biblioteca de nuevo...", "Reloading the library..." },
    { "Buscando carátula...", "Looking for the cover..." },
    { "Nombre de la categoría (ej. Juegos de disparos)", "Category name (e.g. Shooting games)" },
    { "Crear", "Create" },
    { "Nombre de la categoría", "Category name" },
    { "Aceptar", "OK" },
    { "Categoría eliminada", "Category deleted" },
    { "Estadísticas reiniciadas", "Statistics reset" },
    // button hints
    { "Atrás", "Back" },
    { "Editar", "Edit" },
    { "Nueva", "New" },
    { "Borrar", "Delete" },
    { "Mover", "Move" },
    { "Cambiar", "Change" },
    // dates (util.c)
    { "Nunca", "Never" },
    { "Hace un momento", "Just now" },
    { "Hace %d minutos", "%d minutes ago" },
    { "Hoy, %02d:%02d", "Today, %02d:%02d" },
    { "Ayer, %02d:%02d", "Yesterday, %02d:%02d" },
    { "Hace %d días", "%d days ago" },
    { "Hace 1 semana", "1 week ago" },
    { "Hace %d semanas", "%d weeks ago" },
    { "Hace 1 mes", "1 month ago" },
    { "Hace %d meses", "%d months ago" },
};

#define NEN ((int)(sizeof(k_en) / sizeof(k_en[0])))
#define HSIZE 1024 // power of two, well above 2 * NEN

static bool s_english;
static short s_hash[HSIZE]; // index + 1 into k_en, 0 = empty
static bool s_ready;

static uint32_t fnv(const char *s)
{
    uint32_t h = 2166136261u;
    for (; *s; s++)
        h = (h ^ (uint8_t)*s) * 16777619u;
    return h;
}

// built once from the main thread (i18n_set) before any worker thread calls tr()
static void build(void)
{
    for (int i = 0; i < NEN; i++) {
        uint32_t h = fnv(k_en[i].es) & (HSIZE - 1);
        while (s_hash[h])
            h = (h + 1) & (HSIZE - 1);
        s_hash[h] = (short)(i + 1);
    }
    s_ready = true;
}

void i18n_set(int setting)
{
    if (!s_ready)
        build();
    if (setting == LANG_ES)
        s_english = false;
    else if (setting == LANG_EN)
        s_english = true;
    else
        s_english = plat_language() != 5; // Wii U language 5 = Spanish
}

bool i18n_english(void)
{
    return s_english;
}

const char *tr(const char *es)
{
    if (!s_english || !s_ready || !es || !es[0])
        return es;
    for (uint32_t h = fnv(es) & (HSIZE - 1); s_hash[h]; h = (h + 1) & (HSIZE - 1)) {
        const char *k = k_en[s_hash[h] - 1].es;
        if (k == es || !strcmp(k, es))
            return k_en[s_hash[h] - 1].en;
    }
    return es;
}
