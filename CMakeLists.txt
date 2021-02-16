#[[
   Copyright (c) 2021 Sibi Siddharthan

   Distributed under the MIT license.
   Refer to the LICENSE file at the root directory of the parent project
   for details.
]]

cmake_minimum_required(VERSION 3.12)
project(libksba
		VERSION 1.5.0
		LANGUAGES C)

add_compile_definitions(HAVE_CONFIG_H)

set(PACKAGE \"${PROJECT_NAME}\")
set(PACKAGE_BUGREPORT \"https://bugs.gnupg.org\")
set(PACKAGE_NAME \"${PROJECT_NAME}\")
set(PACKAGE_STRING \"${PROJECT_NAME}\ ${PROJECT_VERSION}\")
set(PACKAGE_TARNAME \"${PROJECT_NAME}\")
set(PACKAGE_URL \"\")
set(PACKAGE_VERSION \"${PROJECT_VERSION}\")
set(VERSION \"${PROJECT_VERSION}\")

#find_package(GPGRT REQUIRED)
include(UtilityFunctions)
include(CheckCSourceRuns)
include(alloca)
include(visibility)
include(LargeFiles)
include(inline)

set(BUILD_REVISION \"\")
set(BUILD_TIMESTAMP \"\")

set(GPG_ERR_SOURCE_DEFAULT GPG_ERR_SOURCE_KSBA)
if(WIN32)
	set(HAVE_VISIBILITY 0)
	set(HAVE_W32_SYSTEM 1)
	set(HAVE_W64_SYSTEM 1)
endif()

if(HAVE_VISIBILITY)
	set(KSBA_USE_VISIBILITY 1)
endif()

check_c_headers(dlfcn.h inttypes.h memory.h stdint.h stdlib.h strings.h string.h
sys/stat.h sys/types.h unistd.h)

check_functions(getenv gmtime_r memmove stpcpy strchr strtol strtoul)

check_types("unsigned int" "unsigned long" size_t u32)

if(NOT HAVE_SIZE_T)
	set(size_t "unsigned\ long\ long")
endif()

check_c_source_runs("
int
find_stack_direction (int *addr, int depth)
{
  int dir, dummy = 0;
  if (! addr)
    addr = &dummy;
  *addr = addr < &dummy ? 1 : addr == &dummy ? 0 : -1;
  dir = depth ? find_stack_direction (addr, depth - 1) : 0;
  return dir + dummy;
}

int
main (int argc, char **argv)
{
  return find_stack_direction (0, argc + !argv + 20) < 0;
}"
STACK_DIRECTION)
if(NOT STACK_DIRECTION)
	set(STACK_DIRECTION -1)
endif()

configure_file(libksba-config.h.in config.h)
set(VERSION ${PROJECT_VERSION})
set(VERSION_NUMBER 0x010500)
configure_file(src/ksba.h.in ${CMAKE_BINARY_DIR}/ksba.h @ONLY)
configure_file(src/ksba.h.in ${CMAKE_CURRENT_SOURCE_DIR}/src/ksba.h @ONLY)

include_directories(${CMAKE_CURRENT_BINARY_DIR} src)

set(asn1_gentables_SOURCES
asn1-gentables.c asn1-parse.c asn1-func.c gen-help.c)
list(TRANSFORM asn1_gentables_SOURCES PREPEND "src/")
add_executable(asn1-gentables ${asn1_gentables_SOURCES})
target_compile_definitions(asn1-gentables PRIVATE BUILD_GENTOOLS)

add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/asn1-tables.c
		COMMAND asn1-gentables ${CMAKE_CURRENT_SOURCE_DIR}/src/tmttv2.asn ${CMAKE_CURRENT_SOURCE_DIR}/src/cms.asn
		> ${CMAKE_CURRENT_BINARY_DIR}/asn1-tables.c
		DEPENDS asn1-gentables ${CMAKE_CURRENT_SOURCE_DIR}/src/tmttv2.asn ${CMAKE_CURRENT_SOURCE_DIR}/src/cms.asn)
add_custom_target(gen_tables DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/asn1-tables.c)

set(ksba_SOURCES
ksba.h
visibility.c visibility.h
reader.c reader.h
writer.c writer.h
asn1-parse.c
asn1-func.c asn1-func2.c asn1-func.h asn1-constants.h
ber-help.c ber-help.h
ber-decoder.c ber-decoder.h
der-encoder.c der-encoder.h
der-builder.c der-builder.h
cert.c cert.h
cms.c cms.h cms-parser.c
crl.c crl.h
certreq.c certreq.h
ocsp.c ocsp.h
keyinfo.c keyinfo.h
oid.c name.c dn.c time.c convert.h stringbuf.h
version.c util.c util.h shared.h
sexp-parse.h)
#TODO generate asn1-parse.c with bison

list(TRANSFORM ksba_SOURCES PREPEND "src/")
list(APPEND ksba_SOURCES ${CMAKE_CURRENT_BINARY_DIR}/asn1-tables.c)
if(WIN32)
	list(APPEND ksba_SOURCES src/libksba.def)
endif()


add_library(ksba SHARED ${ksba_SOURCES})
add_dependencies(ksba gen_tables)
target_link_libraries(ksba ${links} GPGRT::GPGRT)
set_target_properties(ksba PROPERTIES PUBLIC_HEADER ${CMAKE_BINARY_DIR}/ksba.h)
set_target_properties(ksba PROPERTIES POSITION_INDEPENDENT_CODE TRUE)
set_dll_output_directory(ksba ${CMAKE_BINARY_DIR})
add_library(KSBA::KSBA ALIAS ksba)

set(ber_dump_SOURCES
ber-dump.c
ber-decoder.c ber-help.c reader.c writer.c asn1-parse.c
asn1-func.c oid.c time.c util.c)

list(TRANSFORM ber_dump_SOURCES PREPEND "src/")
add_executable(ber-dump ${ber_dump_SOURCES})
target_link_libraries(ber-dump ksba GPGRT::GPGRT)

install(TARGETS ksba
		RUNTIME DESTINATION bin
		LIBRARY DESTINATION lib
		ARCHIVE DESTINATION lib
		PUBLIC_HEADER DESTINATION include)

#install(FILES doc/ksba.info DESTINATION share/info)

include(CTest)
if(BUILD_TESTING)

file(CONFIGURE OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/oidtranstbl.h
CONTENT
"static struct { char *oid, *desc, *comment; } oidtranstbl[] = {
	{ NULL, NULL, NULL }
	};"
NEWLINE_STYLE LF)

set(tests
cert-basic t-crl-parser t-dnparser t-oid t-reader t-cms-parser t-der-builder)

foreach(t ${tests})
	add_executable(${t} tests/${t}.c)
	target_link_libraries(${t} ksba)
	add_test(NAME ${t} COMMAND ${t} WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tests)
	if(WIN32)
		set_tests_properties(${t} PROPERTIES ENVIRONMENT "PATH=${CMAKE_BINARY_DIR}\;${NEW_PATH}")
	endif()
endforeach()

add_executable(t-ocsp tests/t-ocsp.c tests/sha1.c)
target_link_libraries(t-ocsp ksba)

endif()
