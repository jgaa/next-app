
#include <algorithm>
#include <ranges>

#include "UseCaseTemplates.h"
#include "nextapp_client.grpc.qpb.h"
#include "ServerComm.h"

using namespace std;

UseCaseTemplates::UseCaseTemplates() {

    templates_ = {
        UseCaseTemplate {
            tr("Front-end Developer"),
            tr("Front-end web developer"),
            {
                List{tr("Inbox"), tr("New tasks"), List::Kind::FOLDER, {}},
                List{tr("Projects"), tr(""), List::Kind::FOLDER, {
                                                                     List{tr("Portfolio Website"), tr(""), List::Kind::PROJECT, {
                                                                                                                                    List{tr("Design landing page"), tr(""), List::Kind::TASK, {}},
                                                                                                                                    List{tr("Implement responsive layout"), tr(""), List::Kind::TASK, {}}
                                                                                                                                }}
                                                                 }},
                List{tr("Design & Prototyping"), tr("Wireframes, mockups, design tokens"), List::Kind::FOLDER, {}},
                List{tr("Component Library"), tr("Reusable UI components"), List::Kind::FOLDER, {}},
                List{tr("Accessibility"), tr("ARIA roles, contrast checks"), List::Kind::FOLDER, {}},
                List{tr("Performance & Optimization"), tr("Load time, bundle size"), List::Kind::FOLDER, {}},
                List{tr("Testing & QA"), tr("Unit, integration, end-to-end tests"), List::Kind::FOLDER, {}},
                List{tr("CI/CD & Deployment"), tr("Automation pipelines"), List::Kind::FOLDER, {}},
                List{tr("Documentation"), tr("Docs, READMEs, style guide"), List::Kind::FOLDER, {}},
                List{tr("DevTools & Extensions"), tr("Browser plugins, IDE setup"), List::Kind::FOLDER, {}},
                List{tr("Learning"), tr("Courses, tutorials, articles"), List::Kind::FOLDER, {}},
                List{tr("Maintenance"), tr("Refactors, upgrades"), List::Kind::FOLDER, {}},
                List{tr("Bugs"), tr("Defects and issues"), List::Kind::FOLDER, {}},
                List{tr("Someday/Maybe"), tr("Ideas to explore later"), List::Kind::FOLDER, {}}
            }
        },
        UseCaseTemplate {
            tr("Back-end Developer"),
            tr("Back-end services and API developer with DevOps responsibilities"),
            {
                List{tr("Inbox"), tr("New tasks"), List::Kind::FOLDER, {}},
                List{tr("Projects"), tr(""), List::Kind::FOLDER, {
                                                                     List{tr("API Service"), tr(""), List::Kind::PROJECT, {
                                                                                                                              List{tr("Design REST endpoints"), tr(""), List::Kind::TASK, {}},
                                                                                                                              List{tr("Implement authentication"), tr(""), List::Kind::TASK, {}}
                                                                                                                          }}
                                                                 }},
                List{tr("DevOps & CI/CD"), tr("Build pipelines, deployments, infrastructure as code"), List::Kind::FOLDER, {}},
                List{tr("Database & Storage"), tr("Schema design, optimization, migrations"), List::Kind::FOLDER, {}},
                List{tr("API Design & Documentation"), tr("Swagger, OpenAPI, docs"), List::Kind::FOLDER, {}},
                List{tr("Security & Compliance"), tr("Authentication, authorization, audits"), List::Kind::FOLDER, {}},
                List{tr("Monitoring & Logging"), tr("Prometheus, ELK, alerts"), List::Kind::FOLDER, {}},
                List{tr("Performance & Scalability"), tr("Load testing, caching"), List::Kind::FOLDER, {}},
                List{tr("Testing & QA"), tr("Unit, integration, end-to-end tests"), List::Kind::FOLDER, {}},
                List{tr("Containerization & Orchestration"), tr("Docker, Kubernetes"), List::Kind::FOLDER, {}},
                List{tr("Infrastructure as Code"), tr("Terraform, CloudFormation"), List::Kind::FOLDER, {}},
                List{tr("Documentation"), tr("Guides, READMEs, runbooks"), List::Kind::FOLDER, {}},
                List{tr("Tools & Setup"), tr("Local tooling, IDE, CLI"), List::Kind::FOLDER, {}},
                List{tr("Maintenance"), tr("Upgrades, refactors"), List::Kind::FOLDER, {}},
                List{tr("Bugs"), tr("Defects and issues"), List::Kind::FOLDER, {}},
                List{tr("Someday/Maybe"), tr("Ideas to explore later"), List::Kind::FOLDER, {}}
            }
        },
        UseCaseTemplate {
            tr("Full-stack Developer"),
            tr("Senior full-stack developer with end-to-end architecture, implementation, and operational responsibilities"),
            {
                List{tr("Inbox"), tr("New tasks and requests"), List::Kind::FOLDER, {}},
                List{tr("Projects"), tr("Active full-stack initiatives"), List::Kind::FOLDER, {
                                                                                                  List{tr("End-to-End Application"), tr(""), List::Kind::PROJECT, {
                                                                                                                                                                      List{tr("Define system architecture"), tr(""), List::Kind::TASK, {}},
                                                                                                                                                                      List{tr("Implement core features"), tr(""), List::Kind::TASK, {}}
                                                                                                                                                                  }}
                                                                                              }},
                List{tr("Architecture & Design"), tr("High-level system and component design"), List::Kind::FOLDER, {}},
                List{tr("Front-end Development"), tr("UI frameworks, component libraries"), List::Kind::FOLDER, {}},
                List{tr("Back-end Development"), tr("Service layers, APIs, business logic"), List::Kind::FOLDER, {}},
                List{tr("Database & Storage"), tr("Schema design, data modeling, migrations"), List::Kind::FOLDER, {}},
                List{tr("API Design & Documentation"), tr("OpenAPI, GraphQL schema, docs"), List::Kind::FOLDER, {}},
                List{tr("DevOps & CI/CD"), tr("Deployment pipelines, automation scripts"), List::Kind::FOLDER, {}},
                List{tr("Containerization & Orchestration"), tr("Docker, Kubernetes configurations"), List::Kind::FOLDER, {}},
                List{tr("Infrastructure as Code"), tr("Terraform, CloudFormation templates"), List::Kind::FOLDER, {}},
                List{tr("Monitoring & Logging"), tr("Metrics, alerts, log aggregation"), List::Kind::FOLDER, {}},
                List{tr("Security & Compliance"), tr("Vulnerabilities, audits, best practices"), List::Kind::FOLDER, {}},
                List{tr("Performance & Scalability"), tr("Load testing, caching, optimization"), List::Kind::FOLDER, {}},
                List{tr("Testing & QA"), tr("Unit, integration, end-to-end tests"), List::Kind::FOLDER, {}},
                List{tr("Documentation"), tr("Architecture docs, READMEs, runbooks"), List::Kind::FOLDER, {}},
                List{tr("DevTools & Setup"), tr("Local environment, debugging tools"), List::Kind::FOLDER, {}},
                List{tr("Mentorship & Leadership"), tr("Code reviews, team guidance"), List::Kind::FOLDER, {}},
                List{tr("Professional Development"), tr("Conferences, courses, certifications"), List::Kind::FOLDER, {}},
                List{tr("Maintenance"), tr("Upgrades, refactors, debt paydown"), List::Kind::FOLDER, {}},
                List{tr("Bugs"), tr("Issue triage and resolution"), List::Kind::FOLDER, {}},
                List{tr("Someday/Maybe"), tr("Future ideas and experiments"), List::Kind::FOLDER, {}}
            }
        },
        UseCaseTemplate {
            tr("Mobile Developer (iOS / Android)"),
            tr("Develops mobile applications for iOS and Android platforms"),
            {
                List{tr("Inbox"), tr("New tasks"), List::Kind::FOLDER, {}},
                List{tr("Projects"), tr(""), List::Kind::FOLDER, {
                                                                     List{tr("Mobile App"), tr(""), List::Kind::PROJECT, {
                                                                                                                             List{tr("Set up project skeleton"), tr(""), List::Kind::TASK, {}},
                                                                                                                             List{tr("Implement login screen"), tr(""), List::Kind::TASK, {}}
                                                                                                                         }}
                                                                 }},
                List{tr("Design & UX"), tr(""), List::Kind::FOLDER, {}},
                List{tr("API & Networking"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Testing & QA"), tr(""), List::Kind::FOLDER, {}},
                List{tr("CI/CD / Build Pipeline"), tr(""), List::Kind::FOLDER, {}},
                List{tr("App Store / Play Store Deployment"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Crash Reporting & Monitoring"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Analytics & Telemetry"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Localization & Accessibility"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Performance & Optimization"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Security & Permissions"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Documentation"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Release Planning / Roadmap"), tr(""), List::Kind::FOLDER, {}},
                List{tr("User Feedback & Support"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Learning"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Maintenance"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Tools & Setup"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Bugs"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Someday/Maybe"), tr(""), List::Kind::FOLDER, {}}
            }
        },
        UseCaseTemplate {
            tr("Junior Software Developer"),
            tr("Entry-level developer focusing on feature implementation, bug fixes, and skill growth"),
            {
                List{tr("Inbox"), tr("New tasks and learning items"), List::Kind::FOLDER, {}},
                List{tr("Projects"), tr("Assigned tasks and small projects"), List::Kind::FOLDER, {
                                                                                                      List{tr("Sample Task Project"), tr("Example project"), List::Kind::PROJECT, {
                                                                                                                                                                                      List{tr("Fix bug #123"), tr("Resolve assigned bug"), List::Kind::TASK, {}},
                                                                                                                                                                                      List{tr("Implement feature X"), tr("Add new small feature"), List::Kind::TASK, {}}
                                                                                                                                                                                  }}
                                                                                                  }},
                List{tr("Learning & Training"), tr("Tutorials, courses, reading"), List::Kind::FOLDER, {}},
                List{tr("Code Reviews"), tr("Review feedback and action items"), List::Kind::FOLDER, {}},
                List{tr("Mentorship"), tr("Pair programming and guidance"), List::Kind::FOLDER, {}},
                List{tr("Development"), tr("Feature work and fixes"), List::Kind::FOLDER, {}},
                List{tr("Testing & QA"), tr("Write and run tests"), List::Kind::FOLDER, {}},
                List{tr("Tools & Setup"), tr("IDE, local environment"), List::Kind::FOLDER, {}},
                List{tr("Documentation"), tr("README updates, docs"), List::Kind::FOLDER, {}},
                List{tr("Someday/Maybe"), tr("Ideas to explore"), List::Kind::FOLDER, {}}
            }
        },
        UseCaseTemplate {
            tr("Senior Software Developer"),
            tr("Senior developer with end-to-end responsibilities from architecture to deployment, plus mentoring and tutoring"),
            {
                List{tr("Inbox"), tr("New tasks and requests"), List::Kind::FOLDER, {}},
                List{tr("Projects"), tr("Active development initiatives"), List::Kind::FOLDER, {
                                                                                                   List{tr("Core Platform Refactor"), tr(""), List::Kind::PROJECT, {
                                                                                                                                                                       List{tr("Define refactor plan"), tr(""), List::Kind::TASK, {}},
                                                                                                                                                                       List{tr("Implement module updates"), tr(""), List::Kind::TASK, {}}
                                                                                                                                                                   }}
                                                                                               }},
                List{tr("Architecture & Design"), tr("System and component design"), List::Kind::FOLDER, {}},
                List{tr("Development"), tr("Feature work and bug fixes"), List::Kind::FOLDER, {}},
                List{tr("DevOps & CI/CD"), tr("Build pipelines, deployments, infra as code"), List::Kind::FOLDER, {}},
                List{tr("Testing & QA"), tr("Unit, integration, and end-to-end tests"), List::Kind::FOLDER, {}},
                List{tr("Performance & Optimization"), tr("Profiling, benchmarking, tuning"), List::Kind::FOLDER, {}},
                List{tr("Security & Compliance"), tr("Code audits, best practices, audits"), List::Kind::FOLDER, {}},
                List{tr("Documentation"), tr("API docs, design docs, guides"), List::Kind::FOLDER, {}},
                List{tr("Code Reviews"), tr("Peer reviews and feedback"), List::Kind::FOLDER, {}},
                List{tr("Mentorship & Tutoring"), tr("Guidance, tutoring sessions"), List::Kind::FOLDER, {}},
                List{tr("Professional Development"), tr("Courses, conferences, certifications"), List::Kind::FOLDER, {}},
                List{tr("Tools & Setup"), tr("Local environment and tooling"), List::Kind::FOLDER, {}},
                List{tr("Maintenance"), tr("Upgrades, refactors, debt paydown"), List::Kind::FOLDER, {}},
                List{tr("Someday/Maybe"), tr("Innovations and experiments"), List::Kind::FOLDER, {}}
            }
        },
        UseCaseTemplate {
            tr("C++ / Systems Developer"),
            tr("Senior C++ and systems-level software architect with broad end-to-end responsibilities"),
            {
                List{tr("Inbox"), tr("New tasks and issues"), List::Kind::FOLDER, {}},
                List{tr("Projects"), tr("Active system and library development"), List::Kind::FOLDER, {
                                                                                                          List{tr("Daemon Service"), tr(""), List::Kind::PROJECT, {
                                                                                                                                                                      List{tr("Define IPC protocol"), tr(""), List::Kind::TASK, {}},
                                                                                                                                                                      List{tr("Implement multithreading"), tr(""), List::Kind::TASK, {}}
                                                                                                                                                                  }}
                                                                                                      }},
                List{tr("System Architecture & Design"), tr("High-level design, component interactions"), List::Kind::FOLDER, {}},
                List{tr("Performance Profiling & Optimization"), tr("Profiling, benchmarks, tuning"), List::Kind::FOLDER, {}},
                List{tr("Memory Management"), tr("Leak detection, pool allocators"), List::Kind::FOLDER, {}},
                List{tr("Build & CI/CD"), tr("Toolchains, cross-compilation, pipelines"), List::Kind::FOLDER, {}},
                List{tr("Testing & QA"), tr("Unit, integration, fuzz, regression tests"), List::Kind::FOLDER, {}},
                List{tr("Debugging & Diagnostics"), tr("Trace, logging, core dumps"), List::Kind::FOLDER, {}},
                List{tr("Security & Hardening"), tr("Code audits, sanitizers, SELinux/AppArmor"), List::Kind::FOLDER, {}},
                List{tr("Embedded Integration"), tr("Hardware interfaces, RTOS"), List::Kind::FOLDER, {}},
                List{tr("Containerization & Orchestration"), tr("Docker, Kubernetes"), List::Kind::FOLDER, {}},
                List{tr("Infrastructure as Code"), tr("Terraform, Ansible, SaltStack"), List::Kind::FOLDER, {}},
                List{tr("Monitoring & Observability"), tr("Metrics, logs, alerts"), List::Kind::FOLDER, {}},
                List{tr("Documentation"), tr("API docs, design docs, runbooks"), List::Kind::FOLDER, {}},
                List{tr("Mentorship & Leadership"), tr("Code reviews, guidance, architecture reviews"), List::Kind::FOLDER, {}},
                List{tr("Professional Development"), tr("Conferences, certifications, publications"), List::Kind::FOLDER, {}},
                List{tr("Maintenance & Refactoring"), tr("Tech debt, legacy support"), List::Kind::FOLDER, {}},
                List{tr("Someday/Maybe"), tr("Innovations and experiments"), List::Kind::FOLDER, {}}
            }
        },
        UseCaseTemplate {
            tr("DevOps / SRE Engineer"),
            tr("Ensures reliability and operational efficiency of systems"),
            {
                List{tr("Inbox"), tr("New tasks"), List::Kind::FOLDER, {}},
                List{tr("Projects"), tr(""), List::Kind::FOLDER, {
                                                                     List{tr("Monitoring Setup"), tr(""), List::Kind::PROJECT, {
                                                                                                                                   List{tr("Configure Prometheus"), tr(""), List::Kind::TASK, {}},
                                                                                                                                   List{tr("Set up Grafana dashboards"), tr(""), List::Kind::TASK, {}}
                                                                                                                               }}
                                                                 }},
                List{tr("Infrastructure as Code"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Deployment Pipelines"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Incident Response"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Capacity Planning"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Automation Scripts"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Security & Compliance"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Observability & Logging"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Disaster Recovery"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Cost Optimization"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Learning"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Maintenance"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Tools & Setup"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Bugs"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Someday/Maybe"), tr(""), List::Kind::FOLDER, {}}
            }
        },
        UseCaseTemplate {
            tr("Data Engineer / Data Scientist"),
            tr("Builds and maintains data pipelines, analytics, and machine learning workflows"),
            {
                List{tr("Inbox"), tr("New tasks"), List::Kind::FOLDER, {}},
                List{tr("Projects"), tr(""), List::Kind::FOLDER, {
                                                                     List{tr("ETL Pipeline"), tr(""), List::Kind::PROJECT, {
                                                                                                                               List{tr("Define source schemas"), tr(""), List::Kind::TASK, {}},
                                                                                                                               List{tr("Implement data transformations"), tr(""), List::Kind::TASK, {}}
                                                                                                                           }}
                                                                 }},
                List{tr("Data Modeling"), tr(""), List::Kind::FOLDER, {}},
                List{tr("ETL & Pipelines"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Data Quality & Validation"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Analytics & Reporting"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Machine Learning"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Experiment Tracking"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Data Storage & Databases"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Big Data & Stream Processing"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Visualization & Dashboarding"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Testing & QA"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Deployment & MLOps"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Security & Compliance"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Documentation"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Maintenance"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Tools & Setup"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Bugs"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Someday/Maybe"), tr(""), List::Kind::FOLDER, {}}
            }
        },
        UseCaseTemplate {
            tr("Embedded Systems Developer"),
            tr("Develops firmware and embedded systems software"),
            {
                List{tr("Inbox"), tr("New tasks"), List::Kind::FOLDER, {}},
                List{tr("Projects"), tr(""), List::Kind::FOLDER, {
                                                                     List{tr("Firmware Control"), tr(""), List::Kind::PROJECT, {
                                                                                                                                   List{tr("Implement sensor driver"), tr(""), List::Kind::TASK, {}},
                                                                                                                                   List{tr("Write integration tests"), tr(""), List::Kind::TASK, {}}
                                                                                                                               }}
                                                                 }},
                List{tr("Firmware Development"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Hardware Integration"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Real-Time OS"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Communication Protocols"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Debugging & Diagnostics"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Toolchain & Build"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Testing & QA"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Performance Profiling"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Memory Management"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Security & Hardening"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Documentation"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Release & Deployment"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Maintenance"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Tools & Setup"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Bugs"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Someday/Maybe"), tr(""), List::Kind::FOLDER, {}}
            }
        },
        UseCaseTemplate {
            tr("Game Developer"),
            tr("Designs and builds video games across platforms"),
            {
                List{tr("Inbox"), tr("New tasks"), List::Kind::FOLDER, {}},
                List{tr("Projects"), tr(""), List::Kind::FOLDER, {
                                                                     List{tr("Game Prototype"), tr(""), List::Kind::PROJECT, {
                                                                                                                                 List{tr("Design core gameplay loop"), tr(""), List::Kind::TASK, {}},
                                                                                                                                 List{tr("Implement rendering pipeline"), tr(""), List::Kind::TASK, {}}
                                                                                                                             }}
                                                                 }},
                List{tr("Game Design"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Level & World Building"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Asset Pipeline"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Engine & Rendering"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Physics & AI"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Audio & SFX"), tr(""), List::Kind::FOLDER, {}},
                List{tr("UI & UX"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Networking & Multiplayer"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Performance & Profiling"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Testing & QA"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Build & Deployment"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Tooling & Editor Extensions"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Community & Feedback"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Documentation"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Learning"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Maintenance"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Bugs"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Someday/Maybe"), tr(""), List::Kind::FOLDER, {}}
            }
        },
        UseCaseTemplate {
            tr("QA / Test Automation Engineer"),
            tr("Ensures software quality through testing and automation"),
            {
                List{tr("Inbox"), tr("New tasks"), List::Kind::FOLDER, {}},
                List{tr("Projects"), tr(""), List::Kind::FOLDER, {
                                                                     List{tr("Automation Framework"), tr(""), List::Kind::PROJECT, {
                                                                                                                                       List{tr("Implement test runner"), tr(""), List::Kind::TASK, {}},
                                                                                                                                       List{tr("Write login tests"), tr(""), List::Kind::TASK, {}}
                                                                                                                                   }}
                                                                 }},
                List{tr("Test Plans & Cases"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Automation Scripts"), tr(""), List::Kind::FOLDER, {}},
                List{tr("CI/CD Integration"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Bug Tracking & Reporting"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Performance Testing"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Security Testing"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Tooling & Frameworks"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Environment Setup"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Reporting & Metrics"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Documentation"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Maintenance"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Learning"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Tools & Setup"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Bugs"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Someday/Maybe"), tr(""), List::Kind::FOLDER, {}}
            }
        },
        UseCaseTemplate {
            tr("Cloud / Infrastructure Engineer"),
            tr("Manages and optimizes cloud infrastructure and services"),
            {
                List{tr("Inbox"), tr("New tasks"), List::Kind::FOLDER, {}},
                List{tr("Projects"), tr(""), List::Kind::FOLDER, {
                                                                     List{tr("Infrastructure Deployment"), tr(""), List::Kind::PROJECT, {
                                                                                                                                            List{tr("Define VPC architecture"), tr(""), List::Kind::TASK, {}},
                                                                                                                                            List{tr("Deploy Kubernetes cluster"), tr(""), List::Kind::TASK, {}}
                                                                                                                                        }}
                                                                 }},
                List{tr("Infrastructure as Code"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Containerization & Orchestration"), tr(""), List::Kind::FOLDER, {}},
                List{tr("CI/CD Pipelines"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Monitoring & Logging"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Security & Compliance"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Networking & Connectivity"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Cost Management"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Disaster Recovery & Backup"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Cloud Provider Services"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Performance & Optimization"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Incident Management"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Documentation"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Maintenance"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Tools & Setup"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Bugs"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Someday/Maybe"), tr(""), List::Kind::FOLDER, {}}
            }
        },
        UseCaseTemplate {
            tr("Network Engineer"),
            tr("Designs and manages network infrastructure"),
            {
                List{tr("Inbox"), tr("New tasks"), List::Kind::FOLDER, {}},
                List{tr("Projects"), tr(""), List::Kind::FOLDER, {
                                                                     List{tr("Network Setup"), tr(""), List::Kind::PROJECT, {
                                                                                                                                   List{tr("Design network topology"), tr(""), List::Kind::TASK, {}},
                                                                                                                                   List{tr("Configure routers and switches"), tr(""), List::Kind::TASK, {}}
                                                                                                                               }}
                                                                 }},
                List{tr("Network Security"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Monitoring & Troubleshooting"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Performance Optimization"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Documentation & Reporting"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Learning"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Maintenance"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Tools & Setup"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Bugs"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Someday/Maybe"), tr(""), List::Kind::FOLDER, {}}
            }
        },
        UseCaseTemplate {
            tr("Consultant"),
            tr("Provides expert advice and solutions to clients"),
            {
                List{tr("Inbox"), tr("New client requests and tasks"), List::Kind::FOLDER, {}},
                List{tr("Projects"), tr(""), List::Kind::FOLDER, {
                                                                     List{tr("Client Engagement XYZ"), tr(""), List::Kind::PROJECT, {
                                                                                                                                        List{tr("Draft proposal"), tr(""), List::Kind::TASK, {}},
                                                                                                                                        List{tr("Conduct discovery session"), tr(""), List::Kind::TASK, {}}
                                                                                                                                    }}
                                                                 }},
                List{tr("Clients"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Proposals & Bidding"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Engagement Deliverables"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Billing & Invoicing"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Research & Insights"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Networking & Marketing"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Tools & Setup"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Finance & Budgeting"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Administration"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Learning & Development"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Maintenance"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Someday/Maybe"), tr(""), List::Kind::FOLDER, {}}
            }
        },
        UseCaseTemplate {
            tr("Technical Writer"),
            tr("Creates and maintains technical documentation"),
            {
                List{tr("Inbox"), tr("New tasks"), List::Kind::FOLDER, {}},
                List{tr("Projects"), tr(""), List::Kind::FOLDER, {
                                                                     List{tr("Documentation Project"), tr(""), List::Kind::PROJECT, {
                                                                                                                                   List{tr("Draft user manual"), tr(""), List::Kind::TASK, {}},
                                                                                                                                   List{tr("Review API documentation"), tr(""), List::Kind::TASK, {}}
                                                                                                                               }}
                                                                 }},
                List{tr("Documentation Standards"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Content Management Systems"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Version Control for Docs"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Collaboration & Review Process"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Learning & Development"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Maintenance"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Tools & Setup"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Bugs"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Someday/Maybe"), tr(""), List::Kind::FOLDER, {}}
            }
        },
        UseCaseTemplate {
            tr("Sales Professional"),
            tr("Manages sales pipeline, client relationships, and revenue growth"),
            {
                List{tr("Inbox"), tr("New leads and tasks"), List::Kind::FOLDER, {}},
                List{tr("Projects"), tr(""), List::Kind::FOLDER, {
                                                                     List{tr("Lead Generation Campaign"), tr(""), List::Kind::PROJECT, {
                                                                                                                                           List{tr("Define target segments"), tr(""), List::Kind::TASK, {}},
                                                                                                                                           List{tr("Create outreach email"), tr(""), List::Kind::TASK, {}}
                                                                                                                                       }}
                                                                 }},
                List{tr("Accounts / Clients"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Leads & Opportunities"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Proposals & Quotes"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Presentations & Demos"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Contract Negotiation"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Orders & Fulfillment"), tr(""), List::Kind::FOLDER, {}},
                List{tr("CRM Maintenance"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Reporting & Analytics"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Forecasting"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Follow-ups"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Marketing Collateral"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Meeting Notes"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Learning & Development"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Tools & Setup"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Administration"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Maintenance"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Someday/Maybe"), tr(""), List::Kind::FOLDER, {}}
            }
        },
        UseCaseTemplate {
            tr("Student"),
            tr("Manages coursework, study plans, and extracurricular activities"),
            {
                List{tr("Inbox"), tr("New tasks and reminders"), List::Kind::FOLDER, {}},
                List{tr("Courses"), tr(""), List::Kind::FOLDER, {
                                                                    List{tr("Capstone Project"), tr(""), List::Kind::PROJECT, {
                                                                                                                                  List{tr("Draft proposal"), tr(""), List::Kind::TASK, {}},
                                                                                                                                  List{tr("Collect research sources"), tr(""), List::Kind::TASK, {}}
                                                                                                                              }}
                                                                }},
                List{tr("Assignments"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Exams & Prep"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Lectures & Notes"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Study Plan"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Research"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Extracurricular"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Career Prep"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Finance & Budgeting"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Tools & Setup"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Maintenance"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Someday/Maybe"), tr(""), List::Kind::FOLDER, {}}
            }
        },
        UseCaseTemplate {
            tr("Freelancer"),
            tr("Manages multiple client projects and self-directed tasks"),
            {
                List{tr("Inbox"), tr("New tasks and client requests"), List::Kind::FOLDER, {}},
                List{tr("Projects"), tr(""), List::Kind::FOLDER, {
                                                                     List{tr("Client Project Alpha"), tr(""), List::Kind::PROJECT, {
                                                                                                                                       List{tr("Define scope of work"), tr(""), List::Kind::TASK, {}},
                                                                                                                                       List{tr("Send contract"), tr(""), List::Kind::TASK, {}}
                                                                                                                                   }}
                                                                 }},
                List{tr("Clients"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Proposals & Contracts"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Invoicing & Payments"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Time Tracking"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Marketing & Networking"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Tools & Resources"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Finance & Budgeting"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Learning & Skill Development"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Administration"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Maintenance"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Someday/Maybe"), tr(""), List::Kind::FOLDER, {}}
            }
        },
        UseCaseTemplate {
            tr("Entrepreneur / Startup Founder"),
            tr("Builds and scales new ventures and products"),
            {
                List{tr("Inbox"), tr("Incoming ideas and tasks"), List::Kind::FOLDER, {}},
                List{tr("Projects"), tr(""), List::Kind::FOLDER, {
                                                                     List{tr("MVP Development"), tr(""), List::Kind::PROJECT, {
                                                                                                                                  List{tr("Define core features"), tr(""), List::Kind::TASK, {}},
                                                                                                                                  List{tr("Develop prototype"), tr(""), List::Kind::TASK, {}}
                                                                                                                              }}
                                                                 }},
                List{tr("Business Model"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Market Research"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Fundraising"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Product Roadmap"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Team & Hiring"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Operations & Legal"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Marketing & Growth"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Sales & Partnerships"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Metrics & Analytics"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Customer Feedback"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Finance & Budgeting"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Pitch Deck & Presentations"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Documentation"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Maintenance"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Someday/Maybe"), tr(""), List::Kind::FOLDER, {}}
            }
        },
        UseCaseTemplate {
            tr("Executive / Leadership"),
            tr("Provides strategic direction and leads the organization"),
            {
                List{tr("Inbox"), tr("New tasks and requests"), List::Kind::FOLDER, {}},
                List{tr("Projects"), tr(""), List::Kind::FOLDER, {
                                                                     List{tr("Strategic Initiative"), tr(""), List::Kind::PROJECT, {
                                                                                                                                       List{tr("Define objectives"), tr(""), List::Kind::TASK, {}},
                                                                                                                                       List{tr("Align stakeholders"), tr(""), List::Kind::TASK, {}}
                                                                                                                                   }}
                                                                 }},
                List{tr("Strategic Planning"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Team Management"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Leadership Development"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Stakeholder Engagement"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Financial Oversight"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Operations & Risk"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Governance & Compliance"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Communication & Reporting"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Board Relations"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Talent & Succession"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Organizational Development"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Innovation & Vision"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Metrics & KPIs"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Meetings & Events"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Mentorship & Coaching"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Learning & Development"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Tools & Setup"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Maintenance"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Someday/Maybe"), tr(""), List::Kind::FOLDER, {}}
            }
        },
        UseCaseTemplate {
            tr("Product Manager"),
            tr("Defines product vision and coordinates cross-functional teams"),
            {
                List{tr("Inbox"), tr("Incoming feature requests and tasks"), List::Kind::FOLDER, {}},
                List{tr("Projects"), tr(""), List::Kind::FOLDER, {
                                                                     List{tr("New Product Launch"), tr(""), List::Kind::PROJECT, {
                                                                                                                                     List{tr("Define MVP scope"), tr(""), List::Kind::TASK, {}},
                                                                                                                                     List{tr("Align launch date"), tr(""), List::Kind::TASK, {}}
                                                                                                                                 }}
                                                                 }},
                List{tr("Roadmap"), tr(""), List::Kind::FOLDER, {}},
                List{tr("User Research"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Requirements & Specs"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Backlog"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Stakeholder Management"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Metrics & Analytics"), tr(""), List::Kind::FOLDER, {}},
                List{tr("UX & Design Collaboration"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Technical Specifications"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Competition Analysis"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Release Planning"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Go-to-Market & Marketing"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Performance Monitoring"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Documentation"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Maintenance"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Someday/Maybe"), tr(""), List::Kind::FOLDER, {}}
            }
        },
        UseCaseTemplate {
            tr("Creative Professional (Designer, Artist)"),
            tr("Manages design and artistic projects and tasks"),
            {
                List{tr("Inbox"), tr("New design ideas and tasks"), List::Kind::FOLDER, {}},
                List{tr("Projects"), tr(""), List::Kind::FOLDER, {
                                                                     List{tr("Design Portfolio"), tr(""), List::Kind::PROJECT, {
                                                                                                                                   List{tr("Curate selected works"), tr(""), List::Kind::TASK, {}},
                                                                                                                                   List{tr("Update case studies"), tr(""), List::Kind::TASK, {}}
                                                                                                                               }}
                                                                 }},
                List{tr("Branding & Identity"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Illustration & Artwork"), tr(""), List::Kind::FOLDER, {}},
                List{tr("UX / UI Design"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Prototyping & Wireframing"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Asset Management"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Feedback & Critique"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Client Work"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Exhibitions & Events"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Marketing & Promotion"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Tools & Setup"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Learning & Experimentation"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Documentation"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Maintenance"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Someday/Maybe"), tr(""), List::Kind::FOLDER, {}}
            }
        },
        UseCaseTemplate {
            tr("Writer / Author"),
            tr("Manages writing projects, research, and publishing workflows"),
            {
                List{tr("Inbox"), tr("New writing ideas and tasks"), List::Kind::FOLDER, {}},
                List{tr("Projects"), tr(""), List::Kind::FOLDER, {
                                                                     List{tr("Manuscript Draft"), tr(""), List::Kind::PROJECT, {
                                                                                                                                   List{tr("Outline chapters"), tr(""), List::Kind::TASK, {}},
                                                                                                                                   List{tr("Draft introduction"), tr(""), List::Kind::TASK, {}}
                                                                                                                               }}
                                                                 }},
                List{tr("Research & Notes"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Editorial Calendar"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Submissions & Pitches"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Editing & Proofreading"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Publishing & Distribution"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Marketing & Promotion"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Agent & Contracts"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Feedback & Reviews"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Tools & Setup"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Learning & Workshops"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Maintenance"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Someday/Maybe"), tr(""), List::Kind::FOLDER, {}}
            }
        },
        UseCaseTemplate {
            tr("Marketer / Marketing Professional"),
            tr("Plans and executes marketing strategies and campaigns"),
            {
                List{tr("Inbox"), tr("New marketing tasks and leads"), List::Kind::FOLDER, {}},
                List{tr("Projects"), tr(""), List::Kind::FOLDER, {
                                                                     List{tr("Campaign Launch"), tr(""), List::Kind::PROJECT, {
                                                                                                                                  List{tr("Define campaign goals"), tr(""), List::Kind::TASK, {}},
                                                                                                                                  List{tr("Develop creative assets"), tr(""), List::Kind::TASK, {}}
                                                                                                                              }}
                                                                 }},
                List{tr("Market Research"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Campaign Planning"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Content Creation"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Analytics & Reporting"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Social Media"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Email Marketing"), tr(""), List::Kind::FOLDER, {}},
                List{tr("SEO / SEM"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Events & Webinars"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Advertising & PPC"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Brand Management"), tr(""), List::Kind::FOLDER, {}},
                List{tr("CRM & Lead Management"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Partnerships & Sponsorships"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Tools & Setup"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Documentation"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Maintenance"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Someday/Maybe"), tr(""), List::Kind::FOLDER, {}}
            }
        },
        UseCaseTemplate {
            tr("Academic / Researcher"),
            tr("Manages research projects, publications, teaching, and academic responsibilities"),
            {
                List{tr("Inbox"), tr("New tasks and ideas"), List::Kind::FOLDER, {}},
                List{tr("Projects"), tr(""), List::Kind::FOLDER, {
                                                                     List{tr("Research Project"), tr(""), List::Kind::PROJECT, {
                                                                                                                                   List{tr("Conduct literature review"), tr(""), List::Kind::TASK, {}},
                                                                                                                                   List{tr("Define research hypothesis"), tr(""), List::Kind::TASK, {}}
                                                                                                                               }}
                                                                 }},
                List{tr("Publications & Writing"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Grants & Funding"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Conferences & Presentations"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Data & Experiments"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Collaboration & Networking"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Teaching & Lectures"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Course Planning"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Student Supervision"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Peer Review & Editing"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Library & Resources"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Grant Reporting"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Administration & Compliance"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Tools & Setup"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Maintenance"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Someday/Maybe"), tr(""), List::Kind::FOLDER, {}}
            }
        },
        UseCaseTemplate {
            tr("Teacher / Educator"),
            tr("Plans lessons, manages classrooms, and supports student learning"),
            {
                List{tr("Inbox"), tr("New tasks and reminders"), List::Kind::FOLDER, {}},
                List{tr("Projects"), tr(""), List::Kind::FOLDER, {
                                                                     List{tr("Course Curriculum"), tr(""), List::Kind::PROJECT, {
                                                                                                                                    List{tr("Design syllabus"), tr(""), List::Kind::TASK, {}},
                                                                                                                                    List{tr("Prepare first lecture"), tr(""), List::Kind::TASK, {}}
                                                                                                                                }}
                                                                 }},
                List{tr("Lesson Planning"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Classroom Management"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Student Assessment"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Grading & Feedback"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Parent Communication"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Professional Development"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Administrative Tasks"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Resources & Materials"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Curriculum Development"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Meetings & Collaboration"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Technology & Tools"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Extracurricular Activities"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Maintenance"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Someday/Maybe"), tr(""), List::Kind::FOLDER, {}}
            }
        },
        UseCaseTemplate {
            tr("Event Planner"),
            tr("Plans and coordinates events of all scales"),
            {
                List{tr("Inbox"), tr("New tasks and requests"), List::Kind::FOLDER, {}},
                List{tr("Projects"), tr(""), List::Kind::FOLDER, {
                                                                     List{tr("Event Organization"), tr(""), List::Kind::PROJECT, {
                                                                                                                                     List{tr("Define venue requirements"), tr(""), List::Kind::TASK, {}},
                                                                                                                                     List{tr("Contact vendors"), tr(""), List::Kind::TASK, {}}
                                                                                                                                 }}
                                                                 }},
                List{tr("Clients & Stakeholders"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Venues"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Vendors"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Budget & Finance"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Schedule & Timeline"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Marketing & Promotion"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Guest Management & RSVP"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Logistics & Setup"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Audio/Visual & Equipment"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Registration & Tickets"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Sponsorships & Partnerships"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Compliance & Permits"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Feedback & Surveys"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Post-Event Wrap-up"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Tools & Setup"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Maintenance"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Someday/Maybe"), tr(""), List::Kind::FOLDER, {}}
            }
        },
        UseCaseTemplate {
            tr("HR Professional"),
            tr("Manages recruitment, employee relations, and HR programs"),
            {
                List{tr("Inbox"), tr("New HR tasks and requests"), List::Kind::FOLDER, {}},
                List{tr("Projects"), tr(""), List::Kind::FOLDER, {
                                                                     List{tr("Recruitment Drive"), tr(""), List::Kind::PROJECT, {
                                                                                                                                    List{tr("Define job descriptions"), tr(""), List::Kind::TASK, {}},
                                                                                                                                    List{tr("Screen candidates"), tr(""), List::Kind::TASK, {}}
                                                                                                                                }}
                                                                 }},
                List{tr("Recruitment & Hiring"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Onboarding & Orientation"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Training & Development"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Performance Management"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Employee Relations"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Compensation & Benefits"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Compliance & Policies"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Payroll & HRIS"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Employee Engagement"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Offboarding"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Diversity & Inclusion"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Workforce Planning"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Surveys & Feedback"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Reporting & Analytics"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Tools & Setup"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Maintenance"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Someday/Maybe"), tr(""), List::Kind::FOLDER, {}}
            }
        },
        UseCaseTemplate {
            tr("Legal Professional"),
            tr("Manages legal cases, contracts, and compliance"),
            {
                List{tr("Inbox"), tr("New tasks and client requests"), List::Kind::FOLDER, {}},
                List{tr("Projects"), tr(""), List::Kind::FOLDER, {
                                                                     List{tr("Case Management"), tr(""), List::Kind::PROJECT, {
                                                                                                                                  List{tr("Review client documents"), tr(""), List::Kind::TASK, {}},
                                                                                                                                  List{tr("Draft contract"), tr(""), List::Kind::TASK, {}}
                                                                                                                              }}
                                                                 }},
                List{tr("Clients"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Contracts & Agreements"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Legal Research"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Compliance & Regulations"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Billing & Invoicing"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Court Filings & Deadlines"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Document Management"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Meetings & Hearings"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Risk Management"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Intellectual Property"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Communication & Correspondence"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Tools & Setup"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Administration"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Learning & Continuing Education"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Maintenance"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Someday/Maybe"), tr(""), List::Kind::FOLDER, {}}
            }
        },
        UseCaseTemplate {
            tr("Healthcare Professional"),
            tr("Manages patient care, clinical workflows, and administrative tasks.\nYou should not use the public server for this kind of confidential data."),
            {
                List{tr("Inbox"), tr("New patient referrals and tasks"), List::Kind::FOLDER, {}},
                List{tr("Projects"), tr(""), List::Kind::FOLDER, {
                                                                     List{tr("Patient Care Plan"), tr(""), List::Kind::PROJECT, {
                                                                                                                                    List{tr("Initial assessment"), tr(""), List::Kind::TASK, {}},
                                                                                                                                    List{tr("Develop treatment plan"), tr(""), List::Kind::TASK, {}}
                                                                                                                                }}
                                                                 }},
                List{tr("Patient Records"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Appointments & Scheduling"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Treatment Protocols"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Billing & Insurance"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Compliance & Regulations"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Telemedicine & Virtual Care"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Pharmacy & Medication"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Lab & Diagnostics"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Emergency Procedures"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Quality & Safety"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Health Education & Counseling"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Equipment & Maintenance"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Research & Continuing Education"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Team & Staff"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Tools & Setup"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Maintenance"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Someday/Maybe"), tr(""), List::Kind::FOLDER, {}}
            }
        },
        UseCaseTemplate {
            tr("Homeowner / Household Manager"),
            tr("Manages household tasks, maintenance, and family schedules"),
            {
                List{tr("Inbox"), tr("New household tasks and reminders"), List::Kind::FOLDER, {}},
                List{tr("Projects"), tr(""), List::Kind::FOLDER, {
                                                                     List{tr("Home Renovation"), tr(""), List::Kind::PROJECT, {
                                                                                                                                  List{tr("Plan kitchen remodel"), tr(""), List::Kind::TASK, {}},
                                                                                                                                  List{tr("Get contractor quotes"), tr(""), List::Kind::TASK, {}}
                                                                                                                              }}
                                                                 }},
                List{tr("Maintenance & Repairs"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Cleaning & Chores"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Budget & Bills"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Family Calendar"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Grocery & Shopping"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Utility Management"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Home Improvement"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Garden & Outdoor"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Insurance & Documents"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Health & Safety"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Pets & Vet"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Events & Hosting"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Tools & Setup"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Maintenance"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Someday/Maybe"), tr(""), List::Kind::FOLDER, {}}
            }
        },
        UseCaseTemplate {
            tr("Traveler / Trip Planner"),
            tr("Plans and organizes travel itineraries and trips"),
            {
                List{tr("Inbox"), tr("New travel ideas and tasks"), List::Kind::FOLDER, {}},
                List{tr("Trip Itineraries"), tr(""), List::Kind::FOLDER, {
                                                                             List{tr("Europe Summer 2025"), tr(""), List::Kind::PROJECT, {
                                                                                                                                             List{tr("Book flights"), tr(""), List::Kind::TASK, {}},
                                                                                                                                             List{tr("Reserve hotels"), tr(""), List::Kind::TASK, {}}
                                                                                                                                         }}
                                                                         }},
                List{tr("Destinations Research"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Flights & Transportation"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Accommodation"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Activities & Sightseeing"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Budget & Expenses"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Packing List"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Travel Documents"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Visa & Permits"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Dining & Restaurants"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Transportation Logistics"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Health & Safety"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Local Contacts"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Photos & Memories"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Travel Tips & Notes"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Maintenance"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Someday/Maybe"), tr(""), List::Kind::FOLDER, {}}
            }
        },
        UseCaseTemplate {
            tr("Influencer"),
            tr("Creates and manages social media content, community engagement, and brand partnerships"),
            {
                List{tr("Inbox"), tr("New content ideas and tasks"), List::Kind::FOLDER, {}},
                List{tr("Projects"), tr(""), List::Kind::FOLDER, {
                                                                     List{tr("Content Series"), tr(""), List::Kind::PROJECT, {
                                                                                                                                 List{tr("Plan topics"), tr(""), List::Kind::TASK, {}},
                                                                                                                                 List{tr("Shoot videos"), tr(""), List::Kind::TASK, {}}
                                                                                                                             }}
                                                                 }},
                List{tr("Content Planning"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Editorial Calendar"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Content Creation"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Graphic Design"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Video Production"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Post-Production"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Social Media Channels"), tr(""), List::Kind::FOLDER, {
                                                                                  List{tr("Instagram"), tr(""), List::Kind::FOLDER, {}},
                                                                                  List{tr("TikTok"), tr(""), List::Kind::FOLDER, {}},
                                                                                  List{tr("YouTube"), tr(""), List::Kind::FOLDER, {}},
                                                                                  List{tr("Blog"), tr(""), List::Kind::FOLDER, {}},
                                                                                  List{tr("Telegram"), tr(""), List::Kind::FOLDER, {}},
                                                                                  List{tr("Substack"), tr(""), List::Kind::FOLDER, {}},
                                                                              }},
                List{tr("Community Engagement"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Partnerships & Sponsorships"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Brand Collaborations"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Analytics & Insights"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Monetization"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Events & Meetups"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Legal & Compliance"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Admin & Finance"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Tools & Equipment"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Learning & Trends"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Maintenance"), tr(""), List::Kind::FOLDER, {}},
                List{tr("Someday/Maybe"), tr(""), List::Kind::FOLDER, {}}
            }
        },
        UseCaseTemplate {
            tr("Life and Work"),
            tr("Balanced template for personal and professional tasks"),
            {
                List{tr("Private"), tr("Personal life and well-being"), List::Kind::FOLDER, {
                    List{tr("Home"), tr("Household tasks and maintenance"), List::Kind::FOLDER, {}},
                    List{tr("Health"), tr("Wellness and medical"), List::Kind::FOLDER, {}},
                    List{tr("Family"), tr("Family activities and commitments"), List::Kind::FOLDER, {}},
                    List{tr("Garden"), tr("Outdoor and gardening tasks"), List::Kind::FOLDER, {}},
                    List{tr("Car"), tr("Vehicle maintenance and appointments"), List::Kind::FOLDER, {}},
                    List{tr("Exercise"), tr("Physical activity and routines"), List::Kind::FOLDER, {}},
                    List{tr("Hobbies"), tr("Personal projects and leisure"), List::Kind::FOLDER, {}},
                    List{tr("Finance"), tr("Personal finances and budgeting"), List::Kind::FOLDER, {}},
                    List{tr("Travel"), tr("Vacation planning and itineraries"), List::Kind::FOLDER, {}},
                    List{tr("Social"), tr("Events and social engagements"), List::Kind::FOLDER, {}},
                    List{tr("Personal Development"), tr("Learning and growth"), List::Kind::FOLDER, {}}
                }},
                List{tr("Work"), tr("Professional tasks and projects"), List::Kind::FOLDER, {
                    List{tr("Inbox"), tr("Work-related incoming tasks"), List::Kind::FOLDER, {}},
                    List{tr("Projects"), tr("Active professional projects"), List::Kind::FOLDER, {
                         List{tr("Sample Project"), tr("Example professional project"), List::Kind::PROJECT, {
                             List{tr("Kickoff meeting"), tr("Initial meeting with stakeholders"), List::Kind::TASK, {}},
                             List{tr("Define deliverables"), tr("Outline project deliverables"), List::Kind::TASK, {}}
                         }}
                    }},
                    List{tr("Clients"), tr("Client relationships and contacts"), List::Kind::FOLDER, {}},
                    List{tr("Administration"), tr("Admin, invoicing, and contracts"), List::Kind::FOLDER, {}},
                    List{tr("Marketing & Networking"), tr("Promotions and connections"), List::Kind::FOLDER, {}},
                    List{tr("Professional Development"), tr("Skills and learning"), List::Kind::FOLDER, {}},
                    List{tr("Tools & Setup"), tr("Software and environment setup"), List::Kind::FOLDER, {}},
                    List{tr("Finance"), tr("Business finances and budgeting"), List::Kind::FOLDER, {}},
                    List{tr("Someday/Maybe"), tr("Future ideas and possibilities"), List::Kind::FOLDER, {}}
                }}
            }
        },
        UseCaseTemplate {
            tr("Software Architect"),
            tr("Defines system architecture, ensures alignment of technology solutions with business goals"),
            {
                List{tr("Inbox"), tr("New requests and ideas"), List::Kind::FOLDER, {}},
                List{tr("Projects"), tr("Active architecture initiatives"), List::Kind::FOLDER, {
                                                                                                    List{tr("Architecture Review"), tr(""), List::Kind::PROJECT, {
                                                                                                                                                                     List{tr("Conduct system design review"), tr(""), List::Kind::TASK, {}},
                                                                                                                                                                     List{tr("Draft architecture document"), tr(""), List::Kind::TASK, {}}
                                                                                                                                                                 }}
                                                                                                }},
                List{tr("Architecture & Standards"), tr("Design principles, patterns, guidelines"), List::Kind::FOLDER, {}},
                List{tr("Technology Evaluation"), tr("Assess frameworks, platforms, tools"), List::Kind::FOLDER, {}},
                List{tr("Integration & APIs"), tr("System interfaces and contracts"), List::Kind::FOLDER, {}},
                List{tr("System Modeling"), tr("UML, diagrams, prototypes"), List::Kind::FOLDER, {}},
                List{tr("Scalability & Performance"), tr("Load considerations, caching"), List::Kind::FOLDER, {}},
                List{tr("Security & Compliance"), tr("Architectural security, audits"), List::Kind::FOLDER, {}},
                List{tr("DevOps & Infrastructure"), tr("Deployment patterns, IaC"), List::Kind::FOLDER, {}},
                List{tr("Documentation & Knowledge Base"), tr("Runbooks, wikis, decisions"), List::Kind::FOLDER, {}},
                List{tr("Research & Innovation"), tr("Proofs of concept, R&D"), List::Kind::FOLDER, {}},
                List{tr("Governance & Risk Management"), tr("Policies, risk assessments"), List::Kind::FOLDER, {}},
                List{tr("Mentorship & Advisory"), tr("Guidance, reviews, workshops"), List::Kind::FOLDER, {}},
                List{tr("Collaboration & Stakeholders"), tr("Meetings, alignments"), List::Kind::FOLDER, {}},
                List{tr("Monitoring & Observability"), tr("Metrics, tracing, dashboards"), List::Kind::FOLDER, {}},
                List{tr("Maintenance & Refactoring"), tr("Tech debt, improvements"), List::Kind::FOLDER, {}},
                List{tr("Tools & Setup"), tr("Architectural tools and environments"), List::Kind::FOLDER, {}},
                List{tr("Someday/Maybe"), tr("Future architecture explorations"), List::Kind::FOLDER, {}}
            }
        },
    };

    // sort templates, based on UseCaseTemplate::name
    ranges::sort(templates_, [](const UseCaseTemplate &a, const UseCaseTemplate &b) {
        return a.name < b.name;
    });

    LOG_INFO << "We have " << templates_.size() << " use-case templates";
}

QStringList UseCaseTemplates::getTemplateNames() const noexcept
{
    QStringList names = {tr("-- None --")};
    for (const auto &template_ : templates_) {
        names.append(template_.name);
    }
    return names;
}

namespace {


void add(nextapp::pb::NodeTemplate& nt, const UseCaseTemplates::List &list)
{
    static constexpr auto kinds = to_array({
        nextapp::pb::Node::Kind::FOLDER,
        nextapp::pb::Node::Kind::ORGANIZATION,
        nextapp::pb::Node::Kind::PERSON,
        nextapp::pb::Node::Kind::PROJECT,
        nextapp::pb::Node::Kind::TASK
    });

    auto children = nt.children();
    nextapp::pb::NodeTemplate child;
    child.setName(list.name);
    child.setDescr(list.description);
    child.setKind(kinds.at(list.kind));

    for(const auto& c : list.children) {
        add(child, c);
    }

    children.push_back(std::move(child));
    nt.setChildren(std::move(children));
}

} // namespace)


void UseCaseTemplates::createFromTemplate(int index)
{
    if (index < 1 || index > templates_.size()) {
        return;
    }

    --index;

    LOG_INFO <<"Creating nodes from template #" << index << ": " << templates_[index].name;

    // Convert our internal representation of the template to the protobuf representation
    const auto &t = templates_[index];
    nextapp::pb::NodeTemplate root;
    for(const auto& list: t.lists) {
        add(root, list);
    };

    ServerComm::instance().createNodesFromTemplate(root);
}

QString UseCaseTemplates::getDescription(int index)
{
    if (index < 1 || index > templates_.size()) {
        return {};
    }

    --index;

    LOG_INFO <<"Getting description for template #" << index << ": " << templates_[index].name;

    return templates_[index].description;
}
