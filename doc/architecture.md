# Architecture for next-app

## Core requirements
- Multi-user, with users rooted to a tenant.
- A user may use multiple devices at one time. That means that changes and updates must be replicated immediately to all the users devices. The client use QT, so we can build custom data classes for representing the data that can send events for all changes to all the users devices.

## Data
- Data is stored in a SQL database by the server.
- Clients can fetch data-sets and pages of data from the server via gRPC
- The client-app caches the entire structural tree in memory, and applies changes from local events and remote events. The aim is for the app to work fast and without comm-delays.
- Conflicts are stored in a structure/list and solved manually by the user.

## Client
- Implemented in QT 6/QML. The initial version will use QT for all platforms:
    - Linux Desktop
    - MacOS desktop
    - Android
    - IOS
    - Microsoft Windows

## Server
- The server must use platform independent code, but the initial target is to run it and its database as containers. That means that it must work on Linux.
