CMAKE_MINIMUM_REQUIRED (VERSION 2.8.12)

PROJECT(hdf5serie)

### The version number
SET (hdf5serie_MAJOR_VERSION 2)
SET (hdf5serie_MINOR_VERSION 0)
SET (hdf5serie_PATCH_VERSION 0)
set(hdf5serie_VERSION ${hdf5serie_MAJOR_VERSION}.${hdf5serie_MINOR_VERSION}.${hdf5serie_PATCH_VERSION})

#############################################
### Options #################################
#############################################

#############################################
### Search for dependencies #################
#############################################
### Use PkgConfig for searching
include(FindPkgConfig) 

### search for hdf5
#find_package(hdf5 REQUIRED)

### Search for fmatvec
pkg_check_modules(FMATVEC REQUIRED fmatvec>=2.0.0)
include_directories(${FMATVEC_INCLUDE_DIRS})
string (REPLACE ";" " " FMATVEC_CFLAGS_STR "${FMATVEC_CFLAGS}")

set(CMAKE_CPP_FLAGS "${CMAKE_CPP_FLAGS} -Wall -Werror -pedantic-errors -Wno-long-long")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=iso9899:1990")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++98")

#############################################
### Include the subdirectories ##############
#############################################
add_subdirectory(hdf5serie)

#############################################
###  Installing #############################
#############################################
###  Install the pkg-config file
configure_file("hdf5serie.pc.in" "hdf5serie.pc" @ONLY)
install(FILES       "${CMAKE_CURRENT_BINARY_DIR}/hdf5serie.pc"
        DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/pkgconfig")
