cmake_minimum_required(VERSION 3.1)

message("")
message("--LIBEXODUS CMAKE INIT --")

project(libexodus)

#use CMakeList.txt options in parent directory
SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-undef")

#######################################
# c++ version 17 or 20 if on Ubuntu 20+
#######################################
execute_process(
        COMMAND         grep "Ubuntu 2" /etc/lsb-release
        OUTPUT_VARIABLE OUTVAR
        ERROR_VARIABLE  ERRVAR
        RESULT_VARIABLE RESVAR
)
if(RESVAR EQUAL "0")
        SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++2a")
else()
        SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17")
endif()

#2014/06/01 exodus doesnt require c++11 but can compile using it
#NB exodus cli "compile" hard codes compilation as c++11 so exodus applications can be written in c++11
#even if libexodus is compiled and linked to c++99 boost libraries etc
#config is set to look in /usr/local/include and /usr/local/lib first for any source version of boost compiled for c++11
#not using c++11 for now until solve problem linking to boost filesystem unless boost libs were compiled in c++11
#perhaps using some BOOST_NO_CXX11_SCOPED_ENUMS macro
#line #define BOOST_NO_CXX11_SCOPED_ENUMS before boost/filesystem is included
#said to be not a good idea to link c++11 lib to pre c+11 libs (whether this applies to exodus annd boost is unknown)
#SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")

### POSTGRES ###
################
##set(PostgreSQL_ADDITIONAL_VERSIONS "9.3" "9.3.4")
##find_package(PostgreSQL REQUIRED)
##find_package(PostgreSQL REQUIRED COMPONENTS pqlib)
#message(PostgreSQL_INCLUDE_DIRS=${PostgreSQL_INCLUDE_DIRS})
execute_process(
        COMMAND                 pg_config --includedir
        OUTPUT_VARIABLE         PostgreSQL_INCLUDE_DIR
        OUTPUT_STRIP_TRAILING_WHITESPACE
)
message(PostgreSQL_INCLUDEDIR=${PostgreSQL_INCLUDE_DIR})
include_directories(. .. ${PostgreSQL_INCLUDE_DIRS} ${PostgreSQL_INCLUDE_DIR} ../../../fast_float/include)

### BOOST ###
#############
#date_time appears to be header only in later boost versions eg 55
#find_package(Boost REQUIRED COMPONENTS filesystem date_time system regex thread)
#find_package(Boost REQUIRED COMPONENTS filesystem system regex thread)
#find_package(Boost REQUIRED COMPONENTS system regex thread locale)
#find_package(Boost REQUIRED COMPONENTS filesystem system regex locale)
#find_package(Boost REQUIRED COMPONENTS filesystem regex locale)
find_package(Boost REQUIRED COMPONENTS date_time system regex thread locale)
message("BUT LOOKING IN /usr/local/lib and /usr/local/include FIRST!")

set(Boost_USE_STATIC_LIBS        ON)
set(Boost_USE_MULTITHREADED      ON)
set(Boost_USE_STATIC_RUNTIME     ON)

### ICU ###
###########
#FindICU.cmake in cmake directory look in three levels of parent directories of source too
#set(CMAKE_MODULE_PATH "${CMAKE_MODULE_PATH};cmake;${PROJECT_SOURCE_DIR}/cmake/;${PROJECT_SOURCE_DIR}/../cmake/;${PROJECT_SOURCE_DIR}/../../cmake;${PROJECT_SOURCE_DIR}/../../../cmake")
set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}")
#note that displayed path confusingly shows no space or ; between multiple paths if multiple are specified either by space or ;
message(CMAKE_MODULE_PATH=${CMAKE_MODULE_PATH})
#NOICU find_package(ICU 4.4 REQUIRED)

### RYU ###
###########
find_library(RYU_LIBRARIES NAMES ryu)
message(${RYU_LIBRARIES})
#find_library(RYULIBRARIES NAMES ryu ryu_parse)

### define an output called exodus of type shared library and list all its source files###
add_library(exodus SHARED
# mvutf.cpp
# ConvertUTF.c
# mvlocale.cpp
 mvdbpostgres.cpp mvregex.cpp mvos.cpp mvos2.cpp mvprogram.cpp mvmv.cpp
 mv.cpp mvnumeric.cpp mvmath.cpp mvdim.cpp mvdatetime.cpp mvdebug.cpp mviter.cpp
 mvfuncs.cpp mvioconv.cpp
 mvenvironment.cpp mvglobalfuncs.cpp
 mvwait.cpp mvunicode.cpp mvposix.cpp mvfunctor.cpp mvbrackets.cpp
# mvipc.cpp mvipc_boost.cpp mvipc_posix.cpp mvipc_ms.cpp
 MurmurHash2_64.cpp naturalorder.cpp gettimeofday.cpp
 mvhandles.cpp mvdbconns.cpp mvprocessno.cpp
 getkey.cpp haskey.cpp echo.cpp cursor.cpp howto.cpp
)

#set_target_properties(exodus PROPERTIES SOVERSION "${MAJOR_VERSION}.${MINOR_VERSION}.${BUILD_VERSION}")
set_target_properties(exodus PROPERTIES SOVERSION "21.03")

### specify what libraries are to be linked in when building libexodus ###

message("Boost_LIBRARIES=${Boost_LIBRARIES}")
#postgres libraries seem not to be set but we include -pq directly to solve
message("PostgreSQL_LIBRARIES=${PostgreSQL_LIBRARIES}")
message("PostgreSQL_LIBRARY_DIR=${PostgreSQL_LIBRARY_DIR}")
#NOICU message("ICU_LIBRARIES=${ICU_LIBRARIES}")
#pq is needed for postgresql client library code
#dl is needed for dynamic link library code
#target_link_libraries(exodus boost_filesystem -L/usr/local/lib ${Boost_LIBRARIES} ${PostgreSQL_LIBRARIES} ${PostgreSQL_LIBRARY_DIR} ${ICU_LIBRARIES} pq dl)
#if (RYU_LIBRARIES STREQUAL "NOTFOUND")
if (RYU_LIBRARIES STREQUAL "RYU_LIBRARIES-NOTFOUND")
target_link_libraries(exodus -L/usr/local/lib ${Boost_LIBRARIES} ${PostgreSQL_LIBRARIES} ${PostgreSQL_LIBRARY_DIR} ${ICU_LIBRARIES} pq dl)
else()
target_link_libraries(exodus -L/usr/local/lib ${Boost_LIBRARIES} ${PostgreSQL_LIBRARIES} ${PostgreSQL_LIBRARY_DIR} ${ICU_LIBRARIES} pq dl ${RYU_LIBRARIES})
endif()

### state that the output libexodus should be installed in the default system library

### CMAKE_INSTALL_PREFIX defaults to /usr/local/
install(TARGETS exodus DESTINATION lib)
##install(TARGETS exodus DESTINATION /usr/lib)

### install some of exodus' include files so programs can be written in exodus c++ ###

install(FILES
 mv.h
 exodus.h
 exodusmacros.h
 mvenvironment.h
 exodusfuncs.h
 #mvimpl.h mvipc.h mvconfig.h mvlink.h
 mvfunctor.h
 program.h
 library.h
 dict.h
 common.h
 mvprogram.h
 ioconv_custom.h
DESTINATION include/exodus)

message("--LIBEXODUS CMAKE EXIT --")
