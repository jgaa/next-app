# Overall
- [x] Choose database (MariaDB)
- [ ] Decide on dataflow, what data is stored and cached where
    - [ ] Concurrent updates on different devices

# Data
- [ ] Schema for database
- [ ] GRPC
    - [ ] Create initial protofile for login, server version, events, folder-list

# Shared code

# Backend
- [x] Add DB driver to project
    [ ] Implement a convenient way to co_await simultaneous requests to the database, using a pool of connections.
- [ ] Add grpc to the project
- [ ] Bootstrapping
    - [ ] Create and initialize the database
    - [ ] Create system tenant and admin user
    - [ ] Optinally, create example data
    - [ ] Re-creation of the system tenant and admin user from the command line
- [ ] Create RBAC framework
    - [ ] Hard-coded permissions for now, based on pre-defined roles
        - [ ] @admin (system admin/root)
        - [ ] @tenat (admi ujser for a tenant)
        - [ ] @user (Normal user)
        - [ ] @guest (Guest access, typically to give a 3rd party limited access to a project)
- [ ] Logging
    - [x] Unique error numbers
    - [x] Source location / class / method
    - [ ] User / Request
- [ ] Build
    - [ ] Build-script that builds a container image locally and on CI (like nsblast)
    - [ ] Jenkins build
    - [ ] Run tests under Jenkins
    - [ ] Upload container images to a public repository

# App / UI
- [ ] Add grpc to the project
    - [ ] Try QT's grpc classes and see if I can use them.
    - [ ] Add a blog-post to my grpc series about the experience.

- [ ] Primary dashboard.
    - [ ] Design a dashboard that can work with the initial running version.
    - [ ] Make reusable components for the various functions
        - [ ] Tree structure
        - [ ] Todays calendar and actions
        - [ ] This weeks calendar and projects/tasks
        - [ ] Time spent Today
        - [ ] Time spent this week, month, quater, year
        - [ ] Project
        - [ ] Actions, with tags and optional sorting
        - [ ] Lists
        - [ ] tasks and actions in a list
        - [ ] Upcoming actions
        - [ ] Current work (WHID's primary functionality)
