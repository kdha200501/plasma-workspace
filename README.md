# How to

##### Install dependencies

```shell
$ sudo dnf builddep plasma-workspace
```



##### Compile and install `kwin`

```shell
$ rpm -q plasma-workspace
$ git checkout customize/v<x.y.z>
$ cmake -DCMAKE_INSTALL_PREFIX="/usr" -DBUILD_TESTING=OFF -B "$PWD/build/" -S "$PWD/"
$ make -C "$PWD/build/" -j"$(nproc)"
$ sudo make -C "$PWD/build/" install DESTDIR=/
```





# Customization

##### Show the logout dialog in the primary screen, only



##### Always show the `krunner` prompt in the primary screen



##### Customize digital clock
