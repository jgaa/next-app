Build status
- [![Build NextApp Docker Images](https://github.com/jgaa/next-app/actions/workflows/build_containers.yaml/badge.svg?branch=main)](https://github.com/jgaa/next-app/actions/workflows/build_containers.yaml)
- [![Build and Release NextApp Flatpak (Static Qt)](https://github.com/jgaa/next-app/actions/workflows/build-ui-flatpak.yaml/badge.svg)](https://github.com/jgaa/next-app/actions/workflows/build-ui-flatpak.yaml)

# next-app
GTD native application for desktop and mobile.

This is a project I have been thinking and talking about for more than 10 years.

I have used a "light" version of David Allans [Getting Things Done](https://gettingthingsdone.com) (GTD) 
idea for even longer. I wrote an app for Android in 2013 [VikingGTD](https://github.com/jgaa/VikingGTD)
that I a still use to organize my life. However, I want something that runs on multiple 
devices - so I can use it on my laptops *and* my phone. I also want it to use location 
and my energy level to suggest the most relevant next action - as well as reminding me about people to
ping and tings I need to get from nearby shops when I move around. 
Not to mention delegation and cooperation with other people.

This is a pretty large project for a lone hacker. It involves a server part, a desktop app, an Android app
and probably an IOS app.

Everything is open source. I will probably offer a hosting-plan for people who don't wan to run
their own backend. But for people and companies where privacy and security is paramount,
the code for the server and the apps will be freely available.

## Native code

The app is a real, native application written in C++ using the QT framework
to be cross platform for user interfaces across operating systems. The advantage
of a *real*, native app, over "web apps", is that it's faster, leaner, and that
it comes without any of the constraints of "software" that is really nothing but a
web-page.

The back-end server is implemented in C++20 and leverages asynchronous coroutines to ensure high efficiency and a low memory footprint across all operations. This design allows the server to handle numerous concurrent connections with a small number of threads. When a client requests a large dataset—such as during a full sync of the local cache—the server fetches a batch of records from the database, streams it to the client via gRPC, and then proceeds with the next batch. This batched processing minimizes resource usage and prevents memory overload. Additionally, the use of the Boost.MySQL library for asynchronous database operations ensures that database queries are handled efficiently, further reducing the need for numerous threads while still serving a large number of users effectively.

## Architecture

NextApp employs a distributed Model-View-Controller (MVC) pattern where the backend serves as the central controller. This design enables a seamless update mechanism across multiple devices. When a change is made on one device, the client sends an update request to the server, which then propagates the change to all connected clients.

### Key Components
- **Centralized Controller**: The server handles all update requests, ensuring data consistency by performing atomic operations on a MariaDB backend.
- **Local Caching with SQLite**: Each client maintains a local cache using SQLite. This approach not only improves startup performance but also reduces reliance on constant server communication. The app leverages Qt’s native support for SQLite and lazy loading to minimize memory footprint and ensure responsiveness.
- **Data Synchronization and Conflict Resolution**: Updates are tagged with an incremental counter and synchronized via gRPC. When a client connects, it performs a full sync of relevant tables from the server, while also subscribing to real-time update notifications. To handle potential race conditions (e.g., updates during sync), any incoming changes are queued and processed only after the full sync completes.
- **Efficient Update Propagation**: By replicating only the updated data rather than the entire dataset, NextApp reduces network overhead and server load. This design is particularly effective in scenarios where frequent, incremental changes occur.
- **Efficient use of the backend**: By leveraging a robust local cache on each client, NextApp minimizes the frequency of server requests by handling most data operations locally. Since interactions and UI updates are predominantly managed via the local SQLite database, only essential changes are pushed to or pulled from the backend. This results in considerably lower network traffic and reduced load on the server, allowing each backend instance to support more concurrent users or operate on less expensive hardware compared to traditional web apps that rely on frequent, full data fetches.

This architecture provides a robust and efficient way to maintain consistency and performance across distributed devices, making NextApp both scalable and responsive in multi-device environments.

## How to play with the current version

The application at this moment is "pre-beta", which means that some things
are unstable, or not ready yet.

## Features that works
(or are supposed to work)

- Green Days
- GTD
  - Actions
  - List (tree of lists)
  - Due dates (Year, quarter, month, week, date, time)
- Recurring actions
  - After *i* days, weeks, months, quarters, years
  - Specific weekday, first/last day in month
- Time Tracking
- Calendar (day plan)
  - Time Blocks
  - Actions in Time Blocks
  - Overlap
- Reports
  - Weekly overview
- Weekly Review

## Platforms

- Linux desktop (Debian, Ubuntu)
- MacOS
- Windows 10/11
- Android

## Roadmap

I'm working towards an Beta version in February 2025.

## Building

The application use CMake, QT 6.8.3 and require g++-12 or never for the client and clang++-17 or newer for the server (the templates are too complex for g++). It's developed under
Linux (Ubuntu 24.04). The server require boost version 1.85 or newer.

**Qt Creator under Windows**

If you just want to build the UI app for Windows with QT Creator, you can copy 
`building\build-configs\vcpkg-windows-qt-creator.json` to `vcpkg.json` in the 
projects root directory. You may need to *re-run with initial parameters* in 
Qt Creatots's build configuration window. This require that **vcpkg** is installed
your machine and is abailable in the **PATH**. When it works, it will make available 
all the non-Qt libraries the project depend on.

## Running

If you don't care about the server, you can start the required back-end components using docker. 
(I have only tested this under Linux).

```sh

bash bootstrap-and-run-backend.sh

```

After that, you can start the client application, and it will connect to the signup service
in the back-end. You may need to specify the address in the server info according to the
output from `bootstrap-and-run-backend.sh`.

When you are done testing, you can stop the containers with this command:

```sh

docker stop signupd-devel nextappd-devel na-mariadb-devel

```

## nextappd

**Enviroment variables**

| Name             | Value                                                                                    |
|------------------|------------------------------------------------------------------------------------------|
| NEXTAPP_REGION   | Region where the instance is running. Adds a `region` tag to all metrics values.         |
| NEXTAPP_DBPASSW  | Database password for the `nextapp` user on Mariadb.                                     |
| NEXTAPP_ROOT_DBPASSW | Root database password for Mariadb. Only used during bootstrap to create the database.|
