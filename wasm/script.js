var Relay = 0;
var Port;

var PortOptions = {
    baudRate: 9600,
    bufferSize: 1024,
    dataBits: 8,
    flowControl: 'none',
    parity: 'none',
    stopBits: 2
};

var Module =
{
    print(text)
    {
        let output = document.querySelector('#output');
        output.value += text + "\n";
        output.scrollTop = output.scrollHeight;
        console.log(text);
    },
    setStatus(text)
    {
        console.log(text);
    }
};

class SerialPort
{
    replyTimeout = 1000; // is 1 second enough?

    constructor()
    {
        if (navigator.serial)
            return;

        alert('Web Serial API is not supported by this browser :(');
    }

    async open(options)
    {
        try
        {
            this.port = await navigator.serial.requestPort();
            await this.port.open(options);
        }
        catch (error)
        {
            console.error("Can't open serial port: ", error);
        }
    }

    async close()
    {
        if (!this.port)
            return;

        await this.port.close();
        this.port = null;
    }

    async write(data)
    {
        if (!this.port || !this.port.writable)
        {
            console.error("Serial port is not open or not writable");
            return;
        }

        const writer = this.port.writable.getWriter();
        await writer.write(data);
        writer.releaseLock();
    }

    async read(length)
    {
        let read = true;
        const reader = this.port.readable.getReader();

        async function receive()
        {
            let data = new Uint8Array(length);
            let offset = 0;

            while (read)
            {
                let {value} = await reader.read();

                if (!read)
                    break;

                value = value.subarray(0, length - offset);
                data.set(value, offset);
                offset += value.length;

                if (length > offset)
                    continue;

                read = false;
                reader.releaseLock();
            }

            return data;
        }

        async function wait(timeout)
        {
            await new Promise(resolve => setTimeout(resolve, timeout));

            if (read)
            {
                read = false;
                reader.cancel();
            }

            return false;
        }

        return await Promise.race([receive(), wait(this.replyTimeout)]);
    }
}

window.onload = function()
{
    Port = new SerialPort();

    document.querySelector('#portButton').addEventListener('click', async function()
    {
        if (!Port.port)
        {
            await Port.open(PortOptions);
            this.innerHTML = 'close port';
        }
        else
        {
            await Port.close();
            this.innerHTML = 'open port';
        }
    });

    document.querySelector('#requestButton').addEventListener('click', async function()
    {
        let request =
        {
            device_type: "WB-MR6C v.3",
            slave_id: 25
        };

        Module.deviceLoadConfig(JSON.stringify(request));
    });

    document.querySelector('#testButton').addEventListener('click', async function()
    {
        Relay = Relay ? 0 : 1;

        let request =
        {
            device_type: "WB-MR6C v.3",
            slave_id: 25,
            channels: {K1: Relay}
        };

        Module.deviceSet(JSON.stringify(request));
    });
}