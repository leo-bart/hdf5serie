SUBDIRS = . dump check

lib_LTLIBRARIES = libhdf5serie.la
libhdf5serie_la_SOURCES = toh5type.cc file.cc group.cc interface.cc \
  simpleattribute.cc \
  simpledataset.cc \
  vectorserie.cc
#  matrixserie.cc

hdf5serieincludedir = $(includedir)/hdf5serie
libhdf5serie_la_CPPFLAGS = -I$(top_srcdir) $(FMATVEC_CFLAGS)
libhdf5serie_la_LIBADD   = $(FMATVEC_LIBS) -l@BOOST_FILESYSTEM_LIB@ -l@BOOST_SYSTEM_LIB@ $(LIBS)

hdf5serieinclude_HEADERS = toh5type.h file.h group.h interface.h \
  simple.h \
  simpleattribute.h \
  simpledataset.h\
  vectorserie.h \
  knownpodtypes.def \
  knowntypes.def
#  structserie.h
#  matrixserie.h
