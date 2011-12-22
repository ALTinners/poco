#!/bin/sh
#
# Author: Cristian Greco <cristian@debian.org>

ENABLED_TEST_LIBS="Crypto Data Data/SQLite Foundation Net Util XML"
DISABLED_TEST_LIBS="Data/MySQL Data/ODBC NetSSL_OpenSSL Zip"

# copy libraries to temp dir
TESTDIR=$(mktemp -d /tmp/pocotest.XXXXXX)
LIBSDIR=$(find lib/ -name libPocoFoundation.so -printf '%h')
cp -d ${LIBSDIR}/* ${TESTDIR}

# additional files needed for the Foundation testsuite
FILE_TESTAPP=$(find Foundation/testsuite/ -type f -name TestApp)
FILE_TESTLIB=$(find Foundation/testsuite/ -type f -name TestLibrary.so)
cp ${FILE_TESTAPP} ${FILE_TESTLIB} ${TESTDIR}

# run tests
for testlib in ${ENABLED_TEST_LIBS}; do
  TESTRUNNER=$(find ${testlib}/testsuite -type f -name testrunner)
  cp ${TESTRUNNER} ${TESTDIR}
  (
    cd ${TESTDIR}; \
    echo "[Running test: ${testlib}]"; \
    export LD_LIBRARY_PATH=.:${LD_LIBRARY_PATH}; \
    ./testrunner -all; \
    echo; \
  )
done

rm -rf ${TESTDIR}