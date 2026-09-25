#include "MaterialDesignStyling.h"
#include "NextAppCore.h"

MaterialDesignStyling *MaterialDesignStyling::instance_ = nullptr;

namespace {

MaterialDesignStyling::ColorTheme lightTheme()
{
    MaterialDesignStyling::ColorTheme theme;
    theme.primary = "#65558F";
    theme.onPrimary = "#FFFFFF";
    theme.primaryContainer = "#EADDFF";
    theme.onPrimaryContainer = "#21005D";
    theme.primaryFixed = "#EADDFF";
    theme.onPrimaryFixed = "#21005D";
    theme.primaryFixedDim = "#D0BCFF";
    theme.onPrimaryFixedVariant = "#4F378B";

    theme.secondary = "#625B71";
    theme.onSecondary = "#FFFFFF";
    theme.secondaryContainer = "#E8DEF8";
    theme.onSecondaryContainer = "#1D192B";
    theme.secondaryFixed = "#E8DEF8";
    theme.onSecondaryFixed = "#1D192B";
    theme.secondaryFixedDim = "#CCC2DC";
    theme.onSecondaryFixedVariant = "#4A4458";

    theme.tertiary = "#476879";
    theme.onTertiary = "#FFFFFF";
    theme.tertiaryContainer = "#D2E5F0";
    theme.onTertiaryContainer = "#234152";
    theme.tertiaryFixed = "#D2E5F0";
    theme.onTertiaryFixed = "#234152";
    theme.tertiaryFixedDim = "#ADCADA";
    theme.onTertiaryFixedVariant = "#2D4F60";

    theme.error = "#B3261E";
    theme.onError = "#FFFFFF";
    theme.errorContainer = "#F9DEDC";
    theme.onErrorContainer = "#410E0B";

    theme.surfaceDim = "#DED8E1";
    theme.surface = "#FEF7FF";
    theme.surfaceBright = "#FEF7FF";

    theme.surfaceContainerLowest = "#FFFFFF";
    theme.surfaceContainerLow = "#F7F2FA";
    theme.surfaceContainer = "#F3EDF7";
    theme.surfaceContainerHigh = "#ECE6F0";
    theme.surfaceContainerHighest = "#E6E0E9";

    theme.onSurface = "#1D1B20";
    theme.onSurfaceVariant = "#49454F";
    theme.outline = "#79747E";
    theme.outlineVariant = "#CAC4D0";

    theme.inverseSurface = "#322F35";
    theme.inverseOnSurface = "#F5EFF7";
    theme.inversePrimary = "#D0BCFF";
    theme.scrim = "#000000";
    theme.shadow = "#000000";
    return theme;
}

MaterialDesignStyling::ColorTheme darkTheme()
{
    MaterialDesignStyling::ColorTheme theme;
    theme.primary = "#D0BCFE";
    theme.onPrimary = "#381E72";
    theme.primaryContainer = "#4F378B";
    theme.onPrimaryContainer = "#EADDFF";
    theme.primaryFixed = "#EADDFF";
    theme.onPrimaryFixed = "#21005D";
    theme.primaryFixedDim = "#D0BCFF";
    theme.onPrimaryFixedVariant = "#4F378B";

    theme.secondary = "#CCC2DC";
    theme.onSecondary = "#332D41";
    theme.secondaryContainer = "#4A4458";
    theme.onSecondaryContainer = "#E8DEF8";
    theme.secondaryFixed = "#E8DEF8";
    theme.onSecondaryFixed = "#1D192B";
    theme.secondaryFixedDim = "#CCC2DC";
    theme.onSecondaryFixedVariant = "#4A4458";

    theme.tertiary = "#B8CCD8";
    theme.onTertiary = "#263746";
    theme.tertiaryContainer = "#3C5363";
    theme.onTertiaryContainer = "#DDE7ED";
    theme.tertiaryFixed = "#DDE7ED";
    theme.onTertiaryFixed = "#263746";
    theme.tertiaryFixedDim = "#B8CCD8";
    theme.onTertiaryFixedVariant = "#3C5363";

    theme.error = "#F2B8B5";
    theme.onError = "#601410";
    theme.errorContainer = "#8C1D18";
    theme.onErrorContainer = "#F9DEDC";

    theme.surfaceDim = "#141218";
    theme.surface = "#141218";
    theme.surfaceBright = "#3B383E";

    theme.surfaceContainerLowest = "#0F0D13";
    theme.surfaceContainerLow = "#1D1B20";
    theme.surfaceContainer = "#211F26";
    theme.surfaceContainerHigh = "#2B2930";
    theme.surfaceContainerHighest = "#36343B";

    theme.onSurface = "#E6E0E9";
    theme.onSurfaceVariant = "#CAC4D0";
    theme.outline = "#938F99";
    theme.outlineVariant = "#49454F";

    theme.inverseSurface = "#E6E0E9";
    theme.inverseOnSurface = "#322F35";
    theme.inversePrimary = "#6750A4";
    theme.scrim = "#000000";
    theme.shadow = "#000000";
    return theme;
}

MaterialDesignStyling::ColorTheme coffeeTheme()
{
    MaterialDesignStyling::ColorTheme theme;
    theme.primary = "#795335";
    theme.onPrimary = "#FFFFFF";
    theme.primaryContainer = "#F5DFC5";
    theme.onPrimaryContainer = "#2D190C";
    theme.primaryFixed = "#F5DFC5";
    theme.onPrimaryFixed = "#2D190C";
    theme.primaryFixedDim = "#E5BB91";
    theme.onPrimaryFixedVariant = "#5D3D24";

    theme.secondary = "#71604C";
    theme.onSecondary = "#FFFFFF";
    theme.secondaryContainer = "#EEDFCB";
    theme.onSecondaryContainer = "#281E12";
    theme.secondaryFixed = "#EEDFCB";
    theme.onSecondaryFixed = "#281E12";
    theme.secondaryFixedDim = "#D4C2A9";
    theme.onSecondaryFixedVariant = "#554532";

    theme.tertiary = "#95513D";
    theme.onTertiary = "#FFFFFF";
    theme.tertiaryContainer = "#FFDACE";
    theme.onTertiaryContainer = "#37150C";
    theme.tertiaryFixed = "#FFDACE";
    theme.onTertiaryFixed = "#37150C";
    theme.tertiaryFixedDim = "#EDB39E";
    theme.onTertiaryFixedVariant = "#743B2A";

    theme.error = "#B3261E";
    theme.onError = "#FFFFFF";
    theme.errorContainer = "#F9DEDC";
    theme.onErrorContainer = "#410E0B";

    theme.surfaceDim = "#E2D5C7";
    theme.surface = "#FFF8EF";
    theme.surfaceBright = "#FFF8EF";

    theme.surfaceContainerLowest = "#FFFCF7";
    theme.surfaceContainerLow = "#FAF0E4";
    theme.surfaceContainer = "#F4E9DC";
    theme.surfaceContainerHigh = "#EEDFD0";
    theme.surfaceContainerHighest = "#E8D8C7";

    theme.onSurface = "#2D221B";
    theme.onSurfaceVariant = "#5B4B3E";
    theme.outline = "#8A7766";
    theme.outlineVariant = "#D5C3B1";

    theme.inverseSurface = "#392D24";
    theme.inverseOnSurface = "#FAEFE2";
    theme.inversePrimary = "#E5BB91";
    theme.scrim = "#000000";
    theme.shadow = "#000000";
    return theme;
}

MaterialDesignStyling::ColorTheme lateNightCoffeeTheme()
{
    // Keep Coffee's fixed accents consistent between its light and dark variants.
    auto theme = coffeeTheme();
    theme.primary = "#E5BB91";
    theme.onPrimary = "#452B16";
    theme.primaryContainer = "#5D3D24";
    theme.onPrimaryContainer = "#F5DFC5";

    theme.secondary = "#D4C2A9";
    theme.onSecondary = "#3D2F20";
    theme.secondaryContainer = "#554532";
    theme.onSecondaryContainer = "#EEDFCB";

    theme.tertiary = "#EDB39E";
    theme.onTertiary = "#562719";
    theme.tertiaryContainer = "#743B2A";
    theme.onTertiaryContainer = "#FFDACE";

    theme.error = "#F2B8B5";
    theme.onError = "#601410";
    theme.errorContainer = "#8C1D18";
    theme.onErrorContainer = "#F9DEDC";

    theme.surfaceDim = "#1A1410";
    theme.surface = "#1A1410";
    theme.surfaceBright = "#44372D";

    theme.surfaceContainerLowest = "#140F0C";
    theme.surfaceContainerLow = "#231B16";
    theme.surfaceContainer = "#2A201A";
    theme.surfaceContainerHigh = "#352920";
    theme.surfaceContainerHighest = "#403228";

    theme.onSurface = "#F0E1D2";
    theme.onSurfaceVariant = "#D5C3B1";
    theme.outline = "#A38E7B";
    theme.outlineVariant = "#5B4B3E";

    theme.inverseSurface = "#F0E1D2";
    theme.inverseOnSurface = "#392D24";
    theme.inversePrimary = "#795335";
    return theme;
}

MaterialDesignStyling::ColorTheme rosewoodTheme()
{
    MaterialDesignStyling::ColorTheme theme;
    theme.primary = "#704F5D";
    theme.onPrimary = "#FFFFFF";
    theme.primaryContainer = "#EAD7DF";
    theme.onPrimaryContainer = "#2D1922";
    theme.primaryFixed = "#EAD7DF";
    theme.onPrimaryFixed = "#2D1922";
    theme.primaryFixedDim = "#D5B6C4";
    theme.onPrimaryFixedVariant = "#563946";

    theme.secondary = "#936B72";
    theme.onSecondary = "#FFFFFF";
    theme.secondaryContainer = "#F0DDE0";
    theme.onSecondaryContainer = "#341D22";
    theme.secondaryFixed = "#F0DDE0";
    theme.onSecondaryFixed = "#341D22";
    theme.secondaryFixedDim = "#DCB8BF";
    theme.onSecondaryFixedVariant = "#61434A";

    theme.tertiary = "#747B68";
    theme.onTertiary = "#060A03";
    theme.tertiaryContainer = "#E0E5D5";
    theme.onTertiaryContainer = "#202719";
    theme.tertiaryFixed = "#E0E5D5";
    theme.onTertiaryFixed = "#202719";
    theme.tertiaryFixedDim = "#C2CBB1";
    theme.onTertiaryFixedVariant = "#444D38";

    theme.error = "#B3261E";
    theme.onError = "#FFFFFF";
    theme.errorContainer = "#F9DEDC";
    theme.onErrorContainer = "#410E0B";

    theme.surfaceDim = "#E2D6CE";
    theme.surface = "#F7F3EF";
    theme.surfaceBright = "#F7F3EF";

    theme.surfaceContainerLowest = "#FFFBF8";
    theme.surfaceContainerLow = "#F3EDE7";
    theme.surfaceContainer = "#EEE7E1";
    theme.surfaceContainerHigh = "#E8DED6";
    theme.surfaceContainerHighest = "#E2D6CE";

    theme.onSurface = "#3B3435";
    // Slightly deeper than the base palette's #74696B for text on selected surfaces.
    theme.onSurfaceVariant = "#655A5C";
    theme.outline = "#85777A";
    theme.outlineVariant = "#CEC0BC";

    theme.inverseSurface = "#3B3435";
    theme.inverseOnSurface = "#F7F3EF";
    theme.inversePrimary = "#D5B6C4";
    theme.scrim = "#000000";
    theme.shadow = "#000000";
    return theme;
}

MaterialDesignStyling::ColorTheme windowsTheme()
{
    MaterialDesignStyling::ColorTheme theme;
    theme.primary = "#0078D4";
    theme.onPrimary = "#FFFFFF";
    theme.primaryContainer = "#DFF0FF";
    theme.onPrimaryContainer = "#003B70";
    theme.primaryFixed = "#DFF0FF";
    theme.onPrimaryFixed = "#003B70";
    theme.primaryFixedDim = "#A7D4F5";
    theme.onPrimaryFixedVariant = "#124F80";

    theme.secondary = "#586775";
    theme.onSecondary = "#FFFFFF";
    theme.secondaryContainer = "#E6EEF5";
    theme.onSecondaryContainer = "#263845";
    theme.secondaryFixed = "#E6EEF5";
    theme.onSecondaryFixed = "#263845";
    theme.secondaryFixedDim = "#C9D9E6";
    theme.onSecondaryFixedVariant = "#3C5263";

    theme.tertiary = "#476F77";
    theme.onTertiary = "#FFFFFF";
    theme.tertiaryContainer = "#D8EDF0";
    theme.onTertiaryContainer = "#153A41";
    theme.tertiaryFixed = "#D8EDF0";
    theme.onTertiaryFixed = "#153A41";
    theme.tertiaryFixedDim = "#AED3D9";
    theme.onTertiaryFixedVariant = "#31555C";

    theme.error = "#C42B1C";
    theme.onError = "#FFFFFF";
    theme.errorContainer = "#FDE7E4";
    theme.onErrorContainer = "#6B1D13";

    theme.surfaceDim = "#EAEAEA";
    theme.surface = "#F3F3F3";
    theme.surfaceBright = "#FFFFFF";

    theme.surfaceContainerLowest = "#FFFFFF";
    theme.surfaceContainerLow = "#FAFAFA";
    theme.surfaceContainer = "#F8F8F8";
    theme.surfaceContainerHigh = "#EAEAEA";
    theme.surfaceContainerHighest = "#E1E1E1";

    theme.onSurface = "#202020";
    theme.onSurfaceVariant = "#444444";
    theme.outline = "#707070";
    theme.outlineVariant = "#D1D1D1";

    theme.inverseSurface = "#202020";
    theme.inverseOnSurface = "#FFFFFF";
    theme.inversePrimary = "#A7D4F5";
    theme.scrim = "#000000";
    theme.shadow = "#000000";
    return theme;
}

}

