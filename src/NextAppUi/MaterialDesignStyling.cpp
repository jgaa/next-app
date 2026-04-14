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

    theme.tertiary = "#7D5260";
    theme.onTertiary = "#FFFFFF";
    theme.tertiaryContainer = "#FFD8E4";
    theme.onTertiaryContainer = "#31111D";
    theme.tertiaryFixed = "#FFD8E4";
    theme.onTertiaryFixed = "#31111D";
    theme.tertiaryFixedDim = "#EFB8C8";
    theme.onTertiaryFixedVariant = "#633B48";

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

    theme.tertiary = "#EFB8C8";
    theme.onTertiary = "#492532";
    theme.tertiaryContainer = "#633B48";
    theme.onTertiaryContainer = "#FFD8E4";
    theme.tertiaryFixed = "#31111D";
    theme.onTertiaryFixed = "#31111D";
    theme.tertiaryFixedDim = "#EFB8C8";
    theme.onTertiaryFixedVariant = "#633B48";

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
    auto theme = runtime_.settings().value("UI/theme", "light").toString();

    if (theme == "dark") {
        setDarkTheme();
    } else {
        setLightTheme();
    }
}

void MaterialDesignStyling::setTheme(const QString &name)
{
    if (name == currentTheme_) {
        return;
    }

    if (name == "dark") {
        setDarkTheme();
    } else {
        setLightTheme();
    }
    emit colorsChanged();
}

QStringList MaterialDesignStyling::availableThemes() const
{
    return {"light", "dark"};
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

void MaterialDesignStyling::setLightTheme()
{
    theme_ = lightTheme();
    currentTheme_ = "light";
}

void MaterialDesignStyling::setDarkTheme()
{
    theme_ = darkTheme();
    currentTheme_ = "dark";
}
