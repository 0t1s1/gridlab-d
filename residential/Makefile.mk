pkglib_LTLIBRARIES += residential/residential.la

residential_residential_la_CPPFLAGS =
residential_residential_la_CPPFLAGS += $(AM_CPPFLAGS)

residential_residential_la_LDFLAGS =
residential_residential_la_LDFLAGS += $(AM_LDFLAGS)

residential_residential_la_LIBADD =
residential_residential_la_LIBADD += -ldl
residential_residential_la_LIBADD += transactional_memory/libtm.la

residential_residential_la_SOURCES =
residential_residential_la_SOURCES += residential/appliance.cpp
residential_residential_la_SOURCES += residential/appliance.h
residential_residential_la_SOURCES += residential/clotheswasher.cpp
residential_residential_la_SOURCES += residential/clotheswasher.h
residential_residential_la_SOURCES += residential/dishwasher.cpp
residential_residential_la_SOURCES += residential/dishwasher.h
residential_residential_la_SOURCES += residential/dryer.cpp
residential_residential_la_SOURCES += residential/dryer.h
residential_residential_la_SOURCES += residential/evcharger.cpp
residential_residential_la_SOURCES += residential/evcharger_det.cpp
residential_residential_la_SOURCES += residential/evcharger_det.h
residential_residential_la_SOURCES += residential/evcharger.h
residential_residential_la_SOURCES += residential/freezer.cpp
residential_residential_la_SOURCES += residential/freezer.h
residential_residential_la_SOURCES += residential/house_a.cpp
residential_residential_la_SOURCES += residential/house_a.h
residential_residential_la_SOURCES += residential/house_e.cpp
residential_residential_la_SOURCES += residential/house_e.h
residential_residential_la_SOURCES += residential/init.cpp
residential_residential_la_SOURCES += residential/lights.cpp
residential_residential_la_SOURCES += residential/lights.h
residential_residential_la_SOURCES += residential/main.cpp
residential_residential_la_SOURCES += residential/microwave.cpp
residential_residential_la_SOURCES += residential/microwave.h
residential_residential_la_SOURCES += residential/occupantload.cpp
residential_residential_la_SOURCES += residential/occupantload.h
residential_residential_la_SOURCES += residential/plugload.cpp
residential_residential_la_SOURCES += residential/plugload.h
residential_residential_la_SOURCES += residential/range.cpp
residential_residential_la_SOURCES += residential/range.h
residential_residential_la_SOURCES += residential/refrigerator.cpp
residential_residential_la_SOURCES += residential/refrigerator.h
residential_residential_la_SOURCES += residential/residential_enduse.cpp
residential_residential_la_SOURCES += residential/residential_enduse.h
residential_residential_la_SOURCES += residential/residential.h
residential_residential_la_SOURCES += residential/rbsa2014.h
residential_residential_la_SOURCES += residential/solvers.cpp
residential_residential_la_SOURCES += residential/solvers.h
residential_residential_la_SOURCES += residential/thermal_storage.cpp
residential_residential_la_SOURCES += residential/thermal_storage.h
residential_residential_la_SOURCES += residential/waterheater.cpp
residential_residential_la_SOURCES += residential/waterheater.h
residential_residential_la_SOURCES += residential/zipload.cpp
residential_residential_la_SOURCES += residential/zipload.h
residential_residential_la_SOURCES += residential/elcap1990.h
residential_residential_la_SOURCES += residential/elcap2010.h