MaterialDesignStyling::MaterialDesignStyling()
    : MaterialDesignStyling(*NextAppCore::instance())
{
}

MaterialDesignStyling::MaterialDesignStyling(RuntimeServices& runtime)
    : runtime_{runtime}
{
    assert(!instance_);
    instance_ = this;
    setTheme(runtime_.settings().value("UI/theme", "light").toString());
}

void MaterialDesignStyling::setTheme(const QString &name)
{
    const auto selectedTheme = availableThemes().contains(name) ? name : QStringLiteral("light");
    if (selectedTheme == currentTheme_) {
        return;
    }

    theme_ = themeForName(selectedTheme);
    currentTheme_ = selectedTheme;
    emit colorsChanged();
}

QStringList MaterialDesignStyling::availableThemes() const
{
    return {"light", "dark", "coffee", "lateNightCoffee", "rosewood", "windows"};
}

QVariantMap MaterialDesignStyling::previewTheme(const QString &name) const
{
    return toVariantMap(themeForName(name));
}

int MaterialDesignStyling::scrollBarWidth() const {
    return runtime_.isMobileUi() ? 16 : 12;
}

QString MaterialDesignStyling::primary() const { return theme_.primary; }
QString MaterialDesignStyling::onPrimary() const { return theme_.onPrimary; }
QString MaterialDesignStyling::primaryContainer() const { return theme_.primaryContainer; }
QString MaterialDesignStyling::onPrimaryContainer() const { return theme_.onPrimaryContainer; }
QString MaterialDesignStyling::primaryFixed() const { return theme_.primaryFixed; }
QString MaterialDesignStyling::onPrimaryFixed() const { return theme_.onPrimaryFixed; }
QString MaterialDesignStyling::primaryFixedDim() const { return theme_.primaryFixedDim; }
QString MaterialDesignStyling::onPrimaryFixedVariant() const { return theme_.onPrimaryFixedVariant; }

