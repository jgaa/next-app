#include <QSettings>

#include "MaterialDesignStyling.h"
#include "NextAppCore.h"

MaterialDesignStyling *MaterialDesignStyling::instance_ = nullptr;

MaterialDesignStyling::MaterialDesignStyling() {

    assert(!instance_);
    instance_ = this;
    QSettings settings;
    auto theme = QSettings{}.value("UI/theme", "light").toString();

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

int MaterialDesignStyling::scrollBarWidth() const {
    return NextAppCore::instance()->isMobile() ? 16 : 12;
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


void MaterialDesignStyling::setLightTheme()
{
    theme_.primary = "#65558F";
    theme_.onPrimary = "#FFFFFF";
    theme_.primaryContainer = "#EADDFF";
    theme_.onPrimaryContainer = "#21005D";
    theme_.primaryFixed = "#EADDFF";
    theme_.onPrimaryFixed = "#21005D";
    theme_.primaryFixedDim = "#D0BCFF";
    theme_.onPrimaryFixedVariant = "#4F378B";

    theme_.secondary = "#625B71";
    theme_.onSecondary = "#FFFFFF";
    theme_.secondaryContainer = "#E8DEF8";
    theme_.onSecondaryContainer = "#1D192B";
    theme_.secondaryFixed = "#E8DEF8";
    theme_.onSecondaryFixed = "#1D192B";
    theme_.secondaryFixedDim = "#CCC2DC";
    theme_.onSecondaryFixedVariant = "#4A4458";

    theme_.tertiary = "#7D5260";
    theme_.onTertiary = "#FFFFFF";
    theme_.tertiaryContainer = "#FFD8E4";
    theme_.onTertiaryContainer = "#31111D";
    theme_.tertiaryFixed = "#FFD8E4";
    theme_.onTertiaryFixed = "#31111D";
    theme_.tertiaryFixedDim = "#EFB8C8";
    theme_.onTertiaryFixedVariant = "#633B48";

    theme_.error = "#B3261E";
    theme_.onError = "#FFFFFF";
    theme_.errorContainer = "#F9DEDC";
    theme_.onErrorContainer = "#410E0B";

    theme_.surfaceDim = "#DED8E1";
    theme_.surface = "#FEF7FF";
    theme_.surfaceBright = "#FEF7FF";

    theme_.surfaceContainerLowest = "#FFFFFF";
    theme_.surfaceContainerLow = "#F7F2FA";
    theme_.surfaceContainer = "#F3EDF7";
    theme_.surfaceContainerHigh = "#ECE6F0";
    theme_.surfaceContainerHighest = "#E6E0E9";

    theme_.onSurface = "#1D1B20";
    theme_.onSurfaceVariant = "#49454F";
    theme_.outline = "#79747E";
    theme_.outlineVariant = "#CAC4D0";

    theme_.inverseSurface = "#322F35";
    theme_.inverseOnSurface = "#F5EFF7";
    theme_.inversePrimary = "#D0BCFF";
    theme_.scrim = "#000000";
    theme_.shadow = "#000000";

    currentTheme_ = "light";
}

void MaterialDesignStyling::setDarkTheme()
{
    theme_.primary = "#D0BCFE";
    theme_.onPrimary = "#381E72";
    theme_.primaryContainer = "#4F378B";
    theme_.onPrimaryContainer = "#EADDFF";
    theme_.primaryFixed = "#EADDFF";
    theme_.onPrimaryFixed = "#21005D";
    theme_.primaryFixedDim = "#D0BCFF";
    theme_.onPrimaryFixedVariant = "#4F378B";

    theme_.secondary = "#CCC2DC";
    theme_.onSecondary = "#332D41";
    theme_.secondaryContainer = "#4A4458";
    theme_.onSecondaryContainer = "#E8DEF8";
    theme_.secondaryFixed = "#E8DEF8";
    theme_.onSecondaryFixed = "#1D192B";
    theme_.secondaryFixedDim = "#CCC2DC";
    theme_.onSecondaryFixedVariant = "#4A4458";

    theme_.tertiary = "#EFB8C8";
    theme_.onTertiary = "#492532";
    theme_.tertiaryContainer = "#633B48";
    theme_.onTertiaryContainer = "#FFD8E4";
    theme_.tertiaryFixed = "#31111D";
    theme_.onTertiaryFixed = "#31111D";
    theme_.tertiaryFixedDim = "#EFB8C8";
    theme_.onTertiaryFixedVariant = "#633B48";

    theme_.error = "#F2B8B5";
    theme_.onError = "#601410";
    theme_.errorContainer = "#8C1D18";
    theme_.onErrorContainer = "#F9DEDC";

    theme_.surfaceDim = "#141218";
    theme_.surface = "#141218";
    theme_.surfaceBright = "#3B383E";

    theme_.surfaceContainerLowest = "#0F0D13";
    theme_.surfaceContainerLow = "#1D1B20";
    theme_.surfaceContainer = "#211F26";
    theme_.surfaceContainerHigh = "#2B2930";
    theme_.surfaceContainerHighest = "#36343B";

    theme_.onSurface = "#E6E0E9";
    theme_.onSurfaceVariant = "#CAC4D0";
    theme_.outline = "#938F99";
    theme_.outlineVariant = "#49454F";

    theme_.inverseSurface = "#E6E0E9";
    theme_.inverseOnSurface = "#322F35";
    theme_.inversePrimary = "#6750A4";
    theme_.scrim = "#000000";
    theme_.shadow = "#000000";

    currentTheme_ = "dark";
}
