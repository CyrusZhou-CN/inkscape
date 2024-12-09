Snap
====

This directory is used for building the snap (https://snapcraft.io/) package of Inkscape.

Each commit to master sends a new build to the "edge" version.

For build status and logs, see https://launchpad.net/~ted/+snap/inkscape-master. That account on launchpad.net is owned by Ted Gould <ted@gould.cx>.

If the snap does no longer build or run, the most probable reason is that we added a new dependency. Have a look at the recent changes in https://gitlab.com/inkscape/inkscape-ci-docker, and try to make a similar change to `build-packages` (build dependency) or `stage-packages` (runtime dependency) in `snapcraft.yaml`.

## Building locally

The following instructions assume that you already have the Inkscape repository cloned (recursively, that means including submodules).
Open a terminal in the top folder of the Inkscape repository.

First setup:
```
# Set up snapcraft
sudo snap install --classic snapcraft
```

Build:
```
# Make sure the build starts fresh, without any leftovers from the previous build.
# (Makes it much slower, but can be helpful to avoid weird errors.)
# You can skip this step for quick experiments.
snapcraft clean
# Build
snapcraft
```

```
# Make sure that no apt version of Inkscape is installed
sudo apt remove inkscape
# Install the build result locally (Adjust filename accordingly)
sudo snap install --dangerous inkscape_1.5-dev_amd64.snap
# Fix access to .config/inkscape directory (TODO why is this needed)
sudo snap connect inkscape:dot-config-inkscape
```

### Troubleshooting

```
The 'snap' directory is meant specifically for snapcraft, but it contains
the following non-snapcraft-related paths:
- README.md

This is unsupported and may cause unexpected behavior. If you must store
these files within the 'snap' directory, move them to 'snap/local'
```
Just ignore the message.


```
LXD is required but not installed. Do you wish to install LXD and configure it with the defaults? [y/N]:
```
Answer `y` <Enter>.


```
craft-providers error: Failed to install LXD: user must be manually added to 'lxd' group before using LXD.
```
Run `sudo adduser $USERNAME lxd` , then log out and log in again.
