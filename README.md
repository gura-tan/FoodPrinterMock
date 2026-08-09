# FoodPrinterMock

## Build

Enter docker env

```
cd ~/projects/FoodPrinterMock
DEV=/dev/ttyACM0;docker run --rm -it -v ${PWD}:/workspaces/FoodPrinterMock -w /workspaces/FoodPrinterMock --device=${DEV} --group-add $(stat -c '%g' ${DEV}) ghcr.io/wurly200a/builder-esp32/esp-idf-v5.5:5.5.5
```
In the docker env

```
idf.py build
idf.py flash
```