QString MaterialDesignStyling::secondary() const { return theme_.secondary; }
QString MaterialDesignStyling::onSecondary() const { return theme_.onSecondary; }
QString MaterialDesignStyling::secondaryContainer() const { return theme_.secondaryContainer; }
QString MaterialDesignStyling::onSecondaryContainer() const { return theme_.onSecondaryContainer; }
QString MaterialDesignStyling::secondaryFixed() const { return theme_.secondaryFixed; }
QString MaterialDesignStyling::onSecondaryFixed() const { return theme_.onSecondaryFixed; }
QString MaterialDesignStyling::secondaryFixedDim() const { return theme_.secondaryFixedDim; }
QString MaterialDesignStyling::onSecondaryFixedVariant() const { return theme_.onSecondaryFixedVariant; }

QString MaterialDesignStyling::tertiary() const { return theme_.tertiary; }
QString MaterialDesignStyling::onTertiary() const { return theme_.onTertiary; }
QString MaterialDesignStyling::tertiaryContainer() const { return theme_.tertiaryContainer; }
QString MaterialDesignStyling::onTertiaryContainer() const { return theme_.onTertiaryContainer; }
QString MaterialDesignStyling::tertiaryFixed() const { return theme_.tertiaryFixed; }
QString MaterialDesignStyling::onTertiaryFixed() const { return theme_.onTertiaryFixed; }
QString MaterialDesignStyling::tertiaryFixedDim() const { return theme_.tertiaryFixedDim; }
QString MaterialDesignStyling::onTertiaryFixedVariant() const { return theme_.onTertiaryFixedVariant; }

