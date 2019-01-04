cmake_minimum_required(VERSION 2.8.12)

set(CMAKE_THREAD_PREFER_PTHREAD TRUE)
find_package(Threads REQUIRED)

if (CMAKE_USE_PTHREADS_INIT)
    set(HAVE_PTHREAD 1)
endif ()

find_package(Curses)

IF (CURSES_FOUND)
    SET(HAVE_CURSES 1)
ENDIF ()

# Link required libraries for symbol checks
list(APPEND CMAKE_REQUIRED_DEFINITIONS -D_GNU_SOURCE)

# Set configurable flags for runtime.
OPTION(USE_MYSQL "Enable building with MySQL" OFF)
OPTION(USE_HELICS "Enable building with HELICS" OFF)
OPTION(USE_FNCS "Enable building with FNCS" OFF)
OPTION(USE_OPENMP "Enable building with OpenMP" OFF)


SET(GL_ZeroMQ_DIR ${ZeroMQ_DIR} CACHE PATH "ZeroMQ library path used by Gridlab-d build process.")
SET(GL_CZMQ_DIR ${CZMQ_DIR} CACHE PATH "CZMQ library path used by Gridlab-d build process.")
SET(GL_Boost_DIR ${Boost_DIR} CACHE PATH "Boost library path used by Gridlab-d build process.")
SET(GL_HELICS_DIR ${HELICS_DIR} CACHE PATH "HELICS library path used by Gridlab-d build process.")
SET(GL_FNCS_DIR ${FNCS_DIR} CACHE PATH "FNCS library path used by Gridlab-d build process.")

# Configure MySQL to be configured and linked if available.
IF (USE_MYSQL)
    INCLUDE(CMakeScripts/FindMySQL.cmake)
    IF (FOUND_MYSQL)
        SET(HAVE_MYSQL ON CACHE INTERNAL "")
    ENDIF ()
ENDIF ()

IF (USE_HELICS)
    IF (GL_Boost_DIR)
        set(BOOST_ROOT ${Boost_DIR} CACHE PATH "Boost internal library root")
        set(Boost_NO_SYSTEM_PATHS on CACHE BOOL "Do not search system for Boost")
    ENDIF ()

    find_package(Boost
            REQUIRED
            COMPONENTS
            chrono
            date_time
            filesystem
            program_options
            system
            timer
            unit_test_framework
            )

    FIND_LIBRARY(GL_ZMQ_LIBRARY NAMES zmq PATHS ${GL_ZeroMQ_DIR})
    FIND_LIBRARY(GL_HELICS_LIBRARY NAMES helics-shared PATHS ${GL_HELICS_DIR})
    IF (Boost_FOUND)
        SET(GL_HELICS_LIBRARIES
                ${GL_HELICS_LIBRARY}
                ${GL_ZMQ_LIBRARY}
                ${GL_Boost_LIBRARIES}
                )
        IF (GL_ZMQ_LIBRARY AND GL_Boost_LIBRARIES AND GL_HELICS_LIBRARY)
            SET(HAVE_HELICS TRUE)
        ELSE ()
            MESSAGE(FATAL_ERROR "One of the required HELICS libraries could not be located. Check your configuration for \
            GL_ZeroMQ_DIR, \
            GL_Boost_DIR, \
            and GL_HELICS_DIR.")
        ENDIF ()
    ELSE ()
        MESSAGE("A library required for HELICS could not be located. Please ensure HELICS_DIR and ZeroMQ_DIR are \
defined, and that Boost_DIR is defined if a custom version of Boost is being used.")
    ENDIF (Boost_FOUND)
ENDIF (USE_HELICS)

IF (USE_FNCS)
    FIND_LIBRARY(GL_ZMQ_LIBRARY NAMES zmq PATHS ${GL_ZeroMQ_DIR})
    FIND_LIBRARY(GL_CZMQ_LIBRARY NAMES czmq PATHS ${GL_CZMQ_DIR})
    FIND_LIBRARY(GL_FNCS_LIBRARY NAMES fncs PATHS ${GL_FNCS_DIR})

    SET(GL_FNCS_LIBRARIES
            ${GL_FNCS_LIBRARY}
            ${GL_CZMQ_LIBRARY}
            ${GL_ZMQ_LIBRARY}
            )
    IF (GL_ZMQ_LIBRARY AND GL_CZMQ_LIBRARY AND GL_FNCS_LIBRARY)
        SET(HAVE_FNCS TRUE)
    ELSE ()
        MESSAGE(FATAL_ERROR "One of the required FNCS libraries could not be located. Check your configuration for \
            GL_ZeroMQ_DIR, \
            GL_CZMQ_DIR, \
            and GL_FNCS_DIR.")
    ENDIF ()
ENDIF ()

# Configure OpenMP to be compiled and linked if available.
if (WITH_OPENMP)
    include(FindOpenMP)
    if (OPENMP_FOUND)
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${OpenMP_C_FLAGS}")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${OpenMP_CXX_FLAGS}")
        set(CMAKE_LD_FLAGS "${CMAKE_LD_FLAGS} ${OpenMP_LD_FLAGS}")
    endif ()
ENDIF () # WITH_OPENMP
