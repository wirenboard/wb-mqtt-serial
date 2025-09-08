#ifdef __EMSCRIPTEN__

#include "wasm_port.h"
#include "log.h"

#include <iomanip>
#include <iostream>
#include <sstream>
#include <unistd.h>
#include <wblib/utils.h>

#include <emscripten/emscripten.h>
#include <emscripten/val.h>

#define LOG(logger) logger.Log() << "[port] "

void TWASMPort::WriteBytes(const uint8_t* buffer, int count)
{
    // clang-format off
    EM_ASM(
        {
            let data = new Array();
            for (let i = 0; i < $1; ++i) {
                data.push(getValue($0 + i, 'i8'));
            }
            Asyncify.handleAsync(async() => { await Port.write(new Uint8Array(data)); });
        },
        buffer, count
    );
    // clang-format on

    // LOG(Info) << ">>> " << WBMQTT::HexDump(buffer, count) << "\r\n";
}

TReadFrameResult TWASMPort::ReadFrame(uint8_t* buffer,
                                      size_t count,
                                      const std::chrono::microseconds& responseTimeout,
                                      const std::chrono::microseconds& frameTimeout,
                                      TFrameCompletePred frame_complete)
{
    TReadFrameResult res;

    usleep(100 * 1000);

    // clang-format off
    auto success = EM_ASM_INT(
        {
            return Asyncify.handleAsync(async() => {
                return await Port.read($0);
            });
        },
        static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(responseTimeout).count())
    );
    // clang-format on

    if (!success) {
        throw std::runtime_error("request timed out");
    }

    // clang-format off
    res.Count = EM_ASM_INT(
        {
            let length = Port.data?.byteLength ?? 0;
            for (let i = 0; i < Math.min(length, $1); ++i) {
                setValue($0 + i, Port.data[i], 'i8');
            }
            return length;
        },
        buffer, count
    );
    // clang-format on

    // LOG(Info) << "<<< " << WBMQTT::HexDump(buffer, res.Count) << "\r\n";
    return res;
}

#endif
