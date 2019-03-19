#!/bin/bash

# This script needs to be edited to bump new master version to new develop for new release.
# - run this script after running bump_master_version.sh and merging develop into master
# - run this script in master branch 
# - after running this script merge master into develop 

OLD_HIPBLAS_VERSION="12.3.0"
NEW_HIPBLAS_VERSION="13.3.0"

sed -i "s/${OLD_HIPBLAS_VERSION}/${NEW_HIPBLAS_VERSION}/g" CMakeLists.txt
