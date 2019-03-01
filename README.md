# verbar

`verbar` is my status bar for the `dwm` window manager. It watches several
things:

- Dropbox status
- Wi-Fi status
- Wired network status
- CPU usage
- Memory usage
- Battery charge
- Volume (PulseAudio only)
- Time

`verbar` has the following dependencies:

- libmnl
- PulseAudio

The installation path and compilation flags can be tweaked by editing
`config.mk`. Then, run the usual

```
make
make install
```
