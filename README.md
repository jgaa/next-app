Build status
- [![Build NextApp Docker Images](https://github.com/jgaa/next-app/actions/workflows/build_containers.yaml/badge.svg?branch=main)](https://github.com/jgaa/next-app/actions/workflows/build_containers.yaml)
- [![Build NextApp Flatpak (Static Qt)](https://github.com/jgaa/next-app/actions/workflows/build-ui-flatpak.yaml/badge.svg)](https://github.com/jgaa/next-app/actions/workflows/build-ui-flatpak.yaml)
- [![Build macOS App arm64 (Static Qt)](https://github.com/jgaa/next-app/actions/workflows/build-macos-arm64.yaml/badge.svg)](https://github.com/jgaa/next-app/actions/workflows/build-macos-arm64.yaml)

# next-app

GTD-native application for desktop and mobile

This is a project I have been thinking and talking about for more than ten years.

I have used a “light” version of David Allen’s [Getting Things Done](https://gettingthingsdone.com) (GTD) methodology even longer. In 2013, I wrote an Android app called [VikingGTD](https://github.com/jgaa/VikingGTD) that I used until the end of 2024. Now, I want something that runs on multiple devices - so I can use it on both my laptop and my phone.

In the future I also want it to use location and my energy level to suggest the most relevant next action, as well as reminding me about people to ping and things I need to pick up from nearby shops when I’m on the move. Not to mention delegation and collaboration with other people.

This is a pretty large project for a lone hacker. It involves two server components, a desktop app, an Android app, and probably an iOS app.

Everything is open source. My company offer a hosting plan (free while the application is in beta) for people who don’t want to run their own backend. But for people and companies where privacy and security are paramount, the code for the server and the app is freely available right here.

You can visit the app’s [home page](https://next-app.org) to see screenshots, videos, and find further documentation.

## Beta

NextApp was released in *beta* on May 31st, 2025.

The priority right now is to improve the documentation, fix issues, and implement minor features that beta users are requesting. Good ideas for new features that require significant development will be saved for future releases.

See [next-app.org](https://next-app.org/beta.html) for more information about beta software.

## Native code

The app is a real, native application. It’s written in C++ using the Qt framework to be cross-platform. The advantage of a *real*, native app over “web apps” is that it’s faster, leaner, and comes without the constraints of software that is really nothing but a web page. It’s also not as vulnerable to supply-chain attacks as web apps can be.

The backend servers are implemented in C++20 and leverage asynchronous coroutines to ensure high efficiency and a low memory footprint across all operations. This design allows a server to handle numerous concurrent connections with a small number of threads. When a client requests a large dataset - such as during a full sync of the local cache - the server fetches a batch of records from the database, streams it to the client via gRPC, and then proceeds with the next batch. This batched processing minimizes resource usage and prevents memory overload. Additionally, the use of the Boost.MySQL C++ library for asynchronous database operations ensures that database queries are handled efficiently, further reducing the need for numerous threads while still serving a large number of users effectively.

## Architecture

NextApp employs a distributed Model-View-Controller (MVC) pattern, where the backend serves as the central controller. This design enables a seamless update mechanism across multiple devices. When a change is made on one device, the client sends an update request to the server, which then propagates the change to all connected clients.

### Key Components

* **Centralized Controller**: The server handles all update requests, ensuring data consistency by performing atomic operations on a MariaDB backend.
* **Local Caching with SQLite**: Each client maintains a local cache using SQLite. This approach improves startup performance and allows the client to use complex database queries that would be impractical on a shared database. The app leverages Qt’s native support for SQLite and lazy loading to minimize memory footprint and ensure responsiveness. The local database runs in its own thread to prevent slow queries from freezing the user interface.
* **Data Synchronization and Conflict Resolution**: Updates are tagged with an incremental counter and synchronized via gRPC. When a client connects, it performs a full sync of relevant tables from the server while also subscribing to real-time update notifications. To handle potential race conditions (e.g., updates arriving during sync), any incoming changes are queued and processed only after the full sync completes.
* **Efficient Update Propagation**: By replicating only the updated data rather than the entire dataset, NextApp reduces network overhead and server load. This design is particularly effective in scenarios where frequent, incremental changes occur.
* **Efficient Use of the Backend**: By leveraging a robust local cache on each client, NextApp minimizes the frequency of server requests by handling most data operations locally. Since interactions and UI updates are predominantly managed via the local SQLite database, only essential changes are pushed to or pulled from the backend. This results in considerably lower network traffic and reduced load on the server, allowing each backend instance to support more concurrent users or operate on less expensive hardware compared to traditional web apps that rely on frequent, verbose, and costly data fetches.

This architecture provides a robust and efficient way to maintain consistency and performance across distributed devices. NextApp is both scalable and responsive in multi-device environments.

## How to Play with the Current Version

The simplest way to get started is to download an installer for your laptop or PC (see the Releases tag on GitHub) and use the public beta backend.

If you are a software developer, you can also clone the repository and build everything yourself. The NextApp client runs on Linux, macOS, Windows, and Android. The backend - which consists of two daemons, `signupd` and `nextappd` - runs only under Linux. These daemons are built automatically as docker images on the `main` and `devel` branches on GitHub. The backend uses MariaDB for storage.

There is a [script](scripts/bootstrap-and-run-backend.sh) that lets you run your own home-grown backend containers, or the containers from GitHub, on your PC, provided that you have Docker installed.

## Platforms

* Linux desktop
* macOS
* Windows 10/11
* Android

## Roadmap

* First stable release by the end of June/early July.
* After that, new releases every few weeks.

## Building

The application uses CMake, Qt 6.8.3, and requires g++ 12 or newer for the client and clang++ 17 or newer for the backend (the C++ templates in the backend makes g++ crash). It’s developed under Linux (Ubuntu 24.04). The project Boost 1.88 or newer.

**Qt Creator under Windows**

If you just want to build the UI app for Windows with Qt Creator, copy
`building\build-configs\vcpkg-windows-qt-creator.json` to `vcpkg.json` in the project’s root directory. You may need to *re-run with initial parameters* in Qt Creator’s build configuration window. This requires that **vcpkg** is installed on your machine and is available in **PATH**. When it works, vcpkg will make all the non-Qt libraries the project depends on available.

## Running

If you don’t care about running your own server, you can start the required backend components using Docker (tested under Linux):

```sh
bash scripts/bootstrap-and-run-backend.sh
```

After that, start the client application. You must specify the server address according to the output from `bootstrap-and-run-backend.sh`.

When you are done testing, you can stop the containers with:

```sh
bash scripts/bootstrap-and-run-backend.sh --stop-only
```
