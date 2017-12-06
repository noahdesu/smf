#!/bin/bash

set -e
set -x

THIS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR=${THIS_DIR}/../

export TRAVIS_BRANCH=
export TRAVIS_OS_NAME="linux"
export TRAVIS_BUILD_DIR=${ROOT_DIR}

IMAGES="
fedora:27
fedora:26
fedora:25
ubuntu:zesty
ubuntu:artful
debian:stretch
debian:sid
debian:buster
"

for img in ${IMAGES}; do
  if [[ ${img} != -* ]]; then
    DOCKER_IMAGE=${img} ci/script.sh
  fi
done
