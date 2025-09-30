# How to

##### branch off from the upstream tag corresponding to the latest package version

```shell
$ cd plasma-workspace
$ git checkout master
$ ./create-branch.sh
```

##### Sort out dependencies

```shell
$ sudo group install "development-tools"
$ sudo dnf builddep plasma-workspace
$ mkdir build
$ cmake -B ./build -S . -DCMAKE_INSTALL_PREFIX="/usr" -DCMAKE_BUILD_TYPE="Release" -DBUILD_TESTING=OFF -DKDE_INSTALL_USE_QT_SYS_PATHS=ON -DCMAKE_INSTALL_LIBDIR=lib64 -DQT_MAJOR_VERSION=6 -DKDE_INSTALL_SYSCONFDIR=/etc -DKDE_INSTALL_LOCALSTATEDIR=/var -DKDE_INSTALL_LIBEXECDIR=libexec -DPAM_OS_CONFIGURATION="fedora"
```

##### Compile and install `plasma-workspace`

```shell
$ cd build
$ make -j"$(nproc)"
$ sudo make install
```