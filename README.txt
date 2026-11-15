PIXMO GAMES 1.6.4 - Your game library for the Wii U (Aroma)
==========================================================
(Español: LEEME.txt)

Pixmo Games is a launcher for your installed games. It opens in the "Cinema"
view: the game's art fills the screen, its box is shown big and in 3D, and its
details sit next to it. It has categories with icons or logos, favorites,
recent games, search, background music and three views. The interface is in
English and Spanish (it follows the console language; change it in Settings >
Language).


REQUIREMENTS
------------
- A Wii U with Aroma (https://aroma.foryour.cafe), preferably up to date
  (Aroma Updater).
- Games installed in system memory or on a USB drive.


INSTALL
-------
1. Copy the "wiiu" folder from this zip to the root of the SD card
   (you get SD:/wiiu/apps/pixmogames/pixmogames.wuhb).
2. Start the console with Aroma. "Pixmo Games" shows up in the Wii U Menu.

Optional - open Pixmo Games at power-on: also copy the "wiiu" folder inside
"autostart_plugin_optional" to the root of the SD card (see below).

Pixmo Games shows every installed game: Wii U games, demos, Virtual Console,
and also Wii and GameCube games installed for the Wii U (from the eShop or
injected). Updates and DLC are not listed as separate games: they are shown on
the game's page.

Wii and GameCube games: like the Wii U Menu, Pixmo Games asks how you want to
play: with the Wii U GamePad (if the game supports it), with a Wii Remote on
the TV, or with a Wii Remote on the GamePad screen. Then the console switches
to Wii mode. Next time it offers your last choice first.

Injected GameCube games (UWUVCI) use Nintendont, which must be on the SD card:
SD:/apps/nintendont/boot.dol and its settings file SD:/nincfg.bin (created when
you open Nintendont once from the Homebrew Channel and save its settings).
Without them you get "boot.dol not found" or "nincfg.bin not found". This is
not Pixmo Games: it happens from the Wii U Menu too.

Aroma homebrew: .wuhb bundles on the SD card (in SD:/wiiu/apps/ or a folder
inside it) are listed like games and open from Pixmo Games just like from the
Wii U Menu. Tools (updaters, installers, homebrew stores) are hidden: show
them in Settings > Hidden games.


OPEN AT POWER-ON AND RETURN AFTER CLOSING A GAME (optional)
----------------------------------------------------------
Aroma has no such option: the "Pixmo Games Autostart" plugin adds it. It is
the file pixmo_inicio.wps, which goes in SD:/wiiu/environments/aroma/plugins/.
- When the console starts, Pixmo Games opens as soon as the Wii U Menu shows.
- When you close a game started from Pixmo Games with HOME > Close software,
  the console passes briefly through the Wii U Menu and returns to Pixmo
  Games. Wii and GameCube games do the same when you leave Wii mode.
- To stay in the Wii U Menu: hold B on the GamePad while the menu appears. If
  you leave Pixmo Games with + > Exit to Wii U Menu, you stay in the menu.
- Each part can be turned on or off in Settings: "Open Pixmo Games at
  power-on" and "Return here after closing a game".
- In Aroma's boot menu (hold + at power-on) the autoboot choice must be
  "Wii U Menu", which is the default.
- The plugin works while Aroma is loaded. If your console starts in the normal
  Wii U Menu and you load Aroma by opening "Health and Safety Information",
  Pixmo Games opens right after Aroma loads. Leaving a Wii game restarts the
  console: open Aroma again and Pixmo Games opens by itself.
- To remove it, delete pixmo_inicio.wps from the SD card.
- If something does not work, the plugin writes what it did to
  SD:/wiiu/apps/pixmogames/inicio_log.txt.


CONTROLS (GamePad, Pro Controller, Wii Remote or the touch screen)
------------------------------------------------------------------
  A            Play
  X            Game options: categories, favorite, hide, information
  Y            Add to / remove from Favorites
  Left/Right   Previous / next game
  L / R        Previous / next category
  ZL / ZR      Jump to the previous / next letter
  Up           Go to the category bar
  -            Search (console keyboard)
  +            Settings
  B            Back / clear search / exit to the Wii U Menu
  Touch        Tap a cover in the carousel to select it and again (or the big
               box) to play; swipe to change games; tap tabs and buttons.


VIEWS (+ > View)
----------------
  Cinema      (default) full-screen game art, 3D box, game details and a
              cover carousel at the bottom.
  Showcase    cover + details on a light Wii U-style background, icon carousel.
  Grid        all icons in a grid with the details on the right.


CATEGORIES
----------
- Shooters, Adventure, Racing, Fighting and Arcade come empty: fill, delete or
  rename them.
- Add games: X on the game > Categories > check the ones you want. A game can
  be in several categories.
- Create one: + > Manage categories > New category (or X on a game >
  Categories > New category...).
- Each category has an icon (28 to choose from), a color and, if you want, its
  own logo: copy PNG or JPG images to SD:/wiiu/apps/pixmogames/logos/ and pick
  them in the category > Custom logo.
- In Manage categories: A edit, Y new, X delete, L/R reorder.


COVERS AND ART
--------------
- With internet, Pixmo Games downloads covers from GameTDB (can be turned off
  in Settings). "Downloading covers..." shows at the top while it works.
- Your own covers: SD:/wiiu/apps/pixmogames/covers/ named with the GameTDB ID
  (e.g. ARPE01.jpg) or the 16-digit Title ID (e.g. 0005000010101C00.png).
- Background art for the Cinema view: fondos/<TITLE ID>.jpg (1280x720). Games
  without one use their boot image from the console.
- Title logo: titulos/<TITLE ID>.png (with transparency). Without one, the
  name is written instead.
- Each game's Title ID is in X > Game information.
- Carousel covers load only once: small copies are kept in miniaturas.bin.


MUSIC
-----
Without your own songs, soft ambient music plays. To use yours, convert them
with tools/musica_consola.py (in the source code; needs ffmpeg) and copy the
.wav files to SD:/wiiu/apps/pixmogames/musica/. They play shuffled and each
song's name shows when it starts. Turn music off in Settings > Background music.


LAST PLAYED AND STATISTICS
--------------------------
"Last played" uses the most recent of: when you started the game from Pixmo
Games, and when the game last saved (so games opened from the Wii U Menu count
too). The Recent tab is sorted by that date.


HIDING GAMES
------------
Crossed-out eye button on the game's page, or X > Hide this game. To see them
again: Settings > Hidden games, or Settings > Show hidden games.


FILES ON THE SD CARD (SD:/wiiu/apps/pixmogames/)
------------------------------------------------
  config.txt      settings, categories, favorites and statistics
  cache.txt       game names (for a faster start; can be deleted)
  covers/         covers             fondos/   background art
  titulos/        title logos        logos/    category logos
  miniaturas.bin  small covers (rebuilt if deleted)
  musica/         songs (.wav IMA ADPCM)
  log.txt         log of the last session (log_anterior.txt: the one before)
  volver.txt      note for the plugin: "come back here when the game closes"
  inicio_log.txt  what the plugin did
To start from scratch, delete config.txt and cache.txt.


TROUBLESHOOTING
---------------
- If the app did not close properly, it starts in "safe mode" (no downloads)
  the next time, and back to normal the time after.
- Games without an icon (just a letter): the app reads icons with the access
  Aroma provides. Without it, games still show up and can be played.
- Odd or outdated names: + > Reload library.
- log.txt and log_anterior.txt tell what the app was doing.


LICENSE
-------
Code under the MIT license (LICENSE.txt). Third-party components and their
licenses: THIRD_PARTY_NOTICES.txt. Pixmo Games is not affiliated with Nintendo.
