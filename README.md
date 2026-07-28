# FoodPrinterMock

## Build

Enter docker env

```
cd ~/projects
DEV=/dev/ttyACM0;docker run --rm -it -v ${PWD}:/workspaces -w /workspaces --device=${DEV} --group-add $(stat -c '%g' ${DEV}) ghcr.io/wurly200a/builder-esp32/esp-idf-v5.5:latest
```
In the docker env

```
cd FoodPrinterMock
```

```
idf.py build
idf.py flash
```
