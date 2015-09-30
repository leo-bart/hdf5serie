#############################################
### Include the subdirectories
#############################################

### add the project library with the sources
add_library(hdf5serie SHARED 
    toh5type.cc 
    file.cc 
    group.cc 
    interface.cc
    simpleattribute.cc
    simpledataset.cc
    vectorserie.cc)
    
SET_TARGET_PROPERTIES(hdf5serie PROPERTIES COMPILE_FLAGS "${FMATVEC_CFLAGS_STR}")
target_link_libraries(hdf5serie ${FMATVEC_LDFLAGS} rt)

# Create a config.h due to the checks made before
configure_file ("${PROJECT_SOURCE_DIR}/hdf5serie/config.h.in"
                "${PROJECT_BINARY_DIR}/config.h" )
# Include the path when compiling
include_directories("${PROJECT_BINARY_DIR}")
include_directories("${CMAKE_SOURCE_DIR}")

#############################################
### Install the library
#############################################

install(TARGETS hdf5serie
  RUNTIME DESTINATION bin
  LIBRARY DESTINATION lib)

install(FILES 
  toh5type.h 
  file.h 
  group.h 
  interface.h 
  interface_creatoroperator_iter.h
  simple.h
  simpleattribute.h
  simpledataset.h
  vectorserie.h 
  knownpodtypes.def
  knowntypes.def
  
  DESTINATION include/hdf5serie/
  )
