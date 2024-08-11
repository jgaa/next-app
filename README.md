# next-app
GTD application for desktop and mobile.

This is a project I have been thinking and talking about for more than 10 years.

I have used a "light" version of David Allans [Getting Things Done](https://gettingthingsdone.com) (GTD) 
idea for even longer. I wrote an app for Android in 2013 [VikingGTD](https://github.com/jgaa/VikingGTD)
that I a still use to organize my life. However, I want something that runs on multiple 
devices - so I can use it on my laptops *and* my phone. I also want it to use location 
and my energy level to suggest the most relevant next action - as well as reminding me abot people to 
ping and tings I need to get from nearby shops when I move around. 
Not to mention delegation and cooperation with other people.

This is a pretty large project for a lone hacker. It involves a server part, a desktop app, an Android app
and probably an IOS app.

Everything is open source. I will probably offer a hosting-plan for people who don't wan to run
their own backend. But for people and companies where privacy and security is paramount,
the code for the server and the apps will be freely available.

# How to play with the current version

The application at this moment is "pre-alpha", which means that lots of things
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
- Calendar
  - Time Blocks
  - Actions in Time Blocks
  - Overlap, up to 8 layers
- Reports
  - Weekly overview

## Platforms

- Linux desktop (Debian, Ubuntu)
- MacOS
- Windows 11
- Android

## Roadmap

I'm working towards a Alpha version in August 2024 and a Beta in September.

## Building

The application use CMake, QT 6.8 and require g++-13 or clang-17. It's developed under
Linux (Ubuntu 24.04). The server require boost version 1.84 or newer.

## Running

If you don't care about the server, you can start the required components using docker.

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

