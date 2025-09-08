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

    async read(timeout)
    {
        const reader = this.port.readable.getReader();
        let success = false;

        async function receive()
        {
            const { value } = await reader.read();
            return value;
        }

        async function wait(timeout)
        {
            await new Promise(resolve => setTimeout(resolve, timeout));
            return "Request timed out";
        }

        let result = await Promise.race([receive(), wait(timeout)]);

        if (result instanceof Uint8Array)
        {
            this.data = result;
            success = true;
        }

        reader.releaseLock();
        return success;
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
            slave_id: 25,
            group: "g_in0"
        };

        Module.loadConfigRequest(JSON.stringify(request));
    });
}