#!/bin/bash
set -e
set -x

THIS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR=${THIS_DIR}/../

BUILD_DIR=$(mktemp -d)
INSTALL_DIR=$(mktemp -d)
TEST_DIR=$(mktemp -d)
trap "rm -rf ${BUILD_DIR} ${INSTALL_DIR} ${TEST_DIR}" EXIT

if [ -n "${USE_CLANG}" ]; then
  export CC=clang
  export CXX=clang++
fi

if [ -n "${USE_NINJA}" ]; then
  CMAKE_GENERATOR="Ninja"
  MAKE=ninja
  MAKE_J=
else
  CMAKE_GENERATOR="Unix Makefiles"
  MAKE=make
  MAKE_J="-j$(nproc)"
fi

# configure/build/install
pushd ${BUILD_DIR}
cmake -G "${CMAKE_GENERATOR}" \
  -DCMAKE_INSTALL_PREFIX=${INSTALL_DIR} \
  ${ROOT_DIR}
${MAKE} ${MAKE_J}
${MAKE} install
echo "Installation size: `du -sh ${INSTALL_DIR}`"

export PATH=${INSTALL_DIR}/bin:${PATH}

# ctest is great, but it's really useful to run the tests with the installed
# binaries to catch errors with linking, or any accidentally embedded paths

it_names=(
wal_writer
histograms
rpc
rpc_recv_timeout
wal
clock_pro
cr
)

it_dirs=(
wal_writer
histograms
rpc
rpc_recv_timeout
wal
wal_clock_pro_cache
chain_replication_utils
)

for idx in "${!it_names[@]}"; do
  mkdir ${TEST_DIR}/${idx}
  pushd ${TEST_DIR}/${idx}

  bname=${it_names[$idx]}_integration_test
  dname=${ROOT_DIR}/src/integration_tests/${it_dirs[$idx]}
  python ${ROOT_DIR}/src/test_runner.py \
    --type integration \
    --binary ${bname} \
    --directory ${dname} \
    --git_root ${ROOT_DIR}

  popd
done
