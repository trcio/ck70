# ck70 (control k70)

native, standalone macOS program to control key colors for an old corsair k70 mk.2 keyboard

- iCUE is spyware
- [OpenRGB](https://gitlab.com/CalcProgrammer1/OpenRGB/) is full of stuff that doesnt apply to me
- why can't I just set the color of my keyboard from the command line????

this repo is the isolated logic from OpenRGB necessary to do that

this repo can be further repurposed to implement animations, remote triggers, etc. all the fun stuff

# build

```sh
chmod +x build.sh
./build.sh
```

you need xcode command line tools installed (you will likely be prompted to install them after running build.sh)

confirmed working on m2 max, macOS 14.2.1

# usage

```sh
./ck70 <r> <g> <b>
```

```sh
./ck70 255 255 255 # full brightness white
```
