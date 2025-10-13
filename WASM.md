Сборка `jinja` шаблнов:
```
wbdev root make templates
```

Сборка модуля WASM с помощью официального [Docker-образа](https://hub.docker.com/r/emscripten/emsdk) Emscripten _(для сборки нужны исходники `libwbmqtt1`)_:
```
docker run --rm -v $(PWD):/src -v $(PWD)/../libwbmqtt1/wblib:/src/wblib -u $(id -u):$(id -g) emscripten/emsdk emmake make -f wasm.mk
```
