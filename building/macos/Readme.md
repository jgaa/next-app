# macOS-specific

## Icons

macOS supports many image formats, but it cannot use an `.svg` file as an application icon. To work around this, we convert our SVG into a set of PNGs at various resolutions.

Whenever the icon changes, regenerate these files manually:

```sh
brew install librsvg
../../scripts/svg2iconset.sh ../../src/NextAppUi/icons/nextapp.svg
````

Once the `.iconset` directory is created, build the `.icns` file:

```sh
iconutil -c icns nextapp.iconset
```
