cmake_minimum_required(VERSION 2.6)
project(inotify_client_server)

add_executable(inotify_client_server iNotify_Check.c socket_client.c)

install(TARGETS inotify_client_server RUNTIME DESTINATION bin)
