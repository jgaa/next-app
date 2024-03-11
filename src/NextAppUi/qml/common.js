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

function formatPbDate(date) {
    console.log("date: ", date.year, " ", date.month, " ", date.mday)
    var d = new Date(date.year, date.month, date.mday)
    return d.toLocaleDateString()
}

function formatPbTimeFromTimet(when) {
    const d = new Date(when * 1000)
    return d.toLocaleString()
}

// ChatGPT Convert minutes to text string
function minutesToText(minutes) {
    // Calculate days, hours, and remaining minutes
    var days = Math.floor(minutes / (8 * 60)); // 8 hours per work day
    var remainingMinutes = minutes % (8 * 60);
    var hours = Math.floor(remainingMinutes / 60);
    var mins = remainingMinutes % 60;

    // Format the string
    var result = '';
    if (days > 0) {
        result += days + ':';
    }
    if (hours < 10) {
        result += '0';
    }
    result += hours + ':';
    if (mins < 10) {
        result += '0';
    }
    result += mins;

    return result;
}

// ChatGPT Convert text string to minutes
function textToMinutes(text) {
    // Split the string into days, hours, and minutes
    var parts = text.split(':');
    var days = 0;
    var hours = 0;
    var mins = 0;

    // Parse the parts
    if (parts.length === 3) {
        days = parseInt(parts[0]) || 0;
        hours = parseInt(parts[1]) || 0;
        mins = parseInt(parts[2]) || 0;
    } else if (parts.length === 2) {
        hours = parseInt(parts[0]) || 0;
        mins = parseInt(parts[1]) || 0;
    } else if (parts.length === 1) {
        mins = parseInt(parts[0]) || 0;
    }

    // Calculate total minutes
    var totalMinutes = (days * 8 * 60) + (hours * 60) + mins;
    return totalMinutes;
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

function getDateFromISOWeekNumber(year, weekNumber) {
    var januaryFirst = new Date(year, 0, 1);
    var dayOfWeek = januaryFirst.getDay();
    var firstThursdayOffset = (11 - dayOfWeek) % 7; // Offset to the first Thursday of the year
    var firstThursday = new Date(year, 0, 1 + firstThursdayOffset);

    var targetDate = new Date(firstThursday);
    targetDate.setDate(targetDate.getDate() + (weekNumber - 1) * 7);
    return targetDate;
}
