#!/bin/bash

cd /development/playground/alembic

source $SOFTWARE/bluebolt/config/env.sh

echo Removing current playground build from $PWD ...
rm -rf BB_Alembic

git clone /git/BB_Alembic.git BB_Alembic

cd BB_Alembic

git submodule init
git submodule update
git submodule foreach `echo $path 'git checkout master'`
git submodule foreach `echo $path 'git pull'`

cd ..

mkdir ALEMBIC_BUILD
cd ALEMBIC_BUILD


python ../BB_Alembic/build/bootstrap/alembic_bootstrap.py --prefix=/development/software/alembic/$UNAME.$DIST.$ARCH/ --shared --with-maya=$MAYA_LOCATION --with-prman=/software/3delight/linux.centos5.x86_64/10.0.7/Linux-x86_64/ --with-arnold=/software/arnold/linux.centos6.x86_64/4.0.6.0/ --boost_include_dir=/software/boost/linux.centos5.x86_64/1.47/include --boost_thread_library=/software/boost/linux.centos5.x86_64/1.47/lib/libboost_thread.so  --hdf5_include_dir=/disk1/playpen/dev/alembic/3rdparty/hdf5-1.8.7/build/include --hdf5_hdf5_library=/disk1/playpen/dev/alembic/3rdparty/hdf5-1.8.7/build/lib/libhdf5_hl.so --ilmbase_include_dir=/disk1/playpen/dev/alembic/3rdparty/build/ilmbase/include/OpenEXR --ilmbase_imath_library=/disk1/playpen/dev/alembic/3rdparty/build/ilmbase/lib/libImath.so --zlib_include_dir=/usr/include --zlib_library=/usr/lib64/libz.so --cxxflags=-I/software/glew/linux.centos5.x86_64/1.6.0/include/ --cmake_flags="-D CMAKE_CXX_COMPILER:FILEPATH=/software/wrappers/g++412 -D CMAKE_C_COMPILER:FILEPATH=/software/wrappers/gcc412 -D Boost_LIBRARY_DIRS:FILEPATH=/development/software/boost/linux.centos5.x86_64/1.47/lib -D Boost_PROGRAM_OPTIONS_LIBRARY:FILEPATH=/software/boost/linux.centos5.x86_64/1.47/lib/libboost_program_options.so -D Boost_PYTHON_LIBRARY:FILEPATH=/software/boost/linux.centos5.x86_64/1.47/lib/libboost_python.so" .


make -j 8 install

