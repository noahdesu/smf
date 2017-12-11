#!/bin/bash

set -e

if [ ! -z ${DOCKER_IMAGE+x} ]; then
  # this builds a docker image that contains all the dependencies contained in
  # the install-deps.sh file, which is used as the base image for running tests
  # to avoid re-install for ever run. the base image is rebuilt anytime the
  # install-deps.sh file is changed.
  depid=$(md5sum ${TRAVIS_BUILD_DIR}/install-deps.sh | cut -c -32)
  depimg=smf_test_deps_${DOCKER_IMAGE//:/_}_${depid}
  if [[ "$(docker images -q ${depimg} 2> /dev/null)" == "" ]]; then
    # remove the cidfile file to make docker run happy
    cidfile=$(mktemp); rm ${cidfile}
    docker run -it --cidfile=${cidfile} \
      -e USE_CLANG=1 \
      -v ${TRAVIS_BUILD_DIR}/install-deps.sh:/tmp/install-deps.sh \
      ${DOCKER_IMAGE} /tmp/install-deps.sh
    cid=$(cat ${cidfile})
    docker commit ${cid} ${depimg}
    rm ${cidfile}
  fi

  docker run --rm -v ${TRAVIS_BUILD_DIR}:/smf:z,ro \
    -e USE_CLANG -it -w /smf ${depimg} \
    /bin/bash -c "./ci/run.sh"
else
  ${TRAVIS_BUILD_DIR}/install-deps.sh
  ${TRAVIS_BUILD_DIR}/ci/run.sh
fi
