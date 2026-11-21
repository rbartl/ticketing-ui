cmake_minimum_required(VERSION 3.21)
project(jira-client VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets Network Sql)
find_package(Qt6Keychain REQUIRED)

qt_add_executable(jira-client WIN32 MACOSX_BUNDLE
    src/main.cpp
    src/Account.h            src/Account.cpp
    src/Adf.h                src/Adf.cpp
    src/Auth.h               src/Auth.cpp
    src/JiraClient.h         src/JiraClient.cpp
    src/Database.h           src/Database.cpp
    src/SyncEngine.h         src/SyncEngine.cpp
    src/AccountContext.h     src/AccountContext.cpp
    src/IssueListModel.h     src/IssueListModel.cpp
    src/AccountDialog.h      src/AccountDialog.cpp
    src/MainWindow.h         src/MainWindow.cpp
)

# OAuth: broker URL (Cloudflare Worker, see broker/) or own app credentials.
# All can also be set at runtime in <AppData>/oauth.json.
set(JIRA_OAUTH_BROKER_URL "" CACHE STRING "URL of the OAuth broker worker")
set(JIRA_OAUTH_CLIENT_ID "" CACHE STRING "Atlassian OAuth 2.0 client id")
set(JIRA_OAUTH_CLIENT_SECRET "" CACHE STRING "Atlassian OAuth 2.0 client secret")
target_compile_definitions(jira-client PRIVATE
    JIRA_OAUTH_BROKER_URL="${JIRA_OAUTH_BROKER_URL}"
    JIRA_OAUTH_CLIENT_ID="${JIRA_OAUTH_CLIENT_ID}"
    JIRA_OAUTH_CLIENT_SECRET="${JIRA_OAUTH_CLIENT_SECRET}")

target_link_libraries(jira-client PRIVATE Qt6::Widgets Qt6::Network Qt6::Sql qt6keychain)
