apt -y install gnupg python3-numpy python-dev-is-python3

echo "Installing vulkan-sdk ..."
wget -qO - http://packages.lunarg.com/lunarg-signing-key-pub.asc | apt-key add -
wget -qO /etc/apt/sources.list.d/lunarg-vulkan-focal.list \
         http://packages.lunarg.com/vulkan/lunarg-vulkan-focal.list
apt update
apt -y install vulkan-sdk

mkdir -p build
cd build

cmake_version=3.21.4
cmake_dir=cmake-${cmake_version}-linux-x86_64
cmake_archive=${cmake_dir}.tar.gz
echo "Downloading cmake"
wget https://github.com/Kitware/CMake/releases/download/v${cmake_version}/${cmake_archive}
tar -xvf ${cmake_archive}
rm ${cmake_archive}
echo "Moving cmake to build/cmake-current"
mv ${cmake_dir} cmake-current

cd ..

