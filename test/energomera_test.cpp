#include <string>
#include "fake_serial_port.h"
#include "devices/energomera_iec_device.h"

namespace
{
    class TEnergomeraIntegrationTest: public TSerialDeviceIntegrationTest, public virtual TExpectorProvider
    {
    protected:
        const char* ConfigPath() const
        {
            return "configs/config-energomera-test.json";
        }

        std::string GetTemplatePath() const override
        {
            return "../wb-mqtt-serial-templates";
        }
    
        void EnqueuePollRequests()
        {
            Expector()->Expect(
                ExpectVectorFromString("/?00000211!\x01R1\x02GROUP(400B(7)400D(1)5003(1))\x03\x68"),
                ExpectVectorFromString("\x02""400B(E12)(118.8)(121.1)400D(50.01)5003(009114135064195)\x03\x1e"),
                __func__);
        }
    };
}

TEST_F(TEnergomeraIntegrationTest, Poll)
{
    EnqueuePollRequests();
    Note() << "LoopOnce()";
    SerialDriver->LoopOnce();
}