QString MaterialDesignStyling::error() const { return theme_.error; }
QString MaterialDesignStyling::onError() const { return theme_.onError; }
QString MaterialDesignStyling::errorContainer() const { return theme_.errorContainer; }
QString MaterialDesignStyling::onErrorContainer() const { return theme_.onErrorContainer; }

QString MaterialDesignStyling::surfaceDim() const { return theme_.surfaceDim; }
QString MaterialDesignStyling::surface() const { return theme_.surface; }
QString MaterialDesignStyling::surfaceBright() const { return theme_.surfaceBright; }

QString MaterialDesignStyling::surfaceContainerLowest() const { return theme_.surfaceContainerLowest; }
QString MaterialDesignStyling::surfaceContainerLow() const { return theme_.surfaceContainerLow; }
QString MaterialDesignStyling::surfaceContainer() const { return theme_.surfaceContainer; }
QString MaterialDesignStyling::surfaceContainerHigh() const { return theme_.surfaceContainerHigh; }
QString MaterialDesignStyling::surfaceContainerHighest() const { return theme_.surfaceContainerHighest; }

QString MaterialDesignStyling::onSurface() const { return theme_.onSurface; }
QString MaterialDesignStyling::onSurfaceVariant() const { return theme_.onSurfaceVariant; }
QString MaterialDesignStyling::outline() const { return theme_.outline; }
QString MaterialDesignStyling::outlineVariant() const { return theme_.outlineVariant; }

