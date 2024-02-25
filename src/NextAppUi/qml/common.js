.pragma library
.import QtQuick as QtQuick

function createDialog(name, parent, args) {
    var ctl = Qt.createComponent(name)
    if (ctl.status !== QtQuick.Component.Ready) {
        console.log("Error loading component ", name, ": ", ctl.errorString())
        return null
    }
    var dialog = ctl.createObject(parent, args)
    if (dialog === null) {
        console.log("Error creating dialog ", name)
        return null
    }

    return dialog
}

function openDialog(name, parent, args, bind) {
    var dialog = createDialog(name, parent, args)
    if (dialog) {
        dialog.open()
    }
    return dialog
}

// From ChatGPT
function getDateFromWeekNumber(year, weekNumber) {
    // Create a date object for January 1st of the given year
    var januaryFirst = new Date(year, 0, 1);

    // Calculate the day of the week for January 1st (0 for Sunday, 1 for Monday, etc.)
    var dayOfWeek = januaryFirst.getDay();

    // Calculate the difference between the desired week's starting day and Sunday
    var firstDayOffset = (7 - dayOfWeek) % 7;

    // Calculate the date of the first day of the first week
    var firstWeekDate = new Date(year, 0, 1 + firstDayOffset);

    // Calculate the desired date by adding the number of days corresponding to the week number
    var desiredDate = new Date(firstWeekDate);
    desiredDate.setDate(desiredDate.getDate() + (7 * (weekNumber - 1)));

    return desiredDate;
}

function getDateFromISOWeekNumber(year, weekNumber) {
    var januaryFirst = new Date(year, 0, 1);
    var dayOfWeek = januaryFirst.getDay();
    var firstThursdayOffset = (11 - dayOfWeek) % 7; // Offset to the first Thursday of the year
    var firstThursday = new Date(year, 0, 1 + firstThursdayOffset);

    var targetDate = new Date(firstThursday);
    targetDate.setDate(targetDate.getDate() + (weekNumber - 1) * 7);

    return targetDate;
}

function getISOWeekNumber(date) {
    var jan1 = new Date(date.getFullYear(), 0, 1);
    var dayOfWeek = jan1.getDay();
    // Adjust the first day of the year to be a Thursday (4 corresponds to Thursday)
    var firstThursday = new Date(jan1.getTime() + ((4 - dayOfWeek + 7) % 7) * 86400000); // milliseconds in a day

    // Calculate the week number by counting the number of Thursdays between the adjusted January 1st and the given date
    var weekNumber = Math.floor(((date - firstThursday) / 86400000 + 3) / 7);

    // If the week number is less than 1, it means it's part of the previous year's last week
    // if (weekNumber < 1) {
    //     weekNumber = getISOWeekNumber(new Date(date.getFullYear() - 1, 11, 31));
    // }

    return weekNumber + 1;
}
