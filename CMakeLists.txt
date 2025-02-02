#use make VERBOSE=1 to debug make

cmake_minimum_required(VERSION 3.1)

#update version in mv.h and libexodus/exodus/CMakeLists.txt
set(CPACK_PACKAGE_CONTACT neo)
set(CPACK_PACKAGE_VERSION 21.03.1)
INCLUDE(CPack)

#######################################
# c++ version 17 or 20 if on Ubuntu 20+
#######################################
#execute_process(
#        COMMAND         grep "Ubuntu 2" /etc/lsb-release
#        OUTPUT_VARIABLE OUTVAR
#        ERROR_VARIABLE  ERRVAR
#        RESULT_VARIABLE RESVAR
#)
#if(RESVAR EQUAL "0")
#        SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++2a")
#else()
#        SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17")
#endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

############################################
# remove -o1 for gdb to debug into libexodus
############################################
# SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -g -march=native -O3 -fvisibility=hidden")
#SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -g -march=native -O3")            #5% faster than -O2
#SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -g -march=native -O2")            #little difference from -O1 running test_main
SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -g -march=native -O1")
#SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -g -march=native -Og")             #5% slower than -01 but supposedly better debugging
#SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -g -march=native -O0")
#SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -g -march=native")
#SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-unknown-pragmas") #switched off to avoid various warnings
#SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wpedantic")           #switched off to avoid warning about elvis operator ?:
#SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -no-pie")

# Turn off some warnings
SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-unknown-warning-option")
SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-cast-function-type")

SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fvisibility=hidden") # Force g++ to hide by default
#SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -flto")               # Link time optimisation. very slow and no observed performance increase

#SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fprofile-arcs -ftest-coverage")

#root@de1:~/exodus/cli/src# ll /usr/local/lib/libexodus.so* -h
#lrwxrwxrwx 1 root root  18 Sep 11 15:37 /usr/local/lib/libexodus.so -> libexodus.so.21.03
#-rw-r--r-- 1 root root 32M Jan 27 04:49 /usr/local/lib/libexodus.so.21.03

message("CMAKE_CXX_FLAGS=${CMAKE_CXX_FLAGS}")

message("CMAKE_CXX_FLAGS_DEBUG is ${CMAKE_CXX_FLAGS_DEBUG}")
message("CMAKE_CXX_FLAGS_RELEASE is ${CMAKE_CXX_FLAGS_RELEASE}")

#allow exodus executable and exodus library
#cmake_policy(SET CMP0002 OLD)

#if using custom postgres then configure and build it to /usr/local
#./configure --prefix=/usr/local --eprefix=/usr/local
#otherwise it builds to rather none-standard /usr/local/pgsql
#and exodus make cannot find it easily

#ld -lpq --verbose to debug library paths ... also check /etc/ld.so.conf.d

#for VERY non-standard postgres installation path use
#message("export PostgreSQL_ROOT=/usr/local/pgsql/")
#message("export PATH=$PATH:/usr/local/pgsql/bin/")
#message("export LIBRARY_PATH=/usr/local/pgsql/lib/")

project(exodus)

#execute_process(
#        COMMAND                 pg_config --pkglibdir
#        OUTPUT_VARIABLE         PostgreSQL_PKGLIBDIR
#        OUTPUT_STRIP_TRAILING_WHITESPACE
#)
#message(PostgreSQL_PKGLIBDIR=${PostgreSQL_PKGLIBDIR})

#execute_process(
#        COMMAND                 pg_config --includedir-server
#        OUTPUT_VARIABLE         PostgreSQL_INCLUDEDIR_SERVER
#        OUTPUT_STRIP_TRAILING_WHITESPACE
#)
#message(PostgreSQL_INCLUDEDIR_SERVER=${PostgreSQL_INCLUDEDIR_SERVER})

include(CTest)
enable_testing()

#no need to install header only library
#add_subdirectory(fast_float)

add_subdirectory(exodus/libexodus/exodus)
add_subdirectory(exodus/libpgexodus/src)
add_subdirectory(cli/src)
add_subdirectory(test/src)

# uninstall target
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake_uninstall.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake"
    IMMEDIATE @ONLY)

add_custom_target(uninstall
    COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake)
