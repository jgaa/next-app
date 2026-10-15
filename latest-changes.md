# NextApp rc1: easier planning, better flow, and optional AI assistance

NextApp’s first release candidate brings together the improvements made since v0.25.0, with a focus on choosing what to do next and moving smoothly from a list of actions to a day of focused work. There are new planning tools, fewer steps for everyday tasks, and an experimental way to connect an AI assistant to NextApp.

One of the biggest additions is **Suggest next actions**. Tell NextApp how much time you have and how much energy you can bring to the task, and it suggests suitable actions based on their priority, deadlines, difficulty, and estimated duration. You can narrow the search to particular categories, lists and sublists, or actions you have selected. If you have half an hour between meetings or are running low on energy, it gives you a practical starting point. Suggestions run locally and require no AI service.

Once you find something to work on, you can start a work session directly from the suggestions or put the action on your calendar. On desktop, drag a suggestion or an action from your list into empty calendar space to create a time block, or drop it onto an existing block to add it there. You can also drag an action onto the work-session list to make it your current work session. Estimated durations now appear in the actions list, making it easier to judge what will fit.

The calendar is more flexible, too. You can create overlapping time blocks directly in the calendar, and a new **Complete All** command lets you finish all the actions attached to a block together. Calendar dragging and moving have also received fixes to make planning smoother.

Capturing and sharing actions takes fewer steps. **Paste as new action** is now available from a list’s context menu, so copied text can become an action right where it belongs. Title extraction handles pasted text and Markdown better, avoids repeating the title in the description, and the action editor accommodates longer titles. You can also copy an existing action’s title or description, or copy it as Markdown for use in notes and other tools.

On Android, swiping left on an action toggles its selection, making it easier to select several actions for changes together. Batch editing has been expanded, alongside fixes to batch priority changes. More visual themes and a choice of audio output device for notification sounds give you additional ways to make NextApp fit your workspace.

For people who use AI assistants, rc1 introduces **experimental, opt-in MCP support**. MCP is a way for compatible assistants to work with other applications. Once enabled and connected, an assistant can find actions and lists, create actions with scheduling and other details, update them, and mark them complete. You control whether each supported type of change is blocked, requires your approval, or is always allowed, and you can review recent agent activity in Settings. This is an optional integration that you configure with a compatible assistant.

Running your own NextApp backend is also easier to get started with. A new self-hosting setup includes a guided setup script, a Docker Compose configuration, and documentation for connecting clients and managing the installation. It runs independently of NextApp’s hosted subscription and payment services.

Alongside these additions, rc1 includes fixes for work reports initially showing no data, work-session durations failing to refresh, connection-error dialogs remaining open after reconnecting, and imports stalling when account limits are reached. Green Days gets a correction to its starting month, and Windows compatibility and macOS packaging have received attention as well.

As the first release candidate, rc1 is a chance to try these improvements in your daily routine and report any remaining rough edges before the final release.
