#!/bin/bash

cd /development/playground/alembic

source $SOFTWARE/bluebolt/config/env.sh

rm -rf BB_Alembic

git clone /net/git/disk2/git/BB_Alembic.git BB_Alembic

cd BB_Alembic

git submodule init
git submodule update
git submodule foreach `echo $path 'git pull'`
git submodule foreach `echo $path 'git checkout master'`

cd ..

mkdir ALEMBIC_BUILD
cd ALEMBIC_BUILD

python ../BB_Alembic/build/bootstrap/alembic_bootstrap.py --shared  --prefix=/development/playground/alembic/ALEMBIC_INSTALL --with-maya=$MAYA_LOCATION --with-prman=$DELIGHT --disable-arnold --boost_include_dir=/development/software/boost/linux.centos5.x86_64/1.47/include --boost_thread_library=/development/software/boost/linux.centos5.x86_64/1.47/lib/libboost_thread.so  --hdf5_include_dir=/development/playground/alembic/3rdparty/hdf5-1.8.6/build/include --hdf5_hdf5_library=/development/playground/alembic/3rdparty/hdf5-1.8.6/build/lib/libhdf5_hl.a --ilmbase_include_dir=/development/playground/alembic/3rdparty/build/ilmbase/include/OpenEXR --ilmbase_imath_library=/development/playground/alembic/3rdparty/build/ilmbase/lib/libImath.so --zlib_include_dir=/usr/include --zlib_library=/usr/lib64/libz.so --cxxflags="-I/development/playground/maya/include -I/software/glew/linux.centos5.x86_64/1.6.0/include/ -L/software/glew/linux.centos5.x86_64/1.6.0/lib64/" --cmake_flags="-D CMAKE_CXX_COMPILER:FILEPATH=/software/wrappers/g++412 -D CMAKE_C_COMPILER:FILEPATH=/software/wrappers/gcc412 -D Boost_LIBRARY_DIRS:FILEPATH=/development/software/boost/linux.centos5.x86_64/1.47/lib -D Boost_PROGRAM_OPTIONS_LIBRARY:FILEPATH=/development/software/boost/linux.centos5.x86_64/1.47/lib/libboost_program_options.so -D Boost_PYTHON_LIBRARY:FILEPATH=/development/software/boost/linux.centos5.x86_64/1.47/lib/libboost_python.so -D GLUT_Xi_LIBRARY:FILEPATH=/usr/lib64/libXi.so.6 -D GLUT_Xmu_LIBRARY:FILEPATH=/usr/lib64/libXmu.so.6 -D PYTHON_ROOT:FILEPATH=/development/software/linux.centos5.x86_64/2.6.4 -D ALEMBIC_PYTHON_EXECUTABLE:FILEPATH=/development/software/linux.centos5.x86_64/2.6.4/bin/python" .

make -j 8 install

