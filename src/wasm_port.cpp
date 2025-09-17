#ifdef __EMSCRIPTEN__

#include "wasm_port.h"
#include "log.h"

#include <wblib/utils.h>

#include <emscripten/emscripten.h>
#include <emscripten/val.h>

#define LOG(logger) logger.Log() << "[wasm port] "

void TWASMPort::WriteBytes(const uint8_t* buffer, int count)
{
    // clang-format off
    EM_ASM(
    {
        let data = new Uint8Array($1);

        for (let i = 0; i < $1; ++i) {
            data[i] = getValue($0 + i, 'i8');
        }

        Asyncify.handleAsync(async() => { await Module.serial.write(data); });
    },
    buffer, count);
    // clang-format on

    LOG(Debug) << "write " << count << " bytes: " << WBMQTT::HexDump(buffer, count);
}

TReadFrameResult TWASMPort::ReadFrame(uint8_t* buffer,
                                      size_t count,
                                      const std::chrono::microseconds& responseTimeout,
                                      const std::chrono::microseconds& frameTimeout,
                                      TFrameCompletePred frame_complete)
{
    // clang-format off
    auto length = EM_ASM_INT(
    {
        let result = Asyncify.handleAsync(async() => { return await Module.serial.read($1); });

        if (!(result instanceof Uint8Array)) {
            return 0;
        }

        for (let i = 0; i < result.length; ++i) {
            setValue($0 + i, result[i], 'i8');
        }

        return result.length;
    },
    buffer, count);
    // clang-format on

    if (!length) {
        throw std::runtime_error("request timed out");
    }

    TReadFrameResult res;
    res.Count = length;

    LOG(Debug) << "read " << length << " bytes: " << WBMQTT::HexDump(buffer, length);
    return res;
}

void TWASMPort::ApplySerialPortSettings(const TSerialPortConnectionSettings& settings)
{
    // clang-format off
    EM_ASM(
    {
        Module.serial.setOptions($0, $1, $2, $3);
    },
    settings.BaudRate, settings.DataBits, settings.Parity, settings.StopBits);
    // clang-format on

    LOG(Debug) << "set options: " << settings.BaudRate << " " << settings.DataBits << "-" << settings.Parity << "-"
               << settings.StopBits;
}

#endif
