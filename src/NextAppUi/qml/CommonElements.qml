import QtQuick

Item {
    id: root
    property string faNormalName: fontAwesome.name
    property string faNormalStyle: fontAwesome.font.styleName
    property string faSolidName: fontAwesomeSolid.name
    property string faSolidStyle: fontAwesomeSolid.font.styleName

    FontLoader { id: fontAwesome; source: "../fonts/Font Awesome 6 Free-Regular-400.otf" }
    FontLoader { id: fontAwesomeSolid; source: "../fonts/Font Awesome 6 Free-Solid-900.otf" }
}