QString MaterialDesignStyling::inverseSurface() const { return theme_.inverseSurface; }
QString MaterialDesignStyling::inverseOnSurface() const { return theme_.inverseOnSurface; }
QString MaterialDesignStyling::inversePrimary() const { return theme_.inversePrimary; }
QString MaterialDesignStyling::scrim() const { return theme_.scrim; }
QString MaterialDesignStyling::shadow() const { return theme_.shadow; }

MaterialDesignStyling::ColorTheme MaterialDesignStyling::themeForName(const QString &name) const
{
    if (name == "dark") {
        return darkTheme();
    }
    if (name == "coffee") {
        return coffeeTheme();
    }
    if (name == "lateNightCoffee") {
        return lateNightCoffeeTheme();
    }
    if (name == "rosewood") {
        return rosewoodTheme();
    }
    if (name == "windows") {
        return windowsTheme();
    }

    return lightTheme();
}

QVariantMap MaterialDesignStyling::toVariantMap(const ColorTheme &theme) const
{
    return {
        {"primary", theme.primary},
        {"onPrimary", theme.onPrimary},
        {"primaryContainer", theme.primaryContainer},
        {"onPrimaryContainer", theme.onPrimaryContainer},
        {"primaryFixed", theme.primaryFixed},
        {"onPrimaryFixed", theme.onPrimaryFixed},
        {"primaryFixedDim", theme.primaryFixedDim},
        {"onPrimaryFixedVariant", theme.onPrimaryFixedVariant},
        {"secondary", theme.secondary},
        {"onSecondary", theme.onSecondary},
        {"secondaryContainer", theme.secondaryContainer},
        {"onSecondaryContainer", theme.onSecondaryContainer},
        {"secondaryFixed", theme.secondaryFixed},
        {"onSecondaryFixed", theme.onSecondaryFixed},
        {"secondaryFixedDim", theme.secondaryFixedDim},
        {"onSecondaryFixedVariant", theme.onSecondaryFixedVariant},
        {"tertiary", theme.tertiary},
        {"onTertiary", theme.onTertiary},
        {"tertiaryContainer", theme.tertiaryContainer},
        {"onTertiaryContainer", theme.onTertiaryContainer},
        {"tertiaryFixed", theme.tertiaryFixed},
        {"onTertiaryFixed", theme.onTertiaryFixed},
        {"tertiaryFixedDim", theme.tertiaryFixedDim},
        {"onTertiaryFixedVariant", theme.onTertiaryFixedVariant},
        {"error", theme.error},
        {"onError", theme.onError},
        {"errorContainer", theme.errorContainer},
        {"onErrorContainer", theme.onErrorContainer},
        {"background", theme.surface},
        {"onBackground", theme.onSurface},
        {"surfaceDim", theme.surfaceDim},
        {"surface", theme.surface},
        {"surfaceBright", theme.surfaceBright},
        {"surfaceVariant", theme.surfaceContainerHighest},
        {"surfaceContainerLowest", theme.surfaceContainerLowest},
        {"surfaceContainerLow", theme.surfaceContainerLow},
        {"surfaceContainer", theme.surfaceContainer},
        {"surfaceContainerHigh", theme.surfaceContainerHigh},
        {"surfaceContainerHighest", theme.surfaceContainerHighest},
        {"onSurface", theme.onSurface},
        {"onSurfaceVariant", theme.onSurfaceVariant},
        {"outline", theme.outline},
        {"outlineVariant", theme.outlineVariant},
        {"inverseSurface", theme.inverseSurface},
        {"inverseOnSurface", theme.inverseOnSurface},
        {"inversePrimary", theme.inversePrimary},
        {"scrim", theme.scrim},
        {"shadow", theme.shadow}
    };
}
