class SerialPort
{
    options = new Object();
    isOpen = false;

    constructor()
    {
        if (navigator.serial)
            return;

        alert('Web Serial API is not supported by this browser :(');
    }

    setOptions(baudRate, dataBits, parity, stopBits)
    {
        switch (true)
        {
            case baudRate < 4800: this.replyTimeout = 1000; break;
            case baudRate < 38400: this.replyTimeout = 500; break;
            default: this.replyTimeout = 250; break;
        }

        switch (String.fromCharCode(parity))
        {
            case 'E': this.options.parity = 'even'; break;
            case 'O': this.options.parity = 'odd';  break;
            default:  this.options.parity = 'none'; break;
        }

        this.options.baudRate = baudRate;
        this.options.dataBits = dataBits;
        this.options.stopBits = stopBits;
    }

    async select(force)
    {
        if (this.port && !force)
            return;

        this.port = await navigator.serial.requestPort();
    }

    async open()
    {
        if (this.isOpen)
            await this.close();

        try
        {
            await this.select(false);
            await this.port.open(this.options);
            this.isOpen = true;
        }
        catch (error)
        {
            console.error('Can not open serial port: ', error);
            delete this.port;
        }
    }

    async close()
    {
        if (!this.port && !this.isOpen)
            return;

        await this.port.close();
        this.isOpen = false;
    }

    async write(data)
    {
        await this.open();

        if (!this.port || !this.port.writable)
        {
            console.error('Serial port is not open or not writable');
            return;
        }

        const writer = this.port.writable.getWriter();
        await writer.write(data);
        writer.releaseLock();
    }

    async read(count)
    {
        if (!this.port || !this.port.readable)
        {
            console.error('Serial port is not open or not readable');
            return;
        }

        const reader = this.port.readable.getReader();
        let data = new Uint8Array();
        let read = true;

        async function receive()
        {
            while (read)
            {
                let {value} = await reader.read();

                if (!read)
                    break;

                let buffer = new Uint8Array(data.length + value.length);
                buffer.set(data, 0)
                buffer.set(value, data.length);
                data = buffer;

                if (data.length < count)
                    continue;

                read = false;
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

            return data;
        }

        let result = await Promise.race([receive(), wait(this.replyTimeout)]);
        reader.releaseLock();
        return result;
    }
}
