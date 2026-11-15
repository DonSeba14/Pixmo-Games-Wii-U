# Same base images as wiiu-env/homebrew_on_menu_plugin (WUPS + librpxloader)
FROM ghcr.io/wiiu-env/devkitppc:20260225

COPY --from=ghcr.io/wiiu-env/wiiupluginsystem:20260418 /artifacts $DEVKITPRO
COPY --from=ghcr.io/wiiu-env/librpxloader:20260329 /artifacts $DEVKITPRO

WORKDIR project
