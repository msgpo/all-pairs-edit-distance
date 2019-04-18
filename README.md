# All Pairs Edit Distance

This repository contains a command line tool that takes a Genie-formatted dataset (id, sentence,
program, tab-separated), and computes a file of edit distances.

I am uploading this not because it's particularly useful, but because a friend was curious.

## Building

```
meson _build --buildtype=debugoptimized
ninja -C _build
sudo ninja -C _build install
```

## Usage

```
all-pairs-edit-distance <input file> <output file>
```

## License

GPLv3 or later.