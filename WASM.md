Сборка `jinja` шаблнов _(для сблоки нужна утилита `j2`)_:
```
make templates
```

Сборка модуля WASM с помощью официального [Docker-образа](https://hub.docker.com/r/emscripten/emsdk) Emscripten _(для сборки нужны исходники `libwbmqtt1` из [этой](https://github.com/wirenboard/libwbmqtt1/compare/master...feature/SOFT-5928-wasm) ветки)_:
```
docker run --rm -v $(PWD):/src -v $(PWD)/../libwbmqtt1/wblib:/src/wblib -u $(id -u):$(id -g) emscripten/emsdk emmake make -f wasm.mk
```
