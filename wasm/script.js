var Module =
{
    serial: new SerialPort(),

    async request(type, data)
    {
        function wait(resolve)
        {
            if (this.result === undefined)
                setTimeout(wait.bind(this, resolve), 10);
            else
                resolve();
        }

        switch (type)
        {
            case 'portScan': this.portScan(JSON.stringify(data)); break;
            case 'deviceLoadConfig': this.deviceLoadConfig(JSON.stringify(data)); break;
            case 'deviceSet': this.deviceSet(JSON.stringify(data)); break;
        }

        this.result = undefined;
        await new Promise(wait.bind(this));
        return this.result;
    },

    onResult(result)
    {
        this.result = JSON.parse(result);
    },

    onError(error)
    {
        this.print('request error: ' + error);
    },

    setStatus(text)
    {
        this.print(text);
    },

    print(text)
    {
        let output = document.querySelector('#output');
        output.value += text + '\n';
        output.scrollTop = output.scrollHeight;
        console.log(text);
    }
};

class PortScan
{
    baudRates = [115200, 57600, 38400, 19200, 9600, 4800, 2400, 1200];
    index = 0;

    async sentRequest(start)
    {
        Module.print('Scan ' + (start ? 'start' : 'next') + ': ' + this.baudRates[this.index]);

        let request =
        {
            command: 96,
            mode: start ? 'start' : 'next',
            baud_rate: this.baudRates[this.index],
            data_bits: 8,
            parity: 'N',
            stop_bits: 2
        };

        return await Module.request('portScan', request);
    }

    async scan()
    {
        let devices = new Array();
        let start = true;

        while (this.index < this.baudRates.length)
        {
            let data = await this.sentRequest(start);

            if (data.devices?.length)
            {
                data.devices.forEach(device => devices.push(device));
                start = false;
                continue;
            }

            this.index++;
            start = true;
        }

        Module.print(devices.length + ' devices found' + (devices.length ? ':' : ''));

        devices.forEach(device =>
        {
            Module.print(device.device_signature + ' ' + device.fw.version + ' (' + device.sn + ') ' + device.cfg.slave_id + ' [' + device.cfg.baud_rate + ']');
        });
    }
}

var Relay = 0;

window.onload = function()
{
    document.querySelector('#portButton').addEventListener('click', async function()
    {
        await Module.port.select(true);
    });

    document.querySelector('#scanButton').addEventListener('click', async function()
    {
        await new PortScan().scan();
    });

    document.querySelector('#requestButton').addEventListener('click', async function()
    {
        let request =
        {
            baud_rate: 2400,
            data_bits: 8,
            parity: 'N',
            stop_bits: 2,
            device_type: 'WB-MR6C v.3',
            slave_id: 25
        };

        let result = await Module.request('deviceLoadConfig', request);
        Module.print(JSON.stringify(result, 0, 2));
    });

    document.querySelector('#testButton1').addEventListener('click', async function()
    {
        Relay = Relay ? 0 : 1;

        let request =
        {
            baud_rate: 2400,
            data_bits: 8,
            parity: 'N',
            stop_bits: 2,
            device_type: 'WB-MR6C v.3',
            slave_id: 25,
            channels: {K1: Relay}
        };

        await Module.request('deviceSet', request);
    });

    document.querySelector('#testButton2').addEventListener('click', async function()
    {
        Relay = Relay ? 0 : 1;

        let request =
        {
            baud_rate: 2400,
            data_bits: 8,
            parity: 'N',
            stop_bits: 2,
            device_type: 'WB-MR6C',
            slave_id: 221,
            channels: {K1: Relay}
        };

        await Module.request('deviceSet', request);
    });
}
