# next-app
GTD application for desktop and mobile.

This is a project I have been thinking and talking about for more than 10 years.

I have used a "light" version of David Allans [Getting Things Done](https://gettingthingsdone.com) (GTD) 
ide for even longer. I wrote an app for Android in 2013 [VikingGTD](https://github.com/jgaa/VikingGTD)
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

The application at this moment is unusable for anything but the "Green Days" feature.

I'm working towards a Beta version in August 2024.

The application use CMake, QT 6.8 and require g++-13 or clang-17. It's developed under
Linux (Ubuntu 23.10). MacOS and Windows will be supported later. The server require boost version 1.84 or newer.

If you don't care about the server, you can start the required components using docker.

```sh

bash bootstrap-and-run-backend.sh

```

After that, you can start the QT client application, and it will connect to the signup service
in the back-end. You may need to specify the address in the server info according to the
output from `bootstrap-and-run-backend.sh`.

When you are done testing, you can stop the containers with this command:

```sh

docker stop signupd-devel nextappd-devel na-mariadb-devel

```

