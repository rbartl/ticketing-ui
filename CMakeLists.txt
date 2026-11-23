cmake_minimum_required(VERSION 3.21)
project(ticketing-ui VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets Network Sql)

qt_add_executable(ticketing-ui WIN32 MACOSX_BUNDLE
    src/main.cpp
    src/Account.h            src/Account.cpp
    src/Adf.h                src/Adf.cpp
    src/Auth.h               src/Auth.cpp
    src/JiraClient.h         src/JiraClient.cpp
    src/Backend.h
    src/JiraBackend.h        src/JiraBackend.cpp
    src/Database.h           src/Database.cpp
    src/SyncEngine.h         src/SyncEngine.cpp
    src/AccountContext.h     src/AccountContext.cpp
    src/IssueListModel.h     src/IssueListModel.cpp
    src/IssueView.h          src/IssueView.cpp
    src/Theme.h              src/Theme.cpp
    src/resources.qrc
    src/AccountPanel.h       src/AccountPanel.cpp
    src/ProjectChecklist.h   src/ProjectChecklist.cpp
    src/MainWindow.h         src/MainWindow.cpp
)

# OAuth: broker URL (Cloudflare Worker, see broker/) or own app credentials.
# All can also be set at runtime in <AppData>/oauth.json.
set(JIRA_OAUTH_BROKER_URL "https://jira-oauth.edv-bartl.at" CACHE STRING "URL of the OAuth broker worker")
set(JIRA_OAUTH_CLIENT_ID "" CACHE STRING "Atlassian OAuth 2.0 client id")
set(JIRA_OAUTH_CLIENT_SECRET "" CACHE STRING "Atlassian OAuth 2.0 client secret")
target_compile_definitions(ticketing-ui PRIVATE
    APP_VERSION="${PROJECT_VERSION}"
    JIRA_OAUTH_BROKER_URL="${JIRA_OAUTH_BROKER_URL}"
    JIRA_OAUTH_CLIENT_ID="${JIRA_OAUTH_CLIENT_ID}"
    JIRA_OAUTH_CLIENT_SECRET="${JIRA_OAUTH_CLIENT_SECRET}")

target_link_libraries(ticketing-ui PRIVATE Qt6::Widgets Qt6::Network Qt6::Sql)
