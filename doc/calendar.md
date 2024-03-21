# The Calendar

## Information in the Calendar

- Actions scheduled inside the window of the calendars time span
 - Appointments that are for a specific date and time
 - Planned Work on a action that is scheduled to be done at a specific time
- Working hours. (to be marked with color / frames)
 - Default
 - For a specific day
- Holidays (to be marked with color
- Vacations, time off (to be marked with color
- Focus time (to be marked with color)


### Actions
Actions have their Due object.
If ActionDueKind is DATETIME, the action is treated as an appointment and visible in the calendar.

### PlannedWork
We keep these as separate objects that points to the relevant Action.

### Holidays
We need to query sone external service to get the list of holidays for the users region.

### Vacations
We need to allow the user to mark days as vacations.

In the future, we may want to allow vacations or days off as a rule-set,
so that the user can define the rule rather than the time.
