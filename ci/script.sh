#!/bin/bash

set -e
set -x

if [ ! -z ${DOCKER_IMAGE+x} ]; then
  docker run --rm -v ${TRAVIS_BUILD_DIR}:/smf:z,ro \
    -w /smf ${DOCKER_IMAGE} \
    /bin/bash -c "./install-deps.sh && ./ci/run.sh"
else
  ${TRAVIS_BUILD_DIR}/install-deps.sh
  ${TRAVIS_BUILD_DIR}/ci/run.sh
fi
